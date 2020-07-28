/*-------------------------------------------------------------------------
 *
 * orc_interface.cpp
 *    An intermediary to connect FDW with Apache ORC wrapper.
 *
 * 2020, Hamid Quddus Akhtar.
 *
 *    The FDW functions are implemented in this file so that we can use c++
 *    and STL constructs rather implementing those ourselves.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    src/orc_interface.cpp
 *
 *-------------------------------------------------------------------------
 */

/* system header files */
#include <sys/stat.h>

/* C++ header files */
#include <sstream>
#include <vector>
#include <bits/stdc++.h>

/* ORC FDW header files */
#include <orc_wrapper.h>
#include <orc_interface.h>
#include <orc_deparse.h>
#include <orc_interface_typedefs.h>

/* PostgreSQL header files */
extern "C"
{
    #include "orc_fdw.h"
    #include "fmgr.h"
    #include "access/table.h"
    #include "catalog/pg_type.h"
    #include "commands/defrem.h"
    #include "commands/explain.h"
    #include "optimizer/cost.h"
    #include "optimizer/optimizer.h"
    #include "optimizer/pathnode.h"
    #include "optimizer/planmain.h"
    #include "optimizer/restrictinfo.h"
    #include "parser/parse_coerce.h"
    #include "utils/builtins.h"
    #include "utils/date.h"
    #include "utils/lsyscache.h"
    #include "utils/memutils.h"
    #include "utils/numeric.h"
    #include "utils/palloc.h"
    #include "utils/rel.h"
    #include "utils/timestamp.h"

    #include "nodes/print.h"
}

/* Declare the functions to use within this file */
static std::vector<OrcFdwColInfo> getMappedColsFromFile(std::string file_pathname);
static std::vector<OrcFdwColInfo> getMappedColsFromReader(ORC_UNIQUE_PTR<orc::Reader> *p_reader, ORC_UNIQUE_PTR<orc::RowReader> *p_rowReader, orc::StructVectorBatch *root);
static std::vector<OrcFdwColInfo> map2PGColsList(orc::StructVectorBatch *root, std::vector<OrcFileColInfo> orc_col_list);
static bool getColMetaData(orc::StructVectorBatch *root, OrcFdwColInfo &col);
static OrcPgTypeKind getColType(int orcKind);
static void setCastingFunc(OrcFdwColInfo &col, Oid targetOid);
static bool getColumnNameList(RelOptInfo *baserel, OrcFdwPlanState *fdw_state, List *tlist);
static Datum getDatumForData(OrcFdwExecState *fdw_estate, int row_in_batch, int orc_index);
static OrcFdwExecState* orcInitExecState(OrcFdwExecState **fdw_estate, char *filename, List *col_orc_file_index, RangeTblEntry *rte, List *fdw_scan_tlist, bool blnShouldSetRowReader);
static TupleTableSlot *fillSlot(OrcFdwExecState *fdw_estate, TupleTableSlot *slot);


/*
 * getMappedColsFromFile
 *    For a given filename, return the mappable columns only and their
 *    meta data; OrcFdwColInfo structure
 */
static
std::vector<OrcFdwColInfo>
getMappedColsFromFile(std::string file_pathname)
{
    ORC_UNIQUE_PTR<orc::Reader> reader;
    orc::StructVectorBatch *root;
    auto orc_col_info = orcGetColsInfo(file_pathname, &reader, &root);

    return map2PGColsList(root, orc_col_info);
}

/*
 * getMappedColsFromFile
 *    For a given ORC file reader, return the mappable columns only and their
 *    meta data; OrcFdwColInfo structure
 */
static
std::vector<OrcFdwColInfo>
getMappedColsFromReader(ORC_UNIQUE_PTR<orc::Reader> *p_reader, ORC_UNIQUE_PTR<orc::RowReader> *p_rowReader, orc::StructVectorBatch *root)
{
    auto orc_col_info = orcGetColsInfo(p_reader, p_rowReader, root);
    return map2PGColsList(root, orc_col_info);
}

/*
 * map2PGColsList
 *    Returns a vector of columns and their meta data that PG understands.
 *    Skips all other columns in ORC.
 */
static
std::vector<OrcFdwColInfo>
map2PGColsList(orc::StructVectorBatch *root, std::vector<OrcFileColInfo> orc_col_list)
{
    std::vector<OrcFdwColInfo> fdw_col_list;

    for (uint col_index = 0; col_index < orc_col_list.size(); col_index++)
    {
        OrcFdwColInfo col;

        col.kind = getColType(orc_col_list[col_index].kind);

        /* Skip unsupported columns */
        if (col.kind == OrcPgTypeKind::UNSUPPORTED_TYPE)
            continue;

        /* Initialize with known value and set remaining as invalid 
         * to be set later. */
        col.name = orc_col_list[col_index].name;
        col.index = orc_col_list[col_index].index;
        col.max_length = orc_col_list[col_index].max_length;
        col.hasNull = orc_col_list[col_index].hasNull;
        col.precision = orc_col_list[col_index].precision;
        col.scale = orc_col_list[col_index].scale;

        /* Fill in and adjust values in the structure */
        (void) getColMetaData(root, col);

        fdw_col_list.push_back(col);
    }

    return fdw_col_list;
}

