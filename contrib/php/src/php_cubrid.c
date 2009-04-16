/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

/************************************************************************
* IMPORTED SYSTEM HEADER FILES
************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "cubrid_php4.h"
#include "php_globals.h"
#include "SAPI.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"
#include <assert.h>

#ifdef PHP_WIN32
# include <winsock.h>
#endif /* PHP_WIN32 */

/************************************************************************
* OTHER IMPORTED HEADER FILES
************************************************************************/

#include "php_cubrid.h"
#include "php_cubrid_error.h"
#include "cas_cci.h"

/************************************************************************
* PRIVATE DEFINITIONS
************************************************************************/

#ifndef TSRMLS_CC
#define TSRMLS_CC
#endif

#define MK_VERSION(MAJOR, MINOR)	(((MAJOR) << 8) | (MINOR))

/* EXECUTE */
#define CUBRID_INCLUDE_OID			1
#define CUBRID_ASYNC				2

/* ARRAY */
typedef enum
{
  CUBRID_NUM = 1,
  CUBRID_ASSOC = 2,
  CUBRID_BOTH = CUBRID_NUM | CUBRID_ASSOC,
  CUBRID_OBJECT = 4,
} T_CUBRID_ARRAY_TYPE;

/* CURSOR ORIGIN */
typedef enum
{
  CUBRID_CURSOR_FIRST = CCI_CURSOR_FIRST,
  CUBRID_CURSOR_CURRENT = CCI_CURSOR_CURRENT,
  CUBRID_CURSOR_LAST = CCI_CURSOR_LAST,
} T_CUBRID_CURSOR_ORIGIN;

/* CURSOR RESULT */
#define CUBRID_CURSOR_SUCCESS		1
#define CUBRID_NO_MORE_DATA			0
#define CUBRID_CURSOR_ERROR			-1

/* SCHEMA */
#define CUBRID_SCH_CLASS			CCI_SCH_CLASS
#define CUBRID_SCH_VCLASS			CCI_SCH_VCLASS
#define CUBRID_SCH_QUERY_SPEC		CCI_SCH_QUERY_SPEC
#define CUBRID_SCH_ATTRIBUTE		CCI_SCH_ATTRIBUTE
#define CUBRID_SCH_CLASS_ATTRIBUTE	CCI_SCH_CLASS_ATTRIBUTE
#define CUBRID_SCH_METHOD			CCI_SCH_METHOD
#define CUBRID_SCH_CLASS_METHOD		CCI_SCH_CLASS_METHOD
#define CUBRID_SCH_METHOD_FILE		CCI_SCH_METHOD_FILE
#define CUBRID_SCH_SUPERCLASS		CCI_SCH_SUPERCLASS
#define CUBRID_SCH_SUBCLASS			CCI_SCH_SUBCLASS
#define CUBRID_SCH_CONSTRAINT		CCI_SCH_CONSTRAINT
#define CUBRID_SCH_TRIGGER			CCI_SCH_TRIGGER
#define CUBRID_SCH_CLASS_PRIVILEGE	CCI_SCH_CLASS_PRIVILEGE
#define CUBRID_SCH_ATTR_PRIVILEGE	CCI_SCH_ATTR_PRIVILEGE

/* ERROR FACILITY */
typedef enum
{
  CUBRID_FACILITY_DBMS = 1,
  CUBRID_FACILITY_CAS,
  CUBRID_FACILITY_CCI,
  CUBRID_FACILITY_CLIENT,
} T_FACILITY_CODE;

/* for converting sql */
#define OUT_STR		0
#define IN_STR		1
#define TRANS(t)	t=1-t


/* error code */

#define CUBRID_ER_NO_MORE_MEMORY 			-2001
#define CUBRID_ER_INVALID_SQL_TYPE 			-2002
#define CUBRID_ER_CANNOT_GET_COLUMN_INFO 	-2003
#define CUBRID_ER_INIT_ARRAY_FAIL 			-2004
#define CUBRID_ER_UNKNOWN_TYPE 				-2005
#define CUBRID_ER_INVALID_PARAM 			-2006
#define CUBRID_ER_INVALID_ARRAY_TYPE 		-2007
#define CUBRID_ER_NOT_SUPPORTED_TYPE 		-2008
#define CUBRID_ER_OPEN_FILE 				-2009
#define CUBRID_ER_CREATE_TEMP_FILE 			-2010
#define CUBRID_ER_TRANSFER_FAIL 			-2011
#define CUBRID_ER_PHP						-2012

/************************************************************************
* PRIVATE TYPE DEFINITIONS
************************************************************************/

typedef struct
{
  int handle;
} T_CUBRID_CONNECT;

typedef struct
{
  int handle;
  int affected_rows;
  int async_mode;
  int col_count;
  int row_count;
  int l_prepare;
  int bind_num;
  short *l_bind;
  T_CCI_CUBRID_STMT sql_type;
  T_CCI_COL_INFO *col_info;
} T_CUBRID_REQUEST;

/************************************************************************
* PRIVATE FUNCTION PROTOTYPES
************************************************************************/

static void close_cubrid_connect (T_CUBRID_CONNECT * conn);
static void close_cubrid_request (T_CUBRID_REQUEST * req);
static void php_cubrid_init_globals (zend_cubrid_globals * cubrid_globals);
static int init_error (void);
static int set_error (T_FACILITY_CODE facility, int code, char *msg, ...);
static int handle_error (int err_code, T_CCI_ERROR * error);
static int fetch_a_row (pval * arg, int req_handle, int type);
static T_CUBRID_REQUEST *new_request (void);
static int convert_sql (char *str, char ***objs);
static char *str2obj (char *str);
static int cubrid_array_destroy (HashTable * ht ZEND_FILE_LINE_DC);

static int cubrid_add_index_array (pval * arg, uint index, T_CCI_SET in_set);
static int cubrid_add_assoc_array (pval * arg, char *key, T_CCI_SET in_set);
static int cubrid_make_set (HashTable * ht, T_CCI_SET * set);

/************************************************************************
* INTERFACE VARIABLES
************************************************************************/

char *cci_client_name = "PHP";

function_entry cubrid_functions[] = {
/*
	PHP_FE(cubrid_test, NULL)
*/
  PHP_FE (cubrid_version, NULL)
    PHP_FE (cubrid_connect, NULL)
    PHP_FE (cubrid_disconnect, NULL)
    PHP_FE (cubrid_prepare, NULL)
    PHP_FE (cubrid_bind, NULL)
    PHP_FE (cubrid_execute, NULL)
    PHP_FE (cubrid_affected_rows, NULL)
    PHP_FE (cubrid_close_request, NULL)
    PHP_FE (cubrid_fetch, NULL)
    PHP_FE (cubrid_current_oid, NULL)
    PHP_FE (cubrid_column_types, NULL)
    PHP_FE (cubrid_column_names, NULL)
    PHP_FE (cubrid_move_cursor, NULL)
    PHP_FE (cubrid_num_rows, NULL)
    PHP_FE (cubrid_num_cols, NULL)
    PHP_FE (cubrid_get, NULL)
    PHP_FE (cubrid_put, NULL)
    PHP_FE (cubrid_drop, NULL)
    PHP_FE (cubrid_is_instance, NULL)
    PHP_FE (cubrid_get_class_name, NULL)
    PHP_FE (cubrid_lock_read, NULL)
    PHP_FE (cubrid_lock_write, NULL)
    PHP_FE (cubrid_schema, NULL)
    PHP_FE (cubrid_col_size, NULL)
    PHP_FE (cubrid_col_get, NULL)
    PHP_FE (cubrid_set_add, NULL)
    PHP_FE (cubrid_set_drop, NULL)
    PHP_FE (cubrid_seq_drop, NULL)
    PHP_FE (cubrid_seq_insert, NULL)
    PHP_FE (cubrid_seq_put, NULL)
    PHP_FE (cubrid_commit, NULL)
    PHP_FE (cubrid_rollback, NULL)
    PHP_FE (cubrid_new_glo, NULL)
    PHP_FE (cubrid_save_to_glo, NULL)
    PHP_FE (cubrid_load_from_glo, NULL)
    PHP_FE (cubrid_send_glo, NULL)
    PHP_FE (cubrid_error_msg, NULL)
    PHP_FE (cubrid_error_code, NULL)
    PHP_FE (cubrid_error_code_facility, NULL)
    PHP_FALIAS (cubrid_close_prepare, cubrid_close_request, NULL)
    {NULL, NULL, NULL}
};

zend_module_entry cubrid_module_entry = {
#if MK_VERSION(PHP_MAJOR_VERSION, PHP_MINOR_VERSION) >= MK_VERSION(4, 1)
  STANDARD_MODULE_HEADER,
  "CUBRID", cubrid_functions, PHP_MINIT (cubrid), NULL, NULL, NULL,
  PHP_MINFO (cubrid), NO_VERSION_YET, STANDARD_MODULE_PROPERTIES
#else
  "CUBRID", cubrid_functions, PHP_MINIT (cubrid), NULL, NULL, NULL,
  PHP_MINFO (cubrid), STANDARD_MODULE_PROPERTIES
#endif
};

ZEND_DECLARE_MODULE_GLOBALS (cubrid)
/************************************************************************
* PRIVATE VARIABLES
************************************************************************/
/* resource type */
     static int le_connect, le_request;

/************************************************************************
* IMPLEMENTATION OF CALLBACK FUNCTION (EXPORT/INIT/SHUTDOWN/INFO)
************************************************************************/

