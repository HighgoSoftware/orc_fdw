/*-------------------------------------------------------------------------
 *
 * import_schema.sql
 *    Test schema import functionality
 *
 *    REQUIRES:
 *      ORC_FDW_DIR variable to be set in the shell which is initiating
 *      the regression.
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    sql/import_schema.sql
 *
 *-------------------------------------------------------------------------
 */

\set orc_sample_dir     `echo ${ORC_FDW_DIR}/sample/data`

/* Create extension */
CREATE EXTENSION orc_fdw;

/* Create server */
CREATE SERVER orc_srv FOREIGN DATA WRAPPER orc_fdw;

/* Import all files with .orc extension */
IMPORT
FOREIGN SCHEMA :"orc_sample_dir"
FROM    SERVER orc_srv
INTO    public;

/* Let's check the imported tables */
\dE
\dS decimal
\dS myfile
\dS orc_file_11_format

SELECT  *
FROM    myfile
LIMIT   1;

SELECT  *
FROM    decimal
LIMIT   1;

SELECT  boolean1
        , byte1
        , short1
        , int1
        , long1
        , float1
        , double1
        -- , bytes1
        -- , string1
        , ts
        , decimal1 
FROM    orc_file_11_format
LIMIT   1;

/* Let's drop a table and retry importing a subset this time */
DROP FOREIGN TABLE myfile;

\dE

IMPORT
FOREIGN SCHEMA :"orc_sample_dir"
LIMIT   TO (myfile)
FROM    SERVER orc_srv
INTO    public;

\dE

/* Let's drop a table and retry importing a subset this time */
DROP FOREIGN TABLE myfile;

\dE

IMPORT
FOREIGN SCHEMA :"orc_sample_dir"
EXCEPT  (decimal, orc_file_11_format)
FROM    SERVER orc_srv
INTO    public;

\dE

/* Cleanup */
DROP EXTENSION orc_fdw CASCADE;
