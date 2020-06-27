/*-------------------------------------------------------------------------
 *
 * orc_fdw.h
 *    ORC FDW wide defines that set default values and constants
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    include/orc_fdw.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __ORC_FDW_H
#define __ORC_FDW_H


/* FDW NAME AND VERSION */

/* FDW name */
#define ORC_FDW_NAME "orc_fdw"

/* Internal version - 1.0.0 - xd major, 3d minor */
#define ORC_FDW_VERSION  "Highgo ORC FDW - 1.0.0"
#define ORC_FDW_MAJOR_VERSION   1
#define ORC_FDW_MINOR_VERSION   0000


/* DEFAULT COSTS */

/* CPU cost to start up a foreign query. */
#define ORC_DEFAULT_FDW_STARTUP_COST    100.0

/* CPU cost to process 1 row beyond cpu_tuple_cost */
#define ORC_DEFAULT_FDW_TUPLE_COST      0.05

/* MISCELLANEOUS */

/* Standard unsupported error message phrase */
#define ORC_MSG_UNSUPPORTED "options are not available in this version."

/* FDW name */
#define ORC_FILE_EXT "orc"

/* Default ORC read batch size */
#define ORC_DEFAULT_BATCH_SIZE 128

#endif
