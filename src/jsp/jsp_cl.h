/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
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

#define SP_CLASS_NAME           "_db_stored_procedure"
#define SP_ARG_CLASS_NAME       "_db_stored_procedure_args"

#define SP_ATTR_NAME            "sp_name"
#define SP_ATTR_SP_TYPE         "sp_type"
#define SP_ATTR_RETURN_TYPE     "return_type"
#define SP_ATTR_ARGS            "args"
#define SP_ATTR_ARG_COUNT       "arg_count"
#define SP_ATTR_LANG            "lang"
#define SP_ATTR_TARGET          "target"
#define SP_ATTR_OWNER           "owner"

#define SP_ATTR_ARG_NAME        "arg_name"
#define SP_ATTR_INDEX_OF_NAME   "index_of"
#define SP_ATTR_DATA_TYPE       "data_type"
#define SP_ATTR_MODE            "mode"

typedef enum
{
  SP_TYPE_PROCEDURE = 1,
  SP_TYPE_FUNCTION
} SP_TYPE_ENUM;

typedef enum
{
  SP_MODE_IN = 1,
  SP_MODE_OUT,
  SP_MODE_INOUT
} SP_MODE_ENUM;

typedef enum
{
  SP_LANG_JAVA = 1
} SP_LANG_ENUM;


extern int jsp_create_stored_procedure (PARSER_CONTEXT * parser,
					PT_NODE * statement);
extern int jsp_drop_stored_procedure (PARSER_CONTEXT * parser,
				      PT_NODE * statement);
extern int jsp_call_stored_procedure (PARSER_CONTEXT * parser,
				      PT_NODE * statement);

extern int jsp_is_exist_stored_procedure (const char *name);
extern int jsp_get_return_type (const char *name);
extern void jsp_init (void);
extern void jsp_close_connection (void);
extern MOP jsp_find_stored_procedure (const char *name);

extern void jsp_set_prepare_call (void);
extern void jsp_unset_prepare_call (void);
extern int jsp_call_from_server (DB_VALUE * returnval, DB_VALUE ** argarray,
				 const char *name, const int arg_cnt);

extern void *jsp_get_db_result_set (int h_id);
extern void jsp_srv_handle_free (int h_id);


#endif /* _JSP_CL_H_ */
