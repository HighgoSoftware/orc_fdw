/*-------------------------------------------------------------------------
 *
 * orc_deparse.h
 *    Deparsing a query; mainly handling remote vs local conditions
 *    in deciding push down capabilities.
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    include/orc_deparse.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __ORC_DEPARSE_H
#define __ORC_DEPARSE_H

#ifdef __cplusplus
extern "C" 
{
#endif

/* PostgreSQL header files */
#include "postgres.h"
#include "foreign/fdwapi.h"


/* Forward declaration of state structures */
typedef struct OrcFdwPlanState OrcFdwPlanState;
typedef struct OrcFdwExecState OrcFdwExecState;

/* Exported functions */
void
classifyConditions(PlannerInfo *root,
				   RelOptInfo *baserel,
				   List *input_conds,
				   List **remote_conds,
				   List **local_conds);
bool
is_foreign_expr(PlannerInfo *root,
				RelOptInfo *baserel,
				Expr *expr);
List *
build_tlist_to_deparse(RelOptInfo *foreignrel);


#ifdef __cplusplus
}
#endif

#endif