/*
 * getColType
 *    Map ORC types to ORC FDW internal types
 */
static
OrcPgTypeKind
getColType(int orcKind)
{
    OrcPgTypeKind type = (OrcPgTypeKind)orcKind;

    /* Let's map the unsupported types to a common type */
    if (type == OrcPgTypeKind::LIST_UNSUPPORTED
        || type == OrcPgTypeKind::MAP_UNSUPPORTED
        || type == OrcPgTypeKind::STRUCT_UNSUPPORTED
        || type == OrcPgTypeKind::UNION_UNSUPPORTED)
    {
        type = OrcPgTypeKind::UNSUPPORTED_TYPE;
    }

    return type;
}

/*
 * getColMetaData
 *    Set type OID and size for ORC FDW column.
 */
static
bool
getColMetaData(orc::StructVectorBatch *root, OrcFdwColInfo &col)
{
    col.size = 0;

    /* Let's assume that no casting function is required. We'll set
     * these later if required. */
    col.cast_func = NULL;
    col.is_binary_compatible = true;

    switch(col.kind)
    {
        case OrcPgTypeKind::BOOLEAN:
        {
            col.col_oid = BOOLOID;
            col.size = sizeof(bool);
            break;
        }
        case OrcPgTypeKind::BYTE:
        case OrcPgTypeKind::SHORT:
        {
            col.col_oid = INT2OID;
            col.size = sizeof(int16_t);
            break;
        }
        case OrcPgTypeKind::INT:
        {
            col.col_oid = INT4OID;
            col.size = sizeof(int32_t);
            break;
        }
        case OrcPgTypeKind::LONG:
        {
            col.col_oid = INT8OID;
            col.size = sizeof(int64_t);
            break;
        }
        case OrcPgTypeKind::FLOAT:
        {
            col.col_oid = FLOAT4OID;
            col.size = sizeof(float4);
            break;
        }
        case OrcPgTypeKind::DOUBLE:
        {
            col.col_oid = FLOAT8OID;
            col.size = sizeof(float8);
            break;
        }
        case OrcPgTypeKind::DECIMAL:
        {
            col.col_oid = NUMERICOID;

            if( col.precision <= 18 )
            {
                col.size = sizeof(int64_t);
            }
            else
            {
                col.size = sizeof(int64_t) * 2;
            }

            break;
        }
        case OrcPgTypeKind::STRING:
        {
            col.col_oid = TEXTOID;
            break;
        }
        case OrcPgTypeKind::BINARY:
        {
            col.col_oid = BYTEAOID;
            break;
        }
        case OrcPgTypeKind::VARCHAR:
        {
            col.col_oid = VARCHAROID;
            break;
        }
        case OrcPgTypeKind::CHAR:
        {
            col.col_oid = CHAROID;
            break;
        }
        case OrcPgTypeKind::TIMESTAMP:
        {
            col.col_oid = TIMESTAMPOID;
            col.size = sizeof(int64_t);
            break;
        }
        case OrcPgTypeKind::DATE:
        {
            col.col_oid = DATEOID;
            col.size = sizeof(int32_t);
            break;
        }

        case OrcPgTypeKind::LIST_UNSUPPORTED:
        case OrcPgTypeKind::MAP_UNSUPPORTED:
        case OrcPgTypeKind::STRUCT_UNSUPPORTED:
        case OrcPgTypeKind::UNION_UNSUPPORTED:
        case OrcPgTypeKind::UNSUPPORTED_TYPE:
        default:
            ereport(ERROR, (errmsg("%s: getColMetaData called for an unsupported type.", ORC_FDW_NAME)));
            return false;
    }

    return true;
}

/*
 * setCastingFunc
 *    At the moment, all types are binary coercible so an explicit casting
 *    function is not required. However, we may need this later when add
 *    other more complex data types. Throws an error if no casting function
 *    is found.
 */
