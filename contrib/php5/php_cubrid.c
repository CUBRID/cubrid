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

#define _CRT_SECURE_NO_WARNINGS

/************************************************************************
* IMPORTED SYSTEM HEADER FILES
************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "php_globals.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"

#include "zend_exceptions.h"

#ifdef PHP_WIN32
#include <winsock.h>
#endif

/************************************************************************
* OTHER IMPORTED HEADER FILES
************************************************************************/

#include "php_cubrid.h"
#include "php_cubrid_version.h"
#include <cas_cci.h>

/************************************************************************
* PRIVATE DEFINITIONS
************************************************************************/

#if defined(WINDOWS)
#define snprintf _snprintf
#define strncasecmp _strnicmp
#endif

#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#if PHP_MINOR_VERSION < 3
#define zend_parse_parameters_none() zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "")
#endif

#define CUBRID_LOB_READ_BUF_SIZE    8192

/* EXECUTE */
#define CUBRID_INCLUDE_OID	    1
#define CUBRID_ASYNC		    2
#define CUBRID_EXEC_QUERY_ALL       4

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

/* SCHEMA */
#define CUBRID_SCH_CLASS		CCI_SCH_CLASS
#define CUBRID_SCH_VCLASS		CCI_SCH_VCLASS
#define CUBRID_SCH_QUERY_SPEC		CCI_SCH_QUERY_SPEC
#define CUBRID_SCH_ATTRIBUTE		CCI_SCH_ATTRIBUTE
#define CUBRID_SCH_CLASS_ATTRIBUTE	CCI_SCH_CLASS_ATTRIBUTE
#define CUBRID_SCH_METHOD		CCI_SCH_METHOD
#define CUBRID_SCH_CLASS_METHOD		CCI_SCH_CLASS_METHOD
#define CUBRID_SCH_METHOD_FILE		CCI_SCH_METHOD_FILE
#define CUBRID_SCH_SUPERCLASS		CCI_SCH_SUPERCLASS
#define CUBRID_SCH_SUBCLASS		CCI_SCH_SUBCLASS
#define CUBRID_SCH_CONSTRAINT		CCI_SCH_CONSTRAINT
#define CUBRID_SCH_TRIGGER		CCI_SCH_TRIGGER
#define CUBRID_SCH_CLASS_PRIVILEGE	CCI_SCH_CLASS_PRIVILEGE
#define CUBRID_SCH_ATTR_PRIVILEGE	CCI_SCH_ATTR_PRIVILEGE
#define CUBRID_SCH_DIRECT_SUPER_CLASS	CCI_SCH_DIRECT_SUPER_CLASS
#define CUBRID_SCH_PRIMARY_KEY		CCI_SCH_PRIMARY_KEY
#define CUBRID_SCH_IMPORTED_KEYS        CCI_SCH_IMPORTED_KEYS
#define CUBRID_SCH_EXPORTED_KEYS        CCI_SCH_EXPORTED_KEYS
#define CUBRID_SCH_CROSS_REFERENCE      CCI_SCH_CROSS_REFERENCE

/* ERROR FACILITY */
typedef enum
{
    CUBRID_FACILITY_DBMS = 1,
    CUBRID_FACILITY_CAS,
    CUBRID_FACILITY_CCI,
    CUBRID_FACILITY_CLIENT,
} T_FACILITY_CODE;

/* error codes */
#define CUBRID_ER_INVALID_SQL_TYPE 		-2002
#define CUBRID_ER_CANNOT_GET_COLUMN_INFO 	-2003
#define CUBRID_ER_INVALID_PARAM 		-2006
#define CUBRID_ER_NOT_SUPPORTED_TYPE 		-2008
#define CUBRID_ER_TRANSFER_FAIL 		-2011
#define CUBRID_ER_PHP				-2012
#define CUBRID_ER_PARAM_UNBIND                  -2015
#define CUBRID_ER_INVALID_PARAM_TYPE            -2022
/* CAUTION! Also add the error message string to db_error[] */

/* Maximum length for the Cubrid data types. 
 *
 * The max len of LOB is the max file size creatable in an external storage, 
 * so we ca't give the max len of LOB type, just use 1G. Please ignore it. 
 */
#define MAX_CUBRID_CHAR_LEN   1073741823
#define MAX_LEN_INTEGER	      (10 + 1)
#define MAX_LEN_SMALLINT      (5 + 1)
#define MAX_LEN_BIGINT	      (19 + 1)
#define MAX_LEN_FLOAT	      (14 + 1)
#define MAX_LEN_DOUBLE	      (28 + 1)
#define MAX_LEN_MONETARY      (28 + 2)
#define MAX_LEN_DATE	      10
#define MAX_LEN_TIME	      8
#define MAX_LEN_TIMESTAMP     23
#define MAX_LEN_DATETIME      MAX_LEN_TIMESTAMP
#define MAX_LEN_OBJECT	      MAX_CUBRID_CHAR_LEN
#define MAX_LEN_SET	      MAX_CUBRID_CHAR_LEN
#define MAX_LEN_MULTISET      MAX_CUBRID_CHAR_LEN
#define MAX_LEN_SEQUENCE      MAX_CUBRID_CHAR_LEN
#define MAX_LEN_LOB           MAX_CUBRID_CHAR_LEN

/* Max Cubrid supported charsets */
#define MAX_DB_CHARSETS 6

/* Max Cubrid unescaped string len */
#define MAX_UNESCAPED_STR_LEN 4096

typedef struct
{
    int err_code;
    char *err_msg;
} DB_ERROR_INFO;

/* Define addtion error info */
static const DB_ERROR_INFO db_error[] = {
    {CUBRID_ER_INVALID_SQL_TYPE, "Invalid API call"},
    {CUBRID_ER_CANNOT_GET_COLUMN_INFO, "Cannot get column info"},
    {CUBRID_ER_INVALID_PARAM, "Invalid parameter"},
    {CUBRID_ER_NOT_SUPPORTED_TYPE, "Invalid type"},
    {CUBRID_ER_TRANSFER_FAIL, "Lob transfering error"},
    {CUBRID_ER_PHP, "PHP error"},
    {CUBRID_ER_PARAM_UNBIND, "Some parameter not binded"},
    {CUBRID_ER_INVALID_PARAM_TYPE, "Invalid db parameter type"},
};

typedef struct
{
    const char *charset_name;
    const char *charset_desc;
    const char *space_char;
    int charset_id;
    int default_collation;
    int space_size;
} DB_CHARSET;

/* Define Cubrid supported charsets, 
 * now we only use charset_name, so just set space_char to empty */
static const DB_CHARSET db_charsets[] = {
    {"ascii", "US English charset - ASCII encoding", "", 0, 0, 1},
    {"raw-bits", "Uninterpreted bits - Raw encoding", "", 1, 0, 1},
    {"raw-bytes", "Uninterpreted bytes - Raw encoding", "", 2, 0, 1},
    {"iso8859-1", "Latin 1 charset - ISO 8859 encoding", "", 3, 0, 1},
    {"ksc-euc", "KSC 5601 1990 charset - EUC encoding", "", 4, 0, 2},
    {"utf-8", "UNICODE charset - UTF-8 encoding", " ", 5, 0, 1},
    {"", "Unknown encoding", "", -1, 0, 0}
};

typedef struct
{
    char *type_name;
    T_CCI_U_TYPE cubrid_u_type;
    int len;
} DB_TYPE_INFO;

/* Define Cubrid supported date types */
static const DB_TYPE_INFO db_type_info[] = {
    {"NULL", CCI_U_TYPE_NULL, 0},
    {"UNKNOWN", CCI_U_TYPE_UNKNOWN, MAX_LEN_OBJECT},

    {"CHAR", CCI_U_TYPE_CHAR, -1},
    {"STRING", CCI_U_TYPE_STRING, -1},
    {"NCHAR", CCI_U_TYPE_NCHAR, -1},
    {"VARNCHAR", CCI_U_TYPE_VARNCHAR, -1},

    {"BIT", CCI_U_TYPE_BIT, -1},
    {"VARBIT", CCI_U_TYPE_VARBIT, -1},

    {"NUMERIC", CCI_U_TYPE_NUMERIC, -1},
    {"NUMBER", CCI_U_TYPE_NUMERIC, -1},
    {"INT", CCI_U_TYPE_INT, MAX_LEN_INTEGER},
    {"SHORT", CCI_U_TYPE_SHORT, MAX_LEN_SMALLINT},
    {"BIGINT", CCI_U_TYPE_BIGINT, MAX_LEN_BIGINT},
    {"MONETARY", CCI_U_TYPE_MONETARY, MAX_LEN_MONETARY},

    {"FLOAT", CCI_U_TYPE_FLOAT, MAX_LEN_FLOAT},
    {"DOUBLE", CCI_U_TYPE_DOUBLE, MAX_LEN_DOUBLE},

    {"DATE", CCI_U_TYPE_DATE, MAX_LEN_DATE},
    {"TIME", CCI_U_TYPE_TIME, MAX_LEN_TIME},
    {"DATETIME", CCI_U_TYPE_DATETIME, MAX_LEN_DATETIME},
    {"TIMESTAMP", CCI_U_TYPE_TIMESTAMP, MAX_LEN_TIMESTAMP},

    {"SET", CCI_U_TYPE_SET, MAX_LEN_SET},
    {"MULTISET", CCI_U_TYPE_MULTISET, MAX_LEN_MULTISET},
    {"SEQUENCE", CCI_U_TYPE_SEQUENCE, MAX_LEN_SEQUENCE},
    {"RESULTSET", CCI_U_TYPE_RESULTSET, -1},

    {"OBJECT", CCI_U_TYPE_OBJECT, MAX_LEN_OBJECT},
    {"BLOB", CCI_U_TYPE_BLOB, MAX_LEN_LOB},
    {"CLOB", CCI_U_TYPE_CLOB, MAX_LEN_LOB}
};

/* DB parameters */
#define CUBRID_PARAM_ISOLATION_LEVEL    CCI_PARAM_ISOLATION_LEVEL
#define CUBRID_PARAM_LOCK_TIMEOUT       CCI_PARAM_LOCK_TIMEOUT

#define CUBRID_AUTOCOMMIT_FALSE    CCI_AUTOCOMMIT_FALSE
#define CUBRID_AUTOCOMMIT_TRUE     CCI_AUTOCOMMIT_TRUE

/* Define CUBRID DB parameters */
typedef struct
{
    T_CCI_DB_PARAM parameter_id;
    const char *parameter_name;
} DB_PARAMETER;

static const DB_PARAMETER db_parameters[] = {
    {CCI_PARAM_ISOLATION_LEVEL, "PARAM_ISOLATION_LEVEL"},
    {CCI_PARAM_LOCK_TIMEOUT, "PARAM_LOCK_TIMEOUT"},
    {CCI_PARAM_MAX_STRING_LENGTH, "PARAM_MAX_STRING_LENGTH"},
    {CCI_PARAM_AUTO_COMMIT, "PARAM_AUTO_COMMIT"}
};

/************************************************************************
* PRIVATE TYPE DEFINITIONS
************************************************************************/

#ifdef PHP_WIN32
typedef __int64 php_cubrid_int64_t;
typedef unsigned __int64 php_cubrid_uint64_t;
#else
typedef long long int php_cubrid_int64_t;
typedef unsigned long long int php_cubrid_uint64_t;
#endif

typedef void *T_CCI_LOB;

typedef struct
{
    T_CCI_LOB lob;
    T_CCI_U_TYPE type;
    php_cubrid_int64_t size;
} T_CUBRID_LOB;

typedef struct cubrid_request T_CUBRID_REQUEST;

typedef struct linked_list_node {
    void *data;
    struct linked_list_node *next;
} LINKED_LIST_NODE;

typedef struct linked_list {
    LINKED_LIST_NODE *head;
    LINKED_LIST_NODE *tail;
} LINKED_LIST;

typedef struct
{
    T_CUBRID_ERROR recent_error;
    int handle;
    int persistent;

    int affected_rows;
    T_CCI_CUBRID_STMT sql_type;

    LINKED_LIST *unclosed_requests;
} T_CUBRID_CONNECT;

struct cubrid_request
{
    T_CUBRID_CONNECT *conn;

    int handle;
    int col_count;
    int row_count;
    int l_prepare;
    int bind_num;
    int *field_lengths;
    short *l_bind;
    int fetch_field_auto_index;
    T_CCI_CUBRID_STMT sql_type;
    T_CCI_COL_INFO *col_info;
    T_CUBRID_LOB *lob;
};

/************************************************************************
* PRIVATE FUNCTION PROTOTYPES
************************************************************************/

static void php_cubrid_init_globals(zend_cubrid_globals *cubrid_globals);

static void close_cubrid_pconnect(zend_rsrc_list_entry *rsrc TSRMLS_DC);
static void close_cubrid_connect(zend_rsrc_list_entry *rsrc TSRMLS_DC);
static void close_cubrid_request(zend_rsrc_list_entry *rsrc TSRMLS_DC);
static void close_cubrid_lob(zend_rsrc_list_entry *rsrc TSRMLS_DC);

static void close_cubrid_request_internal(T_CUBRID_REQUEST * req);
static void close_cubrid_lob_internal(T_CUBRID_LOB *lob);

static int init_error(void);
static int set_error(T_FACILITY_CODE facility, int code, char *msg, ...);
static int init_error_link(T_CUBRID_CONNECT *conn);
static int set_error_link(T_CUBRID_CONNECT *conn, int code, char *msg, ...);
static int get_error_msg(int err_code, char *buf, int buf_size);
static int handle_error(int err_code, T_CCI_ERROR * error, T_CUBRID_CONNECT *conn);

static void php_cubrid_do_connect(INTERNAL_FUNCTION_PARAMETERS, int persistent);
static void php_cubrid_do_connect_with_url(INTERNAL_FUNCTION_PARAMETERS, int persistent);

static T_CUBRID_CONNECT *new_cubrid_connect(int persistent);
static T_CUBRID_REQUEST *new_cubrid_request(void);
static T_CUBRID_LOB *new_cubrid_lob(void);
static void register_cubrid_request(T_CUBRID_CONNECT *conn, T_CUBRID_REQUEST *req);

static void php_cubrid_set_default_conn(int id TSRMLS_DC);
static void php_cubrid_set_default_req(int id TSRMLS_DC);
static void php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAMETERS, long type, int is_object);

static int fetch_a_row(zval *arg, int conn_handle, int req_handle, int *field_lengths, int type TSRMLS_DC);
static int type2str(T_CCI_COL_INFO *column_info, char *type_name, int type_name_len);

static int cubrid_make_set(HashTable *ht, T_CCI_SET *set);
static int cubrid_add_index_array(zval *arg, uint index, T_CCI_SET in_set TSRMLS_DC);
static int cubrid_add_assoc_array(zval *arg, char *key, T_CCI_SET in_set TSRMLS_DC);
static int cubrid_array_destroy(HashTable *ht ZEND_FILE_LINE_DC);

static int numeric_type(T_CCI_U_TYPE type);
static int get_cubrid_u_type_by_name(const char *type_name);
static int get_cubrid_u_type_len(T_CCI_U_TYPE type);

static int cubrid_lob_new(int con_h_id, T_CCI_LOB *lob, T_CCI_U_TYPE type, T_CCI_ERROR *err_buf);
static php_cubrid_int64_t cubrid_lob_size(T_CCI_LOB lob, T_CCI_U_TYPE type);
static int cubrid_lob_write(int con_h_id, T_CCI_LOB lob, T_CCI_U_TYPE type, php_cubrid_int64_t start_pos, int length, const char *buf, T_CCI_ERROR *err_buf);
static int cubrid_lob_read(int con_h_id, T_CCI_LOB lob, T_CCI_U_TYPE type, php_cubrid_int64_t start_pos, int length, char *buf, T_CCI_ERROR *err_buf);
static int cubrid_lob_free(T_CCI_LOB lob, T_CCI_U_TYPE type);

static char *php_cubrid_int64_to_str(php_cubrid_int64_t i64 TSRMLS_DC);
static int cubrid_parse_params(T_CUBRID_REQUEST *req, char *sql_stmt, size_t sql_stmt_len TSRMLS_DC);

static int cubrid_get_charset_internal(int conn, T_CCI_ERROR *error);

static void linked_list_append(LINKED_LIST *list, void *data);
static void linked_list_delete(LINKED_LIST *list, void *data);

/************************************************************************
* INTERFACE VARIABLES
************************************************************************/

char *cci_client_name = "PHP";

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_version, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_connect, 0, 0, 3)
    ZEND_ARG_INFO(0, host)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, dbname)
    ZEND_ARG_INFO(0, userid)
    ZEND_ARG_INFO(0, passwd)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_pconnect, 0, 0, 3)
    ZEND_ARG_INFO(0, host)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, dbname)
    ZEND_ARG_INFO(0, userid)
    ZEND_ARG_INFO(0, passwd)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_connect_with_url, 0, 0, 1)
    ZEND_ARG_INFO(0, url)
    ZEND_ARG_INFO(0, userid)
    ZEND_ARG_INFO(0, passwd)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_pconnect_with_url, 0, 0, 1)
    ZEND_ARG_INFO(0, url)
    ZEND_ARG_INFO(0, userid)
    ZEND_ARG_INFO(0, passwd)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_close, 0, 0, 0)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_prepare, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, prepare_stmt)
    ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_bind, 0, 0, 3)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, bind_index)
    ZEND_ARG_INFO(0, bind_value)
    ZEND_ARG_INFO(0, bind_value_type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_execute, 0, 0, 1)
    ZEND_ARG_INFO(0, id)
    ZEND_ARG_INFO(0, sql_stmt)
    ZEND_ARG_INFO(0, option)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_next_result, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_affected_rows, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_close_request, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_current_oid, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_column_types, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_column_names, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_move_cursor, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
    ZEND_ARG_INFO(0, origin)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_num_rows, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_num_cols, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_put, 0, 0, 3)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_drop, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_is_instance, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lock_read, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lock_write, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get_class_name, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_schema, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, schema_type)
    ZEND_ARG_INFO(0, class_name)
    ZEND_ARG_INFO(0, attr_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_col_size, 0, 0, 3)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_col_get, 0, 0, 3)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_set_add, 0, 0, 4)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
    ZEND_ARG_INFO(0, set_element)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_set_drop, 0, 0, 4)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
    ZEND_ARG_INFO(0, set_element)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_seq_insert, 0, 0, 5)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
    ZEND_ARG_INFO(0, index)
    ZEND_ARG_INFO(0, set_element)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_seq_put, 0, 0, 5)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
    ZEND_ARG_INFO(0, index)
    ZEND_ARG_INFO(0, set_element)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_seq_drop, 0, 0, 4)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, oid)
    ZEND_ARG_INFO(0, attr_name)
    ZEND_ARG_INFO(0, index)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get_autocommit, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_set_autocommit, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, mode)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_commit, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_rollback, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_error_msg, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_error_code, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_error_code_facility, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_errno, 0)
    ZEND_ARG_INFO(0, conn)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_error, 0)
    ZEND_ARG_INFO(0, conn)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_field_name, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_field_table, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_field_type, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_field_flags, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_data_seek, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch_array, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch_assoc, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch_row, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch_field, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_num_fields, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_free_result, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch_lengths, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_fetch_object, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, class_name)
    ZEND_ARG_INFO(0, params)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_field_seek, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_field_len, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_result, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, row)
    ZEND_ARG_INFO(0, field)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_unbuffered_query, 0, 0, 1)
    ZEND_ARG_INFO(0, query)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_query, 0, 0, 1)
    ZEND_ARG_INFO(0, query)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get_charset, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_client_encoding, 0, 0, 0)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cubrid_get_client_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get_server_info, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_real_escape_string, 0, 0, 1)
    ZEND_ARG_INFO(0, unescaped_string)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get_db_parameter, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_set_db_parameter, 0, 0, 3)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, param_type)
    ZEND_ARG_INFO(0, param_value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_list_dbs, 0, 0, 1)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_db_name, 0, 0, 2)
    ZEND_ARG_INFO(0, result)
    ZEND_ARG_INFO(0, row)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lnsert_id, 0, 0, 0)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_ping, 0, 0, 0)
    ZEND_ARG_INFO(0, conn_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lob_get, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, sql_stmt)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lob_size, 0, 0, 1)
    ZEND_ARG_INFO(0, lob_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lob_export, 0, 0, 3)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, lob_id)
    ZEND_ARG_INFO(0, file_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lob_send, 0, 0, 2)
    ZEND_ARG_INFO(0, conn_id)
    ZEND_ARG_INFO(0, lob_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_lob_close, 0, 0, 1)
    ZEND_ARG_INFO(0, lob_id_array)
