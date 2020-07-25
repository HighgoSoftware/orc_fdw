/*-------------------------------------------------------------------------
 *
 * orc_deparse.cpp
 *    Deparsing a query; mainly handling remote vs local conditions
 *    in deciding push down capabilities.
 *
 * 2020, Hamid Quddus Akhtar.
 *
 *    Functions to differentiate between local and foreign
 *    conditions/expressions/etc. are implemented in this file.
 *    It provides the mechanims for creation of a target list 
 *    for a plan.
 *
 * Copyright (c) 2020, Highgo Software Inc.
 *
 * IDENTIFICATION
 *    src/orc_deparse.cpp
 *
 *-------------------------------------------------------------------------
 */

/* ORC FDW header files */
#include <orc_deparse.h>
#include <orc_interface_typedefs.h>

/* PostgreSQL header files */
extern "C"
{
	#include "postgres.h"
	#include "nodes/nodeFuncs.h"
	#include "nodes/plannodes.h"
	#include "optimizer/optimizer.h"
	#include "optimizer/prep.h"
	#include "optimizer/tlist.h"
	#include "utils/builtins.h"
	#include "utils/rel.h"

    #include "nodes/print.h"
}

static bool foreign_expr_walker(Node *node, RelOptInfo *baserel);

/*
 * Classify input condition as remote or local. Remote conditions
 * may be pushed down to ORC library, local can't be done by ORC
 * library.
 */
void
classifyConditions(PlannerInfo *root, RelOptInfo *baserel, List *input_conds, List **remote_conds, List **local_conds)
{
	ListCell   *lc;

	*remote_conds = NIL;
	*local_conds = NIL;

	foreach(lc, input_conds)
	{
		RestrictInfo *ri = lfirst_node(RestrictInfo, lc);

		if (is_foreign_expr(root, baserel, ri->clause))
			*remote_conds = lappend(*remote_conds, ri);
		else
			*local_conds = lappend(*local_conds, ri);
	}
}

/*
 * Returns true if given expression is safe to evaluate on the remotely.
 */
bool
is_foreign_expr(PlannerInfo *root, RelOptInfo *baserel, Expr *expr)
{
	if (!foreign_expr_walker((Node *) expr, baserel))
		return false;

	/*
	 * Mutable functions can't be pushdown.
	 */
	if (contain_mutable_functions((Node *) expr))
		return false;

	/* OK to evaluate remotely */
	return true;
}

/*
 * Returns true if expression is safe to execute remotely.
 */
static bool
foreign_expr_walker(Node *node,
					RelOptInfo *baserel)
{
	/* Need do nothing for empty subexpressions */
	if (node == NULL)
		return true;

	switch (nodeTag(node))
	{
		case T_Var:
		{
			Var		   *var = (Var *) node;

			if (bms_is_member(var->varno, baserel->relids) &&
				var->varlevelsup == 0)
			{
				if (var->varattno < 0 &&
					var->varattno != SelfItemPointerAttributeNumber)
					return false;
			}

			break;
		}
		/* Not handling these in the current version */
		case T_Const:
		case T_Param:
		case T_SubscriptingRef:
		case T_FuncExpr:
		case T_OpExpr:
		case T_DistinctExpr:
		case T_ScalarArrayOpExpr:
		case T_RelabelType:
		case T_BoolExpr:
		case T_NullTest:
		case T_ArrayExpr:
		case T_List:
		case T_Aggref:
		{
			return false;
			break;
		}
		default:
		{
			/*
			 * Return false for everything else
			 */
			return false;
		}
	}

	/* Unreachable code so let's return true */
	return true;
}

/*
 * Returns a target list containing columns that need to be read from the
 * ORC file.
 */
List *
build_tlist_to_deparse(RelOptInfo *foreignrel)
{
	List	   *tlist = NIL;
	OrcFdwPlanState *fpinfo = (OrcFdwPlanState *) foreignrel->fdw_private;
	ListCell   *lc;

	/*
	 * Get columns specified in foreignrel->reltarget->exprs and those
	 * required for evaluating the local conditions.
	 */
	tlist = add_to_flat_tlist(tlist, pull_var_clause((Node *) foreignrel->reltarget->exprs, PVC_RECURSE_PLACEHOLDERS));

	foreach(lc, fpinfo->local_conds)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);
		tlist = add_to_flat_tlist(tlist, pull_var_clause((Node *) rinfo->clause, PVC_RECURSE_PLACEHOLDERS));
	}

	return tlist;
}