static
void
setCastingFunc(OrcFdwColInfo &col, Oid targetOid)
{

    /* Find a casting function; not required for binary coercible ones */
    if (IsBinaryCoercible(col.col_oid, targetOid))
    {
        return;
    }

    /* If the target is valid, let's find casting function */
    if (targetOid != InvalidOid)
    {
        Oid funcid = InvalidOid;
        CoercionPathType c_path = find_coercion_pathway(targetOid, col.col_oid, COERCION_EXPLICIT, &funcid);

        switch (c_path)
        {
            /* Set the casting function */
            case COERCION_PATH_FUNC:
            {
                MemoryContext oldcxt = MemoryContextSwitchTo(CurTransactionContext);
                col.cast_func = (FmgrInfo *) palloc0(sizeof(FmgrInfo));
                fmgr_info(funcid, col.cast_func);
                MemoryContextSwitchTo(oldcxt);
                break;
            }
            /* No explicit casting required */
            case COERCION_PATH_RELABELTYPE:
            {
                break;
            }
            case COERCION_PATH_COERCEVIAIO:
            {
                break;
            }
            /* No casting function found; let's throw an error */
            case COERCION_PATH_ARRAYCOERCE:
            case COERCION_PATH_NONE:
            default:
            {
                ereport(ERROR, (errmsg("%s: No casting function from %d oid to %d oid.", ORC_FDW_NAME, col.col_oid, targetOid)));
                break;
            }
        }
    }
}

/*
 * getSchemaSQL
 *    Get the complete SQL for creating a foreign table
 */
extern "C"
bool
getSchemaSQL(ImportForeignSchemaStmt *stmt, const char *f, char **cmd)
{
    std::string filename(f);
    std::stringstream path_ss; path_ss << stmt->remote_schema << '/' << filename;
    std::stringstream cmd_ss;
    bool hasColumns = false;
    auto cols_list = getMappedColsFromFile(path_ss.str());

    /* Creation statement */
    cmd_ss << "CREATE FOREIGN TABLE " << stmt->local_schema
            << "." << filename.substr(0, filename.find("."))
            << " (";

    /* Add all columns and types */
    for (auto col = cols_list.begin(); col != cols_list.end(); col++)
    {
        if (hasColumns)
            cmd_ss << ", ";

        cmd_ss << (*col).name << " " << format_type_be((*col).col_oid);

        /* Add precision and scale for a decimal column */
        if ((*col).kind == OrcPgTypeKind::DECIMAL && (*col).precision > 0)
        {
            cmd_ss << "(" << (*col).precision << ", " << (*col).scale << ")";
        }

        if ((*col).max_length > 0)
        {
            cmd_ss << " (" << (*col).max_length << ")";
        }

        /* Set NULL-ability */
        if ((*col).hasNull == false)
        {
            cmd_ss << " NOT";
        }

        cmd_ss << " NULL";

        hasColumns = true;
    }

    /* Complete statement with server and filename option */
    cmd_ss << ") SERVER " << stmt->server_name 
            << " OPTIONS (FILENAME " << "'" << path_ss.str() << "'" << ");";

    /* Duplicate for return     */
    *cmd = pstrdup(cmd_ss.str().c_str());

    /* No columns or mappable columns found. Let's throw an error */
    if (hasColumns == false)
    {
        ereport(ERROR, (errmsg("%s: No supported columns found.", ORC_FDW_NAME),
                       (errhint("Did you specify the correct path for ORC file import? Check documentation to see supported column types."))));
    }

    return hasColumns;
}

/*
 * getTableOptionsFromRelID
 *    Return OrcFdwPlanState structure with table options for a given relid
 */
extern "C"
bool
getTableOptionsFromRelID(Oid foriegntableid, OrcFdwPlanState *fdw_state)
{
    fdw_state->table = GetForeignTable(foriegntableid);

    return getTableOptions(fdw_state->table->options, fdw_state);
}

/*
 * getTableOptions
 *    Fill OrcFdwPlanState structure with table options for a table
 *    options list. Returns false in case no valid option and
 *    filename is found.
 */
extern "C"
bool getTableOptions(List *options_list, OrcFdwPlanState *fdw_state)
{
    bool hasFilename = false;
    ListCell *lc;

    foreach(lc, options_list)
    {
        DefElem    *def = (DefElem *) lfirst(lc);

        /* if a filename is provide, validate it */
        if (strcmp(def->defname, "filename") == 0)
        {
            struct stat stat_buf;

            /* If not a valid file */
            if (stat(defGetString(def), &stat_buf) != 0)
            {
                int e = errno;

                ereport(ERROR,
                        (errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                         errmsg("%s: filename %s: %s", ORC_FDW_NAME,
                            defGetString(def), strerror(e))));
            }
            else
            {
                /* fdw_state will be NULL if called from the FDW validator
                 * function which does not need to store the data. */
                if (fdw_state != NULL)
                {
                    /* Set filename in the state structure for later use */
                    fdw_state->filename = defGetString(def);
                }

                hasFilename = true;
            }
        }
        else
        {
            /* Currently, we only support filename as an option. So throw
             * an error otherwise */
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                     errmsg("%s: invalid option specified \"%s\"",
                            ORC_FDW_NAME, def->defname)));
        }
    }

    return hasFilename;
}