#if defined(COMPILE_DL) || defined(COMPILE_DL_CUBRID)
ZEND_GET_MODULE (cubrid)
#endif /* COMPILE_DL */
  PHP_MINIT_FUNCTION (cubrid)
{
  cci_init ();
  ZEND_INIT_MODULE_GLOBALS (cubrid, php_cubrid_init_globals, NULL);
  le_connect = register_list_destructors (close_cubrid_connect, NULL);
  le_request = register_list_destructors (close_cubrid_request, NULL);
  cubrid_module_entry.type = type;
  init_error ();
  /* Register Constants */
  REGISTER_LONG_CONSTANT ("CUBRID_INCLUDE_OID", CUBRID_INCLUDE_OID,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_ASYNC", CUBRID_ASYNC,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_NUM", CUBRID_NUM,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_ASSOC", CUBRID_ASSOC,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_BOTH", CUBRID_BOTH,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_OBJECT", CUBRID_OBJECT,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_CURSOR_FIRST", CUBRID_CURSOR_FIRST,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_CURSOR_CURRENT", CUBRID_CURSOR_CURRENT,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_CURSOR_LAST", CUBRID_CURSOR_LAST,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_CURSOR_SUCCESS", CUBRID_CURSOR_SUCCESS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_NO_MORE_DATA", CUBRID_NO_MORE_DATA,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_CURSOR_ERROR", CUBRID_CURSOR_ERROR,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_CLASS", CUBRID_SCH_CLASS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_VCLASS", CUBRID_SCH_VCLASS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_QUERY_SPEC", CUBRID_SCH_QUERY_SPEC,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_ATTRIBUTE", CUBRID_SCH_ATTRIBUTE,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_CLASS_ATTRIBUTE",
			  CUBRID_SCH_CLASS_ATTRIBUTE,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_METHOD", CUBRID_SCH_METHOD,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_CLASS_METHOD", CUBRID_SCH_CLASS_METHOD,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_METHOD_FILE", CUBRID_SCH_METHOD_FILE,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_SUPERCLASS", CUBRID_SCH_SUPERCLASS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_SUBCLASS", CUBRID_SCH_SUBCLASS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_CONSTRAINT", CUBRID_SCH_CONSTRAINT,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_TRIGGER", CUBRID_SCH_TRIGGER,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_CLASS_PRIVILEGE",
			  CUBRID_SCH_CLASS_PRIVILEGE,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_SCH_ATTR_PRIVILEGE",
			  CUBRID_SCH_ATTR_PRIVILEGE,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_FACILITY_DBMS", CUBRID_FACILITY_DBMS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_FACILITY_CAS", CUBRID_FACILITY_CAS,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_FACILITY_CCI", CUBRID_FACILITY_CCI,
			  CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT ("CUBRID_FACILITY_CLIENT", CUBRID_FACILITY_CLIENT,
			  CONST_CS | CONST_PERSISTENT);

  return SUCCESS;
}

PHP_MINFO_FUNCTION (cubrid)
{
  char *err_path;

  cfg_get_string ("cubrid.err_path", &err_path);
  if (!err_path)
    {
      err_path = "Not specified";
    }

  php_info_print_table_start ();
  php_info_print_table_header (2, "CUBRID", "Value");
  php_info_print_table_row (2, "Version", PHP_CUBRID_VERSION);
  php_info_print_table_row (2, "Error message file path", err_path);
  php_info_print_table_end ();
}

/************************************************************************
* IMPLEMENTATION OF CUBRID API
************************************************************************/

PHP_FUNCTION (cubrid_version)
{
  RETURN_STRING (PHP_CUBRID_VERSION, 1);
}

PHP_FUNCTION (cubrid_connect)
{
  pzval *host, *port, *dbname, *userid, *passwd;
  char *real_userid, *real_passwd;
  T_CUBRID_CONNECT *connect;
  int res, res2;
  char isolation_level[4];
  T_CCI_ERROR error;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 3:
      if (zend_get_parameters_ex (3, &host, &port, &dbname) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (host)->type == IS_NULL ||
	  GET_ZVAL (port)->type == IS_NULL ||
	  GET_ZVAL (dbname)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_userid = "PUBLIC";
      real_passwd = "";
      break;
    case 4:
      if (zend_get_parameters_ex (4, &host, &port, &dbname, &userid) ==
	  FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (host)->type == IS_NULL ||
	  GET_ZVAL (port)->type == IS_NULL ||
	  GET_ZVAL (dbname)->type == IS_NULL ||
	  GET_ZVAL (userid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      convert_to_string_ex (userid);
      real_userid = GET_ZVAL (userid)->value.str.val;
      real_passwd = "";
      break;
    case 5:
      if (zend_get_parameters_ex (5, &host, &port, &dbname, &userid,
				  &passwd) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (host)->type == IS_NULL ||
	  GET_ZVAL (port)->type == IS_NULL ||
	  GET_ZVAL (dbname)->type == IS_NULL ||
	  GET_ZVAL (userid)->type == IS_NULL ||
	  GET_ZVAL (passwd)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      convert_to_string_ex (userid);
      convert_to_string_ex (passwd);
      real_userid = GET_ZVAL (userid)->value.str.val;
      real_passwd = GET_ZVAL (passwd)->value.str.val;
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }
  convert_to_string_ex (host);
  convert_to_long_ex (port);
  convert_to_string_ex (dbname);
  connect = (T_CUBRID_CONNECT *) emalloc (sizeof (T_CUBRID_CONNECT));
  if (!connect)
    {
      /* 메모리 할당 실패 */
      handle_error (CUBRID_ER_NO_MORE_MEMORY, NULL);
      RETURN_FALSE;
    }

  res = cci_connect (GET_ZVAL (host)->value.str.val,
		     GET_ZVAL (port)->value.lval,
		     GET_ZVAL (dbname)->value.str.val, real_userid,
		     real_passwd);
  if (res < 0)
    {
      /* CCI 에러 */
      efree (connect);
      handle_error (res, NULL);
      RETURN_FALSE;
    }

  res2 =
    cci_get_db_parameter (res, CCI_PARAM_ISOLATION_LEVEL, isolation_level,
			  &error);
  if (res2 < 0)
    {
      handle_error (res2, &error);
      cci_disconnect (res, &error);
      efree (connect);
      RETURN_FALSE;
    }

  res2 = cci_end_tran (res, CCI_TRAN_COMMIT, &error);
  if (res2 < 0)
    {
      handle_error (res2, &error);
      cci_disconnect (res, &error);
      efree (connect);
      RETURN_FALSE;
    }

  connect->handle = res;
  ZEND_REGISTER_RESOURCE (return_value, connect, le_connect);
  return;
}

PHP_FUNCTION (cubrid_disconnect)
{
  pzval *con_handle;
  T_CUBRID_CONNECT *connect;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &con_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);
  zend_list_delete (GET_ZVAL (con_handle)->value.lval);
  RETURN_TRUE;
}

/*
 * request_handle cubrid_prepare( connection_handle, query_statement, option )
 *
 * Ricky Jang
 */
PHP_FUNCTION (cubrid_prepare)
{
  pzval *con_handle, *query, *option;
  T_CUBRID_CONNECT *connect;
  T_CUBRID_REQUEST *request;
  T_CCI_ERROR error;
  int real_option = 0;
  int res;
  int request_handle;
  int i;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 2:
      if (zend_get_parameters_ex (2, &con_handle, &query) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (query)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_option = 0;
      break;
    case 3:
      if (zend_get_parameters_ex (3, &con_handle, &query, &option) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (query)->type == IS_NULL ||
	  GET_ZVAL (option)->type != IS_LONG)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      convert_to_long_ex (option);
      real_option = GET_ZVAL (option)->value.lval;
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);
  /* cci_prepare */
  convert_to_string_ex (query);
  res = cci_prepare (connect->handle, GET_ZVAL (query)->value.str.val,
		     (char) ((real_option & CUBRID_INCLUDE_OID) ?
			     CCI_PREPARE_INCLUDE_OID : 0), &error);
  if (res < 0)
    {
      /* cci_prepare에서 에러 발생 */
      handle_error (res, &error);
      RETURN_FALSE;
    }
  request_handle = res;

  request = new_request ();
  if (!request)
    {
      /* 메모리 할당 실패 */
      handle_error (CUBRID_ER_NO_MORE_MEMORY, NULL);
      RETURN_FALSE;
    }

  request->handle = request_handle;

  request->bind_num = cci_get_bind_num (request_handle);
  if (request->bind_num > 0)
    {
      request->l_bind =
	(short *) emalloc (sizeof (short) * (request->bind_num));
      if (!request->l_bind)
	{
	  handle_error (CUBRID_ER_NO_MORE_MEMORY, NULL);
	  efree (request);
	  request = NULL;
	  RETURN_FALSE;
	}
      for (i = 0; i < (request->bind_num); i++)
	{
	  request->l_bind[i] = 0;
	}
    }
  request->l_prepare = 1;

  ZEND_REGISTER_RESOURCE (return_value, request, le_request);
  return;
}

/*
 * cubrid_bind( request_handle, indext_number, bind_value, bind_value_type )
 *
 * cci_bind_param( request_handle, T_CCI_A_TYPE, void *value, T_CCI_U_TYPE, flag )
 * Ricky Jang
 */
PHP_FUNCTION (cubrid_bind)
{
  pzval *req_handle, *nidx, *bind_value, *ptype;
  T_CUBRID_REQUEST *request;
  T_CCI_ERROR error;
  T_CCI_U_TYPE u_type = -1;
  T_CCI_BIT *bit_value;
  int res;
  int idx;
  void *value;
  char *type;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 3:
      if (zend_get_parameters_ex (3, &req_handle, &nidx, &bind_value) ==
	  FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (req_handle)->type == IS_NULL ||
	  GET_ZVAL (nidx)->type == IS_NULL ||
	  GET_ZVAL (bind_value)->type == IS_NULL)
	{
	  RETURN_FALSE;
	}

      u_type = CCI_U_TYPE_STRING;

      break;
    case 4:
      if (zend_get_parameters_ex (4, &req_handle, &nidx, &bind_value, &ptype)
	  == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (req_handle)->type == IS_NULL ||
	  GET_ZVAL (nidx)->type == IS_NULL ||
	  GET_ZVAL (bind_value)->type == IS_NULL ||
	  GET_ZVAL (ptype)->type == IS_NULL)
	{
	  RETURN_FALSE;
	}

      convert_to_string_ex (ptype);
      type = GET_ZVAL (ptype)->value.str.val;

      if (strcasecmp (type, "STRING") == 0)
	u_type = CCI_U_TYPE_STRING;
      else if (strcasecmp (type, "NCHAR") == 0)
	u_type = CCI_U_TYPE_NCHAR;
      else if (strcasecmp (type, "BIT") == 0)
	u_type = CCI_U_TYPE_BIT;
      else if (strcasecmp (type, "INT") == 0)
	u_type = CCI_U_TYPE_INT;
      else if (strcasecmp (type, "NUMERIC") == 0
	       || strcasecmp (type, "NUMBER") == 0)
	u_type = CCI_U_TYPE_NUMERIC;
      else if (strcasecmp (type, "SHORT") == 0)
	u_type = CCI_U_TYPE_SHORT;
      else if (strcasecmp (type, "FLOAT") == 0)
	u_type = CCI_U_TYPE_FLOAT;
      else if (strcasecmp (type, "DOUBLE") == 0)
	u_type = CCI_U_TYPE_DOUBLE;
      else if (strcasecmp (type, "DATE") == 0)
	u_type = CCI_U_TYPE_DATE;
      else if (strcasecmp (type, "TIME") == 0)
	u_type = CCI_U_TYPE_TIME;
      else if (strcasecmp (type, "TIMESTAMP") == 0)
	u_type = CCI_U_TYPE_STRING;
      /*
         else if( strcasecmp( type, "SET" ) == 0 )
         u_type = CCI_U_TYPE_SET;
         else if( strcasecmp( type, "MULTISET" ) == 0 )
         u_type = CCI_U_TYPE_MULTISET;
         else if( strcasecmp( type, "SEQUENCE" ) == 0 )
         u_type = CCI_U_TYPE_SEQUENCE;
       */
      else if (strcasecmp (type, "OBJECT") == 0)
	u_type = CCI_U_TYPE_OBJECT;
      else if (strcasecmp (type, "NULL") == 0)
	u_type = CCI_U_TYPE_NULL;
      else
	{
	  u_type = CCI_U_TYPE_UNKNOWN;
	  zend_error (E_WARNING, "Bind value type unknown : %s\n", type);
	  RETURN_FALSE;
	}

      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }

  request =
    (T_CUBRID_REQUEST *) zend_fetch_resource (req_handle TSRMLS_CC, -1,
					      "CUBRID-Request", NULL, 1,
					      le_request);
  if (!request)
    {
      RETURN_FALSE;
    }
  idx = (int) GET_ZVAL (nidx)->value.lval;

  /*
   * idx와 bind num의 비교가 필요함
   */
  if (idx < 1 || idx > request->bind_num)
    {
      RETURN_FALSE;
    }

  convert_to_string_ex (bind_value);

  if (u_type == CCI_U_TYPE_BIT)
    {

      bit_value = (T_CCI_BIT *) emalloc (sizeof (T_CCI_BIT));

      bit_value->size = GET_ZVAL (bind_value)->value.str.len;
      bit_value->buf = GET_ZVAL (bind_value)->value.str.val;
      res =
	cci_bind_param (request->handle, idx, CCI_A_TYPE_BIT,
			(void *) bit_value, CCI_U_TYPE_BIT, (char) NULL);
      efree (bit_value);
      bit_value = NULL;
    }
  else
    {
      if (u_type == CCI_U_TYPE_NULL)
	value = (void *) NULL;
      else
	value = (void *) GET_ZVAL (bind_value)->value.str.val;

      res =
	cci_bind_param (request->handle, idx, CCI_A_TYPE_STR, value, u_type,
			(char) NULL);
    }

  if (res != 0)
    {
      if (!request->l_bind)
	request->l_bind[idx - 1] = 0;
      RETURN_FALSE;
    }
  if (!request->l_bind)
    {
      RETURN_FALSE;
    }
  request->l_bind[idx - 1] = 1;

  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_execute)
{
  pzval *handle, *query, *option;
  T_CUBRID_CONNECT *connect;
  T_CUBRID_REQUEST *request;
  T_CCI_ERROR error;
  T_CCI_COL_INFO *res_col_info;
  int real_option = 0;
  int res;
  T_CCI_CUBRID_STMT res_sql_type;
  int res_col_count;
  char **objs = NULL;
  int request_handle;
  int objs_num;
  int i;
  int l_prepare = 0;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 1:
      if (zend_get_parameters_ex (1, &handle) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (handle)->type == IS_NULL)
	{
	  RETURN_FALSE;
	}
      break;
    case 2:
      if (zend_get_parameters_ex (2, &handle, &query) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (handle)->type == IS_NULL ||
	  GET_ZVAL (query)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_option = 0;
      break;
    case 3:
      if (zend_get_parameters_ex (3, &handle, &query, &option) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (handle)->type == IS_NULL ||
	  GET_ZVAL (query)->type == IS_NULL ||
	  GET_ZVAL (option)->type != IS_LONG)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      convert_to_long_ex (option);
      real_option = GET_ZVAL (option)->value.lval;
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }

  connect = (T_CUBRID_CONNECT *) zend_fetch_resource (handle TSRMLS_CC, -1,
						      NULL, NULL, 1,
						      le_connect);
  if (!connect)
    {
      /* 일단, connection handle이 아니다. request handle로 판단한다. */
      request =
	(T_CUBRID_REQUEST *) zend_fetch_resource (handle TSRMLS_CC, -1, NULL,
						  NULL, 1, le_request);
      if (!request)
	{
	  /* request handle도 아니다. 완전히 잘못된 변수를 전달 받았다. */
	  zend_error (E_WARNING, "%s(): no %s resource supplied",
		      get_active_function_name (TSRMLS_C),
		      "CUBRID-Connect or CUBRID-Request");
	  RETURN_FALSE;
	}

      l_prepare = request->l_prepare;
      if (request->bind_num > 0 && l_prepare)
	{
	  if (request->l_bind)
	    {
	      for (i = 0; i < request->bind_num; i++)
		{
		  if (!request->l_bind[i])
		    {
		      zend_error (E_WARNING,
				  "Execute without value binding : %d\n",
				  i + 1);
		      RETURN_FALSE;
		    }
		}
	    }
	  else
	    {
	      zend_error (E_WARNING, "Invalid request handle\n");
	      RETURN_FALSE;
	    }
	}
    }
  else
    {
      if (ARG_COUNT () < 2)
	WRONG_PARAM_COUNT;
      if (GET_ZVAL (query)->type != IS_STRING)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }

  if (!l_prepare)
    {
      /* cci_prepare */
      convert_to_string_ex (query);

      /* query string에서 oid를 분리해서 저장 */
      objs_num = convert_sql (GET_ZVAL (query)->value.str.val, &objs);
      /* convert_sql이 (*query)->value.str.val의 내용을 바꾸었을 경우에
         길이도 수정해야함 */
      GET_ZVAL (query)->value.str.len =
	strlen (GET_ZVAL (query)->value.str.val);

      res = cci_prepare (connect->handle, GET_ZVAL (query)->value.str.val,
			 (char) ((real_option & CUBRID_INCLUDE_OID) ?
				 CCI_PREPARE_INCLUDE_OID : 0), &error);
      if (res < 0)
	{
	  /* cci_prepare에서 에러 발생 */
	  handle_error (res, &error);
	  RETURN_FALSE;
	}
      request_handle = res;
      /* 저장된 oid가 있을 경우에 bind */

      for (i = 0; i < objs_num; i++)
	{
	  res = cci_bind_param (request_handle, i + 1, CCI_A_TYPE_STR,
				(void *) (objs[i]), CCI_U_TYPE_OBJECT, 0);
	  if (res < 0)
	    {
	      if (objs)
		{
		  for (i = 0; i < objs_num; i++)
		    {
		      if (objs[i])
			free (objs[i]);
		    }
		  free (objs);
		}
	      handle_error (res, NULL);
	      RETURN_FALSE;
	    }
	}
    }
  else
    {
      if (ARG_COUNT (ht) == 2)
	{
	  convert_to_long_ex (query);
	  real_option = GET_ZVAL (query)->value.lval;
	}
      request_handle = request->handle;
    }

  /* cci_execute */
  res = cci_execute (request_handle,
		     (char) ((real_option & CUBRID_ASYNC) ? CCI_EXEC_ASYNC :
			     0), 0, &error);
  if (res < 0)
    {
      /* cci_execute에서 에러 발생 */

      /* deallocate objs */
      if (objs && !l_prepare)
	{
	  for (i = 0; i < objs_num; i++)
	    {
	      if (objs[i])
		free (objs[i]);
	    }
	  free (objs);
	}
      handle_error (res, &error);
      RETURN_FALSE;
    }

  /* deallocate objs */
  if (objs && !l_prepare)
    {
      for (i = 0; i < objs_num; i++)
	{
	  if (objs[i])
	    free (objs[i]);
	}
      free (objs);
    }

  /* execution phase 끝. 결과 정보 저장 phase 시작 */

  res_col_info = cci_get_result_info (request_handle, &res_sql_type,
				      &res_col_count);
  if (res_sql_type == CUBRID_STMT_SELECT && !res_col_info)
    {
      /* cci_get_result_info 에서 에러발생 */
      RETURN_FALSE;
    }

  if (!l_prepare)
    {
      request = new_request ();
      if (!request)
	{
	  /* 메모리 할당 실패 */
	  handle_error (CUBRID_ER_NO_MORE_MEMORY, NULL);
	  RETURN_FALSE;
	}

      request->handle = request_handle;
    }
  request->col_info = res_col_info;
  request->sql_type = res_sql_type;
  request->col_count = res_col_count;
  request->async_mode = real_option & CUBRID_ASYNC;

  switch (request->sql_type)
    {
    case CUBRID_STMT_SELECT:
      request->row_count = res;
      break;
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
      request->affected_rows = res;
      break;
    case CUBRID_STMT_CALL:
      request->row_count = res;

    default:
      break;
    }

  /* select 질의를 수행했을 경우 cursor를 결과의 처음 위치에 놓아야함 */
  res = cci_cursor (request_handle, 1, CCI_CURSOR_CURRENT, &error);
  if (res < 0 && res != CCI_ER_NO_MORE_DATA)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  if (l_prepare)
    {
      if (request->l_bind)
	{
	  for (i = 0; i < request->bind_num; i++)
	    {
	      request->l_bind[i] = 0;
	    }
	}
    }
  else
    {
      ZEND_REGISTER_RESOURCE (return_value, request, le_request);
      return;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_affected_rows)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  /* 에러났을 때 리턴값이 -1이어야 하기 때문에 
     ZEND_FETCH_RESOURCE를 쓰지 않았음 */
  request =
    (T_CUBRID_REQUEST *) zend_fetch_resource (req_handle TSRMLS_CC, -1,
					      "CUBRID-Request", NULL, 1,
					      le_request);
  if (!request)
    {
      /* 리소스를 찾지 못 했음(Invalid handle) */
      /* handling은 zend_fetch_resource 에서... */
      RETURN_LONG (-1);
    }
  switch (request->sql_type)
    {
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
      RETURN_LONG (request->affected_rows);
      break;
    default:
      /* SQL문 type이 부적절함 */
      handle_error (CUBRID_ER_INVALID_SQL_TYPE, NULL);
      RETURN_LONG (-1);
      break;
    }
}

PHP_FUNCTION (cubrid_close_request)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;
  int res;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (request, T_CUBRID_REQUEST *, req_handle, -1,
		       "CUBRID-Request", le_request);
  /* handle이 자동으로 free되었을 때 생기는 문제를 해결하기 위해
     cci_close_req_handle을 여기서 호출하고, resource deallocation
     handler에서는 호출하지 않음
   */
  res = cci_close_req_handle (request->handle);
  if (res < 0)
    {
      handle_error (res, NULL);
    }
  zend_list_delete (GET_ZVAL (req_handle)->value.lval);
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_fetch)
{
  pzval *req_handle, *type;
  T_CUBRID_REQUEST *request;
  T_CUBRID_ARRAY_TYPE real_type;
  T_CCI_ERROR error;
  int res;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 1:
      if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (req_handle)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_type = CUBRID_BOTH;
      break;
    case 2:
      if (zend_get_parameters_ex (2, &req_handle, &type) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (req_handle)->type == IS_NULL ||
	  GET_ZVAL (type)->type != IS_LONG)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      convert_to_long_ex (type);
      real_type = GET_ZVAL (type)->value.lval;
      /* object type으로 fetch할 때는 일단 associative array로 만들어서
         object로 변환하는 방법을 거쳐야 하므로, 일단 associative array
         로 만들어야함
       */
      if (real_type & CUBRID_OBJECT)
	real_type |= CUBRID_ASSOC;
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }
  ZEND_FETCH_RESOURCE (request, T_CUBRID_REQUEST *, req_handle, -1,
		       "CUBRID-Request", le_request);

  res = cci_cursor (request->handle, 0, CCI_CURSOR_CURRENT, &error);
  if (res == CCI_ER_NO_MORE_DATA)
    {
      RETURN_FALSE;
    }

  res = cci_fetch (request->handle, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  if ((res = fetch_a_row (return_value, request->handle, real_type))
      != SUCCESS)
    {
      handle_error (res, NULL);
      RETURN_FALSE;
    }

  if (real_type & CUBRID_OBJECT)
    {
      if (return_value->type == IS_ARRAY)
	{
	  convert_to_object (return_value);
	}
    }

  res = cci_cursor (request->handle, 1, CCI_CURSOR_CURRENT, &error);
  if (res < 0 && res != CCI_ER_NO_MORE_DATA)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  return;
}

PHP_FUNCTION (cubrid_current_oid)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;
  char oid_buf[1024];
  int res;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }

  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }

  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }

  ZEND_FETCH_RESOURCE (request, T_CUBRID_REQUEST *, req_handle, -1,
		       "CUBRID-Request", le_request);
  if (request->sql_type != CUBRID_STMT_SELECT)
    {
      /* SQL이 select가 아님 */
      handle_error (CUBRID_ER_INVALID_SQL_TYPE, NULL);
      RETURN_FALSE;
    }

  res = cci_get_cur_oid (request->handle, oid_buf);
  if (res < 0)
    {
      handle_error (res, NULL);
      RETURN_FALSE;
    }
  RETURN_STRING (oid_buf, 1);
}

