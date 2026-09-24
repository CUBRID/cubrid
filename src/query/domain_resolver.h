/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#ifndef _DOMAIN_RESOLVER_H_
#define _DOMAIN_RESOLVER_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif

#include "object_domain.h" /* TP_DOMAIN_STATUS and shared value types, no client API. */

typedef TP_DOMAIN_STATUS (*DOMAIN_CONV_FUNC) (const DB_VALUE *, DB_VALUE *, const TP_DOMAIN *);

enum DOMAIN_CTX
{
  DOMAIN_CTX_ARITH, DOMAIN_CTX_COMPARE, DOMAIN_CTX_ASSIGN, DOMAIN_CTX_COMMON_VALUE,
  DOMAIN_CTX_AGG, DOMAIN_CTX_ANALYTIC, DOMAIN_CTX_FUNC_ARG, DOMAIN_CTX_LIST_COLUMN, DOMAIN_CTX_KEY_ELEM
};

struct RESOLVED_DOMAIN
{
  const TP_DOMAIN *domain;
  DOMAIN_CONV_FUNC conv[3];
  const TP_DOMAIN *operand_domain[3];
  const TP_DOMAIN *setdomain;
};

struct DOMAIN_OPERAND
{
  const TP_DOMAIN *domain;	/* plan domain at load, value domain at the gate */
  DB_TYPE val_type;		/* gate only (classified for value-overloaded slots); DB_TYPE_NULL at load */
  int coll_id;			/* character operands only; -1 otherwise */
  int coercibility;		/* character operands only; -1 otherwise */
  bool is_gate_slot;
};

/* Context-to-mode adapter. */
DOMAIN_CONV_FUNC domain_lookup_converter (DB_TYPE source, const TP_DOMAIN *target, DOMAIN_CTX context);

/*
 * The single home of the server G-row grid (D-318-07, D-323-02): the value-type rules that execution applies today
 * in qdata_{add,subtract,multiply,divide}_dbval, tp_value_compare_with_error, tp_infer_common_domain,
 * qexec_resolve_domains_for_aggregation, the analytic late binding, ADDTIME and STR_TO_DATE, kept answer for answer
 * (P0). Result domains are cache domains (no caller-owned allocation, no er_set; #333). An operand with
 * val_type == DB_TYPE_NULL whose domain is not fixed sets *needs_gate and leaves result untouched.
 * opcode is OPERATOR_TYPE for ARITH/COMPARE/COMMON_VALUE/FUNC_ARG and FUNC_CODE for AGG/ANALYTIC.
 * AGG/ANALYTIC: consumer_domain = the function's compiled domain; operands[0] = the argument (is_gate_slot = the gate
 * decides it, opr_dbtype VARIABLE today); result domain = the function domain, operand_domain[0] = accumulator domain.
 * result: domain = result (COMPARE: comparison domain), operand_domain[i] = target of operand i, conv[i] = operand
 * converter in the grid's mode (D-328-03: always looked up against operand_domain[i]).
 */
int domain_resolve (DOMAIN_CTX context, int opcode, const DOMAIN_OPERAND * operands, int n_operands,
		    const TP_DOMAIN * consumer_domain, RESOLVED_DOMAIN * result, bool * needs_gate);

/* val_type of a value-overloaded slot (MEDIAN/PERCENTILE argument, STR_TO_DATE format, ADDTIME left). Gate only,
 * once, before domain_resolve (D-328-06). DB_TYPE_NULL when the value cannot be classified: the function's own error. */
DB_TYPE domain_classify_value (DOMAIN_CTX context, int opcode, int arg_index, const DB_VALUE * value);

/*
 * The character result of a node the compiler typed but whose collation it left to the values (LEAVE) or enforced
 * over an operand it could not type (ENFORCE), decided at the gate from the operands' decided domains (#338): the
 * type, collation and codeset its operator gives the value today. A variable string's precision is floating
 * (D-338-03). opcode is OPERATOR_TYPE for an arithmetic node and FUNC_CODE for a function node; compiled is the
 * node's compiled domain.
 * return: NO_ERROR, ER_QSTR_INCOMPATIBLE_COLLATIONS when the operands' collations do not merge (the row raises it, as
 *	   develop does), or ER_QPROC_DOMAIN_UNRESOLVED when the value's domain depends on the row (a branch chosen per
 *	   row whose domains differ): the gate then leaves the node undecided.
 */
int domain_resolve_character (int opcode, const DOMAIN_OPERAND * operands, int n_operands, const TP_DOMAIN * compiled,
			      RESOLVED_DOMAIN * result);

/* The domain tp_domain_resolve_value gives a value of this domain: a variable string's floating precision reads as
 * its maximum, and an ENUM value keeps no element list (#338). */
const TP_DOMAIN *domain_as_value_domain (const TP_DOMAIN * domain);

#endif /* _DOMAIN_RESOLVER_H_ */
