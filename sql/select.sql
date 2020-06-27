/*-------------------------------------------------------------------------
 *
 * select.sql
 *    Test select statements
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
 *    sql/select.sql
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

/* myfile - Let's try select in different ways */
SELECT  *
FROM    myfile
LIMIT   5;

SELECT  *
FROM    myfile
WHERE   x IN (3, 9, 390);

SELECT  count(*)
FROM    myfile;

SELECT  SUM(x)
        , y
FROM    myfile
WHERE   x < 20
GROUP
BY      y
HAVING  SUM(x) < 10
ORDER
BY      y;


/* orc_file_11_format - Let's try select in different ways */
SELECT  boolean1
        , CASE  boolean1
            WHEN  true    THEN 'yes'
            WHEN  false   THEN 'no'
            ELSE  'never'
          END
        , byte1
        , short1
        , short1 % 5 AS short1_mod5
        , int1
        , int1 * 2 AS int1_x2
        , long1
        , long1 / 2 AS long1_half
        , float1
        , float1 - 0.13 AS float1_less
        , double1
        , double1 + 0.13 AS double1_add
        , bytes1
        , string1
        , 'with string1 ' || string1 AS concat_string1
        , ts
        , decimal1 
        , '2000-03-14 16:10:05'::timestamp without time zone - ts AS diff_ts
FROM    orc_file_11_format
LIMIT   2;

/* Cleanup */
DROP EXTENSION orc_fdw CASCADE;
