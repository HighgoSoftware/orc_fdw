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

/* myfile - Let's try joins in different ways */
SELECT  *
FROM    myfile mf1 INNER JOIN myfile mf2
        ON mf1.x = mf2.x
LIMIT   5;

SELECT  mf1.x
        , mf2.y
FROM    myfile mf1 INNER JOIN myfile mf2
        ON mf1.x = mf2.y
LIMIT   5;

/* Cross join */
SELECT  *
FROM    myfile mf1, myfile mf2
LIMIT   50;

/* Outer joins */
SELECT	*
FROM	myfile mf1 LEFT OUTER JOIN myfile mf2
	ON mf1.x = mf2.x
LIMIT	10;

/* Sub-query */
SELECT  mf1.x
        , mf1.y
FROM    myfile mf1
WHERE   mf1.x IN
        (
                SELECT  mf2.x
                FROM    myfile mf2
                WHERE   mf2.x < 10
        )
LIMIT   30;

SELECT	mf1.x
	, mf1.y
FROM	myfile mf1
WHERE	mf1.x IN
	(
		SELECT	mf2.x
		FROM	myfile mf2
		WHERE	mf2.x = mf1.x
	)
LIMIT	30;

/* Cleanup */
DROP EXTENSION orc_fdw CASCADE;