PHP_FUNCTION (cubrid_column_types)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;
  char buf[100];
  char full_type_name[200];
  int i;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (request, T_CUBRID_REQUEST *, req_handle, -1,
		       "CUBRID-Request", le_request);
  if (array_init (return_value) == FAILURE)
    {
      /* array init error */
      handle_error (CUBRID_ER_INIT_ARRAY_FAIL, NULL);
      RETURN_FALSE;
    }

  for (i = 0; i < request->col_count; i++)
    {
      switch (CCI_GET_COLLECTION_DOMAIN
	      (CCI_GET_RESULT_INFO_TYPE (request->col_info, i + 1)))
	{
	case CCI_U_TYPE_UNKNOWN:
	  sprintf (buf, "unknown");
	  break;
	case CCI_U_TYPE_CHAR:
	  sprintf (buf, "char(%d)",
		   CCI_GET_RESULT_INFO_PRECISION (request->col_info, i + 1));
	  break;
	case CCI_U_TYPE_STRING:
	  sprintf (buf, "varchar(%d)",
		   CCI_GET_RESULT_INFO_PRECISION (request->col_info, i + 1));
	  break;
	case CCI_U_TYPE_NCHAR:
	  sprintf (buf, "nchar(%d)",
		   CCI_GET_RESULT_INFO_PRECISION (request->col_info, i + 1));
	  break;
	case CCI_U_TYPE_VARNCHAR:
	  sprintf (buf, "varnchar(%d)",
		   CCI_GET_RESULT_INFO_PRECISION (request->col_info, i + 1));
	  break;
	case CCI_U_TYPE_BIT:
	  sprintf (buf, "bit");
	  break;
	case CCI_U_TYPE_VARBIT:
	  sprintf (buf, "varbit(%d)",
		   CCI_GET_RESULT_INFO_PRECISION (request->col_info, i + 1));
	  break;
	case CCI_U_TYPE_NUMERIC:
	  sprintf (buf, "numeric(%d,%d)",
		   CCI_GET_RESULT_INFO_PRECISION (request->col_info, i + 1),
		   CCI_GET_RESULT_INFO_SCALE (request->col_info, i + 1));
	  break;
	case CCI_U_TYPE_INT:
	  sprintf (buf, "integer");
	  break;
	case CCI_U_TYPE_SHORT:
	  sprintf (buf, "smallint");
	  break;
	case CCI_U_TYPE_MONETARY:
	  sprintf (buf, "monetary");
	  break;
	case CCI_U_TYPE_FLOAT:
	  sprintf (buf, "float");
	  break;
	case CCI_U_TYPE_DOUBLE:
	  sprintf (buf, "double");
	  break;
	case CCI_U_TYPE_DATE:
	  sprintf (buf, "date");
	  break;
	case CCI_U_TYPE_TIME:
	  sprintf (buf, "time");
	  break;
	case CCI_U_TYPE_TIMESTAMP:
	  sprintf (buf, "timestamp");
	  break;
	case CCI_U_TYPE_SET:
	  sprintf (buf, "set");
	  break;
	case CCI_U_TYPE_MULTISET:
	  sprintf (buf, "multiset");
	  break;
	case CCI_U_TYPE_SEQUENCE:
	  sprintf (buf, "sequence");
	  break;
	case CCI_U_TYPE_OBJECT:
	  sprintf (buf, "object");
	  break;
	default:
	  handle_error (CUBRID_ER_UNKNOWN_TYPE, NULL);
	  RETURN_FALSE;
	  break;
	}

      if (CCI_IS_SET_TYPE
	  (CCI_GET_RESULT_INFO_TYPE (request->col_info, i + 1)))
	{
	  sprintf (full_type_name, "set(%s)", buf);
	}
      else
	if (CCI_IS_MULTISET_TYPE
	    (CCI_GET_RESULT_INFO_TYPE (request->col_info, i + 1)))
	{
	  sprintf (full_type_name, "multiset(%s)", buf);
	}
      else
	if (CCI_IS_SEQUENCE_TYPE
	    (CCI_GET_RESULT_INFO_TYPE (request->col_info, i + 1)))
	{
	  sprintf (full_type_name, "sequence(%s)", buf);
	}
      else
	{
	  sprintf (full_type_name, "%s", buf);
	}

      add_index_stringl (return_value, i, full_type_name,
			 strlen (full_type_name), 1);
    }
  return;
}

