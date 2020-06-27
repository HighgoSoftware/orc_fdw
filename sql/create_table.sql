/*-------------------------------------------------------------------------
 *
 * create_table.sql
 *    Test table creation functionality.
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
 *    sql/create_table.sql
 *
 *-------------------------------------------------------------------------
 */

\set file_myfile `echo ${ORC_FDW_DIR}/sample/data/myfile.orc`
\set file_orc11  `echo ${ORC_FDW_DIR}/sample/data/orc_file_11_format.orc`

/* Create extension */
CREATE EXTENSION orc_fdw;

/* Create server */
CREATE SERVER orc_srv FOREIGN DATA WRAPPER orc_fdw;

/* Create a foreign table */
CREATE FOREIGN TABLE myfile
(
    x       INT
    , y     INT
)
SERVER orc_srv OPTIONS
(
    FILENAME :'file_myfile'
);

/* SELECT data from the foreign table */
SELECT  *
FROM    myfile
LIMIT   5;

/* Create a foreign table */
CREATE FOREIGN TABLE myfile_t
(
    x       INT
    , y     TEXT
)
SERVER orc_srv OPTIONS
(
    FILENAME :'file_myfile'
);

/* SELECT data from the foreign table; get error for y */
SELECT  *
FROM    myfile_t
LIMIT   5;

/* Create a foreign table table with some unmatched columns */
CREATE FOREIGN TABLE orc11
(
    boolean1    BOOLEAN
    , y         TEXT
    , z         INTEGER DEFAULT(5)
)
SERVER orc_srv OPTIONS
(
    FILENAME :'file_orc11'
);

/* Return NULLs for unmatched columns */
SELECT  *
FROM    orc11
LIMIT   5;

/* Cleanup */
DROP FOREIGN TABLE orc11;
DROP FOREIGN TABLE myfile_t;
DROP FOREIGN TABLE myfile;
DROP SERVER orc_srv;
DROP EXTENSION orc_fdw;