/*
 * getColumnNameList
 *    We need to know the columns names in the foreign table
 *    so that we can table columns to the ORC file. This function
 *    fills plan state structure the column name list.
 */
static
bool
getColumnNameList(RelOptInfo *baserel, OrcFdwPlanState *fdw_state, List *tlist)
{
    List *cols_name_reqd = NIL;
    List *cols_oid_reqd = NIL;
    List *cols_index_reqd = NIL;
    ListCell *lc;
    Relation rel;
    TupleDesc tupleDesc;
    AttrNumber attnum;
    Bitmapset *attrs_used = NULL;
    bool has_wholerow = false;
    int numattrs = 0;
    int i;

    /* Get all attributes needed for joins or final output */
    pull_varattnos((Node *) baserel->reltarget->exprs, baserel->relid,
                    &attrs_used);

    /* Pull in all attributes used in restriction clauses */
    foreach(lc, baserel->baserestrictinfo)
    {
        RestrictInfo *ri = (RestrictInfo *) lfirst(lc);
        pull_varattnos((Node *) ri->clause, baserel->relid,
                        &attrs_used);
    }

    /* Let's convert attribute numbers to column names */
    rel = table_open(fdw_state->foreigntableid, AccessShareLock);
    tupleDesc = RelationGetDescr(rel);

    while ((attnum = bms_first_member(attrs_used)) >= 0)
    {
        attnum += FirstLowInvalidHeapAttributeNumber;

        /* Handling of whole row; nothing to filter column names */
        if (attnum == 0)
        {
            has_wholerow = true;
            break;
        }

        /* Skip system attributes */
        if (attnum < 0)
            continue;

        /* User attributes */
        if (attnum > 0)
        {
            Form_pg_attribute attr = TupleDescAttr(tupleDesc, attnum - 1);
            char *attname;

            /* Skip dropped or generated columns */
            if (attr->attisdropped || attr->attgenerated)
                continue;

            /* Add to columns list */
            attname = pstrdup(NameStr(attr->attname));
            cols_name_reqd = lappend(cols_name_reqd, makeString(attname));
        }
    }

    numattrs = 0;
    for (i = 0; i < tupleDesc->natts; i++)
    {
        Form_pg_attribute attr = TupleDescAttr(tupleDesc, i);
        numattrs += (int)(attr->attisdropped == false);
    }

    table_close(rel, AccessShareLock);

    /* Handling of whole row; nothing to filter column names */
    if (has_wholerow || numattrs == list_length(cols_name_reqd))
    {
        return false;
    }

    foreach(lc, cols_name_reqd)
    {
        ListCell *lc_name;
        ListCell *lc_oid;
        ListCell *lc_index;

        forthree(lc_name, fdw_state->col_orc_name,
            lc_oid, fdw_state->col_orc_oid,
            lc_index, fdw_state->col_orc_file_index)
        {

            if (pg_strcasecmp(strVal(lfirst(lc)), strVal(lfirst(lc_name))) == 0)
            {
                cols_oid_reqd = lappend_int(cols_oid_reqd, lfirst_oid(lc_oid));
                cols_index_reqd = lappend_int(cols_index_reqd, lfirst_int(lc_index));
            }
        }
    }

    list_free_deep(fdw_state->col_orc_name);
    list_free(fdw_state->col_orc_oid);
    list_free(fdw_state->col_orc_file_index);

    fdw_state->col_orc_name = cols_name_reqd;
    fdw_state->col_orc_oid = cols_oid_reqd;
    fdw_state->col_orc_file_index = cols_index_reqd;

    return true;
}

/*
 * orcInitExecState
 *    Initializes executation state with table details and ORC FDW
 *    column meta data.
 */