PHP_FUNCTION (cubrid_column_names)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;
  int i;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (request, T_CUBRID_REQUEST *, req_handle, -1,
		       "CUBRID-Request", le_request);
  if (array_init (return_value) == FAILURE)
    {
      RETURN_FALSE;
    }
  for (i = 0; i < request->col_count; i++)
    {
      add_index_stringl (return_value, i,
			 CCI_GET_RESULT_INFO_NAME (request->col_info, i + 1),
			 strlen (CCI_GET_RESULT_INFO_NAME
				 (request->col_info, i + 1)), 1);
    }
  return;
}

PHP_FUNCTION (cubrid_move_cursor)
{
  pzval *req_handle, *offset, *origin;
  T_CUBRID_REQUEST *request;
  T_CUBRID_CURSOR_ORIGIN real_origin = CUBRID_CURSOR_CURRENT;
  T_CCI_ERROR error;
  int res;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 2:
      if (zend_get_parameters_ex (2, &req_handle, &offset) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (req_handle)->type == IS_NULL ||
	  GET_ZVAL (offset)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_origin = CUBRID_CURSOR_CURRENT;
      break;
    case 3:
      if (zend_get_parameters_ex (3, &req_handle, &offset, &origin) ==
	  FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (req_handle)->type == IS_NULL ||
	  GET_ZVAL (offset)->type == IS_NULL ||
	  GET_ZVAL (origin)->type != IS_LONG)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      convert_to_long_ex (origin);
      real_origin = GET_ZVAL (origin)->value.lval;
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }

  ZEND_FETCH_RESOURCE (request, T_CUBRID_REQUEST *, req_handle, -1,
		       "CUBRID-Request", le_request);
  convert_to_long_ex (offset);
  res = cci_cursor (request->handle, GET_ZVAL (offset)->value.lval,
		    real_origin, &error);
  if (res == CCI_ER_NO_MORE_DATA)
    {
      RETURN_LONG (CUBRID_NO_MORE_DATA);
    }
  if (res < 0)
    {
      /* 에러났을 경우... */
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_LONG (CUBRID_CURSOR_SUCCESS);
}

PHP_FUNCTION (cubrid_num_rows)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  /* 에러났을 때 리턴값이 -1이어야 하기 때문에 
     ZEND_FETCH_RESOURCE를 쓰지 않았음 */
  request =
    (T_CUBRID_REQUEST *) zend_fetch_resource (req_handle TSRMLS_CC, -1,
					      "CUBRID-Request", NULL, 1,
					      le_request);
  if (!request)
    {
      /* 리소스를 찾지 못 했음(Invalid handle) */
      /* handling은 zend_fetch_resource 에서... */
      RETURN_LONG (-1);
    }

  switch (request->sql_type)
    {
    case CUBRID_STMT_SELECT:
      RETURN_LONG (request->row_count);
      break;
    default:
      /* SQL문 type이 부적절함 */
      handle_error (CUBRID_ER_INVALID_SQL_TYPE, NULL);
      RETURN_LONG (-1);
      break;
    }
}

