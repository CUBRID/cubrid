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


/*
 * jsp_cl.h - Java Stored Procedure Client Module Header
 *
 * Note:
 */

#ifndef _JSP_CL_H_
#define _JSP_CL_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module.
#endif /* SERVER_MODE */

#ifdef  __cplusplus
#include <vector>
#include <string>
#endif

#include "parse_tree.h"
#include "sp_constants.hpp"
#include "pl_signature.hpp"

extern int jsp_create_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int jsp_alter_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int jsp_drop_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int jsp_call_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int jsp_check_return_type_supported (DB_TYPE type);

extern int jsp_make_pl_signature (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * subquery_as_attr_list,
				  cubpl::pl_signature & signature);
extern int jsp_make_pl_args (PARSER_CONTEXT * parser, PT_NODE * node, int num_params, DB_SET * param_set,
			     cubpl::pl_signature & signature);

extern int jsp_is_exist_stored_procedure (const char *name);

extern char *jsp_get_owner_name (const char *name, char *buf, int buf_size);
extern int jsp_get_return_type (const char *name);
extern int jsp_get_sp_type (const char *name);
extern MOP jsp_find_stored_procedure (const char *name, DB_AUTH purpose);
extern MOP jsp_find_stored_procedure_code (const char *name);
extern int jsp_find_sp_of_another_owner (const char *name, MOP * return_mop);

extern MOP jsp_get_owner (MOP mop_p);
extern char *jsp_get_name (MOP mop_p);
extern char *jsp_get_unique_name (MOP mop_p, char *buf, int buf_size);

extern void jsp_set_prepare_call (void);
extern void jsp_unset_prepare_call (void);
extern bool jsp_is_prepare_call (void);

extern PT_NODE *jsp_get_default_expr_node_list (PARSER_CONTEXT * parser, cubpl::pl_signature & sig);
#endif /* _JSP_CL_H_ */