static
OrcFdwExecState *
orcInitExecState(OrcFdwExecState **fdw_estate, char *filename, List *col_orc_file_index, RangeTblEntry *rte, List *fdw_scan_tlist, bool blnShouldSetRowReader)
{
    int attnum = 0;
    uint i;
    ListCell *lc;
    std::list<uint64_t> orc_cols;

    *fdw_estate = new OrcFdwExecState;

    /* Set values in exec state for our FDW */
    (*fdw_estate)->filename = filename;
    (*fdw_estate)->batchsize = ORC_DEFAULT_BATCH_SIZE;
    (*fdw_estate)->curr_batch_total_rows = -1;
    (*fdw_estate)->curr_batch_number = 0;
    (*fdw_estate)->curr_batch_row_num = 0;
    (*fdw_estate)->row_num = 0;

    /* Fill the list with required ORC column indexes */
    foreach(lc, col_orc_file_index)
    {
        orc_cols.push_back(lfirst_int(lc));
    }

    /* Include the list in the row reader */
    if (orc_cols.size() > 0 && blnShouldSetRowReader)
    {
        (*fdw_estate)->rowReaderOptions.include(orc_cols);
    }

    (*fdw_estate)->is_valid_reader = orcCreateReader((*fdw_estate)->filename, &((*fdw_estate)->reader), (*fdw_estate)->options, false);
    (void) orcCreateRowReader(&((*fdw_estate)->reader), &((*fdw_estate)->rowReader), (*fdw_estate)->rowReaderOptions);

	(*fdw_estate)->batch = ((*fdw_estate)->rowReader)->createRowBatch((*fdw_estate)->batchsize);
    (*fdw_estate)->batch_data = dynamic_cast<orc::StructVectorBatch *>((*fdw_estate)->batch.get());

    /* index, column name, internal type, Oid, column size */
    (*fdw_estate)->cols_info = getMappedColsFromReader(&((*fdw_estate)->reader), &((*fdw_estate)->rowReader), (*fdw_estate)->batch_data);

    /* Resize the column position list to match tuple */
    (*fdw_estate)->attr_orc_index.resize(list_length(fdw_scan_tlist));

    /* Store indexes of matching columns in ORC file */
    foreach(lc, fdw_scan_tlist)
    {
        TargetEntry *tle = lfirst_node(TargetEntry, lc);
        Var *var = (Var *) tle->expr;

        Assert(IsA(var, Var));

        char *attname = get_attname(rte->relid, var->varattno, false);
        Oid targetOid = get_atttype(rte->relid, var->varattno);

        /* Let's assume that we will not able to find the column */
        (*fdw_estate)->attr_orc_index[attnum] = -1;

        for (i = 0; i < (*fdw_estate)->cols_info.size(); i++)
        {
            /* FIXME: Do we need a case insensitive comparison? */
            if ((*fdw_estate)->cols_info[i].name.compare(attname) == 0)
            {
                if ((*fdw_estate)->cols_info[i].col_oid != targetOid)
                {
                    ereport(ERROR, (errmsg("%s: Unable to read data for column %s with data type mismatch against ORC file.", ORC_FDW_NAME, attname)));
                }

                (*fdw_estate)->attr_orc_index[attnum] = i;
                setCastingFunc((*fdw_estate)->cols_info[i], targetOid);
                // printf("column added index = %d; attnum = %d; i = %d - %s\n", (*fdw_estate)->attr_orc_index[attnum], attnum, i, attname);
            }
        }

        /* Increase the attribute counter */
        attnum++;
    }

    /* No columns found, so let's set to entire row */
    if ((*fdw_estate)->attr_orc_index.size() == 0)
    {
        (*fdw_estate)->attr_orc_index.resize((*fdw_estate)->cols_info.size());

        /* Set the column positions to default */
        for (i = 0; i < (*fdw_estate)->attr_orc_index.size(); i++)
        {
            (*fdw_estate)->attr_orc_index[i] = i;
        }
    }

    /* Set total number of rows in exec state */
    (*fdw_estate)->total_rows = orcGetNumberOfRows(&((*fdw_estate)->reader));

    /* Set numeric defaults */
    (*fdw_estate)->default_numeric_scale = orcGetDefaultDecimalScale(&((*fdw_estate)->reader));

    return *fdw_estate;
}

/*
 * getDatumForData
 *    Get data for a given column and row from the ORC file, store it in a
 *    Datum based on column's OID, and return it.
 */
