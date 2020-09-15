/*-------------------------------------------------------------------------
 *
 * orc_fdw.c
 *    Sets the function pointers for FDW to be called by PG
 *
 * 2020, Hamid Quddus Akhtar.
 *
 *    This file sets the function pointers for FDW and defines some basic
 *    functions for FDW.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    src/orc_fdw.c
 *
 *-------------------------------------------------------------------------
 */

/* PG includes */
#include "postgres.h"
#include "fmgr.h"
#include "access/reloptions.h"
#include "catalog/pg_foreign_table.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "foreign/fdwapi.h"
#include "nodes/pg_list.h"
#include "optimizer/planmain.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"

/* ORC FDW specific includes */
#include <orc_fdw.h>
#include <orc_interface.h>


/* Magic */
PG_MODULE_MAGIC;


/* FDW declaration */
void _PG_init(void);

extern Datum orc_fdw_validator(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(orc_fdw_handler);
PG_FUNCTION_INFO_V1(orc_fdw_validator);
PG_FUNCTION_INFO_V1(orc_fdw_version);

List *orcImportForeignSchema(ImportForeignSchemaStmt *stmt, Oid serverOid);
bool orcAnalyzeForeignTable(Relation relation, AcquireSampleRowsFunc *func, BlockNumber *totalpages);
bool orcIsForeignScanParallelSafe(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte);
int orcIsForeignRelUpdatable(Relation rel);
void orcAddForeignUpdateTargets(Query *parsetree, RangeTblEntry *target_rte, Relation target_relation);
List *orcPlanForeignModify(PlannerInfo *root, ModifyTable *plan, Index resultRelation, int subplan_index);
void orcBeginForeignModify(ModifyTableState *mstate, ResultRelInfo *rinfo, List *fdw_private, int subplan_index, int eflags);
TupleTableSlot *orcExecForeignInsert(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot);
TupleTableSlot *orcExecForeignUpdate(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot);
TupleTableSlot *orcExecForeignDelete(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot);
void orcEndForiegnModify(EState *estate, ResultRelInfo *rinfo);
void orcExplainForeignModify(ModifyTableState *mstate, ResultRelInfo *rinfo, List *fdw_private, int subplan_index, struct ExplainState *es);
void orcEndForeignModify(EState *estate, ResultRelInfo *rinfo);

/* FDW routines */

/*
 * _PG_init
 *    Initialisation function
 */
void
_PG_init(void)
{
}

/*
 * orc_fdw_handler
 *    Sets function pointers for FDW functions
 */
Datum
orc_fdw_handler(PG_FUNCTION_ARGS)
{
    FdwRoutine *fdwroutine = makeNode(FdwRoutine);

    /* Implemented functions */
    fdwroutine->GetForeignRelSize = orcGetForeignRelSize;
    fdwroutine->GetForeignPaths = orcGetForeignPaths;
    fdwroutine->GetForeignUpperPaths = orcGetForeignUpperPaths;
    fdwroutine->GetForeignPlan = orcGetForeignPlan;
    fdwroutine->BeginForeignScan = orcBeginForeignScan;
    fdwroutine->IterateForeignScan = orcIterateForeignScan;
    fdwroutine->ReScanForeignScan = orcReScanForeignScan;
    fdwroutine->EndForeignScan = orcEndForeignScan;
    fdwroutine->ImportForeignSchema = orcImportForeignSchema;

    /* Not fully implemented functions; only throwing errors ATM */
    fdwroutine->AnalyzeForeignTable = orcAnalyzeForeignTable;
    fdwroutine->ExplainForeignScan = orcExplainForeignScan;
    fdwroutine->IsForeignScanParallelSafe = orcIsForeignScanParallelSafe;

	fdwroutine->IsForeignRelUpdatable = orcIsForeignRelUpdatable;

	fdwroutine->AddForeignUpdateTargets = orcAddForeignUpdateTargets;
	fdwroutine->PlanForeignModify = orcPlanForeignModify;
	fdwroutine->BeginForeignModify = orcBeginForeignModify;
	fdwroutine->ExecForeignInsert = orcExecForeignInsert;
	fdwroutine->ExecForeignUpdate = orcExecForeignUpdate;
	fdwroutine->ExecForeignDelete = orcExecForeignDelete;
	fdwroutine->EndForeignModify = orcEndForeignModify;
	fdwroutine->ExplainForeignModify = orcExplainForeignModify;

	fdwroutine->GetForeignJoinPaths = orcGetForeignJoinPaths;

    fdwroutine->RecheckForeignScan = orcRecheckForeignScan;

    PG_RETURN_POINTER(fdwroutine);
}

/*
 * orc_fdw_validator
 *    Validate options for FDW. Currently, we are only supporting
 *    filename option for a table.
 */
Datum
orc_fdw_validator(PG_FUNCTION_ARGS)
{
    List       *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
    Oid         catalog = PG_GETARG_OID(1);
    bool        hasFilename = false;

    /* Check only for table options */
    if (catalog != ForeignTableRelationId)
        PG_RETURN_VOID();

    hasFilename = getTableOptions(options_list, NULL);

    /* filename is a must for a table */
    if (!hasFilename)
        elog(ERROR, "%s: filename option not specified for table.", 
             ORC_FDW_NAME);

    PG_RETURN_VOID();
}

/*
 * orc_fdw_version
 *    Return the FDW version
 */
Datum
orc_fdw_version(PG_FUNCTION_ARGS)
{
    PG_RETURN_TEXT_P(cstring_to_text(ORC_FDW_VERSION));
}

/*
 * orcImportForeignSchema
 *    Imports schema from a given folder whilst supporting all the syntax
 *    options.
 *
 *    IMPORT FOREIGN SCHEMA "<PATH>" FROM SERVER <ORC_SRV> INTO <SCHEMA>;
 */
List *orcImportForeignSchema(ImportForeignSchemaStmt *stmt, Oid serverOid)
{
    DIR *schemaDir;
    struct dirent *orc_f;
    List *schemaCmds = NIL;
    int orcFilesFound = 0;

    schemaDir = AllocateDir(stmt->remote_schema);

    /* shouldProcess is true if no table list is provided, otherwise
     * set it to false and check for table entries. */
    while ((orc_f = ReadDir(schemaDir, stmt->remote_schema)) != NULL)
    {
        ListCell *lc;
        bool shouldProcess = true;
        char *filename = orc_f->d_name;
        char *file_ext = strrchr(filename, '.');
        char *cmd = NULL;
        int name_len;

        /* Doesn't have an extension, so probably not a valid ORC file. */
        if (file_ext == NULL)
            continue;

        /* Get length for name part only and skip the dot in extension */
        name_len = (int)(file_ext - filename);
        file_ext++;

        /* Ignore non-regular files */
        if (orc_f->d_type != DT_REG)
            continue;

        /* Extension doesn't match, let's skip this entry as well */
        if (pg_strcasecmp(file_ext, ORC_FILE_EXT))
            continue;

        /* Check if the table listed must be included or excluded */
        foreach(lc, stmt->table_list)
        {
            RangeVar *rel = (RangeVar *) lfirst(lc);
            bool match = (pg_strncasecmp(rel->relname, 
                            filename, name_len) == 0);

            /* Process the file by default, unless it matches a relname */
            shouldProcess = (stmt->list_type != FDW_IMPORT_SCHEMA_LIMIT_TO);

            /* Match found in the list of relnames */
            if (match)
            {
                if(stmt->list_type == FDW_IMPORT_SCHEMA_LIMIT_TO)
                    shouldProcess = true;

                if(stmt->list_type == FDW_IMPORT_SCHEMA_EXCEPT)
                    shouldProcess = false;

                break;
            }
        }

        /* Nothing to process */
        if (! shouldProcess)
            continue;

        /* Found a file with .orc file extension */
        ereport(INFO,
                    (errmsg("ORC file %s found for schema import.",
                                    filename)));

        /* Let's get schema from file */
        if (getSchemaSQL(stmt, filename, &cmd) == false)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                        errmsg("%s: [Filename: %s] something went wrong while trying to read schema.", 
                        ORC_FDW_NAME, filename)));
        }

        ereport(DEBUG1,
                    (errmsg("ORC FDW: import SQL: %s",
                                    cmd)));

        orcFilesFound++;
        schemaCmds = lappend(schemaCmds, cmd);
    }

    /* If no files found, throw an info */
    if (orcFilesFound == 0)
    {
        ereport(INFO,
                    (errmsg("No files processed."),
                    (errhint("Did you specify the correct folder path with .%s files?",
                                    ORC_FILE_EXT))));
    }
    else
    {
        ereport(INFO,
                    (errmsg("Schema read successfully from %d %s files.",
                                    orcFilesFound, ORC_FILE_EXT)));
    }

    FreeDir(schemaDir);
    return schemaCmds;
}

