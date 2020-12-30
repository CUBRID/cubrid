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
 * esql_translate.h - ESQL C Translator Inclusion header file
 */

#ifndef _ESQL_TRANSLATE_H_
#define _ESQL_TRANSLATE_H_

#ident "$Id$"

#include <stdio.h>
#include "esql_host_variable.h"

typedef struct esql_translate_table_s ESQL_TRANSLATE_TABLE;

struct esql_translate_table_s
{
  void (*tr_connect) (HOST_REF * db_name, HOST_REF * user_name, HOST_REF * passwd);
  void (*tr_disconnect) (void);
  void (*tr_commit) (void);
  void (*tr_rollback) (void);
  void (*tr_static) (const char *stmt, int length, bool repeat, int num_in_vars, HOST_REF * in_vars,
		     const char *in_desc_name, int num_out_vars, HOST_REF * out_vars, const char *out_desc_name);
  void (*tr_open_cs) (int cs_no, const char *stmt, int length, int stmt_no, bool readonly, int num_in_vars,
		      HOST_REF * in_vars, const char *desc_name);
  void (*tr_fetch_cs) (int cs_no, int num_out_vars, HOST_REF * out_vars, const char *desc_name);
  void (*tr_update_cs) (int cs_no, const char *text, int length, bool repetitive, int num_in_vars, HOST_REF * in_vars);
  void (*tr_delete_cs) (int cs_no);
  void (*tr_close_cs) (int cs_no);
  void (*tr_prepare_esql) (int stmt_no, HOST_REF * stmt);
  void (*tr_describe) (int stmt_no, const char *desc_name);
  void (*tr_execute) (int stmt_no, int num_in_vars, HOST_REF * in_vars, const char *in_desc_name, int num_out_vars,
		      HOST_REF * out_vars, const char *out_desc_name);
  void (*tr_execute_immediate) (HOST_REF * stmt);
  void (*tr_object_describe) (HOST_REF * obj, int num_attrs, const char **attr_names, const char *desc_name);
  void (*tr_object_fetch) (HOST_REF * obj, int num_attrs, const char **attr_names, int num_out_vars,
			   HOST_REF * out_vars, const char *desc_name);
  void (*tr_object_update) (const char *set_expr, int length, bool repetitive, int num_in_vars, HOST_REF * in_vars);
  void (*tr_whenever) (WHEN_CONDITION condition, WHEN_ACTION action, const char *name);
  void (*tr_set_out_stream) (FILE * out_stream);
  void (*tr_set_line_terminator) (const char *);
};
#ifdef __cplusplus
extern "C"
{
#endif
  extern ESQL_TRANSLATE_TABLE esql_Translate_table;
#ifdef __cplusplus
}
#endif

#endif				/* _ESQL_TRANSLATE_H_ */