ZEND_END_ARG_INFO()



ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_get_query_timeout, 0, 0, 1)
    ZEND_ARG_INFO(0, req_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cubrid_set_query_timeout, 0, 0, 2)
    ZEND_ARG_INFO(0, req_id)
    ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()



zend_function_entry cubrid_functions[] = {
    ZEND_FE(cubrid_version, arginfo_cubrid_version)
    ZEND_FE(cubrid_connect, arginfo_cubrid_connect)
    ZEND_FE(cubrid_pconnect, arginfo_cubrid_pconnect)
    ZEND_FE(cubrid_connect_with_url, arginfo_cubrid_connect_with_url)
    ZEND_FE(cubrid_pconnect_with_url, arginfo_cubrid_pconnect_with_url)
    ZEND_FE(cubrid_close, arginfo_cubrid_close)
    ZEND_FE(cubrid_prepare, arginfo_cubrid_prepare)
    ZEND_FE(cubrid_bind, arginfo_cubrid_bind)
    ZEND_FE(cubrid_execute, arginfo_cubrid_execute)
    ZEND_FE(cubrid_next_result, arginfo_cubrid_next_result)
    ZEND_FE(cubrid_affected_rows, arginfo_cubrid_affected_rows)
    ZEND_FE(cubrid_close_request, arginfo_cubrid_close_request)
    ZEND_FE(cubrid_fetch, arginfo_cubrid_fetch)
    ZEND_FE(cubrid_current_oid, arginfo_cubrid_current_oid)
    ZEND_FE(cubrid_column_types, arginfo_cubrid_column_types)
    ZEND_FE(cubrid_column_names, arginfo_cubrid_column_names)
    ZEND_FE(cubrid_move_cursor, arginfo_cubrid_move_cursor)
    ZEND_FE(cubrid_num_rows, arginfo_cubrid_num_rows)
    ZEND_FE(cubrid_num_cols, arginfo_cubrid_num_cols)
    ZEND_FE(cubrid_get, arginfo_cubrid_get)
    ZEND_FE(cubrid_put, arginfo_cubrid_put)
    ZEND_FE(cubrid_drop, arginfo_cubrid_drop)
    ZEND_FE(cubrid_is_instance, arginfo_cubrid_is_instance)
    ZEND_FE(cubrid_lock_read, arginfo_cubrid_lock_read)
    ZEND_FE(cubrid_lock_write, arginfo_cubrid_lock_write)
    ZEND_FE(cubrid_get_class_name, arginfo_cubrid_get_class_name)
    ZEND_FE(cubrid_schema, arginfo_cubrid_schema)
    ZEND_FE(cubrid_col_size, arginfo_cubrid_col_size)
    ZEND_FE(cubrid_col_get, arginfo_cubrid_col_get)
    ZEND_FE(cubrid_set_add, arginfo_cubrid_set_add)
    ZEND_FE(cubrid_set_drop, arginfo_cubrid_set_drop)
    ZEND_FE(cubrid_seq_insert, arginfo_cubrid_seq_insert)
    ZEND_FE(cubrid_seq_put, arginfo_cubrid_seq_put)
    ZEND_FE(cubrid_seq_drop, arginfo_cubrid_seq_drop)
    ZEND_FE(cubrid_get_autocommit, arginfo_cubrid_get_autocommit)
    ZEND_FE(cubrid_set_autocommit, arginfo_cubrid_set_autocommit)
    ZEND_FE(cubrid_commit, arginfo_cubrid_commit)
    ZEND_FE(cubrid_rollback, arginfo_cubrid_rollback)
    ZEND_FE(cubrid_error_msg, arginfo_cubrid_error_msg)
    ZEND_FE(cubrid_error_code, arginfo_cubrid_error_code)
    ZEND_FE(cubrid_error_code_facility, arginfo_cubrid_error_code_facility)
    ZEND_FE(cubrid_errno, arginfo_cubrid_errno)
    ZEND_FE(cubrid_error, arginfo_cubrid_error)
    ZEND_FE(cubrid_field_name, arginfo_cubrid_field_name)
    ZEND_FE(cubrid_field_table, arginfo_cubrid_field_table)
    ZEND_FE(cubrid_field_type, arginfo_cubrid_field_type)
    ZEND_FE(cubrid_field_flags, arginfo_cubrid_field_flags)
    ZEND_FE(cubrid_data_seek, arginfo_cubrid_data_seek)
    ZEND_FE(cubrid_fetch_array, arginfo_cubrid_fetch_array)
    ZEND_FE(cubrid_fetch_assoc, arginfo_cubrid_fetch_assoc)
    ZEND_FE(cubrid_fetch_row, arginfo_cubrid_fetch_row)
    ZEND_FE(cubrid_fetch_field, arginfo_cubrid_fetch_field)
    ZEND_FE(cubrid_num_fields, arginfo_cubrid_num_fields)
    ZEND_FE(cubrid_free_result, arginfo_cubrid_free_result)
    ZEND_FE(cubrid_fetch_lengths, arginfo_cubrid_fetch_lengths)
    ZEND_FE(cubrid_fetch_object, arginfo_cubrid_fetch_object)
    ZEND_FE(cubrid_field_seek, arginfo_cubrid_field_seek)
    ZEND_FE(cubrid_field_len, arginfo_cubrid_field_len)
    ZEND_FE(cubrid_result, arginfo_cubrid_result)
    ZEND_FE(cubrid_get_charset, arginfo_cubrid_get_charset)
    ZEND_FE(cubrid_client_encoding, arginfo_cubrid_client_encoding)
    ZEND_FE(cubrid_unbuffered_query, arginfo_cubrid_unbuffered_query)
    ZEND_FE(cubrid_query, arginfo_cubrid_query)
    ZEND_FE(cubrid_get_client_info, arginfo_cubrid_get_client_info)
    ZEND_FE(cubrid_get_server_info, arginfo_cubrid_get_server_info)
    ZEND_FE(cubrid_real_escape_string, arginfo_cubrid_real_escape_string)
    ZEND_FE(cubrid_get_db_parameter, arginfo_cubrid_get_db_parameter)
    ZEND_FE(cubrid_set_db_parameter, arginfo_cubrid_set_db_parameter)
    ZEND_FE(cubrid_list_dbs, arginfo_cubrid_list_dbs)
    ZEND_FE(cubrid_db_name, arginfo_cubrid_db_name)
    ZEND_FE(cubrid_insert_id, arginfo_cubrid_lnsert_id)
    ZEND_FE(cubrid_ping, arginfo_cubrid_ping)
    ZEND_FE(cubrid_lob_get, arginfo_cubrid_lob_get)
    ZEND_FE(cubrid_lob_size, arginfo_cubrid_lob_size)
    ZEND_FE(cubrid_lob_export, arginfo_cubrid_lob_export)
    ZEND_FE(cubrid_lob_send, arginfo_cubrid_lob_send)
    ZEND_FE(cubrid_lob_close, arginfo_cubrid_lob_close)

    ZEND_FE(cubrid_get_query_timeout, arginfo_cubrid_get_query_timeout)
    ZEND_FE(cubrid_set_query_timeout, arginfo_cubrid_set_query_timeout)

    ZEND_FALIAS(cubrid_close_prepare, cubrid_close_request, NULL) 
    ZEND_FALIAS(cubrid_disconnect, cubrid_close, NULL) 
    {NULL, NULL, NULL}
};

zend_module_entry cubrid_module_entry = {
    STANDARD_MODULE_HEADER,
    "CUBRID",
    cubrid_functions,
    ZEND_MINIT(cubrid),
    ZEND_MSHUTDOWN(cubrid),
    ZEND_RINIT(cubrid),
    ZEND_RSHUTDOWN(cubrid),
    ZEND_MINFO(cubrid),
    NO_VERSION_YET,
    STANDARD_MODULE_PROPERTIES
};

ZEND_DECLARE_MODULE_GLOBALS(cubrid)

/************************************************************************
* CUBRID PHP.INI SETTINGS
************************************************************************/

ZEND_INI_BEGIN()
/* maybe add settings later */
ZEND_INI_END()

/************************************************************************
* PRIVATE VARIABLES
************************************************************************/

/* resource type */
static int le_pconnect, le_connect, le_request, le_lob;

/************************************************************************
* IMPLEMENTATION OF CALLBACK FUNCTION (EXPORT/INIT/SHUTDOWN/INFO)
************************************************************************/

#if defined(COMPILE_DL_CUBRID)
ZEND_GET_MODULE(cubrid)
#endif