/*
 * -----------------------------
 * FUNCTIONS NOT YET IMPLEMENTED
 * -----------------------------
 * The following function implementation is not included in the current release scope.
 */
bool
orcAnalyzeForeignTable(Relation relation, AcquireSampleRowsFunc *func, BlockNumber *totalpages)
{
    ereport(ERROR, (errmsg("%s: ANALYZE table %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));

    /* Not supporting this at the moment */
    return false;
}

bool
orcIsForeignScanParallelSafe(PlannerInfo *root, RelOptInfo *rel, RangeTblEntry *rte)
{
    /* Not supporting this at the moment */
    return false;
}

int
orcIsForeignRelUpdatable(Relation rel)
{
    ereport(INFO, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));

    /* Not supporting this at the moment */
    return 0;
}

void
orcAddForeignUpdateTargets(Query *parsetree, RangeTblEntry *target_rte, Relation target_relation)
{
    ereport(ERROR, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));
}

List *
orcPlanForeignModify(PlannerInfo *root, ModifyTable *plan, Index resultRelation, int subplan_index)
{
    ereport(ERROR, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));

    /* Not supporting this at the moment */
    return NULL;
}

void
orcBeginForeignModify(ModifyTableState *mstate, ResultRelInfo *rinfo, List *fdw_private, int subplan_index, int eflags)
{
    ereport(ERROR, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));
}