static
Datum
getDatumForData(OrcFdwExecState *fdw_estate, int row_in_batch, int col_index)
{
    Datum d = (Datum) NULL;
    int orc_index = col_index;
    bool is_var_length = false;

    switch(fdw_estate->cols_info[col_index].col_oid)
    {
        case BOOLOID:
        {
            d = BoolGetDatum((dynamic_cast<orc::LongVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch]);
            break;
        }
        case INT2OID:
        {
            d = Int16GetDatum((dynamic_cast<orc::LongVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch]);
            break;
        }
        case INT4OID:
        {
            d = Int32GetDatum((dynamic_cast<orc::LongVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch]);
            break;
        }
        case INT8OID:
        {
            d = Int8GetDatum((dynamic_cast<orc::LongVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch]);
            break;
        }
        case FLOAT4OID:
        {
            d = Float4GetDatum((dynamic_cast<orc::DoubleVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch]);
            break;
        }
        case FLOAT8OID:
        {
            d = Float8GetDatum((dynamic_cast<orc::DoubleVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch]);
            break;
        }
        /* FIXME: Cross type; currently this case is unreachable */
        case NUMERICOID:
        {
            std::string nvalue;
            int precision = fdw_estate->cols_info[col_index].precision;
            int scale = fdw_estate->cols_info[col_index].scale;

            /* Decimal64VectorBatch */
            if (precision == 0 || precision < 18)
            {
                int64_t val = (static_cast<orc::Decimal64VectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->values[row_in_batch];
                nvalue = std::to_string(val);
            }
            /* Decimal128VectorBatch */
            else
            {
                nvalue = ((orc::Int128)((dynamic_cast<orc::Decimal64VectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->values[row_in_batch])).toString();
            }

            /* Let's format the numeric data */
            if (fdw_estate->default_numeric_scale)
            {
                scale = fdw_estate->default_numeric_scale;
                precision = nvalue.length();

                /* Ensure that precision includes scale. If not, add
                 * scale to it. */
                if (precision < scale)
                    precision += scale;
            }

            int typmod = VARHDRSZ + (precision << 16) + scale;

            /* Don't set typmod if we don't have precision and scale data */
            if (scale > 0 && precision > 0)
            {
                nvalue.insert(precision - scale, ".");
            }
            else
            {
                typmod = -1;
            }

            d = DirectFunctionCall3(numeric_in, CStringGetDatum(nvalue.c_str()), ObjectIdGetDatum(InvalidOid), Int32GetDatum(typmod));
            break;
        }
        case TIMESTAMPOID:
        {
            int64_t secs = (dynamic_cast<orc::TimestampVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch];
            int64_t nano = (dynamic_cast<orc::TimestampVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->nanoseconds[row_in_batch];

            /* Convert nano to micro and then divide */
            double val = (double) secs + ((double)(nano / 1000L) / USECS_PER_SEC);

            d = DirectFunctionCall1(float8_timestamptz, (double)Float8GetDatum(val));
            break;
        }
        case DATEOID:
        {
            d = DateADTGetDatum((dynamic_cast<orc::LongVectorBatch *>(fdw_estate->batch_data->fields[orc_index]))->data[row_in_batch] + (UNIX_EPOCH_JDATE - POSTGRES_EPOCH_JDATE));
            break;
        }
        /* Variable lengths are handled later in this function */
        case TEXTOID:
        case BYTEAOID:
        case CHAROID:
        case VARCHAROID:
        {
            is_var_length = true;
            break;
        }
        default:
        {
            /* We should never get to this default, but just in case. */
            ereport(ERROR, (errmsg("%s: unsupported column data type for column %s", ORC_FDW_NAME, fdw_estate->cols_info[orc_index].name.c_str())));
            break;
        }
    }

    /* Let's handle all variable length data here */
    if (is_var_length)
    {
        orc::StringVectorBatch *s = dynamic_cast<orc::StringVectorBatch *>(fdw_estate->batch_data->fields[orc_index]);
        int64_t orc_data_len = s->length[row_in_batch];
        int64_t var_len = VARHDRSZ + orc_data_len;
        char *orc_data = (char *)s->data[row_in_batch];

        bytea *data = (bytea *) palloc(var_len);
        SET_VARSIZE(data, var_len);
        memcpy(VARDATA(data), orc_data, orc_data_len);

        d = PointerGetDatum(data);
    }

    return d;
}

/*
 * fillSlot
 *    Fill data in all ORC mappable columns from the ORC file.
 */
static
TupleTableSlot *
fillSlot(OrcFdwExecState *fdw_estate, TupleTableSlot *slot)
{
    int attnum;

    /* Iterate over all attributes and fill in data */
    for (attnum = 0; attnum < slot->tts_tupleDescriptor->natts; attnum++)
    {
        int col_index = fdw_estate->attr_orc_index[attnum];

        /* Column is in the ORC file */
        if (col_index >= 0)
        {
            Datum d = getDatumForData(fdw_estate, fdw_estate->curr_batch_row_num, col_index);

            slot->tts_values[attnum] = d;
            slot->tts_isnull[attnum] = false;
        }
        else
        {
            slot->tts_isnull[attnum] = true;
        }
    }

    /* Increment row counters */
    fdw_estate->curr_batch_row_num++;
    fdw_estate->row_num++;

    return slot;
}

/*
 * orcGetForeignRelSize
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
void
orcGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid)
{
	orc::ReaderOptions options;
	ORC_UNIQUE_PTR<orc::Reader> reader;
	orc::RowReaderOptions rowReaderOptions;
	ORC_UNIQUE_PTR<orc::RowReader> rowReader;
	ORC_UNIQUE_PTR<orc::ColumnVectorBatch> batch;
    orc::StructVectorBatch *batch_data;

    OrcFdwPlanState *fdw_private = (OrcFdwPlanState *)(palloc0(sizeof(OrcFdwPlanState)));

    baserel->fdw_private = fdw_private;

    (void) getTableOptionsFromRelID(foreigntableid, fdw_private);

    /* Open the ORC file to fetch relevant information for planning */
    (void) orcCreateReader(fdw_private->filename, &reader, options, true);
    (void) orcCreateRowReader(&reader, &rowReader, rowReaderOptions);

    /* We just need to fetch column data, so let's just set row batch size to 1 */
	batch = rowReader->createRowBatch(1);
    batch_data = dynamic_cast<orc::StructVectorBatch *>(batch.get());

    /* Let's get all the columns in the ORC file */
    std::vector<OrcFdwColInfo> cols_info = getMappedColsFromReader(&reader, &rowReader, batch_data);

    /* Assume that we aren't dealing with aggregates */
    fdw_private->hasAggregate = false;

    /* Initialize lists to NIL */
    fdw_private->col_orc_name = NIL;
    fdw_private->col_orc_oid = NIL;
    fdw_private->col_orc_file_index = NIL;

    /* Fill data in lists */
    for (auto col = cols_info.begin(); col != cols_info.end(); col++)
    {
        char *name;
        name = pstrdup((*col).name.c_str());

        fdw_private->col_orc_name = lappend(fdw_private->col_orc_name, makeString(name));
        fdw_private->col_orc_oid = lappend_oid(fdw_private->col_orc_oid, (*col).col_oid);
        fdw_private->col_orc_file_index = lappend_int(fdw_private->col_orc_file_index, (*col).index);
    }

    /* Set total number of rows in the ORC file */
    baserel->rows = fdw_private->rows = orcGetNumberOfRows(&reader);

    /* Classify */
    classifyConditions(root, baserel, baserel->baserestrictinfo,
                    &fdw_private->remote_conds, &fdw_private->local_conds);

    /* Set default costs */
    fdw_private->startup_cost = ORC_DEFAULT_FDW_STARTUP_COST;
    fdw_private->tuple_cost = ORC_DEFAULT_FDW_TUPLE_COST;

    /* FIXME: Do we need to set any other members? */
    fdw_private->foreigntableid = foreigntableid;
}

/*
 * orcGetForeignPaths
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
void
orcGetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid)
{
    OrcFdwPlanState *fdw_private = (OrcFdwPlanState *)(baserel->fdw_private);
    ForeignPath *path = NULL;

    /* FIXME: We are not considering filters or stats in the ORC file
     * for this release. */
    Cost total_cost = fdw_private->startup_cost + (fdw_private->tuple_cost * fdw_private->rows);

    path = create_foreignscan_path(root, baserel, 
                                        NULL,
                                        (double)(fdw_private->rows),
                                        fdw_private->startup_cost,
                                        total_cost,
                                        NIL,        /* FIXME: Do we need to add path keys? */
                                        NULL,       /* FIXME: Add outer rel? */
                                        NULL,       /* FIXME: Extra plans? */
                                        (List *) fdw_private);

    add_path(baserel, (Path *)path);
}

/*
 * orcGetForeignUpperPaths
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
void
orcGetForeignUpperPaths(PlannerInfo *root, UpperRelationKind stage, RelOptInfo *input_rel, RelOptInfo *output_rel, void *extra)
{
	if (stage == UPPERREL_GROUP_AGG && input_rel->fdw_private)
        ((OrcFdwPlanState *)input_rel->fdw_private)->hasAggregate = true;
}

/*
 * orcGetForeignPlan
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
ForeignScan *
orcGetForeignPlan(PlannerInfo *root, RelOptInfo *baserel,
            Oid foreigntableid, ForeignPath *best_path, 
            List *tlist, List *scan_clauses,
            Plan *outer_plan)
{
    Index scan_relid = baserel->relid;
    OrcFdwPlanState *fdw_state = (OrcFdwPlanState *)(best_path->fdw_private);
    List *fdw_private;
    bool blnShouldSetRowReader = (fdw_state->hasAggregate == false);

    scan_clauses = extract_actual_clauses(scan_clauses, false);

    /* Set column details in a list to be used in the execution state */
    (void) getColumnNameList(baserel, fdw_state, tlist);
    fdw_private = list_make3(makeString(fdw_state->filename),
                                fdw_state->col_orc_file_index,
                                makeInteger(blnShouldSetRowReader));

    /* We are not going to update the fdw_scan_tlist for the time being.
     * Scan tlist must also contain any columns required by the query.
     * So, currently, let's pass NIL and assume that the whole row must
     * be fetched. In private data, we have a column list to fetch from
     * from ORC. We'll use that to modify the reader. */
	List *fdw_scan_tlist = NIL;

    if (fdw_state->hasAggregate == false)
    {
        fdw_scan_tlist = build_tlist_to_deparse(baserel);
    }

    /*
     * Now fix the subplan's tlist --- this might result in inserting
     * a Result node atop the plan tree.
     */
    return make_foreignscan(tlist, 
                    scan_clauses, 
                    scan_relid,
                    NIL, 
                    fdw_private,
                    fdw_scan_tlist,
                    NIL,
                    outer_plan);
}

