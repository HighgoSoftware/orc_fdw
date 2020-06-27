/*-------------------------------------------------------------------------
 *
 * misc.sql
 *    Different types of tests including explain and unsupported features
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
 *    sql/misc.sql
 *
 *-------------------------------------------------------------------------
 */

\set orc_sample_dir     `echo ${ORC_FDW_DIR}/sample/data`
\set orc_no_format      `echo ${ORC_FDW_DIR}/sample/data/errs`
\set orc_dir_not_valid  `echo ${ORC_FDW_DIR}/sample/data/xyz103`
\set orc_dir_sql        `echo ${ORC_FDW_DIR}/sql`
\set orc_no_such_file   `echo ${ORC_FDW_DIR}/nosuchfile.orc`
\set file_myfile        `echo ${ORC_FDW_DIR}/sample/data/myfile.orc`

/* Create extension */
CREATE EXTENSION orc_fdw;

/* Create server */
CREATE SERVER orc_srv FOREIGN DATA WRAPPER orc_fdw;

/* Let's check version */
select orc_fdw_version();

/* Import all files with .orc extension */
IMPORT
FOREIGN SCHEMA :"orc_sample_dir"
FROM    SERVER orc_srv
INTO    public;

/* Explain */
EXPLAIN VERBOSE
SELECT  *
FROM    myfile;

EXPLAIN VERBOSE
SELECT  x
FROM    myfile;

EXPLAIN VERBOSE
SELECT  y
FROM    myfile;

EXPLAIN VERBOSE
SELECT  x
FROM    myfile
WHERE   y < 10;

EXPLAIN VERBOSE
SELECT  y
FROM    myfile
WHERE   x < 10;

EXPLAIN VERBOSE
SELECT  *
FROM    orc_file_11_format;

EXPLAIN VERBOSE
SELECT  boolean1
        , CASE  boolean1
            WHEN  true    THEN 'yes'
            WHEN  false   THEN 'no'
            ELSE  'never'
          END
        , short1 % 5 AS short1_mod5
        , int1
        , long1
        , float1
        , double1
        , bytes1
        , string1
        , ts
        , decimal1 
FROM    orc_file_11_format
LIMIT   2;

/* Error checking */
IMPORT
FOREIGN SCHEMA :"orc_dir_not_valid"
FROM    SERVER orc_srv
INTO    public;

IMPORT
FOREIGN SCHEMA :"orc_dir_sql"
FROM    SERVER orc_srv
INTO    public;

IMPORT
FOREIGN SCHEMA :"orc_no_format"
FROM    SERVER orc_srv
INTO    public;

CREATE FOREIGN TABLE orc_no_option
(
    x       INT
    , y     INT
)
SERVER orc_srv;

CREATE FOREIGN TABLE orc_no_such_file
(
    x       INT
    , y     INT
)
SERVER orc_srv OPTIONS
(
    FILENAME :'orc_no_such_file'
);

CREATE FOREIGN TABLE myfile_invalid_option
(
    x       INT
    , y     INT
)
SERVER orc_srv OPTIONS
(
    FILENAME :'file_myfile'
    , INVALID_OPTION 'error'
);

/* Unsupported features */
INSERT
INTO    myfile
VALUES  (19, 1039);

UPDATE  myfile
SET     x = -10
WHERE   x = 1;

DELETE
FROM    myfile
WHERE   x = 1;

ANALYZE myfile;

/* Cleanup */
DROP EXTENSION orc_fdw CASCADE;