ZEND_MINIT_FUNCTION(cubrid)
{
    REGISTER_INI_ENTRIES();

    cci_init();

    ZEND_INIT_MODULE_GLOBALS(cubrid, php_cubrid_init_globals, NULL);

    le_pconnect = zend_register_list_destructors_ex(NULL, close_cubrid_pconnect, "CUBRID Connect Persistent", module_number);
    le_connect = zend_register_list_destructors_ex(close_cubrid_connect, NULL, "CUBRID Connect", module_number);
    le_request = zend_register_list_destructors_ex(close_cubrid_request, NULL, "CUBRID Request", module_number);
    le_lob = zend_register_list_destructors_ex(close_cubrid_lob, NULL, "CUBRID Lob", module_number);

    Z_TYPE(cubrid_module_entry) = type;

    init_error();

    REGISTER_LONG_CONSTANT("CUBRID_INCLUDE_OID", CUBRID_INCLUDE_OID, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_ASYNC", CUBRID_ASYNC, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_EXEC_QUERY_ALL", CUBRID_EXEC_QUERY_ALL, CONST_CS | CONST_PERSISTENT);

    REGISTER_LONG_CONSTANT("CUBRID_NUM", CUBRID_NUM, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_ASSOC", CUBRID_ASSOC, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_BOTH", CUBRID_BOTH, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_OBJECT", CUBRID_OBJECT, CONST_CS | CONST_PERSISTENT);

    REGISTER_LONG_CONSTANT("CUBRID_CURSOR_FIRST", CUBRID_CURSOR_FIRST, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_CURSOR_CURRENT", CUBRID_CURSOR_CURRENT, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_CURSOR_LAST", CUBRID_CURSOR_LAST, CONST_CS | CONST_PERSISTENT);

    REGISTER_LONG_CONSTANT("CUBRID_PARAM_ISOLATION_LEVEL", CUBRID_PARAM_ISOLATION_LEVEL, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_PARAM_LOCK_TIMEOUT", CUBRID_PARAM_LOCK_TIMEOUT, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_AUTOCOMMIT_FALSE", CUBRID_AUTOCOMMIT_FALSE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_AUTOCOMMIT_TRUE", CUBRID_AUTOCOMMIT_TRUE, CONST_CS | CONST_PERSISTENT);

    REGISTER_LONG_CONSTANT("TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE", TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TRAN_COMMIT_CLASS_COMMIT_INSTANCE", TRAN_COMMIT_CLASS_COMMIT_INSTANCE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TRAN_REP_CLASS_UNCOMMIT_INSTANCE", TRAN_REP_CLASS_UNCOMMIT_INSTANCE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TRAN_REP_CLASS_COMMIT_INSTANCE", TRAN_REP_CLASS_COMMIT_INSTANCE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TRAN_REP_CLASS_REP_INSTANCE", TRAN_REP_CLASS_REP_INSTANCE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("TRAN_SERIALIZABLE", TRAN_SERIALIZABLE, CONST_CS | CONST_PERSISTENT);

    REGISTER_LONG_CONSTANT("CUBRID_SCH_CLASS", CUBRID_SCH_CLASS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_VCLASS", CUBRID_SCH_VCLASS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_QUERY_SPEC", CUBRID_SCH_QUERY_SPEC, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_ATTRIBUTE", CUBRID_SCH_ATTRIBUTE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_CLASS_ATTRIBUTE", CUBRID_SCH_CLASS_ATTRIBUTE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_METHOD", CUBRID_SCH_METHOD, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_CLASS_METHOD", CUBRID_SCH_CLASS_METHOD, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_METHOD_FILE", CUBRID_SCH_METHOD_FILE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_SUPERCLASS", CUBRID_SCH_SUPERCLASS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_SUBCLASS", CUBRID_SCH_SUBCLASS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_CONSTRAINT", CUBRID_SCH_CONSTRAINT, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_TRIGGER", CUBRID_SCH_TRIGGER, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_CLASS_PRIVILEGE", CUBRID_SCH_CLASS_PRIVILEGE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_ATTR_PRIVILEGE", CUBRID_SCH_ATTR_PRIVILEGE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_DIRECT_SUPER_CLASS", CUBRID_SCH_DIRECT_SUPER_CLASS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_PRIMARY_KEY", CUBRID_SCH_PRIMARY_KEY, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_IMPORTED_KEYS", CUBRID_SCH_IMPORTED_KEYS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_EXPORTED_KEYS", CUBRID_SCH_EXPORTED_KEYS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_SCH_CROSS_REFERENCE", CUBRID_SCH_CROSS_REFERENCE, CONST_CS | CONST_PERSISTENT);

    REGISTER_LONG_CONSTANT("CUBRID_FACILITY_DBMS", CUBRID_FACILITY_DBMS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_FACILITY_CAS", CUBRID_FACILITY_CAS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_FACILITY_CCI", CUBRID_FACILITY_CCI, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("CUBRID_FACILITY_CLIENT", CUBRID_FACILITY_CLIENT, CONST_CS | CONST_PERSISTENT);

    return SUCCESS;
}

ZEND_MSHUTDOWN_FUNCTION(cubrid)
{
    UNREGISTER_INI_ENTRIES();
    cci_end();

    return SUCCESS;
}

ZEND_RINIT_FUNCTION(cubrid)
{
	CUBRID_G(last_connect_id) = -1;
	CUBRID_G(last_request_id) = -1;

	CUBRID_G(recent_error).code = 0;
	CUBRID_G(recent_error).facility = 0;
	CUBRID_G(recent_error).msg[0] = 0;

	return SUCCESS;
}

static int php_cubrid_persistent_helper(zend_rsrc_list_entry *le TSRMLS_DC)
{
    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval;
    LINKED_LIST_NODE *head, *p;

    if (Z_TYPE_P(le) != le_pconnect) {
        return 0;
    }

    connect = (T_CUBRID_CONNECT *)le->ptr;

    head = connect->unclosed_requests->head;

    for (p = head->next; p != NULL; p = head->next) {
        T_CUBRID_REQUEST *req = (T_CUBRID_REQUEST *)p->data;
        req->conn = NULL;
        req->handle = 0;
        p->data = NULL;

        head->next = p->next;
        efree(p);
    }

    connect->unclosed_requests->tail = head;

    if ((cubrid_retval = cci_end_tran (connect->handle, CCI_TRAN_ROLLBACK, &error)) < 0) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Cannot rollback transaction when shutdown request");
        return -1;
    }

    return 0;
}

ZEND_RSHUTDOWN_FUNCTION(cubrid)
{
    zend_hash_apply(&EG(persistent_list), (apply_func_t)php_cubrid_persistent_helper TSRMLS_CC);
    return SUCCESS;
}

ZEND_MINFO_FUNCTION(cubrid)
{
    int major, minor, patch;
    char info[128];

    cci_get_version(&major, &minor, &patch);

    snprintf(info, sizeof(info), "%d.%d.%d", major, minor, patch);
    
    php_info_print_table_start();
    php_info_print_table_header(2, "CUBRID support", "enabled");
    php_info_print_table_row(2, "Version", PHP_CUBRID_VERSION);
    php_info_print_table_row(2, "CCI Version", info);
    php_info_print_table_row(2, "CUBRID Version", "8.4.1");
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
}

/************************************************************************
* IMPLEMENTATION OF CUBRID API
************************************************************************/

ZEND_FUNCTION(cubrid_version)
{
    if (zend_parse_parameters_none() == FAILURE) {
	return;
    }

    RETURN_STRINGL(PHP_CUBRID_VERSION, strlen(PHP_CUBRID_VERSION), 1);
}

static int check_connect_alive(T_CUBRID_CONNECT *connect)
{
    int req_handle = 0;

    T_CCI_ERROR error;
    int cubrid_retval = 0, connected = 0;
    int result = 0, ind = 0;
    char *query = "SELECT 1 FROM db_root";

    init_error();
    init_error_link(connect);

    if ((cubrid_retval = cci_prepare(connect->handle, query, 0, &error)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        return 0;
    }

    req_handle = cubrid_retval;

    if ((cubrid_retval = cci_execute(req_handle, 0, 0, &error)) < 0) {
        goto HANDLE_ERROR;
    }

    cci_close_req_handle(req_handle);

    return 1;

HANDLE_ERROR:
    cci_close_req_handle(req_handle);
    return 0;
}

static void php_cubrid_do_connect(INTERNAL_FUNCTION_PARAMETERS, int persistent)
{
    char *host = NULL, *dbname = NULL, *userid = NULL, *passwd = NULL;
    long port = 0;
    int host_len, dbname_len, userid_len, passwd_len;

    int cubrid_conn, cubrid_retval = 0;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;

    char hashed_details[1024] = {'\0'};
    int hashed_details_length;

    zend_bool new_link = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sls|ssb", 
		&host, &host_len, &port, &dbname, &dbname_len, 
		&userid, &userid_len, &passwd, &passwd_len, &new_link) == FAILURE) {
	return;
    }

    if (!userid) {
	userid = CUBRID_G(default_userid);
    }

    if (!passwd) {
	passwd = CUBRID_G(default_passwd);
    }

    snprintf(hashed_details, sizeof(hashed_details), "CUBRID:%s:%d:%s:%s:%s", host, (int)port, dbname, userid, passwd);
    hashed_details_length = strlen(hashed_details);

    if (persistent) {
        zend_rsrc_list_entry *le;

        /* try to find if we already have this link in our persistent list */
        if (zend_hash_find(&EG(persistent_list), hashed_details, hashed_details_length+1, (void **) &le) == FAILURE) { /* we don't */
            zend_rsrc_list_entry new_le;
    
            if ((cubrid_conn = cci_connect(host, port, dbname, userid, passwd)) < 0) {
                handle_error(cubrid_conn, NULL, NULL);
                RETURN_FALSE;
            }
    
            if ((cubrid_retval = cci_end_tran(cubrid_conn, CCI_TRAN_COMMIT, &error)) < 0) {
                handle_error(cubrid_retval, &error, NULL);
                RETURN_FALSE;
            }
    
            connect = new_cubrid_connect(1);
            connect->handle = cubrid_conn;
    
            Z_TYPE(new_le) = le_pconnect;
            new_le.ptr = connect;
            if (zend_hash_update(&EG(persistent_list), hashed_details, hashed_details_length + 1, (void *) &new_le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
                free(connect);
                RETURN_FALSE;
            }
        } else { /* The link is in our list of persistent connections */
    
            if (Z_TYPE_P(le) != le_pconnect) {
                RETURN_FALSE;
            }
    
            connect = (T_CUBRID_CONNECT *) le->ptr;
    
            if (!check_connect_alive(connect)) {
                if ((cubrid_conn = cci_connect(host, port, dbname, userid, passwd)) < 0) {
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, "Link to server lost, unable to reconnect");
                    zend_hash_del(&EG(persistent_list), hashed_details, hashed_details_length+1);
                    RETURN_FALSE;
                }
    
                connect->handle = cubrid_conn;
            }
    
            if ((cubrid_retval = cci_end_tran(connect->handle, CCI_TRAN_COMMIT, &error)) < 0) {
                handle_error(cubrid_retval, &error, NULL);
                free(connect);
                RETURN_FALSE;
            }
        }
    
        CUBRID_G(last_request_id) = -1;
        
        ZEND_REGISTER_RESOURCE(return_value, connect, le_pconnect);
   
    } else { /* non persistent */

        zend_rsrc_list_entry *index_ptr, new_index_ptr;
    
        if (!new_link && zend_hash_find(&EG(regular_list), hashed_details, hashed_details_length+1, (void **) &index_ptr) == SUCCESS) {
            int type;
            long link;
            void *ptr;
    
            if (Z_TYPE_P(index_ptr) != le_index_ptr) {
                RETURN_FALSE;
            }
    
            link = (long) index_ptr->ptr;
            ptr = zend_list_find(link, &type);
            if (ptr && (type == le_connect || type == le_pconnect)) {
                zend_list_addref(link);
                Z_LVAL_P(return_value) = link;
                php_cubrid_set_default_conn(link TSRMLS_CC);
                Z_TYPE_P(return_value) = IS_RESOURCE;
                return;
            } else {
                zend_hash_del(&EG(regular_list), hashed_details, hashed_details_length+1);
            }
        }
    
        if ((cubrid_conn = cci_connect(host, port, dbname, userid, passwd)) < 0) {
    	handle_error(cubrid_conn, NULL, NULL);
    	RETURN_FALSE;
        }
    
        CUBRID_G(last_request_id) = -1;
    
        if ((cubrid_retval = cci_end_tran(cubrid_conn, CCI_TRAN_COMMIT, &error)) < 0) {
    	handle_error(cubrid_retval, &error, NULL);
    	cci_disconnect(cubrid_conn, &error);
    	RETURN_FALSE;
        }
    
        connect = new_cubrid_connect(0);
        connect->handle = cubrid_conn;
        
        ZEND_REGISTER_RESOURCE(return_value, connect, le_connect);
    
        new_index_ptr.ptr = (void *) Z_LVAL_P(return_value);
        Z_TYPE(new_index_ptr) = le_index_ptr;
        if (zend_hash_update(&EG(regular_list), hashed_details, hashed_details_length+1, (void *)&new_index_ptr, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
            RETURN_FALSE;
        }
    }

    php_cubrid_set_default_conn(Z_LVAL_P(return_value) TSRMLS_CC);
}

ZEND_FUNCTION(cubrid_pconnect)
{
    php_cubrid_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}

ZEND_FUNCTION(cubrid_connect)
{
    php_cubrid_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

static void php_cubrid_do_connect_with_url(INTERNAL_FUNCTION_PARAMETERS, int persistent)
{
    char *url = NULL, *userid = NULL, *passwd = NULL;
    int url_len, userid_len, passwd_len;
    char buf[4096] = { '\0' };
    char hashed_details[4096] = { '\0' };
    int hashed_details_length;

    int cubrid_conn, cubrid_retval = 0;

    T_CUBRID_CONNECT *connect = NULL;
    T_CCI_ERROR error;

    zend_bool new_link = 0;

    init_error();

    if (zend_parse_parameters (ZEND_NUM_ARGS() TSRMLS_CC, "s|ssb", 
		&url, &url_len, &userid, &userid_len, &passwd, &passwd_len, &new_link) == FAILURE) {
	return;
    }

    if (strncasecmp (url, "cci:", 4) != 0) {
        snprintf (buf, sizeof(buf), "cci:%s", url);
    }
    else {
        strncpy (buf, url, sizeof(buf));
    }

    if (!userid) {
	userid = CUBRID_G(default_userid);
    }

    if (!passwd) {
	passwd = CUBRID_G(default_passwd);
    }

    snprintf(hashed_details, sizeof(hashed_details), "%s:%s:%s", buf, userid, passwd);
    hashed_details_length = strlen(hashed_details);

    if (persistent) {
        zend_rsrc_list_entry *le;

        /* try to find if we already have this link in our persistent list */
        if (zend_hash_find(&EG(persistent_list), hashed_details, hashed_details_length + 1, (void **) &le) == FAILURE) { /* we don't */
            zend_rsrc_list_entry new_le;
    
            if ((cubrid_conn = cci_connect_with_url(buf, userid, passwd)) < 0) {
                handle_error(cubrid_conn, NULL, NULL);
                RETURN_FALSE;
            }
    
            if ((cubrid_retval = cci_end_tran(cubrid_conn, CCI_TRAN_COMMIT, &error)) < 0) {
                handle_error(cubrid_retval, &error, NULL);
                cci_disconnect(cubrid_conn, &error);
                RETURN_FALSE;
            }
    
            connect = new_cubrid_connect(1);
            connect->handle = cubrid_conn;
    
            Z_TYPE(new_le) = le_pconnect;
            new_le.ptr = connect;
            if (zend_hash_update(&EG(persistent_list), hashed_details, hashed_details_length + 1, (void *) &new_le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
                free(connect);
                RETURN_FALSE;
            }
    
        } else {
    
            if (Z_TYPE_P(le) != le_pconnect) {
                RETURN_FALSE;
            }
    
            connect = (T_CUBRID_CONNECT *) le->ptr;
    
            if (!check_connect_alive(connect)) {
                if ((cubrid_conn = cci_connect_with_url(buf, userid, passwd)) < 0) {
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, "Link to server lost, unable to reconnect");
                    zend_hash_del(&EG(persistent_list), hashed_details, hashed_details_length+1);
                    RETURN_FALSE;
                }
    
                connect->handle = cubrid_conn;
            }
    
            if ((cubrid_retval = cci_end_tran(connect->handle, CCI_TRAN_COMMIT, &error)) < 0) {
                handle_error(cubrid_retval, &error, NULL);
                free(connect);
                RETURN_FALSE;
            }
        }
    
        CUBRID_G(last_request_id) = -1;
    
        ZEND_REGISTER_RESOURCE(return_value, connect, le_pconnect);

    } else { /* non persistent */

        zend_rsrc_list_entry *index_ptr, new_index_ptr;
    
        if (!new_link && zend_hash_find(&EG(regular_list), hashed_details, hashed_details_length + 1, (void **) &index_ptr) == SUCCESS) {
            int type;
            long link;
            void *ptr;
    
            if (Z_TYPE_P(index_ptr) != le_index_ptr) {
                RETURN_FALSE;
            }
    
            link = (long) index_ptr->ptr;
            ptr = zend_list_find(link, &type);
            if (ptr && (type == le_connect || type == le_pconnect)) {
                zend_list_addref(link);
                Z_LVAL_P(return_value) = link;
                php_cubrid_set_default_conn(link TSRMLS_CC);
                Z_TYPE_P(return_value) = IS_RESOURCE;
                return;
            } else {
                zend_hash_del(&EG(regular_list), hashed_details, hashed_details_length+1);
            }
        }
    
        if ((cubrid_conn = cci_connect_with_url(buf, userid, passwd)) < 0) {
    	handle_error(cubrid_conn, NULL, NULL);
    	RETURN_FALSE;
        }
    
        CUBRID_G(last_request_id) = -1;
    
        if ((cubrid_retval = cci_end_tran(cubrid_conn, CCI_TRAN_COMMIT, &error)) < 0) {
    	handle_error(cubrid_retval, &error, NULL);
    	cci_disconnect(cubrid_conn, &error);
    	RETURN_FALSE;
        }
    
        connect = new_cubrid_connect(0);
        connect->handle = cubrid_conn;
    
        ZEND_REGISTER_RESOURCE(return_value, connect, le_connect);
    
        new_index_ptr.ptr = (void *)Z_LVAL_P(return_value);
        Z_TYPE(new_index_ptr) = le_index_ptr;
        if (zend_hash_update(&EG(regular_list), hashed_details, hashed_details_length + 1, (void *)&new_index_ptr, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
            RETURN_FALSE;
        }
    }

    php_cubrid_set_default_conn(Z_LVAL_P(return_value) TSRMLS_CC);
}

ZEND_FUNCTION(cubrid_pconnect_with_url)
{
    php_cubrid_do_connect_with_url(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}

ZEND_FUNCTION(cubrid_connect_with_url)
{
    php_cubrid_do_connect_with_url(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}

ZEND_FUNCTION(cubrid_close)
{
    zval *conn_id = NULL;
    T_CUBRID_CONNECT *connect = NULL;
    
    int res_id = -1;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }

    if (conn_id) {
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
        if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE;
        }

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    res_id = conn_id ? Z_RESVAL_P(conn_id) : CUBRID_G(last_connect_id);
    zend_list_delete(res_id);

    /* On an explicit close of the default connection it had a refcount of 2,
     * so we need one more call */
    if (!conn_id || (conn_id && Z_RESVAL_P(conn_id) == CUBRID_G(last_connect_id))) {
        CUBRID_G(last_connect_id) = -1;
        CUBRID_G(last_request_id) = -1;

        if (conn_id) {
            zend_list_delete(res_id);
        }
    }
    
    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_prepare)
{
    zval *conn_id = NULL;
    char *query = NULL;
    long option = 0;
    int query_len;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_REQUEST *request = NULL;
    T_CCI_ERROR error;

    int cubrid_retval = 0, request_handle = -1;
    int i;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs|l", &conn_id, &query, &query_len, &option) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    request = new_cubrid_request();
    request->conn = connect;

    if ((cubrid_retval = cci_prepare(connect->handle, query, 
		    (char) ((option & CUBRID_INCLUDE_OID) ? CCI_PREPARE_INCLUDE_OID : 0), &error)) < 0) {
        close_cubrid_request_internal(request);
        handle_error(cubrid_retval, &error, connect);
        RETURN_FALSE;
    }

    request_handle = cubrid_retval;

    request->handle = request_handle;
    request->bind_num = cci_get_bind_num(request_handle);

    if (request->bind_num > 0) {
        request->l_bind = (short *) safe_emalloc(request->bind_num, sizeof(short), 0);
        for (i = 0; i < request->bind_num; i++) {
            request->l_bind[i] = 0;
        }
    }

    request->l_prepare = 1;
    request->fetch_field_auto_index = 0;

    ZEND_REGISTER_RESOURCE(return_value, request, le_request);
    php_cubrid_set_default_req(Z_LVAL_P(return_value) TSRMLS_CC);
    register_cubrid_request(connect, request);
}

ZEND_FUNCTION(cubrid_bind)
{
    zval *req_id= NULL, *bind_value = NULL;
    char *bind_value_type = NULL;
    long bind_index = -1;
    int bind_value_type_len;

    T_CUBRID_REQUEST *request = NULL;
    T_CCI_ERROR error;

    T_CCI_U_TYPE u_type = -1;
    T_CCI_A_TYPE a_type = -1;

    char *value = NULL;
    long value_len = 0;

    T_CCI_BIT *bit_value = NULL;
    T_CCI_LOB lob = NULL;

    php_stream *stm = NULL;
    char* lobfile_name = NULL;
    char buf[CUBRID_LOB_READ_BUF_SIZE];
    php_cubrid_int64_t lob_start_pos = 0;
    size_t lob_read_size = 0;

    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rlz|s", 
                &req_id, &bind_index, &bind_value, &bind_value_type, &bind_value_type_len) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (bind_index < 1 || bind_index > request->bind_num) {
	RETURN_FALSE;
    }

    if (!bind_value_type) {
	u_type = CCI_U_TYPE_STRING;
    } else {
	u_type = get_cubrid_u_type_by_name(bind_value_type);
	/* collection type should be made by cci_set_make before calling cci_bind_param */
	if (u_type == CCI_U_TYPE_UNKNOWN || u_type == CCI_U_TYPE_SET || 
	    u_type == CCI_U_TYPE_MULTISET || u_type == CCI_U_TYPE_SEQUENCE) {
	    php_error_docref(NULL TSRMLS_CC, E_WARNING, "Bind value type unknown : %s\n", bind_value_type);
	    RETURN_FALSE;
	}
    }

    if (u_type == CCI_U_TYPE_NULL || Z_TYPE_P(bind_value) == IS_NULL) {
        cubrid_retval = cci_bind_param(request->handle, bind_index, CCI_A_TYPE_STR, NULL, u_type, 0);
    } else {
        if (u_type == CCI_U_TYPE_BLOB || u_type == CCI_U_TYPE_CLOB) {
            if (Z_TYPE_P(bind_value) == IS_RESOURCE) {
                php_stream_from_zval_no_verify(stm, &bind_value);
                if (!stm) {
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, "Expected a stream resource when param type is LOB\n");
                    RETURN_FALSE;
                }
            } else {
                /* file name */
                convert_to_string(bind_value);
                lobfile_name = Z_STRVAL_P(bind_value);

                if (!(stm = php_stream_open_wrapper(lobfile_name, "r", REPORT_ERRORS, NULL))) {
                    RETURN_FALSE;
                }
            }
        } else {
            convert_to_string(bind_value);
        }

        value = Z_STRVAL_P(bind_value);
        value_len = Z_STRLEN_P(bind_value);

        switch (u_type) {
        case CCI_U_TYPE_BLOB:
                a_type = CCI_A_TYPE_BLOB;
                break;
        case CCI_U_TYPE_CLOB:
                a_type = CCI_A_TYPE_CLOB;
                break;
        case CCI_U_TYPE_BIT:
        case CCI_U_TYPE_VARBIT:
                a_type = CCI_A_TYPE_BIT;
                break;
        default:
                a_type = CCI_A_TYPE_STR;
                break;
        }
        
        if (u_type == CCI_U_TYPE_BLOB || u_type == CCI_U_TYPE_CLOB) {
            if ((cubrid_retval = cubrid_lob_new(request->conn->handle, &lob, u_type, &error)) < 0) {
                handle_error(cubrid_retval, &error, request->conn);

                if (lobfile_name) {
                    php_stream_close(stm);
                }

                RETURN_FALSE; 
            }

            while (!php_stream_eof(stm)) { 
                lob_read_size = php_stream_read(stm, buf, CUBRID_LOB_READ_BUF_SIZE); 

                if ((cubrid_retval = cubrid_lob_write(request->conn->handle, lob, u_type, 
                                lob_start_pos, lob_read_size, buf, &error)) < 0) {
                    handle_error(cubrid_retval, &error, request->conn);
                    php_stream_close(stm);

                    RETURN_FALSE; 
                }        

                lob_start_pos += lob_read_size;
            }
            
            if (lobfile_name) {
                php_stream_close(stm);
            }

            cubrid_retval = cci_bind_param(request->handle, bind_index, a_type, (void *) lob, u_type, CCI_BIND_PTR); 

            request->lob = new_cubrid_lob();
            request->lob->lob = lob;
            request->lob->type = u_type;
        } else if (u_type == CCI_U_TYPE_BIT) {
            bit_value = (T_CCI_BIT *) emalloc(sizeof(T_CCI_BIT));
            bit_value->size = value_len;
            bit_value->buf = value;

            cubrid_retval = cci_bind_param(request->handle, bind_index, a_type, (void *) bit_value, u_type, 0);

            efree(bit_value);
        } else {
            cubrid_retval = cci_bind_param(request->handle, bind_index, a_type, value, u_type, 0);
        }
    }

    if (cubrid_retval != 0 || !request->l_bind) {
	handle_error(cubrid_retval, NULL, request->conn);
	RETURN_FALSE;
    }

    request->l_bind[bind_index - 1] = 1;

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_execute)
{
    zval *id = NULL, *param = NULL;
    char *sql_stmt = NULL;
    long option = 0;
    int sql_stmt_len;

    char exec_flag = 0;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_REQUEST *request = NULL;
    T_CCI_ERROR error;
    int exec_retval = 0;

    T_CCI_COL_INFO *res_col_info;
    T_CCI_CUBRID_STMT res_sql_type;
    int res_col_count = 0;

    int cubrid_retval = 0;
    int req_handle = 0;
    int l_prepare = 0;
    int i;
    int is_prepare_and_execute_mode = 0;

    init_error();

    switch (ZEND_NUM_ARGS()) {
    case 1:
	/* It must be req_id */
	if (zend_parse_parameters(1 TSRMLS_CC, "r", &id) == FAILURE) {
	    return;
	}

	ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &id, -1, "CUBRID-Request", le_request);

	break;
    case 2:
	/* Param may be conn_id + sql_stmt or req_id + option */
	if (zend_parse_parameters(2 TSRMLS_CC, "rz", &id, &param) == FAILURE) {
	    return;
	}

	switch (Z_TYPE_P(param)) {
	case IS_STRING:
	    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &id, -1, "CUBRID-Connect", le_connect, le_pconnect);
	    sql_stmt = Z_STRVAL_P(param);	
	    sql_stmt_len = Z_STRLEN_P(param);
            is_prepare_and_execute_mode = 1;

	    break;
	case IS_LONG:
	    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &id, -1, "CUBRID-Request", le_request);
	    option = Z_LVAL_P(param);	

	    break;
	default:
	    RETURN_FALSE;
	}

	break;
    case 3:
	/* It must be conn_id + sql_stmt + option */
	if (zend_parse_parameters(3 TSRMLS_CC, "rsl", 
		    &id, &sql_stmt, &sql_stmt_len, &option) == FAILURE) {
	    return;
	}

	ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &id, -1, "CUBRID-Connect", le_connect, le_pconnect);

	break;
    default:
        WRONG_PARAM_COUNT;
    }

    if (connect == NULL && request != NULL) {
        connect = request->conn;
	req_handle = request->handle;

        init_error_link(connect);

	if (!request->l_prepare) {
            handle_error(CCI_ER_REQ_HANDLE, NULL, connect);
            RETURN_FALSE;
	}

	l_prepare = request->l_prepare;

        if (request->bind_num > 0) {
            for (i = 0; i < request->bind_num; i++) {
                if (!request->l_bind[i]) {
                    handle_error(CUBRID_ER_PARAM_UNBIND, NULL, connect);
                    RETURN_FALSE;
                }
            }
        }
    } else if (connect != NULL && request == NULL) {
        init_error_link(connect);

        request = new_cubrid_request();
        request->conn = connect;

        if (!is_prepare_and_execute_mode) {
            if ((cubrid_retval = cci_prepare(connect->handle, sql_stmt,
                    (char)((option & CUBRID_INCLUDE_OID) ? CCI_PREPARE_INCLUDE_OID : 0), &error)) < 0) {
                goto ERR_CUBRID_EXECUTE;
            }
        } else {
            if ((cubrid_retval =
                        cci_prepare_and_execute(connect->handle, sql_stmt, 0,
                            &exec_retval, &error)) < 0) {
                goto ERR_CUBRID_EXECUTE;
            }
        }

        req_handle = cubrid_retval;
        request->handle = req_handle;
        request->l_prepare = 1;
        request->fetch_field_auto_index = 0;

        if ((request->bind_num = cci_get_bind_num(req_handle)) > 0) {
            request->l_bind = (short *) safe_emalloc(request->bind_num, sizeof(short), 0);
            for (i = 0; i < request->bind_num; i++) {
                request->l_bind[i] = 0;
            }
        }
    }

    if (!is_prepare_and_execute_mode) {
        if (option & CUBRID_EXEC_QUERY_ALL) {
            exec_flag |= CCI_EXEC_QUERY_ALL;
        } else if (option & CUBRID_ASYNC) {
           exec_flag |= CCI_EXEC_ASYNC;
        }
    
        if ((exec_retval = cci_execute(req_handle, exec_flag, 0, &error)) < 0) {
            goto ERR_CUBRID_EXECUTE;
        }
    }
    
    res_col_info = cci_get_result_info(req_handle, &res_sql_type, &res_col_count);
    if (res_sql_type == CUBRID_STMT_SELECT && !res_col_info) {
        cubrid_retval = CUBRID_ER_CANNOT_GET_COLUMN_INFO;
        goto ERR_CUBRID_EXECUTE;
    }

    if (request->lob) {
        close_cubrid_lob_internal(request->lob);
        request->lob = NULL;
    }

    request->col_info = res_col_info;
    request->sql_type = res_sql_type;
    request->col_count = res_col_count;

    connect->sql_type = request->sql_type;

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
        request->row_count = exec_retval;
        if (cubrid_retval > 0 && request->field_lengths == NULL) {
            request->field_lengths = (int *)emalloc(sizeof(int) * res_col_count);
        }

        cubrid_retval = cci_cursor(req_handle, 1, CCI_CURSOR_CURRENT, &error);
        if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
            goto ERR_CUBRID_EXECUTE;
        } 

        break;
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
        connect->affected_rows = exec_retval;
        break;
    case CUBRID_STMT_CALL:
        request->row_count = exec_retval;
        connect->affected_rows = exec_retval;
    default:
        break;
    }

    if (!l_prepare) {
        ZEND_REGISTER_RESOURCE(return_value, request, le_request);
        php_cubrid_set_default_req(Z_LVAL_P(return_value) TSRMLS_CC);
        register_cubrid_request(connect, request);
        return;
    }

    RETURN_TRUE;