extern "C"
bool
orcRecheckForeignScan(ForeignScanState *node, TupleTableSlot *slot)
{
    return true;
}

/*
 * orcExplainForeignScan
 *    Put out ORC file reading specific details here
 */
extern "C"
void
orcExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
    OrcFdwExecState *fdw_estate = (OrcFdwExecState *)node->fdw_state;

    if (es->verbose)
    {
        bool hasColumns = false;
        std::stringstream ss;

        for (auto col = fdw_estate->cols_info.begin(); col != fdw_estate->cols_info.end(); col++)
        {
            if (hasColumns)
                ss << ", ";

            ss << (*col).name;
            hasColumns = true;
        }

        ExplainPropertyText("ORC File Reader Columns", ss.str().c_str(), es);
    }
}

/*
 * orcBeginForeignScan
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
void
orcBeginForeignScan(ForeignScanState *node, int eflags)
{
    List *col_orc_file_index;
    char *filename;
    bool blnShouldSetRowReader = false;
    int rtindex;
	RangeTblEntry *rte;
    OrcFdwExecState *fdw_estate;
    ForeignScan *plan = castNode(ForeignScan, node->ss.ps.plan);
    List *fdw_private = plan->fdw_private;
    List *fdw_scan_tlist = plan->fdw_scan_tlist;
    EState *estate = node->ss.ps.state;

	if (plan->scan.scanrelid > 0)
		rtindex = plan->scan.scanrelid;
	else
		rtindex = bms_next_member(plan->fs_relids, -1);

	rte = exec_rt_fetch(rtindex, estate);

    filename = strVal((Value *) linitial(fdw_private));
    col_orc_file_index = (List *) lsecond(fdw_private);
    blnShouldSetRowReader = (bool)intVal((Value *)lthird(fdw_private));

    /* Initialize and set execution state */
    node->fdw_state = orcInitExecState(&fdw_estate, filename, col_orc_file_index, rte, fdw_scan_tlist, blnShouldSetRowReader);
}

