/*-------------------------------------------------------------------------
 *
 * orc_interface.h
 *    An intermediary to connect FDW with Apache ORC wrapper.
 *
 * 2020, Hamid Quddus Akhtar.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    include/orc_interface.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef __ORC_INTERFACE_H
#define __ORC_INTERFACE_H

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

/* FDW function */
void orcGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
void orcGetForeignPaths(PlannerInfo *root, RelOptInfo *baserel, Oid foreigntableid);
void orcGetForeignUpperPaths(PlannerInfo *root, UpperRelationKind stage, RelOptInfo *input_rel, RelOptInfo *output_rel, void *extra);
void orcGetForeignJoinPaths(PlannerInfo *root, RelOptInfo *joinrel, RelOptInfo *outerrel, RelOptInfo *innerrel, JoinType jointype, JoinPathExtraData *extra);

ForeignScan *orcGetForeignPlan(PlannerInfo *root, RelOptInfo *baserel, 
                Oid foreigntableid, ForeignPath *best_path, 
                List *tlist, List *scan_clauses, Plan *outer_plan);

bool orcRecheckForeignScan(ForeignScanState *node, TupleTableSlot *slot);
void orcExplainForeignScan(ForeignScanState *node, ExplainState *es);

void orcBeginForeignScan(ForeignScanState *node, int eflags);
TupleTableSlot *orcIterateForeignScan(ForeignScanState *node);

void orcReScanForeignScan(ForeignScanState *node);
void orcEndForeignScan(ForeignScanState *node);


/* Exported functions */
bool getSchemaSQL(ImportForeignSchemaStmt *stmt, const char *f, char **cmd);
bool getTableOptionsFromRelID(Oid foriegntableid, OrcFdwPlanState *fdw_state);
bool getTableOptions(List *options_list, OrcFdwPlanState *fdw_state);

#ifdef __cplusplus
}
#endif

#endif