TupleTableSlot *
orcExecForeignInsert(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot)
{
    ereport(ERROR, (errmsg("%s: INSERT %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));

    /* Not supporting this at the moment */
    return NULL;
}

TupleTableSlot *
orcExecForeignUpdate(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot)
{
    ereport(ERROR, (errmsg("%s: UPDATE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));

    /* Not supporting this at the moment */
    return NULL;
}

TupleTableSlot *
orcExecForeignDelete(EState *estate, ResultRelInfo *rinfo, TupleTableSlot *slot, TupleTableSlot *planSlot)
{
    ereport(ERROR, (errmsg("%s: DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));

    /* Not supporting this at the moment */
    return NULL;
}

void
orcEndForiegnModify(EState *estate, ResultRelInfo *rinfo)
{
    /* Not supporting this at the moment */
    ereport(ERROR, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));
}

void
orcExplainForeignModify(ModifyTableState *mstate, ResultRelInfo *rinfo, List *fdw_private, int subplan_index, struct ExplainState *es)
{
    /* Not supporting this at the moment */
    ereport(ERROR, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));
}

void
orcEndForeignModify(EState *estate, ResultRelInfo *rinfo)
{
    /* Not supporting this at the moment */
    ereport(ERROR, (errmsg("%s: INSERT, UPDATE and DELETE %s", ORC_FDW_NAME, ORC_MSG_UNSUPPORTED)));
}