ERR_CUBRID_EXECUTE:
    if (!l_prepare) {
        close_cubrid_request_internal(request);
    }
    handle_error(cubrid_retval, &error, connect);
    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_next_result)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request;

    T_CCI_COL_INFO *res_col_info;
    T_CCI_CUBRID_STMT res_sql_type;
    int res_col_count = 0;

    int cubrid_retval = 0;
    T_CCI_ERROR error;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);
   
    cubrid_retval = cci_next_result(request->handle, &error);
    if (cubrid_retval == CAS_ER_NO_MORE_RESULT_SET) {
        RETURN_FALSE;
    }

    if (cubrid_retval < 0) {
        handle_error(cubrid_retval, &error, request->conn);
        RETURN_FALSE;
    }

    if (request->field_lengths != NULL) {
        efree(request->field_lengths);
        request->field_lengths = NULL;
    }

    res_col_info = cci_get_result_info(request->handle, &res_sql_type, &res_col_count);
    if (res_sql_type == CUBRID_STMT_SELECT && !res_col_info) {
        RETURN_FALSE;
    }

    request->col_info = res_col_info;
    request->sql_type = res_sql_type;
    request->col_count = res_col_count;

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
        request->row_count = cubrid_retval;
        request->field_lengths = (int *) emalloc (sizeof(int) * res_col_count); 

        break;
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
        request->conn->affected_rows = cubrid_retval;
        break;
    case CUBRID_STMT_CALL:
        request->row_count = cubrid_retval;
    default:
        break;
    }

    cubrid_retval = cci_cursor(request->handle, 1, CCI_CURSOR_CURRENT, &error);
    if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
        handle_error(cubrid_retval, &error, request->conn);
        RETURN_FALSE;
    }

    request->fetch_field_auto_index = 0;

    request->conn->sql_type = request->sql_type;

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_affected_rows)
{
    zval *conn_id = NULL;
    T_CUBRID_CONNECT *connect= NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }

    if (conn_id) {
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1)
	{
	  RETURN_FALSE;
	}
        
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    switch (connect->sql_type) {
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
        RETURN_LONG(connect->affected_rows);
    default:
        handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, connect);
        RETURN_LONG(-1);
    }
}

ZEND_FUNCTION(cubrid_close_request)
{
    zval *req_id= NULL;
    T_CUBRID_REQUEST *request;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);
    zend_list_delete(Z_RESVAL_P(req_id));

    /* On an explicit close of the default request it had a refcount of 2,
     * so we need one more call */
    if (Z_RESVAL_P(req_id) == CUBRID_G(last_request_id)) {
        CUBRID_G(last_request_id) = -1;

        zend_list_delete(Z_RESVAL_P(req_id));
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_current_oid)
{
    zval *req_id= NULL;

    T_CUBRID_REQUEST *request;
    T_CCI_ERROR error;

    char oid_buf[1024];
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (request->sql_type != CUBRID_STMT_SELECT) {
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_FALSE;
    }

    if ((cubrid_retval = cci_fetch(request->handle, &error)) < 0) {
        handle_error(cubrid_retval, &error, request->conn);
        RETURN_FALSE;
    }

    if ((cubrid_retval = cci_get_cur_oid(request->handle, oid_buf)) < 0) {
        handle_error(cubrid_retval, NULL, request->conn);
        RETURN_FALSE;
    }

    RETURN_STRING(oid_buf, 1);
}

ZEND_FUNCTION(cubrid_column_types)
{
    zval *req_id= NULL;
    T_CUBRID_REQUEST *request;

    char full_type_name[128];
    int i;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);
    
    array_init(return_value);

    for (i = 0; i < request->col_count; i++) {
	if (type2str(&request->col_info[i], full_type_name, sizeof(full_type_name)) < 0) {
	    handle_error(CCI_ER_TYPE_CONVERSION, NULL, request->conn);
            cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);
	    RETURN_FALSE;
	}

	add_index_stringl(return_value, i, full_type_name, strlen(full_type_name), 1);
    }

    return;
}

ZEND_FUNCTION(cubrid_column_names)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request;
    char *column_name;

    int i;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);
    
    array_init(return_value);

    for (i = 0; i < request->col_count; i++) {
	column_name = CCI_GET_RESULT_INFO_NAME(request->col_info, i + 1);
	add_index_stringl(return_value, i, column_name, strlen(column_name), 1);
    }

    return;
}

ZEND_FUNCTION(cubrid_move_cursor)
{
    zval *req_id = NULL;
    long offset = 0, origin = CUBRID_CURSOR_CURRENT;

    T_CUBRID_REQUEST *request = NULL;

    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl|l", &req_id, &offset, &origin) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    cubrid_retval = cci_cursor(request->handle, offset, origin, &error);
    if (cubrid_retval < 0) {
	handle_error(cubrid_retval, &error, request->conn);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_num_rows)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	RETURN_LONG(request->row_count);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_FALSE;
    }
}

ZEND_FUNCTION(cubrid_num_cols)
{
    zval *req_id;
    T_CUBRID_REQUEST *request = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);
    
    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	RETURN_LONG(request->col_count);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_FALSE;
    }
}

ZEND_FUNCTION(cubrid_get)
{
    zval *conn_id = NULL, *attr_name = NULL;
    char *oid = NULL;
    int oid_len;
    int attr_count = -1;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;
    int i;

    zval **elem_buf = NULL;
    char **attr = NULL;
    int request_handle;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs|z", &conn_id, &oid, &oid_len, &attr_name) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if (attr_name) {
	switch (Z_TYPE_P(attr_name)) {
	case IS_STRING:
	    attr_count = 1;
	    attr = (char **) safe_emalloc(attr_count + 1, sizeof(char *), 0);

	    convert_to_string_ex(&attr_name);

	    attr[0] = estrndup(Z_STRVAL_P(attr_name), Z_STRLEN_P(attr_name));
	    attr[1] = NULL;

	    break;
	case IS_ARRAY:
	    attr_count = zend_hash_num_elements(HASH_OF(attr_name));
	    attr = (char **) safe_emalloc(attr_count + 1, sizeof(char *), 0);

	    for (i = 0; i <= attr_count; i++) {
		attr[i] = NULL;
	    }

	    for (i = 0; i < attr_count; i++) {
		if (zend_hash_index_find(HASH_OF(attr_name), i, (void **) &elem_buf) == FAILURE) {
		    handle_error(CUBRID_ER_INVALID_PARAM, NULL, connect);
		    goto ERR_CUBRID_GET;
		}
		convert_to_string_ex(elem_buf);
		attr[i] = estrdup(Z_STRVAL_P(*elem_buf));
	    }
	    attr[i] = NULL;

	    break;
	default:
	    handle_error(CUBRID_ER_INVALID_PARAM, NULL, connect);
	    RETURN_FALSE;
	}
    }

    cubrid_retval = cci_oid_get(connect->handle, oid, attr, &error);
    if (cubrid_retval < 0) {
	handle_error(cubrid_retval, &error, connect);
	goto ERR_CUBRID_GET;
    }
    request_handle = cubrid_retval;

    /* free memory before return */
    for (i = 0; i < attr_count; i++) {
 	if (attr[i]) {
	    efree(attr[i]);   
	}
    }

    if (attr) {
	efree(attr);
    }

    if (attr_name && Z_TYPE_P(attr_name) == IS_STRING) {
	char *result;
	int ind;

	cubrid_retval = cci_get_data(request_handle, 1, CCI_A_TYPE_STR, &result, &ind);
	if (cubrid_retval < 0) {
	    handle_error(cubrid_retval, &error, connect);
	    RETURN_FALSE;
	}

	if (ind < 0) {
	    RETURN_FALSE;
	} else {
	    RETURN_STRINGL(result, ind, 1);
	}
    } else {
	if ((cubrid_retval = fetch_a_row(return_value, connect->handle, request_handle, NULL, CUBRID_ASSOC TSRMLS_CC)) != SUCCESS) {
	    handle_error(cubrid_retval, NULL, connect);
	    RETURN_FALSE;
	}
    }

    return;

ERR_CUBRID_GET:

    for (i = 0; i < attr_count; i++) {
 	if (attr[i]) {
	    efree(attr[i]);   
	}
    }

    if (attr) {
	efree(attr);
    }
    
    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_put)
{
    zval *conn_id = NULL, *attr_value = NULL;
    char *oid = NULL, *attr = NULL;
    int oid_len, attr_len;

    char **attr_name = NULL;
    int attr_count = 0;
    int *attr_type = NULL;

    T_CUBRID_CONNECT *connect;
    T_CCI_SET temp_set = NULL;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    void **value = NULL;
    char *key = NULL;
    ulong index;
    zval **data = NULL;
    int i;

    init_error();

    switch (ZEND_NUM_ARGS()) {
    case 3:
	if (zend_parse_parameters(3 TSRMLS_CC, "rsa", &conn_id, &oid, &oid_len, &attr_value) == FAILURE) {
	    return;
	}

	ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

	attr_count = zend_hash_num_elements(HASH_OF(attr_value));
	zend_hash_internal_pointer_reset(HASH_OF(attr_value));

	attr_name = (char **) safe_emalloc(attr_count + 1, sizeof(char *), 0);
	value = safe_emalloc(attr_count + 1, sizeof(char *), 0);
	attr_type = (int *) safe_emalloc(attr_count + 1, sizeof(int), 0);

	if (attr_count > 0) {
	    for (i = 0; i < attr_count; i++) {
		if (zend_hash_get_current_key(HASH_OF(attr_value), &key, &index, 1) == HASH_KEY_NON_EXISTANT) {
		    break;
		}

		attr_name[i] = (char *) safe_emalloc(strlen(key) + 1, sizeof(char), 0);
		strlcpy(attr_name[i], key, strlen(key) + 1);
		value[i] = NULL;
		attr_type[i] = 0;

		efree(key);

		zend_hash_get_current_data(HASH_OF(attr_value), (void **) &data);
		switch (Z_TYPE_PP(data)) {
		case IS_NULL:
		    value[i] = NULL;

		    break;
		case IS_LONG:
		case IS_DOUBLE:
		    convert_to_string_ex(data);
		case IS_STRING:
		    value[i] = (char *) safe_emalloc(Z_STRLEN_PP(data) + 1, sizeof(char), 0);
		    strlcpy(value[i], Z_STRVAL_PP(data), Z_STRLEN_PP(data) + 1);
		    attr_type[i] = CCI_A_TYPE_STR;

		    break;
		case IS_ARRAY:
		    cubrid_retval = cubrid_make_set(HASH_OF(*data), &temp_set);
		    if (cubrid_retval < 0) {
			handle_error(cubrid_retval, NULL, connect);
			goto ERR_CUBRID_PUT;
		    }

		    value[i] = temp_set;
		    attr_type[i] = CCI_A_TYPE_SET;

		    break;
		case IS_OBJECT:
		case IS_BOOL:
		case IS_RESOURCE:
		case IS_CONSTANT:
		    cubrid_retval = -1;
		    handle_error(CUBRID_ER_NOT_SUPPORTED_TYPE, NULL, connect);
		    goto ERR_CUBRID_PUT;
		}

		zend_hash_move_forward(HASH_OF(attr_value));
	    }

	    attr_name[attr_count] = NULL;
	    value[attr_count] = NULL;	
	}

	break;
    case 4:
	if (zend_parse_parameters(4 TSRMLS_CC, "rssz", 
		    &conn_id, &oid, &oid_len, &attr, &attr_len, &attr_value) == FAILURE) {
	    return;
	}

	ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

	attr_count = 1;

	attr_name = (char **) safe_emalloc(attr_count + 1, sizeof(char *), 0);
	value = safe_emalloc(attr_count + 1, sizeof(char *), 0);
	attr_type = safe_emalloc(attr_count + 1, sizeof(int), 0);

	attr_name[0] = (char *) safe_emalloc (attr_len + 1, sizeof(char), 0);
	strlcpy(attr_name[0], attr, attr_len + 1);
	attr_name[1] = NULL;

	value[0] = NULL;
	attr_type[0] = 0;

	switch (Z_TYPE_P(attr_value)) {
	case IS_NULL:
	    value[0] = NULL;
	    
	    break;
	case IS_LONG:
	case IS_DOUBLE:
	    convert_to_string_ex(&attr_value);
	case IS_STRING:
	    value[0] = (char *) safe_emalloc(Z_STRLEN_P(attr_value) + 1, sizeof(char), 0);
	    strlcpy(value[0], Z_STRVAL_P(attr_value), Z_STRLEN_P(attr_value) + 1);
	    attr_type[0] = CCI_A_TYPE_STR;

	    break;
	case IS_ARRAY:
	    cubrid_retval = cubrid_make_set(HASH_OF(attr_value), &temp_set);
	    if (cubrid_retval < 0) {
		handle_error(cubrid_retval, NULL, connect);
		goto ERR_CUBRID_PUT;
	    }

	    value[0] = temp_set;
	    attr_type[0] = CCI_A_TYPE_SET;

	    break;
	case IS_OBJECT:
	case IS_BOOL:
	case IS_RESOURCE:
	case IS_CONSTANT:
	    cubrid_retval = -1;
	    handle_error(CUBRID_ER_NOT_SUPPORTED_TYPE, NULL, connect);
	    goto ERR_CUBRID_PUT;
	}
	value[1] = NULL;

	break;
    default:
	WRONG_PARAM_COUNT;
    }

    cubrid_retval = cci_oid_put2(connect->handle, oid, attr_name, value, attr_type, &error);
    if (cubrid_retval < 0) {
	handle_error(cubrid_retval, &error, connect);
	goto ERR_CUBRID_PUT;
    }

ERR_CUBRID_PUT:

    if (attr_name) {
	for (i = 0; i < attr_count; i++) {
	    if (attr_name[i])
		efree(attr_name[i]);
	}
	efree(attr_name);
    }

    if (value) {
	for (i = 0; i < attr_count; i++) {
	    switch (attr_type[i]) {
	    case CCI_A_TYPE_SET:
		cci_set_free(value[i]);
		break;
	    default:
		if (value[i]) {
		    efree(value[i]);
		}
		break;
	    }
	}
	efree(value);
    }

    if (attr_type) {
	efree(attr_type);
    }

    if (cubrid_retval < 0) {
	RETURN_FALSE;
    } else {
	RETURN_TRUE;
    }
}