/*
 * orcIterateForeignScan
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
TupleTableSlot *
orcIterateForeignScan(ForeignScanState *node)
{
    OrcFdwExecState *fdw_estate = (OrcFdwExecState *) node->fdw_state;
    TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
    bool fetch_next_batch = false;

    ExecClearTuple(slot);

    /* We've reached the end */
    if (fdw_estate->row_num >= fdw_estate->total_rows)
    {
        return slot;
    }

    /* FIXME: batchsize instead */
    if (fdw_estate->curr_batch_total_rows == -1
        || (fdw_estate->row_num % fdw_estate->batch->numElements) == 0)
    {
        fetch_next_batch = true;
    }

    if (fetch_next_batch)
    {
        /* If next fails, we've reached the end. */
        if (! fdw_estate->rowReader->next(*(fdw_estate->batch)))
            return slot;

        fdw_estate->curr_batch_number++;
        fdw_estate->curr_batch_row_num = 0;
        fdw_estate->curr_batch_total_rows = fdw_estate->batch->numElements;
    }

    /* Store virtual tuple with details in slot */
    ExecStoreVirtualTuple(fillSlot(fdw_estate, slot));

    return slot;
}

/*
 * orcReScanForeignScan
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
void
orcReScanForeignScan(ForeignScanState *node)
{
    OrcFdwExecState *fdw_estate = (OrcFdwExecState *)(node->fdw_state);

    /* Reset all counters and state variables */
    fdw_estate->batch = fdw_estate->rowReader->createRowBatch(fdw_estate->batchsize);
    fdw_estate->curr_batch_total_rows = -1;
    fdw_estate->curr_batch_number = 0;
    fdw_estate->curr_batch_row_num = 0;
    fdw_estate->row_num = 0;
}

/*
 * orcEndForeignScan
 *    ORC FDW function set in orc_fdw.c
 */
extern "C"
void
orcEndForeignScan(ForeignScanState *node)
{
    OrcFdwExecState *fdw_estate = (OrcFdwExecState *)(node->fdw_state);

    if (fdw_estate != NULL)
    {
        if (fdw_estate->is_valid_reader)
        {
            if (fdw_estate->batch)
                fdw_estate->batch.reset();

            if (fdw_estate->rowReader)
                fdw_estate->rowReader.reset();

            if (fdw_estate->reader)
                fdw_estate->reader.reset();
        }

        delete fdw_estate;
    }
}