PHP_FUNCTION (cubrid_num_cols)
{
  pzval *req_handle;
  T_CUBRID_REQUEST *request;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &req_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (req_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  /* 에러났을 때 리턴값이 -1이어야 하기 때문에 
     ZEND_FETCH_RESOURCE를 쓰지 않았음 */
  request =
    (T_CUBRID_REQUEST *) zend_fetch_resource (req_handle TSRMLS_CC, -1,
					      "CUBRID-Request", NULL, 1,
					      le_request);
  if (!request)
    {
      /* 리소스를 찾지 못 했음(Invalid handle) */
      /* handling은 zend_fetch_resource 에서... */
      RETURN_LONG (-1);
    }
  switch (request->sql_type)
    {
    case CUBRID_STMT_SELECT:
      RETURN_LONG (request->col_count);
      break;
    default:
      /* SQL문 type이 부적절함 */
      handle_error (CUBRID_ER_INVALID_SQL_TYPE, NULL);
      RETURN_LONG (-1);
      break;
    }
}


PHP_FUNCTION (cubrid_get)
{
  pzval *con_handle, *oid, *attr_name;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char **real_attr_name = NULL;
  int real_attr_count = -1;
  pzval *elem_buf;
  int i;
  int request_handle;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 2:
      if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_attr_name = NULL;
      real_attr_count = 0;
      break;
    case 3:
      if (zend_get_parameters_ex (3, &con_handle, &oid, &attr_name)
	  == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  /* connect를 먼저 fetch하기 위해서 순서를 바꿨음 */
  /* connect를 fetch할 때 함수를 빠져나갈 수도 있기 때문에 leak을 막기 위해 */

  if (ARG_COUNT (ht) == 3)
    {				/* attr_name 이 정해진 경우 */
      switch (GET_ZVAL (attr_name)->type)
	{
	case IS_STRING:
	  real_attr_count = 1;
	  real_attr_name = (char **) emalloc (sizeof (char *) *
					      (real_attr_count + 1));
	  if (!real_attr_name)
	    {
	      /* 메모리 할당 에러 */
	      handle_error (CUBRID_ER_NO_MORE_MEMORY, NULL);
	      goto cubrid_get_error;
	    }
	  convert_to_string_ex (attr_name);
	  real_attr_name[0] = estrdup (GET_ZVAL (attr_name)->value.str.val);
	  real_attr_name[1] = NULL;
	  break;
	case IS_ARRAY:
	  real_attr_count =
	    zend_hash_num_elements (GET_ZVAL (attr_name)->value.ht);
	  real_attr_name =
	    (char **) emalloc (sizeof (char *) * (real_attr_count + 1));
	  if (!real_attr_name)
	    {
	      /* 메모리 할당 에러 */
	      handle_error (CUBRID_ER_NO_MORE_MEMORY, NULL);
	      goto cubrid_get_error;
	    }
	  /* initialization */
	  for (i = 0; i <= real_attr_count; i++)
	    real_attr_name[i] = NULL;
	  for (i = 0; i < real_attr_count; i++)
	    {
	      if (zend_hash_index_find (GET_ZVAL (attr_name)->value.ht, i,
					(void **) &elem_buf) == FAILURE)
		{
		  /* 매개변수를 가져오는데 실패 */
		  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
		  goto cubrid_get_error;
		}
	      convert_to_string_ex (elem_buf);
	      real_attr_name[i] =
		estrdup (GET_ZVAL (elem_buf)->value.str.val);
	    }
	  real_attr_name[i] = NULL;
	  break;
	default:
	  /* 입력된 매개변수가 잘 못 되었음(string, array이어야 함) */
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  goto cubrid_get_error;
	  break;
	}
    }

  convert_to_string_ex (oid);

  res = cci_oid_get (connect->handle, GET_ZVAL (oid)->value.str.val,
		     real_attr_name, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      goto cubrid_get_error;
    }
  request_handle = res;

  if (ARG_COUNT (ht) == 3 && GET_ZVAL (attr_name)->type == IS_STRING)
    {
      char *result;
      int ind;

      res = cci_get_data (request_handle, 1, CCI_A_TYPE_STR, &result, &ind);
      if (res < 0)
	{
	  handle_error (res, &error);
	  goto cubrid_get_error;
	}
      if (ind < 0)
	{
	  RETURN_FALSE;
	}
      else
	{
	  RETURN_STRINGL (result, ind, 1);
	}
    }
  else
    {
      if ((res = fetch_a_row (return_value, request_handle, CUBRID_ASSOC))
	  != SUCCESS)
	{
	  /* fetch 및 get_data에서 에러 발생 */
	  handle_error (res, NULL);
	  goto cubrid_get_error;
	}
    }

  for (i = 0; i < real_attr_count; i++)
    if (real_attr_name[i])
      efree (real_attr_name[i]);
  if (real_attr_name)
    efree (real_attr_name);
  return;

cubrid_get_error:
  /* deallocation */
  for (i = 0; i < real_attr_count; i++)
    if (real_attr_name[i])
      efree (real_attr_name[i]);
  if (real_attr_name)
    efree (real_attr_name);
  RETURN_FALSE;

}

PHP_FUNCTION (cubrid_put)
{
  pzval *con_handle, *oid, *attr_name, *attr_value, *attr;
  char **real_attr_name = NULL;
  void **real_attr_value = NULL;
  int *real_attr_type = NULL;
  int real_attr_count = 0;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char *key;
  ulong index;
  pzval *data;
  int i = 0;

  T_CCI_SET temp_set = NULL;

  init_error ();

  switch (ARG_COUNT (ht))
    {
    case 3:
      if (zend_get_parameters_ex (3, &con_handle, &oid, &attr) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL || GET_ZVAL (attr)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
			   "CUBRID-Connect", le_connect);
      zend_hash_internal_pointer_reset (GET_ZVAL (attr)->value.ht);
      for (;; zend_hash_move_forward (GET_ZVAL (attr)->value.ht))
	{
	  res =
	    zend_hash_get_current_key (GET_ZVAL (attr)->value.ht, &key,
				       &index, 1);
	  if (res == HASH_KEY_NON_EXISTANT)
	    break;
	  else if (res == HASH_KEY_IS_LONG)
	    {
	      /* Hash table 의 key가 integer인 경우 */
	      /* attr은 항상 associative array이어야 함 */
	      handle_error (CUBRID_ER_INVALID_ARRAY_TYPE, NULL);
	      RETURN_FALSE;
	    }
	  real_attr_count = i + 1;
	  /* row count + 1(NULL row) */
	  real_attr_name = (char **) erealloc (real_attr_name,
					       sizeof (char *) *
					       (real_attr_count + 1));
	  real_attr_value =
	    erealloc (real_attr_value,
		      sizeof (char *) * (real_attr_count + 1));
	  real_attr_type =
	    erealloc (real_attr_type, sizeof (int) * (real_attr_count + 1));
	  real_attr_name[i] =
	    (char *) emalloc (sizeof (char) * (strlen (key) + 1));
	  strcpy (real_attr_name[i], key);
	  real_attr_value[i] = NULL;
	  real_attr_type[i] = 0;
	  efree (key);

	  res = zend_hash_get_current_data (GET_ZVAL (attr)->value.ht,
					    (void **) &data);

	  switch (GET_ZVAL (data)->type)
	    {
	    case IS_NULL:
	      real_attr_value[i] = NULL;
	      break;
	    case IS_LONG:
	    case IS_DOUBLE:
	      convert_to_string_ex (data);
	    case IS_STRING:
	      real_attr_value[i] = (char *) emalloc (sizeof (char) *
						     (GET_ZVAL (data)->value.
						      str.len + 1));
	      strcpy (real_attr_value[i], GET_ZVAL (data)->value.str.val);
	      real_attr_type[i] = CCI_A_TYPE_STR;

	      break;
	    case IS_ARRAY:
	      res = cubrid_make_set (GET_ZVAL (data)->value.ht, &temp_set);
	      if (res < 0)
		{
		  handle_error (res, NULL);
		  goto return_error;
		}
	      real_attr_value[i] = temp_set;
	      real_attr_type[i] = CCI_A_TYPE_SET;
	      break;
	    case IS_OBJECT:
	    case IS_BOOL:
	    case IS_RESOURCE:
	    case IS_CONSTANT:
	      /* 지원되지 않는 type입니다 */
	      handle_error (CUBRID_ER_NOT_SUPPORTED_TYPE, NULL);
	      goto return_error;
	      break;
	    }
	  i++;
	}
      real_attr_name[real_attr_count] = NULL;
      real_attr_value[real_attr_count] = NULL;

      break;
    case 4:
      if (zend_get_parameters_ex (4, &con_handle, &oid, &attr_name,
				  &attr_value) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
			   "CUBRID-Connect", le_connect);
      convert_to_string_ex (attr_name);

      real_attr_count = 1;
      real_attr_name = (char **) emalloc (sizeof (char *) *
					  (real_attr_count + 1));
      real_attr_value = emalloc (sizeof (char *) * (real_attr_count + 1));
      real_attr_type = emalloc (sizeof (int) * (real_attr_count + 1));

      real_attr_name[0] = (char *) emalloc (sizeof (char) *
					    (GET_ZVAL (attr_name)->value.str.
					     len + 1));
      strcpy (real_attr_name[0], GET_ZVAL (attr_name)->value.str.val);
      real_attr_name[1] = NULL;
      real_attr_value[0] = NULL;
      real_attr_type[0] = 0;

      switch (GET_ZVAL (attr_value)->type)
	{
	case IS_NULL:
	  real_attr_value[0] = NULL;
	  break;
	case IS_LONG:
	case IS_DOUBLE:
	  convert_to_string_ex (attr_value);
	case IS_STRING:
	  real_attr_value[0] = (char *) emalloc (sizeof (char) *
						 GET_ZVAL (attr_value)->value.
						 str.len + 1);
	  strcpy (real_attr_value[0], GET_ZVAL (attr_value)->value.str.val);
	  real_attr_type[0] = CCI_A_TYPE_STR;
	  break;
	case IS_ARRAY:
	  res = cubrid_make_set (GET_ZVAL (attr_value)->value.ht, &temp_set);
	  if (res < 0)
	    {
	      handle_error (res, NULL);
	      goto return_error;
	    }
	  real_attr_value[0] = temp_set;
	  real_attr_type[0] = CCI_A_TYPE_SET;
	  break;
	case IS_OBJECT:
	case IS_BOOL:
	case IS_RESOURCE:
	case IS_CONSTANT:
	  /* 지원되지 않는 type입니다 */
	  handle_error (CUBRID_ER_NOT_SUPPORTED_TYPE, NULL);
	  goto return_error;
	  break;
	}
      real_attr_value[1] = NULL;

      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }
  res = cci_oid_put2 (connect->handle, GET_ZVAL (oid)->value.str.val,
		      real_attr_name, real_attr_value, real_attr_type,
		      &error);
  if (res < 0)
    {
      handle_error (res, &error);
      goto return_error;
    }

  if (real_attr_name)
    {
      for (i = 0; i < real_attr_count; i++)
	{
	  if (real_attr_name[i])
	    efree (real_attr_name[i]);
	}
      efree (real_attr_name);
    }
  if (real_attr_value)
    {
      for (i = 0; i < real_attr_count; i++)
	{
	  switch (real_attr_type[i])
	    {
	    case CCI_A_TYPE_SET:
	      cci_set_free (real_attr_value[i]);
	      break;
	    default:
	      if (real_attr_value[i])
		efree (real_attr_value[i]);
	      break;
	    }
	}
      efree (real_attr_value);
    }
  if (real_attr_type)
    {
      efree (real_attr_type);
    }
  RETURN_TRUE;

return_error:
  if (real_attr_name)
    {
      for (i = 0; i < real_attr_count; i++)
	{
	  if (real_attr_name[i])
	    efree (real_attr_name[i]);
	}
      efree (real_attr_name);
    }
  if (real_attr_value)
    {
      for (i = 0; i < real_attr_count; i++)
	{
	  switch (real_attr_type[i])
	    {
	    case CCI_A_TYPE_SET:
	      cci_set_free (real_attr_value[i]);
	      break;
	    default:
	      if (real_attr_value[i])
		efree (real_attr_value[i]);
	      break;
	    }
	}
      efree (real_attr_value);
    }
  if (real_attr_type)
    {
      efree (real_attr_type);
    }
  if (temp_set)
    cci_set_free (temp_set);
  RETURN_FALSE;
}

PHP_FUNCTION (cubrid_drop)
{
  pzval *con_handle, *oid;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) == 2)
    {
      if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res = cci_oid (connect->handle, CCI_OID_DROP,
		 GET_ZVAL (oid)->value.str.val, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  RETURN_TRUE;

}

PHP_FUNCTION (cubrid_is_instance)
{
  pzval *con_handle, *oid;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) == 2)
    {
      if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res = cci_oid (connect->handle, CCI_OID_IS_INSTANCE,
		 GET_ZVAL (oid)->value.str.val, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_LONG (-1);
    }

  if (res == 1)
    {
      RETURN_LONG (res);
    }
  else if (res == 0)
    {
      RETURN_LONG (res);
    }
  else
    {
      handle_error (res, NULL);
      RETURN_LONG (-1);
    }
}

PHP_FUNCTION (cubrid_get_class_name)
{
  pzval *con_handle, *oid;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char out_buf[1024];

  init_error ();
  if (ARG_COUNT (ht) == 2)
    {
      if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res =
    cci_oid_get_class_name (connect->handle, GET_ZVAL (oid)->value.str.val,
			    out_buf, 1024, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  RETURN_STRING (out_buf, 1);

}

PHP_FUNCTION (cubrid_lock_read)
{
  pzval *con_handle, *oid;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) == 2)
    {
      if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res = cci_oid (connect->handle, CCI_OID_LOCK_READ,
		 GET_ZVAL (oid)->value.str.val, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_lock_write)
{
  pzval *con_handle, *oid;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) == 2)
    {
      if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res = cci_oid (connect->handle, CCI_OID_LOCK_WRITE,
		 GET_ZVAL (oid)->value.str.val, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }

  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_schema)
{
  pzval *con_handle, *schema_type, *class_name, *attr_name;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  char *real_class_name = NULL;
  char *real_attr_name = NULL;
  int real_flag = 0;
  int res;
  int request_handle;
  int i = 0;
  pzval temp_element;

  init_error ();
  switch (ARG_COUNT (ht))
    {
    case 2:
      if (zend_get_parameters_ex (2, &con_handle, &schema_type) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (schema_type)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_class_name = NULL;
      real_attr_name = NULL;
      break;
    case 3:
      if (zend_get_parameters_ex (3, &con_handle, &schema_type,
				  &class_name) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (schema_type)->type == IS_NULL ||
	  GET_ZVAL (class_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_class_name = GET_ZVAL (class_name)->value.str.val;
      real_attr_name = NULL;
      break;
    case 4:
      if (zend_get_parameters_ex (4, &con_handle, &schema_type,
				  &class_name, &attr_name) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (schema_type)->type == IS_NULL ||
	  GET_ZVAL (class_name)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}
      real_class_name = GET_ZVAL (class_name)->value.str.val;
      real_attr_name = GET_ZVAL (attr_name)->value.str.val;
      break;
    default:
      WRONG_PARAM_COUNT;
      break;
    }
  /* 에러났을 때 리턴값이 -1이어야 하기 때문에 ZEND_FETCH_RESOURCE를 
     쓰지 않았음 */
  connect =
    (T_CUBRID_CONNECT *) zend_fetch_resource (con_handle TSRMLS_CC, -1,
					      "CUBRID-Connect", NULL, 1,
					      le_connect);
  if (!connect)
    {
      /* 리소스를 찾지 못 했음(Invalid handle) */
      RETURN_LONG (-1);
    }

  switch (GET_ZVAL (schema_type)->value.lval)
    {
    case CUBRID_SCH_CLASS:
    case CUBRID_SCH_VCLASS:
      real_flag = CCI_CLASS_NAME_PATTERN_MATCH;
      break;
    case CUBRID_SCH_ATTRIBUTE:
    case CUBRID_SCH_CLASS_ATTRIBUTE:
      real_flag = CCI_ATTR_NAME_PATTERN_MATCH;
      break;
    default:
      real_flag = 0;
      break;
    }

  res = cci_schema_info (connect->handle, GET_ZVAL (schema_type)->value.lval,
			 real_class_name, real_attr_name, (char) real_flag,
			 &error);

  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_LONG (-1);
    }
  request_handle = res;

  array_init (return_value);
  while (1)
    {
      res = cci_cursor (request_handle, 1, CCI_CURSOR_CURRENT, &error);
      if (res == CCI_ER_NO_MORE_DATA)
	{
	  break;
	}
      if (res < 0)
	{
	  handle_error (res, &error);
	  RETURN_LONG (-1);
	}

      res = cci_fetch (request_handle, &error);
      if (res < 0)
	{
	  handle_error (res, &error);
	  RETURN_LONG (-1);
	}

      ALLOC_ZVAL (temp_element);
      if ((res = fetch_a_row (temp_element, request_handle,
			      CUBRID_ASSOC)) != SUCCESS)
	{
	  handle_error (res, NULL);
	  FREE_ZVAL (temp_element);
	  RETURN_LONG (-1);
	}
      INIT_PZVAL (temp_element);

      zend_hash_index_update (return_value->value.ht, i++,
			      (void *) &temp_element, sizeof (pzval), NULL);
      if (res < 0)
	{
	  handle_error (res, &error);
	  RETURN_LONG (-1);
	}
    }
  cci_close_req_handle (request_handle);
  return;

}

PHP_FUNCTION (cubrid_col_size)
{
  pzval *con_handle, *oid, *attr_name;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  int col_size;

  init_error ();
  if (ARG_COUNT (ht) == 3)
    {
      if (zend_get_parameters_ex (3, &con_handle, &oid, &attr_name) ==
	  FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);

  res = cci_col_size (connect->handle, GET_ZVAL (oid)->value.str.val,
		      GET_ZVAL (attr_name)->value.str.val, &col_size, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_LONG (-1);
    }
  RETURN_LONG (col_size);

}

PHP_FUNCTION (cubrid_col_get)
{
  pzval *con_handle, *oid, *attr_name;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char *res_buf;
  int ind;
  int col_size;
  int col_type;
  int request_handle;
  int i = 0;

  init_error ();
  if (ARG_COUNT (ht) == 3)
    {
      if (zend_get_parameters_ex (3, &con_handle, &oid, &attr_name) ==
	  FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);

  res = cci_col_get (connect->handle, GET_ZVAL (oid)->value.str.val,
		     GET_ZVAL (attr_name)->value.str.val, &col_size,
		     &col_type, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  request_handle = res;
  array_init (return_value);
  while (1)
    {
      res = cci_cursor (request_handle, 1, CCI_CURSOR_CURRENT, &error);
      if (res == CCI_ER_NO_MORE_DATA)
	{
	  break;
	}
      if (res < 0)
	{
	  handle_error (res, &error);
	  RETURN_FALSE;
	}

      res = cci_fetch (request_handle, &error);
      if (res < 0)
	{
	  handle_error (res, &error);
	  RETURN_FALSE;
	}

      res = cci_get_data (request_handle, 1, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0)
	{
	  handle_error (res, NULL);
	  RETURN_FALSE;
	}
      if (ind < 0)
	{
	  add_index_unset (return_value, i);
	}
      else
	{
	  add_index_stringl (return_value, i, res_buf, ind, 1);
	}
      i++;
    }
  cci_close_req_handle (request_handle);
  return;
}

PHP_FUNCTION (cubrid_set_add)
{
  pzval *con_handle, *oid, *attr_name, *set_element;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char *value;

  init_error ();
  if (ARG_COUNT (ht) == 4)
    {
      if (zend_get_parameters_ex (4, &con_handle, &oid, &attr_name,
				  &set_element) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);

  if (GET_ZVAL (set_element)->type == IS_NULL)
    {
      value = NULL;
    }
  else
    {
      convert_to_string_ex (set_element);
      value = GET_ZVAL (set_element)->value.str.val;
    }
  res = cci_col_set_add (connect->handle, GET_ZVAL (oid)->value.str.val,
			 GET_ZVAL (attr_name)->value.str.val, value, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_set_drop)
{
  pzval *con_handle, *oid, *attr_name, *set_element;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) == 4)
    {
      if (zend_get_parameters_ex (4, &con_handle, &oid, &attr_name,
				  &set_element) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL ||
	  GET_ZVAL (set_element)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);
  convert_to_string_ex (set_element);

  res = cci_col_set_drop (connect->handle, GET_ZVAL (oid)->value.str.val,
			  GET_ZVAL (attr_name)->value.str.val,
			  GET_ZVAL (set_element)->value.str.val, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_seq_insert)
{
  pzval *con_handle, *oid, *attr_name, *index, *seq_element;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char *value;

  init_error ();
  if (ARG_COUNT (ht) == 5)
    {
      if (zend_get_parameters_ex (5, &con_handle, &oid, &attr_name, &index,
				  &seq_element) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL ||
	  GET_ZVAL (index)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);
  convert_to_long_ex (index);

  if (GET_ZVAL (seq_element)->type == IS_NULL)
    {
      value = NULL;
    }
  else
    {
      convert_to_string_ex (seq_element);
      value = GET_ZVAL (seq_element)->value.str.val;
    }

  res = cci_col_seq_insert (connect->handle, GET_ZVAL (oid)->value.str.val,
			    GET_ZVAL (attr_name)->value.str.val,
			    GET_ZVAL (index)->value.lval, value, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_seq_put)
{
  pzval *con_handle, *oid, *attr_name, *index, *seq_element;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;
  char *value;


  init_error ();
  if (ARG_COUNT (ht) == 5)
    {
      if (zend_get_parameters_ex (5, &con_handle, &oid, &attr_name, &index,
				  &seq_element) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL ||
	  GET_ZVAL (index)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);
  convert_to_long_ex (index);

  if (GET_ZVAL (seq_element)->type == IS_NULL)
    {
      value = NULL;
    }
  else
    {
      convert_to_string_ex (seq_element);
      value = GET_ZVAL (seq_element)->value.str.val;
    }

  res = cci_col_seq_put (connect->handle, GET_ZVAL (oid)->value.str.val,
			 GET_ZVAL (attr_name)->value.str.val,
			 GET_ZVAL (index)->value.lval, value, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_seq_drop)
{
  pzval *con_handle, *oid, *attr_name, *index;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) == 4)
    {
      if (zend_get_parameters_ex (4, &con_handle, &oid, &attr_name,
				  &index) == FAILURE)
	{
	  RETURN_FALSE;
	}
      if (GET_ZVAL (con_handle)->type == IS_NULL ||
	  GET_ZVAL (oid)->type == IS_NULL ||
	  GET_ZVAL (attr_name)->type == IS_NULL ||
	  GET_ZVAL (index)->type == IS_NULL)
	{
	  handle_error (CUBRID_ER_INVALID_PARAM, NULL);
	  RETURN_FALSE;
	}

    }
  else
    {
      WRONG_PARAM_COUNT;
    }

  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  convert_to_string_ex (oid);
  convert_to_string_ex (attr_name);
  convert_to_long_ex (index);

  res = cci_col_seq_drop (connect->handle, GET_ZVAL (oid)->value.str.val,
			  GET_ZVAL (attr_name)->value.str.val,
			  GET_ZVAL (index)->value.lval, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_commit)
{
  pzval *con_handle;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &con_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res = cci_end_tran (connect->handle, CCI_TRAN_COMMIT, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_rollback)
{
  pzval *con_handle;
  T_CUBRID_CONNECT *connect;
  T_CCI_ERROR error;
  int res;

  init_error ();
  if (ARG_COUNT (ht) != 1)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (1, &con_handle) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  res = cci_end_tran (connect->handle, CCI_TRAN_ROLLBACK, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_new_glo)
{
  pzval *con_handle, *class_name, *file_name;
  T_CUBRID_CONNECT *connect;
  int res;
  char oid_buf[1024];
  T_CCI_ERROR error;

  init_error ();
  if (ARG_COUNT (ht) != 3)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (3, &con_handle, &class_name, &file_name)
      == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL ||
      GET_ZVAL (class_name)->type == IS_NULL ||
      GET_ZVAL (file_name)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);
  res = cci_glo_new (connect->handle, GET_ZVAL (class_name)->value.str.val,
		     GET_ZVAL (file_name)->value.str.val, oid_buf, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_STRING (oid_buf, 1);
}

PHP_FUNCTION (cubrid_save_to_glo)
{
  pzval *con_handle, *oid, *file_name;
  T_CUBRID_CONNECT *connect;
  int res;
  T_CCI_ERROR error;

  init_error ();
  if (ARG_COUNT (ht) != 3)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (3, &con_handle, &oid, &file_name) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL ||
      GET_ZVAL (oid)->type == IS_NULL ||
      GET_ZVAL (file_name)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);
  res = cci_glo_save (connect->handle, GET_ZVAL (oid)->value.str.val,
		      GET_ZVAL (file_name)->value.str.val, &error);
  if (res < 0)
    {
      handle_error (res, &error);
      RETURN_FALSE;
    }
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_load_from_glo)
{
  pzval *con_handle, *oid, *file_name;
  T_CUBRID_CONNECT *connect;
  int res;
  T_CCI_ERROR error;
  FILE *fp;

  init_error ();
  if (ARG_COUNT (ht) != 3)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (3, &con_handle, &oid, &file_name) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL ||
      GET_ZVAL (oid)->type == IS_NULL ||
      GET_ZVAL (file_name)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  if ((fp = fopen (GET_ZVAL (file_name)->value.str.val, "wb")) == NULL)
    {
      /* 파일을 열지 못 했음 */
      handle_error (CUBRID_ER_OPEN_FILE, NULL);
      RETURN_FALSE;
    }
  res = cci_glo_load (connect->handle, GET_ZVAL (oid)->value.str.val,
		      fileno (fp), &error);
  if (res < 0)
    {
      handle_error (res, &error);
      fclose (fp);
      RETURN_FALSE;
    }
  fclose (fp);
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_send_glo)
{
  pzval *con_handle, *oid;
  T_CUBRID_CONNECT *connect;
  int res;
  T_CCI_ERROR error;
  FILE *fp;
  char out_buf[1024];
  int buf_len;

  init_error ();
  if (ARG_COUNT (ht) != 2)
    {
      WRONG_PARAM_COUNT;
    }
  if (zend_get_parameters_ex (2, &con_handle, &oid) == FAILURE)
    {
      RETURN_FALSE;
    }
  if (GET_ZVAL (con_handle)->type == IS_NULL ||
      GET_ZVAL (oid)->type == IS_NULL)
    {
      handle_error (CUBRID_ER_INVALID_PARAM, NULL);
      RETURN_FALSE;
    }
  ZEND_FETCH_RESOURCE (connect, T_CUBRID_CONNECT *, con_handle, -1,
		       "CUBRID-Connect", le_connect);

  if ((fp = tmpfile ()) == NULL)
    {
      /* tmp file을 못 만들었음 */
      handle_error (CUBRID_ER_CREATE_TEMP_FILE, NULL);
      RETURN_FALSE;
    }
  res = cci_glo_load (connect->handle, GET_ZVAL (oid)->value.str.val,
		      fileno (fp), &error);
  if (res < 0)
    {
      handle_error (res, &error);
      fclose (fp);
      RETURN_FALSE;
    }
  rewind (fp);
  while (!feof (fp))
    {
      buf_len = fread (out_buf, sizeof (char), 1024, fp);
      /*if (zend_write(out_buf, buf_len) != buf_len) { */
      if (PHPWRITE (out_buf, buf_len) != buf_len)
	{
	  /* 클라이언트로 보내줄 때 에러 발생 */
	  handle_error (CUBRID_ER_TRANSFER_FAIL, NULL);
	  fclose (fp);
	  RETURN_FALSE;
	}
    }
  fclose (fp);
  RETURN_TRUE;
}

PHP_FUNCTION (cubrid_error_msg)
{
  UniSLS_FETCH ();
  RETURN_STRING (UniSG (recent_error).msg, 1);
}

PHP_FUNCTION (cubrid_error_code)
{
  UniSLS_FETCH ();
  RETURN_LONG (UniSG (recent_error).code);
}

PHP_FUNCTION (cubrid_error_code_facility)
{
  UniSLS_FETCH ();
  RETURN_LONG (UniSG (recent_error).facility);
}


/************************************************************************
* IMPLEMENTATION OF PRIVATE FUNCTIONS
************************************************************************/

static void
close_cubrid_connect (T_CUBRID_CONNECT * conn)
{
  T_CCI_ERROR error;
  cci_disconnect (conn->handle, &error);
  efree (conn);
}

static void
close_cubrid_request (T_CUBRID_REQUEST * req)
{
  /* cci_close_req_handle은 cubrid_close_request에서 호출함
     cci_close_req_handle(req->handle);
   */
  if (req->l_bind)
    efree (req->l_bind);
  efree (req);
}

static void
php_cubrid_init_globals (zend_cubrid_globals * cubrid_globals)
{
}

static int
init_error (void)
{
  set_error (0, 0, "");
  return SUCCESS;
}

static int
set_error (T_FACILITY_CODE facility, int code, char *msg, ...)
{
  va_list args;
  UniSLS_FETCH ();
  UniSG (recent_error).facility = facility;
  UniSG (recent_error).code = code;
  va_start (args, msg);
  sprintf (UniSG (recent_error).msg, msg, args);
  va_end (args);
  return SUCCESS;
}

static int
handle_error (int err_code, T_CCI_ERROR * error)
{
  int real_err_code;
  char *real_err_msg;
  T_FACILITY_CODE facility;
  char err_msg[1000];
  char *facility_msg;

  if (err_code == CCI_ER_DBMS)
    {
      facility = CUBRID_FACILITY_DBMS;
      facility_msg = "DBMS";
      if (error)
	{
	  real_err_code = error->err_code;
	  real_err_msg = error->err_msg;
	}
      else
	{
	  /* Unknown error */
	  real_err_code = 0;
	  real_err_msg = "Unknown DBMS error";
	}
    }
  else if (err_code > -1000)
    {
      if (get_error_msg (err_code, err_msg) < 0)
	{
	  strcpy (err_msg, "Unknown error message");
	}
      facility = CUBRID_FACILITY_CCI;
      facility_msg = "CCI";
      real_err_code = err_code;
      real_err_msg = err_msg;
    }
  else if (err_code > -2000)
    {
      if (get_error_msg (err_code, err_msg) < 0)
	{
	  strcpy (err_msg, "Unknown error message");
	}
      facility = CUBRID_FACILITY_CAS;
      facility_msg = "CAS";
      real_err_code = err_code;
      real_err_msg = err_msg;
    }
  else if (err_code > -3000)
    {
      if (get_error_msg (err_code, err_msg) < 0)
	{
	  strcpy (err_msg, "Unknown error message");
	}
      facility = CUBRID_FACILITY_CLIENT;
      facility_msg = "CLIENT";
      real_err_code = err_code;
      real_err_msg = err_msg;
    }
  else
    {
      real_err_code = -1;
      real_err_msg = NULL;
      return FAILURE;
    }
  set_error (facility, real_err_code, real_err_msg);
  php_error (E_WARNING, "Error: %s, %d, %s", facility_msg, real_err_code,
	     real_err_msg);
  return SUCCESS;
}

static int
fetch_a_row (pval * arg, int req_handle, int type)
{
  int i;
  int res;
  int ind;
  T_CCI_COL_INFO *col_info;
  int col_count;
//      pzval new_element;
  int error_occured = 0;
  int error_code = 0;

  col_info = cci_get_result_info (req_handle, NULL, &col_count);
  if (!col_info)
    {
      /* 컬럼정보를 읽을 수 없음 */
      return CUBRID_ER_CANNOT_GET_COLUMN_INFO;
    }
  if (array_init (arg) == FAILURE)
    {
      /* array를 초기화 할 수 없음 */
      return CUBRID_ER_INIT_ARRAY_FAIL;
    }

  for (i = 0; i < col_count; i++)
    {
      if (error_occured)
	break;
      /*
         return FAILURE;
       */
      if (CCI_IS_SET_TYPE (CCI_GET_RESULT_INFO_TYPE (col_info, i + 1)) ||
	  CCI_IS_MULTISET_TYPE (CCI_GET_RESULT_INFO_TYPE (col_info, i + 1)) ||
	  CCI_IS_SEQUENCE_TYPE (CCI_GET_RESULT_INFO_TYPE (col_info, i + 1)))
	{


	  T_CCI_SET res_buf;

	  res = cci_get_data (req_handle, i + 1, CCI_A_TYPE_SET,
			      &res_buf, &ind);
	  if (res < 0)
	    {
	      error_occured = 1;
	      error_code = res;
	      goto fetch_error_handler;
	    }
	  if (ind < 0)
	    {
	      if (type & CUBRID_NUM)
		add_index_unset (arg, i);
	      if (type & CUBRID_ASSOC)
		add_assoc_unset (arg,
				 CCI_GET_RESULT_INFO_NAME (col_info, i + 1));
	    }
	  else
	    {
	      if (type & CUBRID_NUM)
		{
		  res = cubrid_add_index_array (arg, i, res_buf);
		  if (res < 0)
		    {
		      cci_set_free (res_buf);
		      error_occured = 1;
		      error_code = res;
		      goto fetch_error_handler;
		    }
		}
	      if (type & CUBRID_ASSOC)
		{
		  res = cubrid_add_assoc_array (arg,
						CCI_GET_RESULT_INFO_NAME
						(col_info, i + 1), res_buf);
		  if (res < 0)
		    {
		      cci_set_free (res_buf);
		      error_occured = 1;
		      error_code = res;
		      goto fetch_error_handler;
		    }
		}
	      cci_set_free (res_buf);
	    }
	}
      else
	{			/* string으로 얻어냄 */
	  char *res_buf;
	  res = cci_get_data (req_handle, i + 1, CCI_A_TYPE_STR,
			      &res_buf, &ind);
	  if (res < 0)
	    {
	      error_occured = 1;
	      error_code = res;
	      goto fetch_error_handler;
	    }
	  if (ind < 0)
	    {
	      if (type & CUBRID_NUM)
		add_index_unset (arg, i);
	      if (type & CUBRID_ASSOC)
		add_assoc_unset (arg,
				 CCI_GET_RESULT_INFO_NAME (col_info, i + 1));
	    }
	  else
	    {
	      if (type & CUBRID_NUM)
		{
		  add_index_stringl (arg, i, res_buf, ind, 1);
		}
	      if (type & CUBRID_ASSOC)
		{
		  add_assoc_stringl (arg,
				     CCI_GET_RESULT_INFO_NAME (col_info,
							       i + 1),
				     res_buf, ind, 1);
		}
	    }
	}
    }
  return SUCCESS;

fetch_error_handler:
  cubrid_array_destroy (arg->value.ht ZEND_FILE_LINE_CC);
  return error_code;
}

static T_CUBRID_REQUEST *
new_request (void)
{
  T_CUBRID_REQUEST *tmp_request;

  tmp_request = (T_CUBRID_REQUEST *) emalloc (sizeof (T_CUBRID_REQUEST));
  if (!tmp_request)
    return NULL;
  tmp_request->handle = 0;
  tmp_request->affected_rows = -1;
  tmp_request->async_mode = 0;
  tmp_request->row_count = -1;
  tmp_request->col_count = -1;
  tmp_request->sql_type = 0;
  tmp_request->bind_num = -1;
  tmp_request->l_bind = NULL;
  tmp_request->l_prepare = 0;

  return tmp_request;
}

static int
convert_sql (char *str, char ***objs)
{
  char *p, *q;
  char *obj;
  int i = 0, j = 0;
  /* j: objs count */
  int state = OUT_STR;

  q = (char *) malloc (strlen (str) + 1);
  for (p = str; *p; p++)
    {
      switch (*p)
	{
	case '?':
	  if (state == OUT_STR)
	    {
	      q[i++] = ' ';
	      continue;
	    }
	  break;
	case '\'':
	  TRANS (state);
	  break;
	case '@':
	  if (state == OUT_STR && (obj = str2obj (p)))
	    {
	      q[i++] = '?';
	      *objs = (char **) realloc (*objs, sizeof (char *) * (++j));
	      (*objs)[j - 1] = strdup (obj);
	      p += strlen (obj) - 1;
	      continue;
	    }
	  break;
	default:
	  break;
	}
      q[i++] = *p;
    }
  q[i] = '\0';
  strcpy (str, q);
  free (q);
  return j;
}

static char *
str2obj (char *str)
{
  int page_id, slot_id, vol_id;
  char del1, del2, del3;
  char buf[1024];
  if (sscanf (str, "%c%d%c%d%c%d", &del1, &page_id, &del2, &slot_id, &del3,
	      &vol_id) == 6)
    {
      if (del1 == '@' && del2 == '|' && del3 == '|' && page_id >= 0 &&
	  slot_id >= 0 && vol_id >= 0)
	{
	  sprintf (buf, "@%d|%d|%d", page_id, slot_id, vol_id);
	  return strdup (buf);
	}
    }
  return NULL;
}

static int
cubrid_add_index_array (pval * arg, uint index, T_CCI_SET in_set)
{
  pval *tmp_ptr;
  int count;
  int i;
  char *buffer;
  int res;
  int ind;

  count = cci_set_size (in_set);
  ALLOC_ZVAL (tmp_ptr);
  array_init (tmp_ptr);
  for (i = 0; i < count; i++)
    {
      res = cci_set_get (in_set, i + 1, CCI_A_TYPE_STR, &buffer, &ind);
      if (res < 0)
	{
	  cubrid_array_destroy (tmp_ptr->value.ht ZEND_FILE_LINE_CC);
	  FREE_ZVAL (tmp_ptr);
	  return res;
	}
      if (ind < 0)
	{
	  add_index_unset (tmp_ptr, i);
	}
      else
	{
	  add_index_string (tmp_ptr, i, buffer, 1);
	}
    }
  INIT_PZVAL (tmp_ptr);

  res = zend_hash_index_update (arg->value.ht, index,
				(void *) &tmp_ptr, sizeof (zval *), NULL);

  if (res == FAILURE)
    {
      cubrid_array_destroy (tmp_ptr->value.ht ZEND_FILE_LINE_CC);
      FREE_ZVAL (tmp_ptr);
      return CUBRID_ER_PHP;
    }
  return 0;
}

static int
cubrid_add_assoc_array (pval * arg, char *key, T_CCI_SET in_set)
{
  pval *tmp_ptr;
  int count;
  int i;
  char *buffer;
  int res;
  int ind;

  count = cci_set_size (in_set);
  ALLOC_ZVAL (tmp_ptr);
  array_init (tmp_ptr);
  for (i = 0; i < count; i++)
    {
      res = cci_set_get (in_set, i + 1, CCI_A_TYPE_STR, &buffer, &ind);
      if (res < 0)
	{
	  cubrid_array_destroy (tmp_ptr->value.ht ZEND_FILE_LINE_CC);
	  FREE_ZVAL (tmp_ptr);
	  return res;
	}
      if (ind < 0)
	{
	  add_index_unset (tmp_ptr, i);
	}
      else
	{
	  add_index_string (tmp_ptr, i, buffer, 1);
	}
    }
  INIT_PZVAL (tmp_ptr);
  res = zend_hash_update (arg->value.ht, key, strlen (key) + 1,
			  (void *) &tmp_ptr, sizeof (zval *), NULL);
  if (res == FAILURE)
    {
      cubrid_array_destroy (tmp_ptr->value.ht ZEND_FILE_LINE_CC);
      FREE_ZVAL (tmp_ptr);
      return CUBRID_ER_PHP;
    }
  return 0;
}

static int
cubrid_array_destroy (HashTable * ht ZEND_FILE_LINE_DC)
{
  zend_hash_destroy (ht);
  FREE_HASHTABLE_REL (ht);
  return SUCCESS;
}

static int
cubrid_make_set (HashTable * ht, T_CCI_SET * set)
{
  void **temp_set_array = NULL;
  int *temp_set_null = NULL;
  char *key;
  ulong index;
  pzval *data;
  int set_size;
  int res;
  int i;
  int error_code;

  set_size = zend_hash_num_elements (ht);
  temp_set_array = (void **) emalloc (sizeof (void *) * set_size);
  for (i = 0; i < set_size; i++)
    temp_set_array[i] = NULL;
  temp_set_null = (int *) emalloc (sizeof (int) * set_size);
  zend_hash_internal_pointer_reset (ht);
  i = 0;
  for (;; zend_hash_move_forward (ht))
    {
      res = zend_hash_get_current_key (ht, &key, &index, 0);
      if (res == HASH_KEY_NON_EXISTANT)
	break;
      res = zend_hash_get_current_data (ht, (void **) &data);
      switch (GET_ZVAL (data)->type)
	{
	case IS_NULL:
	  temp_set_array[i] = NULL;
	  temp_set_null[i] = 1;
	  break;
	case IS_LONG:
	case IS_DOUBLE:
	  convert_to_string_ex (data);
	case IS_STRING:
	  temp_set_array[i] = GET_ZVAL (data)->value.str.val;
	  temp_set_null[i] = 0;
	  break;
	default:
	  error_code = CUBRID_ER_NOT_SUPPORTED_TYPE;
	  goto return_error;
	  break;
	}
      i++;
    }
  res = cci_set_make (set, CCI_U_TYPE_STRING,
		      set_size, temp_set_array, temp_set_null);
  if (res < 0)
    {
      *set = NULL;
      error_code = res;
      goto return_error;
    }
  efree (temp_set_array);
  efree (temp_set_null);
  return 0;

return_error:
  if (temp_set_array)
    {
      efree (temp_set_array);
    }
  if (temp_set_null)
    {
      efree (temp_set_null);
    }
  return error_code;
}