ZEND_FUNCTION(cubrid_drop)
{
    zval *conn_id = NULL;
    char *oid = NULL;
    int oid_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &conn_id, &oid, &oid_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_oid(connect->handle, CCI_OID_DROP, oid, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE; 
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_is_instance)
{
    zval *conn_id = NULL;
    char *oid = NULL;
    int oid_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &conn_id, &oid, &oid_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_oid(connect->handle, CCI_OID_IS_INSTANCE, oid, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_LONG(-1);
    }

    RETURN_LONG(cubrid_retval);
}

ZEND_FUNCTION(cubrid_lock_read)
{
    zval *conn_id = NULL;
    char *oid = NULL;
    int oid_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &conn_id, &oid, &oid_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_oid(connect->handle, CCI_OID_LOCK_READ, oid, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE; 
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_lock_write)
{
    zval *conn_id = NULL;
    char *oid = NULL;
    int oid_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &conn_id, &oid, &oid_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_oid(connect->handle, CCI_OID_LOCK_WRITE, oid, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE; 
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_get_class_name)
{
    zval *conn_id = NULL;
    char *oid = NULL;
    int oid_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;
    char out_buf[1024];

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &conn_id, &oid, &oid_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_oid_get_class_name(connect->handle, oid, out_buf, 1024, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_STRING(out_buf, 1);
}

ZEND_FUNCTION(cubrid_schema)
{
    zval *conn_id = NULL;
    char *class_name = NULL, *attr_name = NULL;
    long schema_type;
    int class_name_len, attr_name_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;

    int flag = 0;
    int cubrid_retval = 0;
    int request_handle;
    int i = 0;
    zval *temp_element = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl|ss", 
		&conn_id, &schema_type, &class_name, &class_name_len, &attr_name, &attr_name_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    switch (schema_type) {
    case CUBRID_SCH_CLASS:
    case CUBRID_SCH_VCLASS:
	flag = CCI_CLASS_NAME_PATTERN_MATCH;
	break;
    case CUBRID_SCH_ATTRIBUTE:
    case CUBRID_SCH_CLASS_ATTRIBUTE:
	flag = CCI_ATTR_NAME_PATTERN_MATCH;
	break;
    default:
	flag = 0;
	break;
    }

    if ((cubrid_retval = cci_schema_info(connect->handle, 
		    schema_type, class_name, attr_name, (char) flag, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    request_handle = cubrid_retval;

    array_init(return_value);

    for (i = 0; ; i++) {
	cubrid_retval = cci_cursor(request_handle, 1, CCI_CURSOR_CURRENT, &error);
	if (cubrid_retval == CCI_ER_NO_MORE_DATA) {
	    break;
	}

	if (cubrid_retval < 0) {
	    handle_error(cubrid_retval, &error, connect);
            goto ERR_CUBRID_SCHEMA;
	}

	if ((cubrid_retval = cci_fetch(request_handle, &error)) < 0) {
	    handle_error(cubrid_retval, &error, connect);
            goto ERR_CUBRID_SCHEMA;
	}

	MAKE_STD_ZVAL(temp_element);
	if ((cubrid_retval = fetch_a_row(temp_element, connect->handle, request_handle, NULL, CUBRID_ASSOC TSRMLS_CC)) != SUCCESS) {
	    handle_error(cubrid_retval, NULL, connect);
	    FREE_ZVAL(temp_element);
            goto ERR_CUBRID_SCHEMA;
	}

	zend_hash_index_update(Z_ARRVAL_P(return_value), i, (void *) &temp_element, sizeof(zval *), NULL);
    }

    cci_close_req_handle(request_handle);

    return;

ERR_CUBRID_SCHEMA:

    cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);
    cci_close_req_handle(request_handle);

    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_col_size)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL;
    int oid_len, attr_name_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;
    int col_size = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);
    
    if ((cubrid_retval = cci_col_size(connect->handle, oid, attr_name, &col_size, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_LONG(col_size);
}

ZEND_FUNCTION(cubrid_col_get)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL;
    int oid_len, attr_name_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    char *res_buf;
    int ind;
    int col_size;
    int col_type;
    int request_handle;
    int i = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_col_get(connect->handle, oid, attr_name, &col_size, &col_type, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    request_handle = cubrid_retval;

    array_init(return_value);

    for (i = 0; ;i++) {
	cubrid_retval = cci_cursor(request_handle, 1, CCI_CURSOR_CURRENT, &error);
	if (cubrid_retval == CCI_ER_NO_MORE_DATA) {
	    break;
	}

	if (cubrid_retval < 0) {
	    handle_error(cubrid_retval, &error, connect);
            goto ERR_CUBRID_COL_GET;
	}

	if ((cubrid_retval = cci_fetch(request_handle, &error)) < 0) {
	    handle_error(cubrid_retval, &error, connect);
            goto ERR_CUBRID_COL_GET;
	}

	if ((cubrid_retval = cci_get_data(request_handle, 1, CCI_A_TYPE_STR, &res_buf, &ind)) < 0) {
	    handle_error(cubrid_retval, NULL, connect);
            goto ERR_CUBRID_COL_GET;
	}

	if (ind < 0) {
	    add_index_unset(return_value, i);
	} else {
	    add_index_stringl(return_value, i, res_buf, ind, 1);
	}
    }

    cci_close_req_handle(request_handle);

    return;

ERR_CUBRID_COL_GET:

    cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);
    cci_close_req_handle(request_handle);

    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_set_add)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL, *set_element = NULL;
    int oid_len, attr_name_len, set_element_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsss", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len, &set_element, &set_element_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_col_set_add(connect->handle, oid, attr_name, set_element, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_set_drop)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL, *set_element = NULL;
    int oid_len, attr_name_len, set_element_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsss", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len, &set_element, &set_element_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_col_set_drop(connect->handle, oid, attr_name, set_element, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_seq_insert)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL, *seq_element = NULL;
    long index = -1;
    int oid_len, attr_name_len, seq_element_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rssls", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len, &index, 
		&seq_element, &seq_element_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_col_seq_insert(connect->handle, oid, attr_name, index, seq_element, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_seq_put)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL, *seq_element = NULL;
    long index = -1;
    int oid_len, attr_name_len, seq_element_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rssls", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len, &index, 
		&seq_element, &seq_element_len) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_col_seq_put(connect->handle, oid, attr_name, index, seq_element, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_seq_drop)
{
    zval *conn_id = NULL;
    char *oid = NULL, *attr_name = NULL;
    long index = -1;
    int oid_len, attr_name_len;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rssl", 
		&conn_id, &oid, &oid_len, &attr_name, &attr_name_len, &index) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_col_seq_drop(connect->handle, oid, attr_name, index, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_get_autocommit)
{
    zval *conn_id = NULL;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    CCI_AUTOCOMMIT_MODE mode;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &conn_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((mode = cci_get_autocommit(connect->handle)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        return; 
    }

    if (mode == CCI_AUTOCOMMIT_TRUE) {
	RETURN_TRUE;
    }

    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_set_autocommit)
{
    zval *conn_id = NULL;
    zend_bool param = FALSE;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    CCI_AUTOCOMMIT_MODE mode;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rb", &conn_id, &param) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    mode = param ? CCI_AUTOCOMMIT_TRUE : CCI_AUTOCOMMIT_FALSE;
    if ((cubrid_retval = cci_set_autocommit(connect->handle, mode)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        RETURN_FALSE;
    }
	
    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_commit)
{
    zval *conn_id = NULL;
    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &conn_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_end_tran(connect->handle, CCI_TRAN_COMMIT, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_rollback)
{
    zval *conn_id = NULL;
    
    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &conn_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if ((cubrid_retval = cci_end_tran(connect->handle, CCI_TRAN_ROLLBACK, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_error_msg)
{
    if (zend_parse_parameters_none() == FAILURE) {
	return;
    }

    RETURN_STRING(CUBRID_G(recent_error).msg, 1);
}

ZEND_FUNCTION(cubrid_error_code)
{
    if (zend_parse_parameters_none() == FAILURE) {
	return;
    }

    RETURN_LONG(CUBRID_G(recent_error).code);
}

ZEND_FUNCTION(cubrid_error_code_facility)
{
    if (zend_parse_parameters_none() == FAILURE) {
	return;
    }

    RETURN_LONG(CUBRID_G(recent_error).facility);
}

ZEND_FUNCTION(cubrid_error)
{
    zval *conn_id = NULL;
    T_CUBRID_CONNECT *connect = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }
    
    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    RETURN_STRING(connect->recent_error.msg, 1);
}

ZEND_FUNCTION(cubrid_errno)
{
    zval *conn_id = NULL;
    T_CUBRID_CONNECT *connect = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }

    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    RETURN_LONG(connect->recent_error.code);
}

ZEND_FUNCTION(cubrid_field_name)
{
    zval *req_id = NULL;
    long offset = -1;

    T_CUBRID_REQUEST *request;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (offset < 0 || offset >= request->col_count) {
	handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	RETURN_FALSE;
    }

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	RETURN_STRING(request->col_info[offset].col_name, 1);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_LONG(-1);
    }
}

ZEND_FUNCTION(cubrid_field_table)
{
    zval *req_id = NULL;
    long offset = -1;

    T_CUBRID_REQUEST *request;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (offset < 0 || offset >= request->col_count) {
	handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	RETURN_FALSE;
    }

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	RETURN_STRING(request->col_info[offset].class_name, 1);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_LONG(-1);
    }
}

ZEND_FUNCTION(cubrid_field_type)
{
    zval *req_id = NULL;
    long offset = -1;
    T_CUBRID_REQUEST *request;
    char string_type[128];

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (offset < 0 || offset >= request->col_count) {
	handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	RETURN_FALSE;
    }

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	type2str(&request->col_info[offset], string_type, sizeof(string_type));
	RETURN_STRING(string_type, 1);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_LONG(-1);
    }
}

ZEND_FUNCTION(cubrid_field_flags)
{
    zval *req_id = NULL; 
    long offset = -1;
    T_CUBRID_REQUEST *request;
    int n;
    char sz[1024];

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (offset < 0 || offset >= request->col_count) {
	handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	RETURN_FALSE;
    }

    strlcpy(sz, "", sizeof(sz));

    if (request->col_info[offset].is_non_null) {
	strcat(sz, "not_null ");
    }

    if (request->col_info[offset].is_primary_key) {
	strcat(sz, "primary_key ");
    }

    if (request->col_info[offset].is_unique_key) {
	strcat(sz, "unique_key ");
    }

    if (request->col_info[offset].is_foreign_key) {
	strcat(sz, "foreign_key ");
    }

    if (request->col_info[offset].is_auto_increment) {
	strcat(sz, "auto_increment ");
    }

    if (request->col_info[offset].is_shared) {
	strcat(sz, "shared ");
    }

    if (request->col_info[offset].is_reverse_index) {
	strcat(sz, "reverse_index ");
    }

    if (request->col_info[offset].is_reverse_unique) {
	strcat(sz, "reverse_unique ");
    }

    if (request->col_info[offset].type == CCI_U_TYPE_TIMESTAMP) {
	strcat(sz, "timestamp ");
    }

    n = strlen(sz);
    if (n > 0 && sz[n - 1] == ' ') {
	sz[n - 1] = 0;
    }

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	RETURN_STRING(sz, 1);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_LONG(-1);
    }
}

ZEND_FUNCTION(cubrid_data_seek)
{
    zval *req_id = NULL;
    long offset = -1;

    T_CUBRID_REQUEST *request;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (request->row_count == 0) {
	php_error_docref(NULL TSRMLS_CC, E_WARNING, "Number of rows is NULL.\n");
	RETURN_FALSE;
    }

    if ((cubrid_retval = cci_cursor(request->handle, offset + 1, CUBRID_CURSOR_FIRST, &error)) < 0) {
	handle_error(cubrid_retval, &error, request->conn);
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_fetch)
{
    php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0, 0);
}

ZEND_FUNCTION(cubrid_fetch_array)
{
    php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0, 0);
}

ZEND_FUNCTION(cubrid_fetch_assoc)
{
    php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAM_PASSTHRU, CUBRID_ASSOC, 0);
}

ZEND_FUNCTION(cubrid_fetch_row)
{
    php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAM_PASSTHRU, CUBRID_NUM, 0);
}

ZEND_FUNCTION(cubrid_fetch_object)
{
    php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAM_PASSTHRU, CUBRID_BOTH | CUBRID_OBJECT, 1);
}

ZEND_FUNCTION(cubrid_fetch_field)
{
    zval *req_id = NULL;
    long offset = 0;

    T_CUBRID_REQUEST *request = NULL;

    zend_bool is_numeric = 0, is_blob = 0;
    int max_length = 0;

    int res = 0;
    char *buffer = NULL;
    char string_type[128];

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (ZEND_NUM_ARGS() == 1) {
	offset = request->fetch_field_auto_index++;

        if (offset >= request->col_count) {
            RETURN_FALSE;
        }

    } else if (ZEND_NUM_ARGS() == 2) {
	request->fetch_field_auto_index = offset + 1;
    
        if (offset < 0 || offset >= request->col_count) {
    	    handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
    	    RETURN_FALSE;
        }
    }

    array_init(return_value);

    is_numeric = numeric_type(request->col_info[offset].type);
    max_length = 0;
    is_blob = (request->col_info[offset].type == CCI_U_TYPE_BLOB)?1:0;

    add_assoc_string(return_value, "name", request->col_info[offset].col_name, 1);
    add_assoc_string(return_value, "table", request->col_info[offset].class_name, 1);

    if (is_numeric) {
        if (request->col_info[offset].default_value[0] == '\0') {
            add_assoc_null(return_value, "def");
        } else {
            add_assoc_string(return_value, "def", request->col_info[offset].default_value, 1);
        }
    } else {
        add_assoc_string(return_value, "def", request->col_info[offset].default_value, 1);
    }

    add_assoc_long(return_value, "max_length", max_length);
    add_assoc_long(return_value, "not_null", request->col_info[offset].is_non_null);
    add_assoc_long(return_value, "primary_key", request->col_info[offset].is_primary_key);
    add_assoc_long(return_value, "unique_key", request->col_info[offset].is_unique_key);
    add_assoc_long(return_value, "multiple_key", !request->col_info[offset].is_unique_key);
    add_assoc_long(return_value, "numeric", is_numeric);
    add_assoc_long(return_value, "blob", is_blob);

    type2str(&request->col_info[offset], string_type, sizeof(string_type));
    add_assoc_string(return_value, "type", string_type, 1);

    add_assoc_long(return_value, "unsigned", 0);
    add_assoc_long(return_value, "zerofill", 0);

    if (return_value->type == IS_ARRAY) {
	convert_to_object(return_value);
    }

    return;

ERR_CUBRID_FETCH_FIELD:
    cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);
    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_num_fields)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
	RETURN_LONG(request->col_count);
    default:
	handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, request->conn);
	RETURN_LONG(-1);
    }
}

ZEND_FUNCTION(cubrid_free_result)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if ((cubrid_retval = cci_fetch_buffer_clear(request->handle)) < 0) {
	handle_error(cubrid_retval, NULL, request->conn); 
	RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_fetch_lengths)
{
    zval *req_id = NULL;

    T_CUBRID_REQUEST *request;
    int col;
    long len = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (request->field_lengths == NULL) {
        handle_error (CUBRID_ER_CANNOT_GET_COLUMN_INFO, NULL, request->conn);
        RETURN_FALSE;
    }

    array_init(return_value);

    for (col = 0; col < request->col_count; col++) {
	add_index_long(return_value, col, request->field_lengths[col]);
    }

    return;
}

ZEND_FUNCTION(cubrid_field_seek)
{
    zval *req_id = NULL;
    long offset = 0;
    T_CUBRID_REQUEST *request = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (offset < 0 || offset > request->col_count - 1) {
	handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	RETURN_FALSE;
    }

    /* Set the offset which will be used by cubrid_fetch_field() */
    request->fetch_field_auto_index = offset;

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_field_len)
{
    zval *req_id = NULL;
    long offset = 0;

    T_CUBRID_REQUEST *request;
    long len = 0;
    T_CCI_U_TYPE type;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &offset) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if (offset < 0 || offset > request->col_count - 1) {
	handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	RETURN_FALSE;
    }

    type = CCI_GET_COLLECTION_DOMAIN(CCI_GET_RESULT_INFO_TYPE(request->col_info, offset + 1));
    if ((len = get_cubrid_u_type_len(type)) == -1) {
	len = CCI_GET_RESULT_INFO_PRECISION(request->col_info, offset + 1); 
	if (type == CCI_U_TYPE_NUMERIC) {
	    len += 2; /* "," + "-" */
	}
    }

    if (CCI_IS_COLLECTION_TYPE(CCI_GET_RESULT_INFO_TYPE(request->col_info, offset + 1))) {
	len = MAX_LEN_SET;
    }

    RETURN_LONG(len);
}

ZEND_FUNCTION(cubrid_result)
{
    zval *req_id = NULL;
    long row_offset = 0;
    zval *column = NULL;

    long col_offset = 0;
    char *col_name = NULL;
    long col_name_len = 0;

    int cubrid_retval = 0;
    int i;

    T_CUBRID_REQUEST *request = NULL;
    T_CCI_ERROR error;

    char *res_buf = NULL;
    int ind = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl|z", &req_id, &row_offset, &column) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    if ((cubrid_retval = cci_cursor(request->handle, row_offset + 1, 
		    CCI_CURSOR_FIRST, &error)) == CCI_ER_NO_MORE_DATA) {
	RETURN_FALSE;
    } else if (cubrid_retval < 0) {
        handle_error(cubrid_retval, &error, request->conn);
        RETURN_FALSE;
    }

    if (column) {
	switch (Z_TYPE_P(column)) {
	case IS_STRING: {
            char *table_name = NULL, *field_name = NULL, *tmp = NULL;

	    convert_to_string_ex(&column);
	    col_name = Z_STRVAL_P(column);
	    col_name_len = Z_STRLEN_P(column);

            tmp = strchr(col_name, '.');

	    for (i = 0; i < request->col_count; i++) {
		if (tmp == NULL) {
                    if (strcmp(request->col_info[i].col_name, col_name) == 0) {
		        col_offset = i;
		        break;
                    }
		} else { 
                    if ((strcmp(request->col_info[i].col_name, tmp + 1) == 0) && 
                            (strncmp(request->col_info[i].class_name, col_name, tmp - col_name) == 0)) {
                        col_offset = i;
                        break;
                    }
                }
	    }

	    if (i == request->col_count) {
		handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
		RETURN_FALSE;
	    }
        }
	    break;
	case IS_LONG:
	    convert_to_long_ex(&column);
	    col_offset = Z_LVAL_P(column); 

	    if (col_offset < 0 || col_offset >= request->col_count) {
		handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
		RETURN_FALSE;
	    }

	    break;
	default:
	    handle_error(CCI_ER_COLUMN_INDEX, NULL, request->conn);
	    RETURN_FALSE;
	}
    }

    if ((cubrid_retval = cci_fetch(request->handle, &error)) < 0) {
	handle_error(cubrid_retval, &error, request->conn);
	RETURN_FALSE;
    }

    if ((cubrid_retval = cci_get_data(request->handle, col_offset + 1, 
		    CCI_A_TYPE_STR, &res_buf, &ind)) < 0) {
	handle_error(cubrid_retval, NULL, request->conn);
	RETURN_FALSE;
    }

    if (ind == -1) {
	return; 
    } else {
	RETURN_STRINGL(res_buf, ind, 1);
    }
}

