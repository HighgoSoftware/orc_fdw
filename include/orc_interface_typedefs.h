/*-------------------------------------------------------------------------
 *
 * orc_interface_typedefs.h
 *    Defines state and column information structures.
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    include/orc_interface_typedefs.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __ORC_INTERFACE_TYPEDEFS_H
#define __ORC_INTERFACE_TYPEDEFS_H

/* C++ header files */
#include <string>
#include <vector>

/* Apache ORC header files */
#include <orc/OrcFile.hh>
#include <orc/Type.hh>

/* PostgreSQL header files */
extern "C"
{
    #include "postgres.h"
    #include "fmgr.h"
    #include "access/tupdesc.h"
    #include "foreign/foreign.h"
}

/* To be used for mapping of ORC to PG data types */
typedef enum OrcPgTypeKind
{
    BOOLEAN = 0,
    BYTE = 1,
    SHORT = 2,
    INT = 3,
    LONG = 4,
    FLOAT = 5,
    DOUBLE = 6,
    STRING = 7,
    BINARY = 8,
    TIMESTAMP = 9,
    LIST_UNSUPPORTED = 10,       // UNSUPPORTED; ORC Type = LIST
    MAP_UNSUPPORTED = 11,        // UNSUPPORTED; ORC Type = MAP
    STRUCT_UNSUPPORTED = 12,     // UNSUPPORTED; ORC Type = STRUCT
    UNION_UNSUPPORTED = 13,      // UNSUPPORTED; ORC Type = UNION
    DECIMAL = 14,
    DATE = 15,
    VARCHAR = 16,
    CHAR = 17,
    UNKNOWN_TYPE = 8888,
    UNSUPPORTED_TYPE = 9999
} OrcPgTypeKind;


/*
 * Column meta data in ORC file
 * - column name
 * - index in ORC file
 * - column ORC type
 * - max_length
 * - precision
 * - scale
 * - has NULLs?
 */
struct OrcFileColInfo
{
    int index;
    std::string name;
    orc::TypeKind kind;
    int64_t max_length;
    int precision;
    int scale;
    bool hasNull;
};

/* Column information for supported columns in ORC file:
 * - index: index in ORC file
 * - column name
 * - internal type
 * - Oid
 * - column size
 *
 * This will help us manage in planning, costing and 
 *  conversions.
 */
struct OrcFdwColInfo
{
    int index;
    std::string name;
    OrcPgTypeKind kind;
    Oid col_oid;
    Oid col_atttypid;
    size_t size;
    int64_t max_length;
    bool hasNull;

    /* Decimal attributes */
    int precision;
    int scale;

    /* Function for typecasting data from ORC to PG */
    FmgrInfo *cast_func;
    bool is_binary_compatible;
};


/* ORC FDW - Internal Plan State */
struct OrcFdwPlanState
{
    Cost startup_cost;
    Cost tuple_cost;

    Oid foreigntableid;
    ForeignTable *table;
    uint64_t rows;

    List *col_orc_name;
    List *col_orc_oid;
    List *col_orc_file_index;

    char *filename;
};

/* ORC FDW - Internal State */
struct OrcFdwExecState
{
	orc::ReaderOptions options;
	ORC_UNIQUE_PTR<orc::Reader> reader;

	orc::RowReaderOptions rowReaderOptions;
	ORC_UNIQUE_PTR<orc::RowReader> rowReader;
	ORC_UNIQUE_PTR<orc::ColumnVectorBatch> batch;

    orc::StructVectorBatch *batch_data;

    MemoryContext estate_cxt;
    TupleDesc tupleDesc;

    bool is_valid_reader;

    /* Index of column in the ORC file */
    std::vector<int> attr_orc_index;

    /* Columns data */
    std::vector<OrcFdwColInfo> cols_info;

    /* Pathname of the ORC file */
    std::string filename;

    /* Batch size for fetching */
    int64_t batchsize;

     /* Number of rows in current batch; -1 = no batch fetched */
    int64_t curr_batch_total_rows;

    /* Batch number and row number in batch */
    int curr_batch_number;
    int64_t curr_batch_row_num;

    /* Current row number */
    int64_t row_num;

    /* Total number of rows */
    int64_t total_rows;

    /* Numeric data type defaults */
    int default_numeric_scale;
};

#endif
