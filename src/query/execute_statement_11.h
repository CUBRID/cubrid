/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_stat.h - This file contains do_stat extern prototypes.
 */

#ifndef _DO_STAT_H_
#define _DO_STAT_H_

#ident "$Id$"

#include "parser.h"

typedef int (PT_DO_FUNC) (PARSER_CONTEXT *, PT_NODE *);

extern bool do_Trigger_involved;

/* These are defined in db_admin.c */
extern char Db_database_name[];
extern char Db_program_name[];

/* These are defined in do_alter.c */
extern int do_alter (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_index.c */
extern int do_alter_index (PARSER_CONTEXT * parser,
			   const PT_NODE * statement);
extern int do_create_index (const PARSER_CONTEXT * parser,
			    const PT_NODE * statement);
extern int do_drop_index (PARSER_CONTEXT * parser, const PT_NODE * statement);

/* These are defined in do_tran.c */
extern int do_attach (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_commit (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_optimization_param (PARSER_CONTEXT * parser,
				      PT_NODE * statement);
extern int do_get_xaction (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_to_commit (PARSER_CONTEXT * parser,
				 PT_NODE * statement);
extern int do_rollback (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_rollback_savepoints (PARSER_CONTEXT * parser,
				   const char *sp_name);
extern int do_savepoint (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_optimization_param (PARSER_CONTEXT * parser,
				      PT_NODE * statement);
extern int do_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_xaction (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_trig.c */
extern int do_create_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_del.c */
extern int do_delete (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_delete (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_delete (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_drop.c */
extern int do_drop (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in pt_eval.c */
extern int do_drop_variable (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_eval.c */
extern int do_evaluate (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_auth.c */
extern int do_grant (const PARSER_CONTEXT * parser,
		     const PT_NODE * statement);
extern int do_revoke (const PARSER_CONTEXT * parser,
		      const PT_NODE * statement);
extern int do_create_user (const PARSER_CONTEXT * parser,
			   const PT_NODE * statement);
extern int do_drop_user (const PARSER_CONTEXT * parser,
			 const PT_NODE * statement);
extern int do_alter_user (const PARSER_CONTEXT * parser,
			  const PT_NODE * statement);

/* These are defined in do_ins.c */
extern int do_insert (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_insert (const PARSER_CONTEXT * parser,
			      const PT_NODE * statement);
extern int do_execute_insert (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_insert_template (PARSER_CONTEXT * parser, DB_OTMPL ** otemplate,
			       PT_NODE * statement,
			       const char **savepoint_name);

/* These are defined in do_meth.c */
extern int do_call_method (PARSER_CONTEXT * parser, PT_NODE * statement);
extern void do_print_classname_on_method (DB_OBJECT * self,
					  DB_VALUE * result);
extern void do_print_on_method (DB_OBJECT * self, DB_VALUE * result,
				DB_VALUE * msg);
extern void dbmeth_class_name (DB_OBJECT * self, DB_VALUE * result);
extern void dbmeth_print (DB_OBJECT * self, DB_VALUE * result,
			  DB_VALUE * msg);

/* These are defined in do_drop.c */
extern int do_rename (const PARSER_CONTEXT * parser,
		      const PT_NODE * statement);

/* These are defined in do_scope.c */
extern int do_scope (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_query.c */
extern int do_select (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_select (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_select (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_upd.c */
extern int do_update (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_update (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_update (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_stats.c */
extern int do_update_stats (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_stats (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_trig.c */
extern int do_check_delete_trigger (PARSER_CONTEXT * parser,
				    PT_NODE * statement,
				    PT_DO_FUNC * do_func);
extern int do_check_insert_trigger (PARSER_CONTEXT * parser,
				    PT_NODE * statement,
				    PT_DO_FUNC * do_func);
extern int do_check_update_trigger (PARSER_CONTEXT * parser,
				    PT_NODE * statement,
				    PT_DO_FUNC * do_func);
/* These are defined in do_repl.c */
extern int do_replicate_schema (PARSER_CONTEXT * parser, PT_NODE * statement);

/* These are defined in do_stat.c */
extern int do_statement (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_statements (PARSER_CONTEXT * parser, PT_NODE * statement_list);
extern int do_prepare_statement (PARSER_CONTEXT * parser,
				 PT_NODE * statement);
extern int do_execute_statement (PARSER_CONTEXT * parser,
				 PT_NODE * statement);
extern int do_internal_statements (PARSER_CONTEXT * parser,
				   PT_NODE * internal_stmt_list, int phase);
extern int do_check_internal_statements (PARSER_CONTEXT * parser,
					 PT_NODE * statement,
					 /* PT_NODE * internal_stmt_list, */
					 PT_DO_FUNC do_func);
#endif /* _DO_STAT_H_ */