ZEND_FUNCTION(cubrid_unbuffered_query)
{
    zval *conn_id = NULL;
    char *query = NULL;
    int query_len;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_REQUEST *request = NULL;
    T_CCI_ERROR error;

    T_CCI_COL_INFO *res_col_info = NULL;
    T_CCI_CUBRID_STMT res_sql_type = 0;
    int res_col_count = 0;

    int cubrid_retval = 0;
    int req_handle = 0;
    int req_id = 0;

    init_error ();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|r", &query, &query_len, &conn_id) == FAILURE) {
	return;
    }

    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    request = new_cubrid_request();
    request->conn = connect;

    if ((cubrid_retval = cci_prepare(connect->handle, query, 0, &error)) < 0) {
        goto ERR_CUBRID_UNBUFFERED_QUERY;
    }

    req_handle = cubrid_retval;
    request->handle = req_handle;

    if ((cubrid_retval = cci_execute(req_handle, CCI_EXEC_ASYNC, 0, &error)) < 0) {
        goto ERR_CUBRID_UNBUFFERED_QUERY;
    }

    res_col_info = cci_get_result_info(req_handle, &res_sql_type, &res_col_count);
    request->sql_type = res_sql_type;

    if (res_sql_type == CUBRID_STMT_SELECT && !res_col_info) {
        cubrid_retval = CUBRID_ER_CANNOT_GET_COLUMN_INFO;
        goto ERR_CUBRID_UNBUFFERED_QUERY;

    } else if (res_sql_type == CUBRID_STMT_SELECT) {
        request->col_info = res_col_info;
        request->col_count = res_col_count;
    }

    connect->sql_type = request->sql_type;
    request->fetch_field_auto_index = 0;

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
        request->row_count = cubrid_retval;
        if (cubrid_retval > 0 && request->field_lengths == NULL) {
            request->field_lengths = (int *) emalloc (sizeof(int) * res_col_count);
        }

        cubrid_retval = cci_cursor(req_handle, 1, CCI_CURSOR_CURRENT, &error);
        if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
            goto ERR_CUBRID_UNBUFFERED_QUERY;
        }

        CUBRID_G(last_request_id) = req_id;
        connect->affected_rows = 0;

        break;
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
        connect->affected_rows = cubrid_retval; 
        break;
    case CUBRID_STMT_CALL:
        request->row_count = cubrid_retval;
        connect->affected_rows = cubrid_retval;
    default:
        break;
    }

    req_id = ZEND_REGISTER_RESOURCE(return_value, request, le_request);
    register_cubrid_request(connect, request);
    php_cubrid_set_default_req(Z_LVAL_P(return_value) TSRMLS_CC);

    if (request->sql_type == CUBRID_STMT_SELECT) {
        return;
    }

    RETURN_TRUE;

ERR_CUBRID_UNBUFFERED_QUERY:
    close_cubrid_request_internal(request);
    handle_error(cubrid_retval, &error, connect);
    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_query)
{
    zval *conn_id = NULL;
    char *query = NULL;
    int query_len;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_REQUEST *request = NULL;
    T_CCI_ERROR error;

    T_CCI_COL_INFO *res_col_info = NULL;
    T_CCI_CUBRID_STMT res_sql_type = 0;
    int res_col_count = 0;

    int cubrid_retval = 0;
    int exec_retval = 0;
    int req_handle = 0;
    int req_id = 0;

    init_error ();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|r", &query, &query_len, &conn_id) == FAILURE) {
	return;
    }

    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    request = new_cubrid_request();
    request->conn = connect;

    if ((cubrid_retval =
       cci_prepare_and_execute(connect->handle, query, 0, &exec_retval, &error)) < 0) {
        goto ERR_CUBRID_QUERY;
    }

    req_handle = cubrid_retval;
    request->handle = req_handle;

    res_col_info = cci_get_result_info(req_handle, &res_sql_type, &res_col_count);
    request->sql_type = res_sql_type;

    if (res_sql_type == CUBRID_STMT_SELECT && !res_col_info) {
        cubrid_retval = CUBRID_ER_CANNOT_GET_COLUMN_INFO;
        goto ERR_CUBRID_QUERY;

    } else if (res_sql_type == CUBRID_STMT_SELECT) {
        request->col_info = res_col_info;
        request->col_count = res_col_count;
    }

    connect->sql_type = request->sql_type;
    request->fetch_field_auto_index = 0;

    switch (request->sql_type) {
    case CUBRID_STMT_SELECT:
        request->row_count = exec_retval;
        if (cubrid_retval > 0 && request->field_lengths == NULL) {
            request->field_lengths = (int *) emalloc (sizeof(int) * res_col_count);
        }

        cubrid_retval = cci_cursor(req_handle, 1, CCI_CURSOR_CURRENT, &error);
        if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
            goto ERR_CUBRID_QUERY;
        }

        CUBRID_G(last_request_id) = req_id;
        connect->affected_rows = -1;

        break;
    case CUBRID_STMT_INSERT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
        CUBRID_G(last_request_id) = req_id;
        connect->affected_rows = exec_retval;
        break;
    case CUBRID_STMT_CALL:
	request->row_count = exec_retval;
        connect->affected_rows = exec_retval;
    default:
        break;
    }

    req_id = ZEND_REGISTER_RESOURCE(return_value, request, le_request);
    php_cubrid_set_default_req(Z_LVAL_P(return_value) TSRMLS_CC);
    register_cubrid_request(connect, request);

    if (request->sql_type == CUBRID_STMT_SELECT) {
        return;
    }

    RETURN_TRUE;

ERR_CUBRID_QUERY:
    close_cubrid_request_internal(request);
    handle_error(cubrid_retval, &error, connect);
    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_get_charset)
{
    zval *conn_id = NULL;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;

    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &conn_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);
    
    if ((cubrid_retval = cubrid_get_charset_internal (connect->handle, &error)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        RETURN_FALSE;
    }

    RETURN_STRING((char *) db_charsets[cubrid_retval].charset_name, 1);
}

ZEND_FUNCTION(cubrid_client_encoding)
{
    zval *conn_id = NULL;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;

    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }

    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    if ((cubrid_retval = cubrid_get_charset_internal (connect->handle, &error)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        RETURN_FALSE;
    }

    RETURN_STRING((char *) db_charsets[cubrid_retval].charset_name, 1);
}

ZEND_FUNCTION(cubrid_get_client_info)
{
    int major, minor, patch;
    char info[128];

    init_error();

    if (zend_parse_parameters_none() == FAILURE) {
	return;
    }

    cci_get_version(&major, &minor, &patch);

    snprintf(info, sizeof(info), "%d.%d.%d", major, minor, patch);

    RETURN_STRING(info, 1);
}

ZEND_FUNCTION(cubrid_get_server_info)
{
    zval *conn_id = NULL;
    T_CUBRID_CONNECT *connect;

    char buf[64];

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &conn_id) == FAILURE) {
	return;
    }
    
    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    cci_get_db_version(connect->handle, buf, sizeof(buf));

    RETURN_STRING(buf, 1);
}

ZEND_FUNCTION(cubrid_real_escape_string)
{
    zval *conn_id = NULL;
    zend_bool no_backslash_escapes = TRUE;

    char *unescaped_str = NULL;
    int unescaped_str_len = 0;

    char *escaped_str;
    int escaped_str_len = 0;

    char *s1, *s2;
    int i;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|r", 
		&unescaped_str, &unescaped_str_len, &conn_id) == FAILURE) {
	return;
    }

    if (unescaped_str_len > MAX_UNESCAPED_STR_LEN) {
	RETURN_FALSE; 
    }

    s1 = unescaped_str;
    for (i = 0; i < unescaped_str_len; i++) {
        switch (s1[i]) {
        case '\'':
            escaped_str_len++;

            break;
        case '\n':
        case '\r':
        case '\t':
        case '_':
        case '%':
        case '`':
        case '"':
        case '\\':
            if (!no_backslash_escapes) {
                escaped_str_len++;
            }

            break;
        default:
            break;
        }
        
        escaped_str_len++;
    }

    escaped_str = safe_emalloc(escaped_str_len + 1, sizeof(char), 0);

    s1 = unescaped_str;
    s2 = escaped_str;
    for (i = 0; i < unescaped_str_len; i++) {
        switch (s1[i]) {
        case '\'':
            if (!no_backslash_escapes) {
                *s2++ = '\\';
            } else {
                *s2++ = '\'';
            }

            break;
        case '\n':
        case '\r':
        case '\t':
        case '_':
        case '%':
        case '`':
        case '"':
        case '\\':
            if (!no_backslash_escapes) {
                *s2++ = '\\';
            }

            break;
        default:
            break;
        }

        switch (s1[i]) {
        case '\r':
            *s2++ = no_backslash_escapes ? s1[i] : 'r';
            break;
        case '\n':
            *s2++ = no_backslash_escapes ? s1[i] : 'n';
            break;
        case '\t':
            *s2++ = no_backslash_escapes ? s1[i] : 't';
            break;
        default:
            *s2++ = s1[i]; 
            break;
        } 
    }

    *s2 = '\0';
    
    RETURN_STRINGL(escaped_str, escaped_str_len, 0);
}

ZEND_FUNCTION(cubrid_get_db_parameter)
{
    zval *conn_id = NULL;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;

    int cubrid_retval = 0;
    int i, val;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &conn_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    array_init(return_value);

    for (i = CCI_PARAM_FIRST; i <= CCI_PARAM_LAST; i++) {
	if ((cubrid_retval = cci_get_db_parameter(connect->handle, (T_CCI_DB_PARAM) i, (void *) &val, &error)) < 0) {
	    handle_error(cubrid_retval, &error, connect);
            cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);
	    RETURN_FALSE;
	}

	add_assoc_long(return_value, (char *) db_parameters[i - 1].parameter_name, val);
    }

    return;
}

ZEND_FUNCTION(cubrid_set_db_parameter)
{
    zval *conn_id = NULL;
    long param_type, param_value;

    T_CUBRID_CONNECT *connect;
    T_CCI_ERROR error;

    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &conn_id, &param_type, &param_value) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    switch (param_type) {
    case CUBRID_PARAM_ISOLATION_LEVEL:
        if ((cubrid_retval = cci_set_isolation_level(connect->handle, param_value, &error)) < 0) {
            handle_error(cubrid_retval, &error, connect);
            RETURN_FALSE; 
        }

        break;
    case CUBRID_PARAM_LOCK_TIMEOUT:
        /* msec -> sec */
        param_value *= 1000;

        if ((cubrid_retval = cci_set_db_parameter(connect->handle, param_type, (void *) &param_value, &error)) < 0) {
            handle_error(cubrid_retval, &error, connect);
            RETURN_FALSE;
        }

        break;
    default:
        handle_error(CUBRID_ER_INVALID_PARAM_TYPE, NULL, connect);
        RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_db_name)
{
    zval *db_list = NULL;
    long db_index = 0;

    int db_list_size = 0;
    zval **elem_buf = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "al", &db_list, &db_index) == FAILURE) {
	return;
    }

    db_list_size = zend_hash_num_elements(HASH_OF(db_list));
    if (db_index < 0 || db_index >= db_list_size) {
        RETURN_FALSE; 
    }

    zend_hash_internal_pointer_reset(HASH_OF(db_list));
    if (zend_hash_index_find(HASH_OF(db_list), db_index, (void **) &elem_buf) == FAILURE) {
        RETURN_FALSE; 
    }
    convert_to_string_ex(elem_buf);

    RETURN_STRINGL(Z_STRVAL_P(*elem_buf), Z_STRLEN_P(*elem_buf), 1);
}

ZEND_FUNCTION(cubrid_list_dbs) 
{
    zval *conn_id = NULL;
    char *query = "SELECT LIST_DBS()";

    T_CUBRID_CONNECT *connect = NULL;
    T_CCI_ERROR error;

    int cubrid_retval = 0;
    int request_handle = 0;

    char *buffer = NULL;
    int ind = 0;

    int i;
    char *pos = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }

    if (conn_id) {
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
        if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE;
        }

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    array_init(return_value);
   
    if ((cubrid_retval = cci_prepare(connect->handle, query, 0, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
	RETURN_FALSE;
    }

    request_handle = cubrid_retval;

    if ((cubrid_retval = cci_execute(request_handle, CCI_EXEC_ASYNC, 0, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
        goto ERR_CUBRID_LIST_DBS;
    }

    cubrid_retval = cci_cursor(request_handle, 1, CCI_CURSOR_CURRENT, &error);
    if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
	handle_error(cubrid_retval, &error, connect);
        goto ERR_CUBRID_LIST_DBS;
    }

    if ((cubrid_retval = cci_fetch(request_handle, &error)) < 0) {
	handle_error(cubrid_retval, &error, connect);
        goto ERR_CUBRID_LIST_DBS;
    }

    if ((cubrid_retval = cci_get_data(request_handle, 1, CCI_A_TYPE_STR, &buffer, &ind)) < 0) {
	handle_error(cubrid_retval, &error, connect);
        goto ERR_CUBRID_LIST_DBS;
    }


    /* Databases names are separated by spaces */
    i = 0;
    if (ind != -1) {
	pos = strtok(buffer, " ");
	if (pos) {
	    while (pos != NULL) {
		add_index_stringl(return_value, i++, pos, strlen(pos), 1);
		pos = strtok(NULL, " ");
	    }
	} else {
	    add_index_stringl(return_value, 0, buffer, strlen(buffer), 1);
	}
    } else {
        goto ERR_CUBRID_LIST_DBS;
    }

    cci_close_req_handle(request_handle);
    return;

ERR_CUBRID_LIST_DBS:

    cci_close_req_handle(request_handle);
    cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);

    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_insert_id)
{
    zval *conn_id = NULL;

    T_CUBRID_CONNECT *connect = NULL;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    char *last_id = NULL;

    init_error();

    if (CUBRID_G(last_request_id) == -1) {
	RETURN_FALSE;
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
	return;
    }

    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    switch (connect->sql_type) {
    case CUBRID_STMT_INSERT:
        if (connect->affected_rows < 1) {
            RETURN_LONG(0);
        }

        if ((cubrid_retval = cci_last_insert_id(connect->handle, &last_id, &error)) < 0) {
            handle_error(cubrid_retval, &error, connect);
            RETURN_FALSE;
        }

        if (last_id == NULL) {
            RETURN_LONG(0);
        } else {
            RETURN_STRINGL(last_id, strlen(last_id), 1);
            free(last_id);
        } 

        break;
    case CUBRID_STMT_SELECT:
    case CUBRID_STMT_UPDATE:
    case CUBRID_STMT_DELETE:
        RETURN_LONG(0);
    default:
        handle_error(CUBRID_ER_INVALID_SQL_TYPE, NULL, connect);
        RETURN_FALSE;
    }
}

ZEND_FUNCTION(cubrid_ping)
{
    zval *conn_id = NULL;

    T_CUBRID_CONNECT *connect = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|r", &conn_id) == FAILURE) {
        return;
    }

    if (conn_id){
        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    } else {
	if (CUBRID_G(last_connect_id) == -1) {
            RETURN_FALSE; 
        } 

        ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, NULL, CUBRID_G(last_connect_id), "CUBRID-Connect", le_connect, le_pconnect);
    }

    init_error_link(connect);

    if (!check_connect_alive(connect)) {
        RETURN_FALSE;
    } 

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_lob_get)
{
    zval *conn_id = NULL;
    char *sql_stmt = NULL;
    int sql_stmt_len;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_LOB *cubrid_lob = NULL;

    T_CCI_LOB lob = NULL;
    T_CCI_ERROR error;

    int req_handle = 0;
    int cubrid_retval = 0;
    int ind = 0;

    T_CCI_A_TYPE a_type = -1;
    T_CCI_U_TYPE u_type = -1;

    T_CCI_COL_INFO *res_col_info;
    T_CCI_SQLX_CMD cmd_type;

    int column_count = 0;
    int row_count = 0;
    int res_id = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &conn_id, &sql_stmt, &sql_stmt_len) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);

    init_error_link(connect);

    if (!sql_stmt) {
        RETURN_FALSE;
    }

    if ((cubrid_retval = cci_prepare(connect->handle, sql_stmt, 0, &error)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        RETURN_FALSE;
    }

    req_handle = cubrid_retval;

    array_init(return_value);

    res_col_info = cci_get_result_info(req_handle, &cmd_type, &column_count);
    if (!res_col_info || cmd_type != CUBRID_STMT_SELECT) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Get result info fail or sql type is not select");
        goto ERR_CUBRID_LOB_GET;
    }

    if (column_count > 1) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "More than one columns returned");
        goto ERR_CUBRID_LOB_GET;
    }

    u_type = CCI_GET_RESULT_INFO_TYPE(res_col_info, 1);

    if (u_type == CCI_U_TYPE_BLOB) {
        a_type = CCI_A_TYPE_BLOB; 
    } else if (u_type == CCI_U_TYPE_CLOB) {
        a_type = CCI_A_TYPE_CLOB; 
    } else {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "Column type is not BLOB or CLOB.");
        goto ERR_CUBRID_LOB_GET;
    }

    if ((cubrid_retval = cci_execute(req_handle, 0, 0, &error)) < 0) {
        handle_error(cubrid_retval, &error, connect);
        RETURN_FALSE;
    }

    while (1) {
        cubrid_retval = cci_cursor(req_handle, 1, CCI_CURSOR_CURRENT, &error);
        if (cubrid_retval == CCI_ER_NO_MORE_DATA) {
            break;
        }

        if (cubrid_retval < 0) {
            handle_error(cubrid_retval, &error, connect);
            goto ERR_CUBRID_LOB_GET;
        }

        if ((cubrid_retval = cci_fetch(req_handle, &error)) < 0) {
            handle_error(cubrid_retval, &error, connect);
            goto ERR_CUBRID_LOB_GET;
        }

        if ((cubrid_retval = cci_get_data(req_handle, 1, a_type, (void *) &lob, &ind)) < 0) {
            handle_error(cubrid_retval, NULL, connect);
            goto ERR_CUBRID_LOB_GET;
        }

        if (ind < 0) {
            goto ERR_CUBRID_LOB_GET;
        }

        cubrid_lob = new_cubrid_lob();
        cubrid_lob->lob = lob;
        cubrid_lob->type = u_type;
        cubrid_lob->size = cubrid_lob_size(lob, u_type);

        res_id = ZEND_REGISTER_RESOURCE(NULL, cubrid_lob, le_lob);
        add_index_resource(return_value, row_count, res_id);

        row_count++;
    }

    cci_close_req_handle(req_handle);
    return;

ERR_CUBRID_LOB_GET:

    cubrid_array_destroy(return_value->value.ht ZEND_FILE_LINE_CC);
    cci_close_req_handle(req_handle);

    RETURN_FALSE;
}

ZEND_FUNCTION(cubrid_lob_size)
{
    zval *lob_id = NULL;
    T_CUBRID_LOB *lob = NULL;

    char *id = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &lob_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE(lob, T_CUBRID_LOB *, &lob_id, -1, "CUBRID-Lob", le_lob);

    id = php_cubrid_int64_to_str(lob->size TSRMLS_CC);

    RETURN_STRINGL(id, strlen(id), 0);
}

ZEND_FUNCTION(cubrid_lob_export)
{
    zval *conn_id = NULL, *lob_id = NULL;
    char *file_name = NULL;
    int file_name_len = 0;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_LOB *lob = NULL;
    T_CCI_ERROR error;

    php_stream *stm = NULL;
    
    char buf[CUBRID_LOB_READ_BUF_SIZE];
    php_cubrid_int64_t lob_size = 0, start_pos = 0;
    int write_len = 0;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrs", &conn_id, &lob_id, &file_name, &file_name_len) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    ZEND_FETCH_RESOURCE(lob, T_CUBRID_LOB *, &lob_id, -1, "CUBRID-Lob", le_lob);

    init_error_link(connect);

    if (!(stm = php_stream_open_wrapper(file_name, "w", REPORT_ERRORS, NULL))) {
        RETURN_FALSE;
    }

    lob_size = lob->size;

    while (lob_size > 0) {
        if (lob_size < CUBRID_LOB_READ_BUF_SIZE) {
            write_len = (int)lob_size; 
        } else {
            write_len = CUBRID_LOB_READ_BUF_SIZE; 
        }

        if ((cubrid_retval = cubrid_lob_read(connect->handle, lob->lob, lob->type, start_pos, write_len, buf, &error)) < 0) {
            handle_error(cubrid_retval, &error, connect);
            php_stream_close(stm);

            RETURN_FALSE;
        }
        php_stream_write(stm, buf, write_len);

        start_pos += CUBRID_LOB_READ_BUF_SIZE;
        lob_size -= CUBRID_LOB_READ_BUF_SIZE;
    }

    php_stream_close(stm);

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_lob_send) 
{
    zval *conn_id = NULL, *lob_id = NULL;

    T_CUBRID_CONNECT *connect = NULL;
    T_CUBRID_LOB *lob = NULL;
    T_CCI_ERROR error;

    char buf[CUBRID_LOB_READ_BUF_SIZE];
    php_cubrid_int64_t lob_size = 0, start_pos = 0;
    int write_len = 0;
    int cubrid_retval = 0;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rr", &conn_id, &lob_id) == FAILURE) {
	return;
    }

    ZEND_FETCH_RESOURCE2(connect, T_CUBRID_CONNECT *, &conn_id, -1, "CUBRID-Connect", le_connect, le_pconnect);
    ZEND_FETCH_RESOURCE(lob, T_CUBRID_LOB *, &lob_id, -1, "CUBRID-Lob", le_lob);

    init_error_link(connect);

    lob_size = lob->size;

    while (lob_size > 0) {
        if (lob_size < CUBRID_LOB_READ_BUF_SIZE) {
            write_len = (int)lob_size; 
        } else {
            write_len = CUBRID_LOB_READ_BUF_SIZE; 
        }

        if ((cubrid_retval = cubrid_lob_read(connect->handle, lob->lob, lob->type, start_pos, write_len, buf, &error)) < 0) {
            handle_error(cubrid_retval, &error, connect);
            RETURN_FALSE;
        }

        if (PHPWRITE(buf, write_len) != write_len) {
	    handle_error(CUBRID_ER_TRANSFER_FAIL, NULL, connect);
            RETURN_FALSE;
	}

        start_pos += CUBRID_LOB_READ_BUF_SIZE;
        lob_size -= CUBRID_LOB_READ_BUF_SIZE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_lob_close)
{
    zval *lob_id_array = NULL;
    T_CUBRID_LOB *lob = NULL;

    int lob_id_count = 0, i;
    zval **data = NULL;

    init_error();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &lob_id_array) == FAILURE) {
	return;
    }

    lob_id_count = zend_hash_num_elements(HASH_OF(lob_id_array));
    zend_hash_internal_pointer_reset(HASH_OF(lob_id_array));

    for (i = 0; i < lob_id_count; i++) {
        zend_hash_get_current_data(HASH_OF(lob_id_array), (void **) &data);

        ZEND_FETCH_RESOURCE(lob, T_CUBRID_LOB *, data, -1, "CUBRID-Lob", le_lob);
        zend_list_delete(Z_RESVAL_PP(data));

        zend_hash_move_forward(HASH_OF(lob_id_array));
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_set_query_timeout)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request;
    long timeout = 0;
    int cubrid_retval = 0;

    init_error ();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &req_id, &timeout) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link (request->conn);

    if ((cubrid_retval = cci_set_query_timeout (request->handle, timeout)) < 0) {
        handle_error (cubrid_retval, NULL, request->conn);
        RETURN_FALSE;
    }

    RETURN_TRUE;
}

ZEND_FUNCTION(cubrid_get_query_timeout)
{
    zval *req_id = NULL;
    T_CUBRID_REQUEST *request;
    int timeout;

    init_error ();

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &req_id) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link (request->conn);

    if ((timeout = cci_get_query_timeout (request->handle)) < 0) {
        handle_error (timeout, NULL, request->conn);
        RETURN_FALSE;
    }

    RETURN_LONG (timeout);
}

/************************************************************************
* PRIVATE FUNCTIONS IMPLEMENTATION
************************************************************************/

static void php_cubrid_set_default_conn(int id TSRMLS_DC)
{
    if (CUBRID_G(last_connect_id) != -1) {
        zend_list_delete(CUBRID_G(last_connect_id));
    }

    CUBRID_G(last_connect_id) = id;
    zend_list_addref(id);
}

static void php_cubrid_set_default_req(int id TSRMLS_DC)
{
    if (CUBRID_G(last_request_id) != -1) {
        zend_list_delete(CUBRID_G(last_request_id));
    }

    CUBRID_G(last_request_id) = id;
    zend_list_addref(id);
}

static void close_cubrid_connect(zend_rsrc_list_entry * rsrc TSRMLS_DC)
{
    T_CUBRID_CONNECT *conn = (T_CUBRID_CONNECT *)rsrc->ptr;
    T_CCI_ERROR error;
    LINKED_LIST_NODE *p;
    LINKED_LIST_NODE *head = conn->unclosed_requests->head;

    /* When calling cci_disconnect, all request handle in cci will be released,
     * just like calling cci_close_req_handle. So we must prevent the PHP
     * garbage collector calling cci_close_req_handle again in close_cubrid_request.
     */

    for (p = head->next; p != NULL ; p = head->next) {
        T_CUBRID_REQUEST *req = (T_CUBRID_REQUEST *)p->data;
        req->conn = NULL;
        req->handle = 0;
        p->data = NULL;

        head->next = p->next;
        efree(p);
    }

    if (conn->handle) {
        cci_disconnect(conn->handle, &error); 
    }

    efree(head);
    efree(conn->unclosed_requests);
    efree(conn);
}

static void close_cubrid_pconnect(zend_rsrc_list_entry * rsrc TSRMLS_DC)
{
    T_CUBRID_CONNECT *conn = (T_CUBRID_CONNECT *)rsrc->ptr;

    T_CCI_ERROR error;

    if (conn->handle) {
        cci_disconnect(conn->handle, &error);
    }

    free(conn->unclosed_requests->head);
    free(conn->unclosed_requests);
    free(conn);
}

static int is_connection_exist(T_CUBRID_REQUEST *req)
{
    return req->conn ? 1 : 0;
}

static void close_cubrid_request_internal(T_CUBRID_REQUEST * req)
{
    if (is_connection_exist(req)) {
        linked_list_delete(req->conn->unclosed_requests, (void *)req);
        cci_close_req_handle(req->handle);
    }

    if (req->l_bind) {
        efree(req->l_bind);
    }

    if (req->field_lengths) {
        efree(req->field_lengths);
        req->field_lengths = NULL;
    }

    efree(req);
}

static void close_cubrid_request(zend_rsrc_list_entry * rsrc TSRMLS_DC)
{
    T_CUBRID_REQUEST *req = (T_CUBRID_REQUEST *)rsrc->ptr;

    close_cubrid_request_internal (req);
}

static void close_cubrid_lob_internal(T_CUBRID_LOB *lob)
{
    if (lob->lob) {
        cubrid_lob_free(lob->lob, lob->type);
    }
    efree(lob);
}

static void close_cubrid_lob(zend_rsrc_list_entry * rsrc TSRMLS_DC)
{
    T_CUBRID_LOB *lob = (T_CUBRID_LOB *)rsrc->ptr;

    close_cubrid_lob_internal (lob);
}

static void php_cubrid_init_globals(zend_cubrid_globals * cubrid_globals)
{
    cubrid_globals->recent_error.code = 0;
    cubrid_globals->recent_error.facility = 0;
    cubrid_globals->recent_error.msg[0] = 0;

    cubrid_globals->last_connect_id = -1;
    cubrid_globals->last_request_id = -1;

    cubrid_globals->default_userid = "PUBLIC";
    cubrid_globals->default_passwd = "";
}

static int init_error(void)
{
    set_error(0, 0, "");

    return SUCCESS;
}

static int set_error(T_FACILITY_CODE facility, int code, char *msg, ...)
{
    va_list args;
    TSRMLS_FETCH();

    CUBRID_G(recent_error).facility = facility;
    CUBRID_G(recent_error).code = code;

    va_start(args, msg);
    snprintf(CUBRID_G(recent_error).msg, 1024, msg, args);
    va_end(args);

    return SUCCESS;
}

static int init_error_link(T_CUBRID_CONNECT *conn)
{
    if (conn) {
        set_error_link(conn, 0, "");
    }

    return SUCCESS;
}

static int set_error_link(T_CUBRID_CONNECT *conn, int code, char *msg, ...)
{
    va_list args;

    conn->recent_error.code = code;

    va_start(args, msg);
    snprintf(conn->recent_error.msg, 1024, msg, args);
    va_end(args);

    return SUCCESS;
}

static int get_error_msg(int err_code, char *buf, int buf_size)
{
    const char *err_msg = "";
    int size = sizeof(db_error) / sizeof(db_error[0]);
    int i;

    if (err_code > -2000) {
	return cci_get_err_msg(err_code, buf, buf_size);
    }

    for (i = 0; i < size; i++) {
	if (err_code == db_error[i].err_code) {
	    err_msg = db_error[i].err_msg;
	    break;
	}
    }

    if (i == size) {
	err_msg = "Unknown Error";
    }

    strlcpy(buf, err_msg, buf_size);

    return SUCCESS;
}

static int handle_error(int err_code, T_CCI_ERROR * error, T_CUBRID_CONNECT *conn)
{
    int real_err_code = 0;
    char *real_err_msg = NULL;

    T_FACILITY_CODE facility = CUBRID_FACILITY_CLIENT;

    char err_msg[1024] = { 0 };
    char *facility_msg = NULL;

    if (err_code == CCI_ER_DBMS) {
	facility = CUBRID_FACILITY_DBMS;
	facility_msg = "DBMS";
	if (error) {
	    real_err_code = error->err_code;
	    real_err_msg = error->err_msg;
	} else {
	    real_err_code = 0;
	    real_err_msg = "Unknown DBMS error";
	}
    } else {
	if (err_code > -1000) {
	    facility = CUBRID_FACILITY_CCI;
	    facility_msg = "CCI";
	} else if (err_code > -2000) {
	    facility = CUBRID_FACILITY_CAS;
	    facility_msg = "CAS";
	} else if (err_code > -3000) {
	    facility = CUBRID_FACILITY_CLIENT;
	    facility_msg = "CLIENT";
	} else {
	    real_err_code = -1;
	    real_err_msg = NULL;
	    return FAILURE;
	}

	if (get_error_msg(err_code, err_msg, (int) sizeof(err_msg)) < 0) {
	    strlcpy(err_msg, "Unknown error message", sizeof(err_msg));
	}

	real_err_code = err_code;
	real_err_msg = err_msg;
    }

    set_error(facility, real_err_code, real_err_msg);

    if(conn) {
        set_error_link(conn, real_err_code, real_err_msg);
    }

    php_error(E_WARNING, "Error: %s, %d, %s", facility_msg, real_err_code, real_err_msg);

    return SUCCESS;
}

static int fetch_a_row(zval *arg, int conn_handle, int req_handle, int *field_lengths, int type TSRMLS_DC)
{
    T_CCI_COL_INFO *column_info = NULL;
    T_CCI_U_TYPE column_type;

    int column_count = 0;
    char *column_name;

    int cubrid_retval = 0;
    int null_indicator;
    int i;

    if ((column_info = cci_get_result_info(req_handle, NULL, &column_count)) == NULL) {
	return CUBRID_ER_CANNOT_GET_COLUMN_INFO;
    }

    array_init(arg);

    for (i = 0; i < column_count; i++) { 
        column_type = CCI_GET_RESULT_INFO_TYPE(column_info, i + 1);
	column_name = CCI_GET_RESULT_INFO_NAME(column_info, i + 1);

        if (CCI_IS_SET_TYPE(column_type) || CCI_IS_MULTISET_TYPE(column_type) || CCI_IS_SEQUENCE_TYPE(column_type)) {
	    T_CCI_SET res_buf = NULL;

	    if ((cubrid_retval = cci_get_data(req_handle, i + 1, CCI_A_TYPE_SET, &res_buf, &null_indicator)) < 0) {
		goto ERR_FETCH_A_ROW;
	    }

	    if (null_indicator >= 0) {
		if (type & CUBRID_NUM) {
		    cubrid_retval = cubrid_add_index_array(arg, i, res_buf TSRMLS_CC);
		} 
		
		if (type & CUBRID_ASSOC) {
		    cubrid_retval = cubrid_add_assoc_array(arg, column_name, res_buf TSRMLS_CC);
		}
		
		if (cubrid_retval < 0) {
		    cci_set_free(res_buf);
		    goto ERR_FETCH_A_ROW;
		}

		cci_set_free(res_buf);
	    }
	} else {
	    char *res_buf = NULL;

	    if ((cubrid_retval = cci_get_data(req_handle, i + 1, CCI_A_TYPE_STR, &res_buf, &null_indicator)) < 0) {
		goto ERR_FETCH_A_ROW;
	    }

	    if (null_indicator >= 0) {
		if (type & CUBRID_NUM) {
		    add_index_stringl(arg, i, res_buf, null_indicator, 1);
		} 
		
		if (type & CUBRID_ASSOC) {
		    add_assoc_stringl(arg, column_name, res_buf, null_indicator, 1);
		}
	    }
	}

        if (field_lengths != NULL) {
            if (null_indicator != -1) {
                field_lengths[i] = null_indicator;
            } else {
                field_lengths[i] = 0;
            }
        }

        if (null_indicator < 0) {
            if (type & CUBRID_NUM) {
                add_index_unset(arg, i);
            }

            if (type & CUBRID_ASSOC) {
                add_assoc_unset(arg, column_name);
            }
        }
    }

    return SUCCESS;

ERR_FETCH_A_ROW:
    cubrid_array_destroy(arg->value.ht ZEND_FILE_LINE_CC);
    return cubrid_retval;
}

static T_CUBRID_CONNECT *new_cubrid_connect(int persistent)
{
    T_CUBRID_CONNECT *connect = NULL;

    if (persistent) {
        connect = (T_CUBRID_CONNECT *) malloc(sizeof(T_CUBRID_CONNECT));
        if (!connect) {
            goto ERR_CUBRID_CONNECT;
        }

        connect->unclosed_requests = 
            (LINKED_LIST *) malloc(sizeof(LINKED_LIST));
        if (!connect->unclosed_requests) {
            goto ERR_CUBRID_CONNECT;
        }

        connect->unclosed_requests->head =
            (LINKED_LIST_NODE *) malloc(sizeof(LINKED_LIST_NODE));
        if (!connect->unclosed_requests->head) {
            goto ERR_CUBRID_CONNECT;
        }
    } else {
        connect = (T_CUBRID_CONNECT *) emalloc(sizeof(T_CUBRID_CONNECT));
        if (!connect) {
            goto ERR_CUBRID_CONNECT;
        }

        connect->unclosed_requests = 
            (LINKED_LIST *) emalloc(sizeof(LINKED_LIST));
        if (!connect->unclosed_requests) {
            goto ERR_CUBRID_CONNECT;
        }

        connect->unclosed_requests->head =
            (LINKED_LIST_NODE *) emalloc(sizeof(LINKED_LIST_NODE));
        if (!connect->unclosed_requests->head) {
            goto ERR_CUBRID_CONNECT;
        }
    }

    connect->unclosed_requests->head->data = NULL;
    connect->unclosed_requests->head->next = NULL;
    connect->unclosed_requests->tail = connect->unclosed_requests->head;

    connect->recent_error.code = 0;
    connect->recent_error.msg[0] = 0;
    connect->handle = 0;

    connect->persistent = persistent;

    connect->affected_rows = 0;
    connect->sql_type = 0;

    return connect;

ERR_CUBRID_CONNECT:
    if (connect->unclosed_requests) {
        if (persistent) {
            free(connect->unclosed_requests);
        } else {
            efree(connect->unclosed_requests);
        }
    }

    if (connect) {
        if (persistent) {

            free(connect);
        } else {
            efree(connect);
        }
    }

    return NULL;
}

static T_CUBRID_REQUEST *new_cubrid_request(void)
{
    T_CUBRID_REQUEST *request = (T_CUBRID_REQUEST *) emalloc(sizeof(T_CUBRID_REQUEST));

    request->conn = NULL;
    request->handle = 0;
    request->row_count = -1;
    request->col_count = -1;
    request->sql_type = 0;
    request->bind_num = -1;
    request->l_bind = NULL;
    request->l_prepare = 0;
    request->field_lengths = NULL;
    request->lob = NULL;

    return request;
}

static T_CUBRID_LOB *new_cubrid_lob(void)
{
    T_CUBRID_LOB *lob = (T_CUBRID_LOB *) emalloc(sizeof(T_CUBRID_LOB));

    lob->lob = NULL;
    lob->size = 0;
    lob->type = CCI_U_TYPE_BLOB;

    return lob;
}

static void register_cubrid_request(T_CUBRID_CONNECT *conn, T_CUBRID_REQUEST *req)
{
    linked_list_append(conn->unclosed_requests, (void *)req);
}

static int cubrid_add_index_array(zval *arg, uint index, T_CCI_SET in_set TSRMLS_DC)
{
    zval *tmp_ptr;

    int i;
    int res;
    int ind;
    char *buffer;

    int set_size = cci_set_size(in_set);

    MAKE_STD_ZVAL(tmp_ptr);
    array_init(tmp_ptr);

    for (i = 0; i < set_size; i++) {
	res = cci_set_get(in_set, i + 1, CCI_A_TYPE_STR, &buffer, &ind);
	if (res < 0) {
	    cubrid_array_destroy(HASH_OF(tmp_ptr) ZEND_FILE_LINE_CC);
	    FREE_ZVAL(tmp_ptr);
	    return res;
	}

	if (ind < 0) {
	    add_index_unset(tmp_ptr, i);
	} else {
	    add_index_string(tmp_ptr, i, buffer, 1);
	}
    }

    res = zend_hash_index_update(HASH_OF(arg), index, (void *) &tmp_ptr, sizeof(zval *), NULL);
    if (res == FAILURE) {
	cubrid_array_destroy(HASH_OF(tmp_ptr) ZEND_FILE_LINE_CC);
	FREE_ZVAL(tmp_ptr);
	return CUBRID_ER_PHP;
    }

    return 0;
}

static int cubrid_add_assoc_array(zval *arg, char *key, T_CCI_SET in_set TSRMLS_DC)
{
    zval *tmp_ptr;

    int i;
    int ind;
    char *buffer;
    int cubrid_retval = 0;

    int set_size = cci_set_size(in_set);

    MAKE_STD_ZVAL(tmp_ptr);
    array_init(tmp_ptr);

    for (i = 0; i < set_size; i++) {
	if ((cubrid_retval = cci_set_get(in_set, i + 1, CCI_A_TYPE_STR, &buffer, &ind)) < 0) {
	    cubrid_array_destroy(HASH_OF(tmp_ptr) ZEND_FILE_LINE_CC);
	    FREE_ZVAL(tmp_ptr);
	    return cubrid_retval;
	}

	if (ind < 0) {
	    add_index_unset(tmp_ptr, i);
	} else {
	    add_index_string(tmp_ptr, i, buffer, 1);
	}
    }

    if ((cubrid_retval = zend_hash_update(HASH_OF(arg), key, strlen(key) + 1, 
		    (void *) &tmp_ptr, sizeof(zval *), NULL)) == FAILURE) {
	cubrid_array_destroy(HASH_OF(tmp_ptr) ZEND_FILE_LINE_CC);
	FREE_ZVAL(tmp_ptr);
	return CUBRID_ER_PHP;
    }

    return 0;
}

static int cubrid_array_destroy(HashTable * ht ZEND_FILE_LINE_DC)
{
    zend_hash_destroy(ht);
    FREE_HASHTABLE_REL(ht);
    return SUCCESS;
}

static int cubrid_make_set(HashTable *ht, T_CCI_SET *set)
{
    void **set_array = NULL;
    int *set_null = NULL;
    char *key;
    ulong index;
    zval **data;

    int set_size;
    int i;
    int error_code;
    int cubrid_retval = 0;

    set_size = zend_hash_num_elements(ht);
    set_array = (void **) safe_emalloc(set_size, sizeof(void *), 0);

    for (i = 0; i < set_size; i++) {
	set_array[i] = NULL;
    }

    set_null = (int *) safe_emalloc(set_size, sizeof(int), 0);

    zend_hash_internal_pointer_reset(ht);
    for (i = 0; i < set_size; i++) {
	if (zend_hash_get_current_key(ht, &key, &index, 0) == HASH_KEY_NON_EXISTANT) {
	    break;
	}

	zend_hash_get_current_data(ht, (void **) &data);
	switch (Z_TYPE_PP(data)) {
	case IS_NULL:
	    set_array[i] = NULL;
	    set_null[i] = 1;

	    break;
	case IS_LONG:
	case IS_DOUBLE:
	    convert_to_string_ex(data);
	case IS_STRING:
	    set_array[i] = Z_STRVAL_PP(data);
	    set_null[i] = 0;

	    break;
	default:
	    error_code = CUBRID_ER_NOT_SUPPORTED_TYPE;
	    goto ERR_CUBRID_MAKE_SET;
	}

	zend_hash_move_forward(ht);
    }

    if ((cubrid_retval = cci_set_make(set, CCI_U_TYPE_STRING, set_size, set_array, set_null)) < 0) {
	*set = NULL;
	error_code = cubrid_retval;
	goto ERR_CUBRID_MAKE_SET;
    }

    efree(set_array);
    efree(set_null);

    return 0;

ERR_CUBRID_MAKE_SET:

    if (set_array) {
	efree(set_array);
    }

    if (set_null) {
	efree(set_null);
    }

    return error_code;
}

static int type2str(T_CCI_COL_INFO * column_info, char *type_name, int type_name_len)
{
    char buf[64] = {'\0'};

    switch (CCI_GET_COLLECTION_DOMAIN(column_info->type)) {
    case CCI_U_TYPE_UNKNOWN:
        strncpy(buf, "unknown", 7);
	break;
    case CCI_U_TYPE_CHAR:
        strncpy(buf, "char", 4);
	break;
    case CCI_U_TYPE_STRING:
        strncpy(buf, "varchar", 7);
	break;
    case CCI_U_TYPE_NCHAR:
        strncpy(buf, "nchar", 5);
	break;
    case CCI_U_TYPE_VARNCHAR:
        strncpy(buf, "varnchar", 8);
	break;
    case CCI_U_TYPE_BIT:
        strncpy(buf, "bit", 3);
	break;
    case CCI_U_TYPE_VARBIT:
        strncpy(buf, "varbit", 6);
	break;
    case CCI_U_TYPE_NUMERIC:
        strncpy(buf, "numeric", 7);
	break;
    case CCI_U_TYPE_INT:
        strncpy(buf, "integer", 7);
	break;
    case CCI_U_TYPE_SHORT:
        strncpy(buf, "smallint", 8);
	break;
    case CCI_U_TYPE_MONETARY:
        strncpy(buf, "monetary", 8);
	break;
    case CCI_U_TYPE_FLOAT:
        strncpy(buf, "float", 5);
	break;
    case CCI_U_TYPE_DOUBLE:
        strncpy(buf, "double", 6);
	break;
    case CCI_U_TYPE_DATE:
        strncpy(buf, "date", 4);
	break;
    case CCI_U_TYPE_TIME:
        strncpy(buf, "time", 4);
	break;
    case CCI_U_TYPE_TIMESTAMP:
        strncpy(buf, "timestamp", 9);
	break;
    case CCI_U_TYPE_SET:
        strncpy(buf, "set", 3);
	break;
    case CCI_U_TYPE_MULTISET:
        strncpy(buf, "multiset", 8);
	break;
    case CCI_U_TYPE_SEQUENCE:
        strncpy(buf, "sequence", 8);
	break;
    case CCI_U_TYPE_OBJECT:
        strncpy(buf, "object", 6);
	break;
    case CCI_U_TYPE_BIGINT:
        strncpy(buf, "bigint", 6);
	break;
    case CCI_U_TYPE_DATETIME:
        strncpy(buf, "datetime", 8);
	break;
    case CCI_U_TYPE_BLOB:
        strncpy(buf, "blob", 4);
        break;
    case CCI_U_TYPE_CLOB:
        strncpy(buf, "clob", 4);
        break;
    default:
	/* should not enter here */
        strncpy(buf, "[unknown]", 9);
	return -1;
    }

    if (CCI_IS_SET_TYPE(column_info->type)) {
        strncpy(type_name, "set", type_name_len);
    } else if (CCI_IS_MULTISET_TYPE(column_info->type)) {
        strncpy(type_name, "multiset", type_name_len);
    } else if (CCI_IS_SEQUENCE_TYPE(column_info->type)) {
        strncpy(type_name, "sequence", type_name_len);
    } else {
        strncpy(type_name, buf, type_name_len);
    }

    return 0;
}

static int numeric_type(T_CCI_U_TYPE type)
{
    if (type == CCI_U_TYPE_NUMERIC || 
	type == CCI_U_TYPE_INT ||
	type == CCI_U_TYPE_SHORT || 
	type == CCI_U_TYPE_FLOAT ||
	type == CCI_U_TYPE_DOUBLE || 
	type == CCI_U_TYPE_BIGINT ||
	type == CCI_U_TYPE_MONETARY) {
	return 1;
    } else {
	return 0;
    }
}

static int get_cubrid_u_type_by_name(const char *type_name)
{
    int i;
    int size = sizeof(db_type_info) / sizeof(db_type_info[0]);

    for (i = 0; i < size; i++) {
	if (strcasecmp(type_name, db_type_info[i].type_name) == 0) {
	    return db_type_info[i].cubrid_u_type;
	}
    }

    return CCI_U_TYPE_UNKNOWN;
}

static int get_cubrid_u_type_len(T_CCI_U_TYPE type)
{
    int i;
    int size = sizeof(db_type_info) / sizeof(db_type_info[0]);
    DB_TYPE_INFO type_info;

    for (i = 0; i < size; i++) {
	type_info = db_type_info[i];
	if (type == type_info.cubrid_u_type) {
	    return type_info.len;
	}
    }

    return 0;
}

static int cubrid_lob_new(int con_h_id, T_CCI_LOB *lob, T_CCI_U_TYPE type, T_CCI_ERROR *err_buf)
{
    return (type == CCI_U_TYPE_BLOB) ? 
        cci_blob_new(con_h_id, lob, err_buf) : cci_clob_new(con_h_id, lob, err_buf);
}

static php_cubrid_int64_t cubrid_lob_size(T_CCI_LOB lob, T_CCI_U_TYPE type)
{
    return (type == CCI_U_TYPE_BLOB) ? cci_blob_size(lob) : cci_clob_size(lob);
}

static int cubrid_lob_write(int con_h_id, T_CCI_LOB lob, T_CCI_U_TYPE type, php_cubrid_int64_t start_pos, int length, const char *buf, T_CCI_ERROR *err_buf)
{
    return (type == CCI_U_TYPE_BLOB) ? 
        cci_blob_write(con_h_id, lob, start_pos, length, buf, err_buf) : 
        cci_clob_write(con_h_id, lob, start_pos, length, buf, err_buf);
}

static int cubrid_lob_read(int con_h_id, T_CCI_LOB lob, T_CCI_U_TYPE type, php_cubrid_int64_t start_pos, int length, char *buf, T_CCI_ERROR *err_buf)
{
    return (type == CCI_U_TYPE_BLOB) ?
        cci_blob_read(con_h_id, lob, start_pos, length, buf, err_buf) :
        cci_clob_read(con_h_id, lob, start_pos, length, buf, err_buf);
}

static int cubrid_lob_free(T_CCI_LOB lob, T_CCI_U_TYPE type)
{
    return (type == CCI_U_TYPE_BLOB) ?
        cci_blob_free(lob) : cci_clob_free(lob);
}

/* method learn from pdo */
static const char digit_vec[] = "0123456789";
static char *php_cubrid_int64_to_str(php_cubrid_int64_t i64 TSRMLS_DC)
{
    char buffer[65];
    char outbuf[65] = "";
    register char *p;
    long long_val;
    char *dst = outbuf;

    if (i64 < 0) {
        i64 = -i64;
        *dst++ = '-';
    }

    if (i64 == 0) {
        *dst++ = '0';
        *dst++ = '\0';
        return estrdup(outbuf);
    }

    p = &buffer[sizeof(buffer)-1];
    *p = '\0';

    while ((php_cubrid_uint64_t)i64 > (php_cubrid_uint64_t)LONG_MAX) {
        php_cubrid_uint64_t quo = (php_cubrid_uint64_t)i64 / (unsigned int)10;
        unsigned int rem = (unsigned int)(i64 - quo*10U);
        *--p = digit_vec[rem];
        i64 = (php_cubrid_int64_t)quo;
    }
    long_val = (long)i64;
    while (long_val != 0) {
        long quo = long_val / 10;
        *--p = digit_vec[(unsigned int)(long_val - quo * 10)];
        long_val = quo;
    }
    while ((*dst++ = *p++) != 0)
            ;
    *dst = '\0';
    return estrdup(outbuf);
}

static int cubrid_get_charset_internal(int conn, T_CCI_ERROR *error)
{
    char *query = "SELECT charset FROM db_root";
    char *buffer;

    int cubrid_retval = 0;
    int request_handle = 0;
    int ind, index = -1;

    if ((cubrid_retval = cci_prepare(conn, query, 0, error)) < 0) {
        goto ERR_CUBRID_GET_CHARSET_INTERNAL;
    }

    request_handle = cubrid_retval;

    if ((cubrid_retval = cci_execute(request_handle, CCI_EXEC_ASYNC, 0, error)) < 0) {
        goto ERR_CUBRID_GET_CHARSET_INTERNAL;
    }

    cubrid_retval = cci_cursor(request_handle, 1, CCI_CURSOR_CURRENT, error);
    if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
        goto ERR_CUBRID_GET_CHARSET_INTERNAL;
    }

    if ((cubrid_retval = cci_fetch(request_handle, error)) < 0) {
        goto ERR_CUBRID_GET_CHARSET_INTERNAL;
    }

    if ((cubrid_retval = cci_get_data(request_handle, 1, CCI_A_TYPE_STR, &buffer, &ind)) < 0) {
        goto ERR_CUBRID_GET_CHARSET_INTERNAL;
    }

    if (ind != -1) {
	index = atoi(buffer);
    } else {
	goto ERR_CUBRID_GET_CHARSET_INTERNAL;
    }

    if (index < 0 || index > MAX_DB_CHARSETS) {
	index = MAX_DB_CHARSETS;
    }

    cci_close_req_handle(request_handle);
    return index;

ERR_CUBRID_GET_CHARSET_INTERNAL:

    if (request_handle > 0) {
        cci_close_req_handle(request_handle);
    }

    return cubrid_retval;
}

static void php_cubrid_fetch_hash(INTERNAL_FUNCTION_PARAMETERS, long type, int is_object)
{
    zval *req_id = NULL, *ctor_params = NULL;
    zend_class_entry *ce = NULL;

    T_CUBRID_REQUEST *request;
    T_CCI_ERROR error;
    int cubrid_retval = 0;

    init_error();

    if (is_object) {
        char *class_name = NULL;
        int class_name_len = 0;

        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|sz",
                    &req_id, &class_name, &class_name_len, &ctor_params) == FAILURE) {
            return;
        }

        if (ZEND_NUM_ARGS() < 2) {
            ce = zend_standard_class_def;
        } else {
            ce = zend_fetch_class(class_name, class_name_len, ZEND_FETCH_CLASS_AUTO TSRMLS_CC);
        }

        if (!ce) {
            php_error_docref(NULL TSRMLS_CC, E_WARNING, "Could not find class '%s'", class_name);
            return;
        }
    } else {
        if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &req_id, &type) == FAILURE) {
            return;
        }

        if (!type) {
            type = CUBRID_BOTH;
        }

        if (type & CUBRID_OBJECT) {
            type |= CUBRID_ASSOC;
        }
    }

    ZEND_FETCH_RESOURCE(request, T_CUBRID_REQUEST *, &req_id, -1, "CUBRID-Request", le_request);

    init_error_link(request->conn);

    cubrid_retval = cci_cursor(request->handle, 0, CCI_CURSOR_CURRENT, &error);
    if (cubrid_retval == CCI_ER_NO_MORE_DATA) {
        if (request->field_lengths != NULL) {
            efree (request->field_lengths);
            request->field_lengths = NULL;
        }

        RETURN_FALSE;
    } else if (cubrid_retval < 0) {
        handle_error(cubrid_retval, &error, request->conn);
        return;
    }

    if ((cubrid_retval = cci_fetch(request->handle, &error)) < 0) {
        handle_error(cubrid_retval, &error, request->conn);
        return;
    }

    if ((cubrid_retval = fetch_a_row(return_value, request->conn->handle, request->handle, request->field_lengths, type TSRMLS_CC)) != SUCCESS) {
        handle_error(cubrid_retval, NULL, request->conn);
        return;
    }

    if (is_object) {
        zval dataset = *return_value;
        zend_fcall_info fci;
        zend_fcall_info_cache fcc;
        zval *retval_ptr;

        object_and_properties_init(return_value, ce, NULL);
        zend_merge_properties(return_value, Z_ARRVAL(dataset), 1 TSRMLS_CC);

        if (ce->constructor) {
            fci.size = sizeof(fci);
            fci.function_table = &ce->function_table;
            fci.function_name = NULL;
            fci.symbol_table = NULL;
#if PHP_MINOR_VERSION < 3
            fci.object_pp = &return_value;
#else
            fci.object_ptr = return_value;
#endif
            fci.retval_ptr_ptr = &retval_ptr;

            if (ctor_params && Z_TYPE_P(ctor_params) != IS_NULL) {
                if (Z_TYPE_P(ctor_params) == IS_ARRAY) {
                    HashTable *ht = Z_ARRVAL_P(ctor_params);
                    Bucket *p;

                    fci.param_count = 0;
                    fci.params = safe_emalloc(sizeof(zval*), ht->nNumOfElements, 0);
                    p = ht->pListHead;
                    while (p != NULL) {
                        fci.params[fci.param_count++] = (zval**)p->pData;
                        p = p->pListNext;
                    }
                } else {
                    zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Parameter ctor_params must be an array", 0 TSRMLS_CC);
                    return;
                }
            } else {
                fci.param_count = 0;
                fci.params = NULL;
            }

            fci.no_separation = 1;
            fcc.function_handler = ce->constructor;
            fcc.calling_scope = EG(scope);
#if PHP_MINOR_VERSION < 3
            fcc.object_pp = &return_value;
#else
            fcc.called_scope = Z_OBJCE_P(return_value);
            fcc.object_ptr = return_value;
#endif

            if (zend_call_function(&fci, &fcc TSRMLS_CC) == FAILURE) {
                zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC, 
                        "Could not execute %s::%s()", ce->name, ce->constructor->common.function_name);
            } else {
                if (retval_ptr) {
                    zval_ptr_dtor(&retval_ptr);
                }
            }

            if (fci.params) {
                efree(fci.params);
            }
        } else if (ctor_params) {
            zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,
                    "Class %s does not have a constructor hence you cannot use ctor_params", ce->name);
        }

        if (Z_TYPE_P(return_value) == IS_ARRAY) {
            object_and_properties_init(return_value, ZEND_STANDARD_CLASS_DEF_PTR, Z_ARRVAL_P(return_value));
        }
    }

    if (type & CUBRID_OBJECT) {
        if (return_value->type == IS_ARRAY) {
            convert_to_object(return_value);
        }
    }

    cubrid_retval = cci_cursor (request->handle, 1, CCI_CURSOR_CURRENT, &error);
    if (cubrid_retval < 0 && cubrid_retval != CCI_ER_NO_MORE_DATA) {
        handle_error(cubrid_retval, &error, request->conn);
        return;
    }
}

static void linked_list_append(LINKED_LIST *list, void *data)
{
    LINKED_LIST_NODE *new_node = (LINKED_LIST_NODE *) emalloc(sizeof(LINKED_LIST_NODE));

    if (!new_node) {
        return;
    }

    new_node->data = data;
    new_node->next = NULL;

    list->tail->next = new_node;
    list->tail = new_node;
}

static void linked_list_delete(LINKED_LIST *list, void *data)
{
    LINKED_LIST_NODE *p, *q;

    for (p = list->head, q = p->next; q != NULL; p = q, q = q->next) {
        if (q->data == data) {
            p->next = q->next;

            if (list->tail == q) {
                list->tail = p;
            }

            efree(q);
            break;
        }
    }
}
