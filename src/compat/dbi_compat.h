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
 * dbi.h - Definitions and function prototypes for the CUBRID Application Program Interface (API).
 */

#include <stdio.h>
#include <time.h>
#include <sys/types.h>

#include "dbtype_common.h"

#ifndef _DBI_COMPAT_H_
#define _DBI_COMPAT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define bool char

#ifdef NO_ERROR
#undef NO_ERROR
#endif

#define DB_TRUE 1
#define DB_FALSE 0

#define TRAN_ASYNC_WS_BIT                        0x10	/* 1 0000 */
#define TRAN_ISO_LVL_BITS                        0x0F	/* 0 1111 */

#define DB_AUTH_ALL \
  ((DB_AUTH) (DB_AUTH_SELECT | DB_AUTH_INSERT | DB_AUTH_UPDATE | DB_AUTH_DELETE | \
   DB_AUTH_ALTER  | DB_AUTH_INDEX  | DB_AUTH_EXECUTE))

/* It is strongly advised that applications use these macros for access
   to the fields of the DB_QUERY_ERROR structure */

#define DB_QUERY_ERROR_LINE(error) ((error)->err_lineno)
#define DB_QUERY_ERROR_CHAR(error) ((error)->err_posno)

/*  These are the status codes that can be returned by
    the functions that iterate over statement results. */
#define DB_CURSOR_SUCCESS      0
#define DB_CURSOR_END          1
#define DB_CURSOR_ERROR       -1

#define DB_IS_CONSTRAINT_UNIQUE_FAMILY(c) \
                                    ( ((c) == DB_CONSTRAINT_UNIQUE          || \
                                       (c) == DB_CONSTRAINT_REVERSE_UNIQUE  || \
                                       (c) == DB_CONSTRAINT_PRIMARY_KEY)       \
                                      ? true : false )

#define DB_IS_CONSTRAINT_INDEX_FAMILY(c) \
                                    ( (DB_IS_CONSTRAINT_UNIQUE_FAMILY(c)    || \
                                       (c) == DB_CONSTRAINT_INDEX           || \
                                       (c) == DB_CONSTRAINT_REVERSE_INDEX   || \
                                       (c) == DB_CONSTRAINT_FOREIGN_KEY)       \
                                      ? true : false )

#define DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY(c) \
                                    ( ((c) == DB_CONSTRAINT_REVERSE_UNIQUE  || \
                                       (c) == DB_CONSTRAINT_REVERSE_INDEX)     \
                                      ? true : false )

#define DB_IS_CONSTRAINT_FAMILY(c) \
                                    ( (DB_IS_CONSTRAINT_UNIQUE_FAMILY(c)    || \
                                       (c) == DB_CONSTRAINT_NOT_NULL        || \
                                       (c) == DB_CONSTRAINT_FOREIGN_KEY)       \
                                      ? true : false )

/* Volume purposes constants.  These are intended for use by the
   db_add_volext API function. */
  typedef enum
  {
    DB_PERMANENT_DATA_PURPOSE = 0,
    DB_TEMPORARY_DATA_PURPOSE = 1,	/* internal use only */
    DISK_UNKNOWN_PURPOSE = 2,	/* internal use only: Does not mean anything */
  } DB_VOLPURPOSE;

  typedef enum
  {
    DB_PERMANENT_VOLTYPE,
    DB_TEMPORARY_VOLTYPE
  } DB_VOLTYPE;

/* These are the status codes that can be returned by db_value_compare. */
  typedef enum
  {

    DB_SUBSET = -3,		/* strict subset for set types.  */
    DB_UNK = -2,		/* unknown */
    DB_LT = -1,			/* cannonical less than */
    DB_EQ = 0,			/* equal */
    DB_GT = 1,			/* cannonical greater than, */
    DB_NE = 2,			/* not equal because types incomparable */
    DB_SUPERSET = 3		/* strict superset for set types.  */
  } DB_VALUE_COMPARE_RESULT;

/* Object fetch and locking constants.  These are used to specify
   a lock mode when fetching objects using of the explicit fetch and
   lock functions. */
  typedef enum
  {
    DB_FETCH_READ = 0,		/* Read an object (class or instance) */
    DB_FETCH_WRITE = 1,		/* Update an object (class or instance) */
    DB_FETCH_DIRTY = 2,		/* Does not care about the state of the object (class or instance). Get it even if it
				 * is obsolete or if it becomes obsolete. INTERNAL USE ONLY */
    DB_FETCH_CLREAD_INSTREAD = 3,	/* Read the class and read an instance of class. This is to access an instance in
					 * shared mode Note class must be given INTERNAL USE ONLY */
    DB_FETCH_CLREAD_INSTWRITE = 4,	/* Read the class and update an instance of the class. Note class must be given
					 * This is for creation of instances INTERNAL USE ONLY */
    DB_FETCH_QUERY_READ = 5,	/* Read the class and query (read) all instances of the class. Note class must be given
				 * This is for SQL select INTERNAL USE ONLY */
    DB_FETCH_QUERY_WRITE = 6,	/* Read the class and query (read) all instances of the class and update some of those
				 * instances. Note class must be given This is for Query update (SQL update) or Query
				 * delete (SQL delete) INTERNAL USE ONLY */
    DB_FETCH_SCAN = 7,		/* Read the class for scan purpose The lock of the lock should be kept since the actual 
				 * access happens later. This is for loading an index. INTERNAL USE ONLY */
    DB_FETCH_EXCLUSIVE_SCAN = 8	/* Read the class for exclusive scan purpose The lock of the lock should be kept since
				 * the actual access happens later. This is for loading an index. INTERNAL USE ONLY */
  } DB_FETCH_MODE;

/* Authorization type identifier constants.  The numeric values of these
   are defined such that they can be used with the bitwise or operator
    "|" in order to specify more than one authorization type. */
  typedef enum
  {
    DB_AUTH_NONE = 0,
    DB_AUTH_SELECT = 1,
    DB_AUTH_INSERT = 2,
    DB_AUTH_UPDATE = 4,
    DB_AUTH_DELETE = 8,
    DB_AUTH_ALTER = 16,
    DB_AUTH_INDEX = 32,
    DB_AUTH_EXECUTE = 64
  } DB_AUTH;

/* object_id type constants used in a db_register_ldb api call to specify
   whether a local database supports intrinsic object identity or user-
   defined object identity. */
  typedef enum
  {
    DB_OID_INTRINSIC = 1,
    DB_OID_USER_DEFINED
  } DB_OBJECT_ID_TYPE;

/* These are abstract data type pointers used by the functions
   that issue SQL statements and return their results. */
  typedef struct db_query_result DB_QUERY_RESULT;
  typedef struct db_query_type DB_QUERY_TYPE;

/* Type of the column in SELECT list within DB_QUERY_TYPE structure */
  typedef enum
  {
    DB_COL_EXPR,
    DB_COL_VALUE,
    DB_COL_NAME,
    DB_COL_OID,
    DB_COL_PATH,
    DB_COL_FUNC,
    DB_COL_OTHER
  } DB_COL_TYPE;

  typedef enum db_class_modification_status
  {
    DB_CLASS_NOT_MODIFIED,
    DB_CLASS_MODIFIED,
    DB_CLASS_ERROR
  } DB_CLASS_MODIFICATION_STATUS;

  typedef enum
  {
    CUBRID_STMT_NONE = -1,
    CUBRID_STMT_ALTER_CLASS,
    CUBRID_STMT_ALTER_SERIAL,
    CUBRID_STMT_COMMIT_WORK,
    CUBRID_STMT_REGISTER_DATABASE,
    CUBRID_STMT_CREATE_CLASS,
    CUBRID_STMT_CREATE_INDEX,
    CUBRID_STMT_CREATE_TRIGGER,
    CUBRID_STMT_CREATE_SERIAL,
    CUBRID_STMT_DROP_DATABASE,
    CUBRID_STMT_DROP_CLASS,
    CUBRID_STMT_DROP_INDEX,
    CUBRID_STMT_DROP_LABEL,
    CUBRID_STMT_DROP_TRIGGER,
    CUBRID_STMT_DROP_SERIAL,
    CUBRID_STMT_EVALUATE,
    CUBRID_STMT_RENAME_CLASS,
    CUBRID_STMT_ROLLBACK_WORK,
    CUBRID_STMT_GRANT,
    CUBRID_STMT_REVOKE,
    CUBRID_STMT_UPDATE_STATS,
    CUBRID_STMT_INSERT,
    CUBRID_STMT_SELECT,
    CUBRID_STMT_UPDATE,
    CUBRID_STMT_DELETE,
    CUBRID_STMT_CALL,
    CUBRID_STMT_GET_ISO_LVL,
    CUBRID_STMT_GET_TIMEOUT,
    CUBRID_STMT_GET_OPT_LVL,
    CUBRID_STMT_SET_OPT_LVL,
    CUBRID_STMT_SCOPE,
    CUBRID_STMT_GET_TRIGGER,
    CUBRID_STMT_SET_TRIGGER,
    CUBRID_STMT_SAVEPOINT,
    CUBRID_STMT_PREPARE,
    CUBRID_STMT_ATTACH,
    CUBRID_STMT_USE,
    CUBRID_STMT_REMOVE_TRIGGER,
    CUBRID_STMT_RENAME_TRIGGER,
    CUBRID_STMT_ON_LDB,
    CUBRID_STMT_GET_LDB,
    CUBRID_STMT_SET_LDB,
    CUBRID_STMT_GET_STATS,
    CUBRID_STMT_CREATE_USER,
    CUBRID_STMT_DROP_USER,
    CUBRID_STMT_ALTER_USER,
    CUBRID_STMT_SET_SYS_PARAMS,
    CUBRID_STMT_ALTER_INDEX,

    CUBRID_STMT_CREATE_STORED_PROCEDURE,
    CUBRID_STMT_DROP_STORED_PROCEDURE,
    CUBRID_STMT_PREPARE_STATEMENT,
    CUBRID_STMT_EXECUTE_PREPARE,
    CUBRID_STMT_DEALLOCATE_PREPARE,
    CUBRID_STMT_TRUNCATE,
    CUBRID_STMT_DO,
    CUBRID_STMT_SELECT_UPDATE,
    CUBRID_STMT_SET_SESSION_VARIABLES,
    CUBRID_STMT_DROP_SESSION_VARIABLES,
    CUBRID_STMT_MERGE,
    CUBRID_STMT_SET_NAMES,
    CUBRID_STMT_ALTER_STORED_PROCEDURE,
    CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER = CUBRID_STMT_ALTER_STORED_PROCEDURE,
    CUBRID_STMT_KILL,

    CUBRID_MAX_STMT_TYPE
  } CUBRID_STMT_TYPE;

#define SQLX_CMD_TYPE CUBRID_STMT_TYPE

#define SQLX_CMD_ALTER_CLASS   CUBRID_STMT_ALTER_CLASS
#define SQLX_CMD_ALTER_SERIAL   CUBRID_STMT_ALTER_SERIAL
#define SQLX_CMD_COMMIT_WORK   CUBRID_STMT_COMMIT_WORK
#define SQLX_CMD_REGISTER_DATABASE   CUBRID_STMT_REGISTER_DATABASE
#define SQLX_CMD_CREATE_CLASS   CUBRID_STMT_CREATE_CLASS
#define SQLX_CMD_CREATE_INDEX   CUBRID_STMT_CREATE_INDEX
#define SQLX_CMD_CREATE_TRIGGER   CUBRID_STMT_CREATE_TRIGGER
#define SQLX_CMD_CREATE_SERIAL   CUBRID_STMT_CREATE_SERIAL
#define SQLX_CMD_DROP_DATABASE   CUBRID_STMT_DROP_DATABASE
#define SQLX_CMD_DROP_CLASS   CUBRID_STMT_DROP_CLASS
#define SQLX_CMD_DROP_INDEX   CUBRID_STMT_DROP_INDEX
#define SQLX_CMD_DROP_LABEL   CUBRID_STMT_DROP_LABEL
#define SQLX_CMD_DROP_TRIGGER   CUBRID_STMT_DROP_TRIGGER
#define SQLX_CMD_DROP_SERIAL   CUBRID_STMT_DROP_SERIAL
#define SQLX_CMD_EVALUATE   CUBRID_STMT_EVALUATE
#define SQLX_CMD_RENAME_CLASS   CUBRID_STMT_RENAME_CLASS
#define SQLX_CMD_ROLLBACK_WORK   CUBRID_STMT_ROLLBACK_WORK
#define SQLX_CMD_GRANT   CUBRID_STMT_GRANT
#define SQLX_CMD_REVOKE   CUBRID_STMT_REVOKE
#define SQLX_CMD_UPDATE_STATS   CUBRID_STMT_UPDATE_STATS
#define SQLX_CMD_INSERT   CUBRID_STMT_INSERT
#define SQLX_CMD_SELECT   CUBRID_STMT_SELECT
#define SQLX_CMD_UPDATE   CUBRID_STMT_UPDATE
#define SQLX_CMD_DELETE   CUBRID_STMT_DELETE
#define SQLX_CMD_CALL   CUBRID_STMT_CALL
#define SQLX_CMD_GET_ISO_LVL   CUBRID_STMT_GET_ISO_LVL
#define SQLX_CMD_GET_TIMEOUT   CUBRID_STMT_GET_TIMEOUT
#define SQLX_CMD_GET_OPT_LVL   CUBRID_STMT_GET_OPT_LVL
#define SQLX_CMD_SET_OPT_LVL   CUBRID_STMT_SET_OPT_LVL
#define SQLX_CMD_SCOPE   CUBRID_STMT_SCOPE
#define SQLX_CMD_GET_TRIGGER   CUBRID_STMT_GET_TRIGGER
#define SQLX_CMD_SET_TRIGGER   CUBRID_STMT_SET_TRIGGER
#define SQLX_CMD_SAVEPOINT   CUBRID_STMT_SAVEPOINT
#define SQLX_CMD_PREPARE   CUBRID_STMT_PREPARE
#define SQLX_CMD_ATTACH   CUBRID_STMT_ATTACH
#define SQLX_CMD_USE   CUBRID_STMT_USE
#define SQLX_CMD_REMOVE_TRIGGER   CUBRID_STMT_REMOVE_TRIGGER
#define SQLX_CMD_RENMAE_TRIGGER   CUBRID_STMT_RENAME_TRIGGER
#define SQLX_CMD_ON_LDB   CUBRID_STMT_ON_LDB
#define SQLX_CMD_GET_LDB   CUBRID_STMT_GET_LDB
#define SQLX_CMD_SET_LDB   CUBRID_STMT_SET_LDB
#define SQLX_CMD_GET_STATS   CUBRID_STMT_GET_STATS
#define SQLX_CMD_CREATE_USER   CUBRID_STMT_CREATE_USER
#define SQLX_CMD_DROP_USER   CUBRID_STMT_DROP_USER
#define SQLX_CMD_ALTER_USER   CUBRID_STMT_ALTER_USER
#define SQLX_CMD_SET_SYS_PARAMS   CUBRID_STMT_SET_SYS_PARAMS
#define SQLX_CMD_ALTER_INDEX   CUBRID_STMT_ALTER_INDEX

#define SQLX_CMD_CREATE_STORED_PROCEDURE   CUBRID_STMT_CREATE_STORED_PROCEDURE
#define SQLX_CMD_DROP_STORED_PROCEDURE   CUBRID_STMT_DROP_STORED_PROCEDURE
#define SQLX_CMD_PREPARE_STATEMENT  CUBRID_STMT_PREPARE_STATEMENT
#define SQLX_CMD_EXECUTE_PREPARE  CUBRID_STMT_EXECUTE_PREPARE
#define SQLX_CMD_DEALLOCATE_PREPARE  CUBRID_STMT_DEALLOCATE_PREPARE
#define SQLX_CMD_TRUNCATE  CUBRID_STMT_TRUNCATE
#define SQLX_CMD_DO  CUBRID_STMT_DO
#define SQLX_CMD_SELECT_UPDATE   CUBRID_STMT_SELECT_UPDATE
#define SQLX_CMD_SET_SESSION_VARIABLES  CUBRID_STMT_SET_SESSION_VARIABLES
#define SQLX_CMD_DROP_SESSION_VARIABLES  CUBRID_STMT_DROP_SESSION_VARIABLES
#define SQLX_CMD_STMT_MERGE  CUBRID_STMT_MERGE
#define SQLX_CMD_SET_NAMES   CUBRID_STMT_SET_NAMES
#define SQLX_CMD_ALTER_STORED_PROCEDURE   CUBRID_STMT_ALTER_STORED_PROCEDURE
#define SQLX_CMD_ALTER_STORED_PROCEDURE_OWNER   CUBRID_STMT_ALTER_STORED_PROCEDURE
#define SQLX_MAX_CMD_TYPE   CUBRID_MAX_STMT_TYPE

/* Structure used to contain information about the position of
   an error detected while compiling a statement. */
  typedef struct db_query_error DB_QUERY_ERROR;
  struct db_query_error
  {

    int err_lineno;		/* Line number where error occurred */
    int err_posno;		/* Position number where error occurred */
  };

/* ESQL/CSQL/API INTERFACE */
  typedef struct db_session DB_SESSION;
  typedef struct parser_node DB_NODE;
  typedef DB_NODE DB_SESSION_ERROR;
  typedef DB_NODE DB_SESSION_WARNING;
  typedef DB_NODE DB_PARAMETER;
  typedef DB_NODE DB_MARKER;
  typedef int STATEMENT_ID;

/* These are abstract data type pointers used by the "browsing" functions.
 * Currently they map directly onto internal unpublished data
 * structures but that are subject to change. API programs are
 * allowed to use them only for those API functions that
 * return them or accept them as arguments. API functions cannot
 * make direct structure references or make any assumptions about
 * the actual definition of these structures.
 */
  typedef struct sm_attribute DB_ATTRIBUTE;
  typedef struct sm_method DB_METHOD;
  typedef struct sm_method_argument DB_METHARG;
  typedef struct sm_method_file DB_METHFILE;
  typedef struct sm_resolution DB_RESOLUTION;
  typedef struct sm_query_spec DB_QUERY_SPEC;
  typedef struct tp_domain DB_DOMAIN;
  typedef struct tp_domain SM_DOMAIN;
  typedef struct tp_domain TP_DOMAIN;

/* These are handles to attribute and method descriptors that can
   be used for optimized lookup during repeated operations.
   They are NOT the same as the DB_ATTRIBUTE and DB_METHOD handles. */
  typedef struct sm_descriptor DB_ATTDESC;
  typedef struct sm_descriptor DB_METHDESC;

/* These structures are used for building editing templates on classes
 * and objects.  Templates allow the specification of multiple
 * operations to the object that are treated as an atomic unit.  If any
 * of the operations in the template fail, none of the operations
 * will be applied to the object.
 * They are defined as abstract data types on top of internal
 * data structures, API programs are not allowed to make assumptions
 * about the contents of these structures.
 */

  typedef struct sm_template DB_CTMPL;
  typedef struct obj_template DB_OTMPL;

/* Structure used to define statically linked methods. */
  typedef void (*METHOD_LINK_FUNCTION) ();
  typedef struct db_method_link DB_METHOD_LINK;
  struct db_method_link
  {

    const char *method;
    METHOD_LINK_FUNCTION function;

  };

/* Used to indicate the status of a trigger.
 * If a trigger is ACTIVE, it will be raised when its event is
 * detected.  If it is INACTIVE, it will not be raised.  If it is
 * INVALID, it indicates that the class associated with the trigger
 * has been deleted.
 */
  typedef enum
  {

    TR_STATUS_INVALID = 0,
    TR_STATUS_INACTIVE = 1,
    TR_STATUS_ACTIVE = 2
  } DB_TRIGGER_STATUS;


/* These define the possible trigger event types.
 * The system depends on the numeric order of these constants, do not
 * modify this definition without understanding the trigger manager
 * source.
 */
  typedef enum
  {

    /* common to both class cache & attribute cache */
    TR_EVENT_UPDATE = 0,
    TR_EVENT_STATEMENT_UPDATE = 1,

    /* class cache events */
    TR_EVENT_DELETE = 2,
    TR_EVENT_STATEMENT_DELETE = 3,
    TR_EVENT_INSERT = 4,
    TR_EVENT_STATEMENT_INSERT = 5,
    TR_EVENT_ALTER = 6,		/* currently unsupported */
    TR_EVENT_DROP = 7,		/* currently unsupported */

    /* user cache events */
    TR_EVENT_COMMIT = 8,
    TR_EVENT_ROLLBACK = 9,
    TR_EVENT_ABORT = 10,	/* currently unsupported */
    TR_EVENT_TIMEOUT = 11,	/* currently unsupported */

    /* default */
    TR_EVENT_NULL = 12,

    /* not really event, but used for processing */
    TR_EVENT_ALL = 13
  } DB_TRIGGER_EVENT;

/* These define the possible trigger activity times. Numeric order is
 * important here, don't change without understanding
 * the trigger manager source.
 */
  typedef enum
  {
    TR_TIME_NULL = 0,
    TR_TIME_BEFORE = 1,
    TR_TIME_AFTER = 2,
    TR_TIME_DEFERRED = 3
  } DB_TRIGGER_TIME;

/* These define the possible trigger action types. */
  typedef enum
  {
    TR_ACT_NULL = 0,		/* no action */
    TR_ACT_EXPRESSION = 1,	/* complex expression */
    TR_ACT_REJECT = 2,		/* REJECT action */
    TR_ACT_INVALIDATE = 3,	/* INVALIDATE TRANSACTION action */
    TR_ACT_PRINT = 4		/* PRINT action */
  } DB_TRIGGER_ACTION;

/* This is the generic pointer to database objects.  An object may be
 * either an instance or a class.  The actual structure is defined
 * elsewhere and it is not necessary for database applications to
 * understand its contents.
 */
  typedef struct db_object DB_OBJECT, *MOP;

/* Structure defining the common list link header used by the general
 * list routines.  Any structure in the db_ layer that are linked in
 * lists will follow this convention.
 */
  typedef struct db_list DB_LIST;
  struct db_list
  {

    struct db_list *next;

  };

/* List structure with an additional name field.
 * Used by: obsolete browsing functions
 *  pt_find_labels
 *  db_get_savepoints
 *  "object id" functions in SQL/M
 */
  typedef struct db_namelist DB_NAMELIST;

  struct db_namelist
  {
    struct db_namelist *next;
    const char *name;

  };

/* List structure with additional object pointer field.
   Might belong in dbtype.h but we rarely use object lists on the server. */
  typedef struct db_objlist DB_OBJLIST;
  typedef struct db_objlist *MOPLIST;

  struct db_objlist
  {
    struct db_objlist *next;
    struct db_object *op;

  };

  typedef struct sm_class_constraint DB_CONSTRAINT;


/* Types of constraints that may be applied to applibutes.  This type
   is used by the db_add_constraint()/db_drop_constraint() API functions. */
  typedef enum
  {
    DB_CONSTRAINT_UNIQUE = 0,
    DB_CONSTRAINT_INDEX = 1,
    DB_CONSTRAINT_NOT_NULL = 2,
    DB_CONSTRAINT_REVERSE_UNIQUE = 3,
    DB_CONSTRAINT_REVERSE_INDEX = 4,
    DB_CONSTRAINT_PRIMARY_KEY = 5,
    DB_CONSTRAINT_FOREIGN_KEY = 6
  } DB_CONSTRAINT_TYPE;

  typedef enum
  {
    DB_FK_DELETE = 0,
    DB_FK_UPDATE = 1
  } DB_FK_ACTION_TYPE;

  typedef enum
  {
    DB_INSTANCE_OF_A_CLASS = 'a',
    DB_INSTANCE_OF_A_PROXY = 'b',
    DB_INSTANCE_OF_A_VCLASS_OF_A_CLASS = 'c',
    DB_INSTANCE_OF_A_VCLASS_OF_A_PROXY = 'd',
    DB_INSTANCE_OF_NONUPDATABLE_OBJECT = 'e'
  } DB_OBJECT_TYPE;

/* session state id */
  typedef unsigned int SESSION_ID;
/* uninitialized value for session id */
#define DB_EMPTY_SESSION 0
/* uninitialized value for row count */
#define DB_ROW_COUNT_NOT_SET -2

  enum tz_region_type
  {
    TZ_REGION_OFFSET = 0,
    TZ_REGION_ZONE = 1
  };
  typedef enum tz_region_type TZ_REGION_TYPE;

  typedef struct tz_region TZ_REGION;
  struct tz_region
  {
    TZ_REGION_TYPE type;	/* 0 : offset ; 1 : zone */
    union
    {
      int offset;		/* in seconds */
      unsigned int zone_id;	/* geographical zone id */
    };
  };

  extern void db_date_decode (const DB_DATE * date, int *monthp, int *dayp, int *yearp);
  extern int db_date_weekday (DB_DATE * date);
  extern int db_date_to_string (char *buf, int bufsize, DB_DATE * date);
  extern bool db_string_check_explicit_date (const char *str, int str_len);
  extern int db_string_to_date (const char *buf, DB_DATE * date);
  extern int db_string_to_date_ex (const char *buf, int str_len, DB_DATE * date);
  extern int db_date_parse_date (char const *str, int str_len, DB_DATE * date);

/* DB_DATETIME functions */
  extern int db_datetime_encode (DB_DATETIME * datetime, int month, int day, int year, int hour, int minute, int second,
				 int millisecond);
  extern int db_datetime_decode (const DB_DATETIME * datetime, int *month, int *day, int *year, int *hour, int *minute,
				 int *second, int *millisecond);
  extern int db_datetime_to_string (char *buf, int bufsize, DB_DATETIME * datetime);
  extern int db_datetimetz_to_string (char *buf, int bufsize, DB_DATETIME * dt, const TZ_ID * tz_id);
  extern int db_datetimeltz_to_string (char *buf, int bufsize, DB_DATETIME * dt);
  extern int db_datetime_to_string2 (char *buf, int bufsize, DB_DATETIME * datetime);
  extern int db_string_to_datetime (const char *str, DB_DATETIME * datetime);
  extern int db_string_to_datetime_ex (const char *str, int str_len, DB_DATETIME * datetime);
  extern int db_string_to_datetimetz (const char *str, DB_DATETIMETZ * dt_tz, bool * has_zone);
  extern int db_string_to_datetimetz_ex (const char *str, int str_len, DB_DATETIMETZ * dt_tz, bool * has_zone);
  extern int db_string_to_datetimeltz (const char *str, DB_DATETIME * datetime);
  extern int db_string_to_datetimeltz_ex (const char *str, int str_len, DB_DATETIME * datetime);
  extern int db_date_parse_datetime_parts (char const *str, int str_len, DB_DATETIME * date, bool * is_explicit_time,
					   bool * has_explicit_msec, bool * fits_as_timestamp, char const **endp);
  extern int db_date_parse_datetime (char const *str, int str_len, DB_DATETIME * datetime);
  extern int db_subtract_int_from_datetime (DB_DATETIME * dt1, DB_BIGINT i2, DB_DATETIME * result_datetime);
  extern int db_add_int_to_datetime (DB_DATETIME * datetime, DB_BIGINT i2, DB_DATETIME * result_datetime);
/* DB_TIMESTAMP functions */
  extern int db_timestamp_encode (DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval);
  extern int db_timestamp_encode_ses (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime,
				      TZ_ID * dest_tz_id);
  extern int db_timestamp_encode_utc (const DB_DATE * date, const DB_TIME * timeval, DB_TIMESTAMP * utime);
  extern int db_timestamp_decode_ses (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval);
  extern void db_timestamp_decode_utc (const DB_TIMESTAMP * utime, DB_DATE * date, DB_TIME * timeval);
  extern int db_timestamp_decode_w_reg (const DB_TIMESTAMP * utime, const TZ_REGION * tz_region, DB_DATE * date,
					DB_TIME * timeval);
  extern int db_timestamp_decode_w_tz_id (const DB_TIMESTAMP * utime, const TZ_ID * tz_id, DB_DATE * date,
					  DB_TIME * timeval);
  extern int db_timestamp_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime);
  extern int db_timestamptz_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime, const TZ_ID * tz_id);
  extern int db_timestampltz_to_string (char *buf, int bufsize, DB_TIMESTAMP * utime);
  extern int db_string_to_timestamp (const char *buf, DB_TIMESTAMP * utime);
  extern int db_string_to_timestamp_ex (const char *buf, int buf_len, DB_TIMESTAMP * utime);
  extern int db_date_parse_timestamp (char const *str, int str_len, DB_TIMESTAMP * utime);
  extern int db_string_to_timestamptz (const char *str, DB_TIMESTAMPTZ * ts_tz, bool * has_zone);
  extern int db_string_to_timestamptz_ex (const char *str, int str_len, DB_TIMESTAMPTZ * ts_tz, bool * has_zone);
  extern int db_string_to_timestampltz (const char *str, DB_TIMESTAMP * ts);
  extern int db_string_to_timestampltz_ex (const char *str, int str_len, DB_TIMESTAMP * ts);

/* DB_TIME functions */
  extern int db_time_encode (DB_TIME * timeval, int hour, int minute, int second);
  extern void db_time_decode (DB_TIME * timeval, int *hourp, int *minutep, int *secondp);
  extern int db_time_to_string (char *buf, int bufsize, DB_TIME * dbtime);
  extern int db_timetz_to_string (char *buf, int bufsize, DB_TIME * dbtime, const TZ_ID * tz_id);
  extern int db_timeltz_to_string (char *buf, int bufsize, DB_TIME * time);
  extern bool db_string_check_explicit_time (const char *str, int str_len);
  extern int db_string_to_time (const char *buf, DB_TIME * dbtime);
  extern int db_string_to_time_ex (const char *buf, int buf_len, DB_TIME * dbtime);
  extern int db_string_to_timetz (const char *buf, DB_TIMETZ * time_tz, bool * has_zone);
  extern int db_string_to_timetz_ex (const char *buf, int buf_len, DB_TIMETZ * time_tz, bool * has_zone);
  extern int db_string_to_timeltz (const char *buf, DB_TIME * time);
  extern int db_string_to_timeltz_ex (const char *buf, int buf_len, DB_TIME * time);
  extern int db_date_parse_time (char const *str, int str_len, DB_TIME * time, int *milisec);

/* Unix-like functions */
  extern time_t db_mktime (DB_DATE * date, DB_TIME * timeval);
  extern int db_strftime (char *s, int smax, const char *fmt, DB_DATE * date, DB_TIME * timeval);
  extern void db_localtime (time_t * epoch_time, DB_DATE * date, DB_TIME * timeval);
  extern void db_localdatetime (time_t * epoch_time, DB_DATETIME * datetime);


/* generic calculation functions */
  extern int julian_encode (int m, int d, int y);
  extern void julian_decode (int jul, int *monthp, int *dayp, int *yearp, int *weekp);
  extern int day_of_week (int jul_day);
  extern bool is_leap_year (int year);
  extern int db_tm_encode (struct tm *c_time_struct, DB_DATE * date, DB_TIME * timeval);
  extern int db_get_day_of_year (int year, int month, int day);
  extern int db_get_day_of_week (int year, int month, int day);
  extern int db_get_week_of_year (int year, int month, int day, int mode);
  extern int db_check_time_date_format (const char *format_s);
  extern int db_add_weeks_and_days_to_date (int *day, int *month, int *year, int weeks, int day_week);

/* DB_ELO function */
  extern int db_create_fbo (DB_VALUE * value, DB_TYPE type);
  extern int db_elo_copy_structure (const DB_ELO * src, DB_ELO * dest);
  extern void db_elo_free_structure (DB_ELO * elo);

  extern int db_elo_copy (const DB_ELO * src, DB_ELO * dest);
  extern int db_elo_delete (DB_ELO * elo);

  extern int64_t db_elo_size (DB_ELO * elo);
  extern int db_elo_read (const DB_ELO * elo, int64_t pos, void *buf, size_t count);
  extern int db_elo_write (DB_ELO * elo, int64_t pos, void *buf, size_t count);

/* Unix-like functions */
  extern time_t db_mktime (DB_DATE * date, DB_TIME * timeval);
  extern int db_strftime (char *s, int smax, const char *fmt, DB_DATE * date, DB_TIME * timeval);
  extern void db_localtime (time_t * epoch_time, DB_DATE * date, DB_TIME * timeval);

/* generic calculation functions */
  extern int db_tm_encode (struct tm *c_time_struct, DB_DATE * date, DB_TIME * timeval);



  typedef struct cache_time CACHE_TIME;
  struct cache_time
  {
    int sec;
    int usec;
  };

#define CACHE_TIME_EQ(T1, T2)               \
        (((T1)->sec != 0) &&                \
         ((T1)->sec == (T2)->sec) &&        \
         ((T1)->usec == (T2)->usec))

#define CACHE_TIME_RESET(T)     \
        do {                    \
          (T)->sec = 0;         \
          (T)->usec = 0;        \
        } while (0)

#define CACHE_TIME_MAKE(CT, TV)         \
        do {                            \
          (CT)->sec = (TV)->tv_sec;     \
          (CT)->usec = (TV)->tv_usec;   \
        } while (0)

#define OR_CACHE_TIME_SIZE      (OR_INT_SIZE * 2)

#define OR_PACK_CACHE_TIME(PTR, T)                      \
        do {                                            \
          if ((CACHE_TIME *) (T) != NULL) {                                      \
            PTR = or_pack_int(PTR, (T)->sec);        \
            PTR = or_pack_int(PTR, (T)->usec);       \
          }                                             \
          else {                                        \
            PTR = or_pack_int(PTR, 0);                  \
            PTR = or_pack_int(PTR, 0);                  \
          }                                             \
        } while (0)

#define OR_UNPACK_CACHE_TIME(PTR, T)                    \
        do {                                            \
          if ((CACHE_TIME *) (T) != NULL) {                                      \
            PTR = or_unpack_int(PTR, &((T)->sec));      \
            PTR = or_unpack_int(PTR, &((T)->usec));     \
          }                                             \
        } while (0)



  extern bool db_is_client_cache_reusable (DB_QUERY_RESULT * result);
  extern int db_query_seek_tuple (DB_QUERY_RESULT * result, int offset, int seek_mode);
  extern int db_query_get_cache_time (DB_QUERY_RESULT * result, CACHE_TIME * cache_time);


  typedef enum
  {
    TRAN_UNKNOWN_ISOLATION = 0x00,	/* 0 0000 */

    TRAN_READ_COMMITTED = 0x04,	/* 0 0100 */
    TRAN_REP_CLASS_COMMIT_INSTANCE = 0x04,	/* Alias of above */
    TRAN_CURSOR_STABILITY = 0x04,	/* Alias of above */

    TRAN_REPEATABLE_READ = 0x05,	/* 0 0101 */
    TRAN_REP_READ = 0x05,	/* Alias of above */
    TRAN_REP_CLASS_REP_INSTANCE = 0x05,	/* Alias of above */
    TRAN_DEGREE_2_9999_CONSISTENCY = 0x05,	/* Alias of above */

    TRAN_SERIALIZABLE = 0x06,	/* 0 0110 */
    TRAN_DEGREE_3_CONSISTENCY = 0x06,	/* Alias of above */
    TRAN_NO_PHANTOM_READ = 0x06,	/* Alias of above */

    TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,
    MVCC_TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,

    TRAN_MINVALUE_ISOLATION = 0x04,	/* internal use only */
    TRAN_MAXVALUE_ISOLATION = 0x06	/* internal use only */
  } DB_TRAN_ISOLATION;

/* Memory reclamation functions */
  extern void db_objlist_free (DB_OBJLIST * list);
  extern void db_string_free (char *string);

/* Session control */
  extern int db_auth_login (char *signed_data, int len);
  extern int db_auth_logout (void);

  extern int db_login (const char *name, const char *password);
  extern int db_restart (const char *program, int print_version, const char *volume);
  extern int db_restart_ex (const char *program, const char *db_name, const char *db_user, const char *db_password,
			    const char *preferred_hosts, int client_type);
  extern SESSION_ID db_get_session_id (void);
  extern void db_set_session_id (const SESSION_ID session_id);
  extern int db_end_session (void);
  extern int db_find_or_create_session (const char *db_user, const char *program_name);
  extern int db_get_row_count_cache (void);
  extern void db_update_row_count_cache (const int row_count);
  extern int db_get_row_count (int *row_count);
  extern int db_get_last_insert_id (DB_VALUE * value);
  extern int db_get_variable (DB_VALUE * name, DB_VALUE * value);
  extern int db_shutdown (void);
  extern int db_ping_server (int client_val, int *server_val);
  extern int db_disable_modification (void);
  extern int db_enable_modification (void);
  extern int db_commit_transaction (void);
  extern int db_abort_transaction (void);
  extern int db_commit_is_needed (void);
  extern int db_savepoint_transaction (const char *savepoint_name);
  extern int db_abort_to_savepoint (const char *savepoint_name);
  extern int db_set_global_transaction_info (int gtrid, void *info, int size);
  extern int db_get_global_transaction_info (int gtrid, void *buffer, int size);
  extern int db_2pc_start_transaction (void);
  extern int db_2pc_prepare_transaction (void);
  extern int db_2pc_prepared_transactions (int gtrids[], int size);
  extern int db_2pc_prepare_to_commit_transaction (int gtrid);
  extern int db_2pc_attach_transaction (int gtrid);
  extern void db_set_interrupt (int set);
  extern int db_set_suppress_repl_on_transaction (int set);
  extern int db_freepgs (const char *vlabel);
  extern int db_totalpgs (const char *vlabel);
  extern char *db_vol_label (int volid, char *vol_fullname);
  extern void db_warnspace (const char *vlabel);
  extern int db_add_volume (const char *ext_path, const char *ext_name, const char *ext_comments, const int ext_npages,
			    const DB_VOLPURPOSE ext_purpose);
  extern int db_num_volumes (void);
  extern void db_print_stats (void);

  extern void db_preload_classes (const char *name1, ...);
  extern void db_link_static_methods (DB_METHOD_LINK * methods);
  extern void db_unlink_static_methods (DB_METHOD_LINK * methods);
  extern void db_flush_static_methods (void);

  extern const char *db_error_string (int level);
  extern int db_error_code (void);
  extern int db_error_init (const char *logfile);

  extern int db_set_lock_timeout (int seconds);
  extern int db_set_isolation (DB_TRAN_ISOLATION isolation);
  extern void db_synchronize_cache (void);
  extern void db_get_tran_settings (int *lock_wait, DB_TRAN_ISOLATION * tran_isolation);

/* Authorization */
  extern DB_OBJECT *db_get_user (void);
  extern DB_OBJECT *db_get_owner (DB_OBJECT * classobj);
  extern char *db_get_user_name (void);
  extern char *db_get_user_and_host_name (void);
  extern DB_OBJECT *db_find_user (const char *name);
  extern int db_find_user_to_drop (const char *name, DB_OBJECT ** user);
  extern DB_OBJECT *db_add_user (const char *name, int *exists);
  extern int db_drop_user (DB_OBJECT * user);
  extern int db_add_member (DB_OBJECT * user, DB_OBJECT * member);
  extern int db_drop_member (DB_OBJECT * user, DB_OBJECT * member);
  extern int db_set_password (DB_OBJECT * user, const char *oldpass, const char *newpass);
  extern int db_set_user_comment (DB_OBJECT * user, const char *comment);
  extern int db_grant (DB_OBJECT * user, DB_OBJECT * classobj, DB_AUTH auth, int grant_option);
  extern int db_revoke (DB_OBJECT * user, DB_OBJECT * classobj, DB_AUTH auth);
  extern int db_check_authorization (DB_OBJECT * op, DB_AUTH auth);
  extern int db_check_authorization_and_grant_option (MOP op, DB_AUTH auth);
  extern int db_get_class_privilege (DB_OBJECT * op, unsigned int *auth);

/*  Serial value manipulation */
  extern int db_get_serial_current_value (const char *serial_name, DB_VALUE * serial_value);
  extern int db_get_serial_next_value (const char *serial_name, DB_VALUE * serial_value);
  extern int db_get_serial_next_value_ex (const char *serial_name, DB_VALUE * serial_value, int num_alloc);

/* Instance manipulation */
  extern DB_OBJECT *db_create (DB_OBJECT * obj);
  extern DB_OBJECT *db_create_by_name (const char *name);
  extern int db_get (DB_OBJECT * object, const char *attpath, DB_VALUE * value);
  extern int db_put (DB_OBJECT * obj, const char *name, DB_VALUE * value);
  extern int db_drop (DB_OBJECT * obj);
  extern int db_get_expression (DB_OBJECT * object, const char *expression, DB_VALUE * value);
  extern void db_print (DB_OBJECT * obj);
  extern void db_fprint (FILE * fp, DB_OBJECT * obj);
  extern DB_OBJECT *db_find_unique (DB_OBJECT * classobj, const char *attname, DB_VALUE * value);
  extern DB_OBJECT *db_find_unique_write_mode (DB_OBJECT * classobj, const char *attname, DB_VALUE * value);
  extern DB_OBJECT *db_find_multi_unique (DB_OBJECT * classobj, int size, char *attnames[], DB_VALUE * values[],
					  DB_FETCH_MODE purpose);
  extern DB_OBJECT *db_dfind_unique (DB_OBJECT * classobj, DB_ATTDESC * attdesc, DB_VALUE * value,
				     DB_FETCH_MODE purpose);
  extern DB_OBJECT *db_dfind_multi_unique (DB_OBJECT * classobj, int size, DB_ATTDESC * attdesc[], DB_VALUE * values[],
					   DB_FETCH_MODE purpose);
  extern DB_OBJECT *db_find_primary_key (MOP classmop, const DB_VALUE ** values, int size, DB_FETCH_MODE purpose);

  extern int db_send (DB_OBJECT * obj, const char *name, DB_VALUE * returnval, ...);
  extern int db_send_arglist (DB_OBJECT * obj, const char *name, DB_VALUE * returnval, DB_VALUE_LIST * args);
  extern int db_send_argarray (DB_OBJECT * obj, const char *name, DB_VALUE * returnval, DB_VALUE ** args);

/* Explicit lock & fetch functions */
  extern int db_lock_read (DB_OBJECT * op);
  extern int db_lock_write (DB_OBJECT * op);

  extern int db_fetch_array (DB_OBJECT ** objects, DB_FETCH_MODE mode, int quit_on_error);
  extern int db_fetch_list (DB_OBJLIST * objects, DB_FETCH_MODE mode, int quit_on_error);
  extern int db_fetch_set (DB_COLLECTION * set, DB_FETCH_MODE mode, int quit_on_error);
  extern int db_fetch_seq (DB_SEQ * set, DB_FETCH_MODE mode, int quit_on_error);
  extern int db_fetch_composition (DB_OBJECT * object, DB_FETCH_MODE mode, int max_level, int quit_on_error);

/* Class definition */
  extern DB_OBJECT *db_create_class (const char *name);
  extern DB_OBJECT *db_create_vclass (const char *name);
  extern int db_drop_class (DB_OBJECT * classobj);
  extern int db_drop_class_ex (DB_OBJECT * classobj, bool is_cascade_constraints);
  extern int db_rename_class (DB_OBJECT * classobj, const char *new_name);

  extern int db_add_index (DB_OBJECT * classobj, const char *attname);
  extern int db_drop_index (DB_OBJECT * classobj, const char *attname);

  extern int db_add_super (DB_OBJECT * classobj, DB_OBJECT * super);
  extern int db_drop_super (DB_OBJECT * classobj, DB_OBJECT * super);
  extern int db_drop_super_connect (DB_OBJECT * classobj, DB_OBJECT * super);

  extern int db_rename (DB_OBJECT * classobj, const char *name, int class_namespace, const char *newname);

  extern int db_add_attribute (DB_OBJECT * obj, const char *name, const char *domain, DB_VALUE * default_value);
  extern int db_add_shared_attribute (DB_OBJECT * obj, const char *name, const char *domain, DB_VALUE * default_value);
  extern int db_add_class_attribute (DB_OBJECT * obj, const char *name, const char *domain, DB_VALUE * default_value);
  extern int db_add_set_attribute_domain (DB_OBJECT * classobj, const char *name, int class_attribute,
					  const char *domain);
  extern int db_drop_attribute (DB_OBJECT * classobj, const char *name);
  extern int db_drop_class_attribute (DB_OBJECT * classobj, const char *name);
  extern int db_change_default (DB_OBJECT * classobj, const char *name, DB_VALUE * value);

  extern int db_constrain_non_null (DB_OBJECT * classobj, const char *name, int class_attribute, int on_or_off);
  extern int db_constrain_unique (DB_OBJECT * classobj, const char *name, int on_or_off);
  extern int db_add_method (DB_OBJECT * classobj, const char *name, const char *implementation);
  extern int db_add_class_method (DB_OBJECT * classobj, const char *name, const char *implementation);
  extern int db_drop_method (DB_OBJECT * classobj, const char *name);
  extern int db_drop_class_method (DB_OBJECT * classobj, const char *name);
  extern int db_add_argument (DB_OBJECT * classobj, const char *name, int class_method, int arg_index,
			      const char *domain);
  extern int db_add_set_argument_domain (DB_OBJECT * classobj, const char *name, int class_method, int arg_index,
					 const char *domain);
  extern int db_change_method_implementation (DB_OBJECT * classobj, const char *name, int class_method,
					      const char *newname);
  extern int db_set_loader_commands (DB_OBJECT * classobj, const char *commands);
  extern int db_add_method_file (DB_OBJECT * classobj, const char *name);
  extern int db_drop_method_file (DB_OBJECT * classobj, const char *name);
  extern int db_drop_method_files (DB_OBJECT * classobj);

  extern int db_add_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name, const char *alias);
  extern int db_add_class_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name, const char *alias);
  extern int db_drop_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name);
  extern int db_drop_class_resolution (DB_OBJECT * classobj, DB_OBJECT * super, const char *name);
  extern int db_add_constraint (MOP classmop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
				const char **att_names, int class_attributes);
  extern int db_drop_constraint (MOP classmop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
				 const char **att_names, int class_attributes);

/* Browsing functions */
  extern char *db_get_database_name (void);
  extern const char *db_get_database_comments (void);
  extern void db_set_client_type (int client_type);
  extern void db_set_preferred_hosts (const char *hosts);
  extern int db_get_client_type (void);
  extern const char *db_get_type_name (DB_TYPE type_id);
  extern DB_TYPE db_type_from_string (const char *name);
  extern int db_get_schema_def_dbval (DB_VALUE * result, DB_VALUE * name_val);
  extern const char *db_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

  extern DB_OBJECT *db_find_class_of_index (const char *index, DB_CONSTRAINT_TYPE type);
  extern DB_OBJECT *db_find_class (const char *name);
  extern DB_OBJECT *db_get_class (DB_OBJECT * obj);
  extern DB_OBJLIST *db_get_all_objects (DB_OBJECT * classobj);
  extern DB_OBJLIST *db_get_all_classes (void);
  extern DB_OBJLIST *db_get_base_classes (void);
  extern DB_OBJLIST *db_fetch_all_objects (DB_OBJECT * op, DB_FETCH_MODE mode);
  extern DB_OBJLIST *db_fetch_all_classes (DB_FETCH_MODE mode);
  extern DB_OBJLIST *db_fetch_base_classes (DB_FETCH_MODE mode);

  extern int db_is_class (DB_OBJECT * obj);
  extern int db_is_any_class (DB_OBJECT * obj);
  extern int db_is_instance (DB_OBJECT * obj);
  extern int db_is_instance_of (DB_OBJECT * obj, DB_OBJECT * classobj);
  extern int db_is_subclass (DB_OBJECT * classobj, DB_OBJECT * supermop);
  extern int db_is_superclass (DB_OBJECT * supermop, DB_OBJECT * classobj);
  extern int db_is_partition (DB_OBJECT * classobj, DB_OBJECT * superobj);
  extern int db_is_system_class (DB_OBJECT * op);
  extern int db_is_deleted (DB_OBJECT * obj);

  extern const char *db_get_class_name (DB_OBJECT * classobj);
  extern DB_OBJLIST *db_get_superclasses (DB_OBJECT * obj);
  extern DB_OBJLIST *db_get_subclasses (DB_OBJECT * obj);
  extern DB_ATTRIBUTE *db_get_attribute (DB_OBJECT * obj, const char *name);
  extern DB_ATTRIBUTE *db_get_attribute_by_name (const char *class_name, const char *attribute_name);
  extern DB_ATTRIBUTE *db_get_attributes (DB_OBJECT * obj);
  extern DB_ATTRIBUTE *db_get_class_attribute (DB_OBJECT * obj, const char *name);
  extern DB_ATTRIBUTE *db_get_class_attributes (DB_OBJECT * obj);
  extern DB_METHOD *db_get_method (DB_OBJECT * obj, const char *name);
  extern DB_METHOD *db_get_class_method (DB_OBJECT * obj, const char *name);
  extern DB_METHOD *db_get_methods (DB_OBJECT * obj);
  extern DB_METHOD *db_get_class_methods (DB_OBJECT * obj);
  extern DB_RESOLUTION *db_get_resolutions (DB_OBJECT * obj);
  extern DB_RESOLUTION *db_get_class_resolutions (DB_OBJECT * obj);
  extern DB_METHFILE *db_get_method_files (DB_OBJECT * obj);
  extern const char *db_get_loader_commands (DB_OBJECT * obj);

  extern DB_TYPE db_attribute_type (DB_ATTRIBUTE * attribute);
  extern DB_ATTRIBUTE *db_attribute_next (DB_ATTRIBUTE * attribute);
  extern const char *db_attribute_name (DB_ATTRIBUTE * attribute);
  extern int db_attribute_id (DB_ATTRIBUTE * attribute);
  extern int db_attribute_order (DB_ATTRIBUTE * attribute);
  extern DB_DOMAIN *db_attribute_domain (DB_ATTRIBUTE * attribute);
  extern DB_OBJECT *db_attribute_class (DB_ATTRIBUTE * attribute);
  extern DB_VALUE *db_attribute_default (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_unique (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_primary_key (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_foreign_key (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_auto_increment (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_reverse_unique (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_non_null (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_indexed (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_reverse_indexed (DB_ATTRIBUTE * attribute);
  extern int db_attribute_is_shared (DB_ATTRIBUTE * attribute);
  extern int db_attribute_length (DB_ATTRIBUTE * attribute);
  extern DB_DOMAIN *db_type_to_db_domain (DB_TYPE type);

  extern DB_DOMAIN *db_domain_next (const DB_DOMAIN * domain);
  extern DB_TYPE db_domain_type (const DB_DOMAIN * domain);
  extern DB_OBJECT *db_domain_class (const DB_DOMAIN * domain);
  extern DB_DOMAIN *db_domain_set (const DB_DOMAIN * domain);
  extern int db_domain_precision (const DB_DOMAIN * domain);
  extern int db_domain_scale (const DB_DOMAIN * domain);
  extern int db_domain_codeset (const DB_DOMAIN * domain);

  extern DB_METHOD *db_method_next (DB_METHOD * method);
  extern const char *db_method_name (DB_METHOD * method);
  extern const char *db_method_function (DB_METHOD * method);
  extern DB_OBJECT *db_method_class (DB_METHOD * method);
  extern DB_DOMAIN *db_method_return_domain (DB_METHOD * method);
  extern DB_DOMAIN *db_method_arg_domain (DB_METHOD * method, int arg);
  extern int db_method_arg_count (DB_METHOD * method);

  extern DB_RESOLUTION *db_resolution_next (DB_RESOLUTION * resolution);
  extern DB_OBJECT *db_resolution_class (DB_RESOLUTION * resolution);
  extern const char *db_resolution_name (DB_RESOLUTION * resolution);
  extern const char *db_resolution_alias (DB_RESOLUTION * resolution);
  extern int db_resolution_isclass (DB_RESOLUTION * resolution);

  extern DB_METHFILE *db_methfile_next (DB_METHFILE * methfile);
  extern const char *db_methfile_name (DB_METHFILE * methfile);

  extern DB_OBJLIST *db_objlist_next (DB_OBJLIST * link);
  extern DB_OBJECT *db_objlist_object (DB_OBJLIST * link);


  extern int db_get_class_num_objs_and_pages (DB_OBJECT * classmop, int approximation, int *nobjs, int *npages);
  extern int db_get_btree_statistics (DB_CONSTRAINT * cons, int *num_leaf_pages, int *num_total_pages, int *num_keys,
				      int *height);

/* Constraint Functions */
  extern DB_CONSTRAINT *db_get_constraints (DB_OBJECT * obj);
  extern DB_CONSTRAINT *db_constraint_next (DB_CONSTRAINT * constraint);
  extern DB_CONSTRAINT *db_constraint_find_primary_key (DB_CONSTRAINT * constraint);
  extern DB_CONSTRAINT_TYPE db_constraint_type (DB_CONSTRAINT * constraint);
  extern const char *db_constraint_name (DB_CONSTRAINT * constraint);
  extern DB_ATTRIBUTE **db_constraint_attributes (DB_CONSTRAINT * constraint);
  extern const int *db_constraint_asc_desc (DB_CONSTRAINT * constraint);

  extern const char *db_get_foreign_key_action (DB_CONSTRAINT * constraint, DB_FK_ACTION_TYPE type);
  extern DB_OBJECT *db_get_foreign_key_ref_class (DB_CONSTRAINT * constraint);

/* Trigger functions */
  extern DB_OBJECT *db_create_trigger (const char *name, DB_TRIGGER_STATUS status, double priority,
				       DB_TRIGGER_EVENT event, DB_OBJECT * class_obj, const char *attr,
				       DB_TRIGGER_TIME cond_time, const char *cond_source, DB_TRIGGER_TIME action_time,
				       DB_TRIGGER_ACTION action_type, const char *action_source);

  extern int db_drop_trigger (DB_OBJECT * obj);
  extern int db_rename_trigger (DB_OBJECT * obj, const char *newname);

  extern DB_OBJECT *db_find_trigger (const char *name);
  extern int db_find_all_triggers (DB_OBJLIST ** list);
  extern int db_find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_obj, const char *attr,
				     DB_OBJLIST ** list);
  extern int db_alter_trigger_priority (DB_OBJECT * trobj, double priority);
  extern int db_alter_trigger_status (DB_OBJECT * trobj, DB_TRIGGER_STATUS status);

  extern int db_execute_deferred_activities (DB_OBJECT * trigger_obj, DB_OBJECT * target);
  extern int db_drop_deferred_activities (DB_OBJECT * trigger_obj, DB_OBJECT * target);

  extern int db_trigger_name (DB_OBJECT * trobj, char **name);
  extern int db_trigger_status (DB_OBJECT * trobj, DB_TRIGGER_STATUS * status);
  extern int db_trigger_priority (DB_OBJECT * trobj, double *priority);
  extern int db_trigger_event (DB_OBJECT * trobj, DB_TRIGGER_EVENT * event);
  extern int db_trigger_class (DB_OBJECT * trobj, DB_OBJECT ** class_obj);
  extern int db_trigger_attribute (DB_OBJECT * trobj, char **attr);
  extern int db_trigger_condition (DB_OBJECT * trobj, char **condition);
  extern int db_trigger_condition_time (DB_OBJECT * trobj, DB_TRIGGER_TIME * tr_time);
  extern int db_trigger_action_type (DB_OBJECT * trobj, DB_TRIGGER_ACTION * type);
  extern int db_trigger_action_time (DB_OBJECT * trobj, DB_TRIGGER_TIME * tr_time);
  extern int db_trigger_action (DB_OBJECT * trobj, char **action);
  extern int db_trigger_comment (DB_OBJECT * trobj, char **comment);

/* Schema template functions */
  extern DB_CTMPL *dbt_create_class (const char *name);
  extern DB_CTMPL *dbt_create_vclass (const char *name);
  extern DB_CTMPL *dbt_edit_class (DB_OBJECT * classobj);
  extern DB_OBJECT *dbt_finish_class (DB_CTMPL * def);
  extern void dbt_abort_class (DB_CTMPL * def);

  extern int dbt_add_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value);
  extern int dbt_add_shared_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value);
  extern int dbt_add_class_attribute (DB_CTMPL * def, const char *name, const char *domain, DB_VALUE * default_value);
  extern int dbt_constrain_non_null (DB_CTMPL * def, const char *name, int class_attribute, int on_or_off);
  extern int dbt_constrain_unique (DB_CTMPL * def, const char *name, int on_or_off);
  extern int dbt_add_constraint (DB_CTMPL * def, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
				 const char **attnames, int class_attributes, const char *comment);
  extern int dbt_drop_constraint (DB_CTMPL * def, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
				  const char **attnames, int class_attributes);
  extern int dbt_add_set_attribute_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain);
  extern int dbt_change_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain);
  extern int dbt_change_default (DB_CTMPL * def, const char *name, int class_attribute, DB_VALUE * value);
  extern int dbt_drop_set_attribute_domain (DB_CTMPL * def, const char *name, int class_attribute, const char *domain);
  extern int dbt_drop_attribute (DB_CTMPL * def, const char *name);
  extern int dbt_drop_shared_attribute (DB_CTMPL * def, const char *name);
  extern int dbt_drop_class_attribute (DB_CTMPL * def, const char *name);
  extern int dbt_add_method (DB_CTMPL * def, const char *name, const char *implementation);
  extern int dbt_add_class_method (DB_CTMPL * def, const char *name, const char *implementation);
  extern int dbt_add_argument (DB_CTMPL * def, const char *name, int class_method, int arg_index, const char *domain);
  extern int dbt_add_set_argument_domain (DB_CTMPL * def, const char *name, int class_method, int arg_index,
					  const char *domain);
  extern int dbt_change_method_implementation (DB_CTMPL * def, const char *name, int class_method, const char *newname);
  extern int dbt_drop_method (DB_CTMPL * def, const char *name);
  extern int dbt_drop_class_method (DB_CTMPL * def, const char *name);
  extern int dbt_add_super (DB_CTMPL * def, DB_OBJECT * super);
  extern int dbt_drop_super (DB_CTMPL * def, DB_OBJECT * super);
  extern int dbt_drop_super_connect (DB_CTMPL * def, DB_OBJECT * super);
  extern int dbt_rename (DB_CTMPL * def, const char *name, int class_namespace, const char *newname);
  extern int dbt_add_method_file (DB_CTMPL * def, const char *name);
  extern int dbt_drop_method_file (DB_CTMPL * def, const char *name);
  extern int dbt_drop_method_files (DB_CTMPL * def);
  extern int dbt_rename_method_file (DB_CTMPL * def, const char *new_name, const char *old_name);

  extern int dbt_set_loader_commands (DB_CTMPL * def, const char *commands);
  extern int dbt_add_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name, const char *alias);
  extern int dbt_add_class_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name, const char *alias);
  extern int dbt_drop_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name);
  extern int dbt_drop_class_resolution (DB_CTMPL * def, DB_OBJECT * super, const char *name);

  extern int dbt_add_query_spec (DB_CTMPL * def, const char *query);
  extern int dbt_drop_query_spec (DB_CTMPL * def, const int query_no);
  extern int dbt_change_query_spec (DB_CTMPL * def, const char *new_query, const int query_no);
  extern int dbt_set_object_id (DB_CTMPL * def, DB_NAMELIST * id_list);
  extern int dbt_add_foreign_key (DB_CTMPL * def, const char *constraint_name, const char **attnames,
				  const char *ref_class, const char **ref_attrs, int del_action, int upd_action,
				  const char *comment);

/* Object template functions */
  extern DB_OTMPL *dbt_create_object (DB_OBJECT * classobj);
  extern DB_OTMPL *dbt_edit_object (DB_OBJECT * object);
  extern DB_OBJECT *dbt_finish_object (DB_OTMPL * def);
  extern DB_OBJECT *dbt_finish_object_and_decache_when_failure (DB_OTMPL * def);
  extern void dbt_abort_object (DB_OTMPL * def);

  extern int dbt_put (DB_OTMPL * def, const char *name, DB_VALUE * value);
  extern int dbt_set_label (DB_OTMPL * def, DB_VALUE * label);

/* Descriptor functions.
 * The descriptor interface offers an alternative to attribute & method
 * names that can be substantially faster for repetitive operations.
 */
  extern int db_get_attribute_descriptor (DB_OBJECT * obj, const char *attname, int class_attribute, int for_update,
					  DB_ATTDESC ** descriptor);
  extern void db_free_attribute_descriptor (DB_ATTDESC * descriptor);

  extern int db_get_method_descriptor (DB_OBJECT * obj, const char *methname, int class_method,
				       DB_METHDESC ** descriptor);
  extern void db_free_method_descriptor (DB_METHDESC * descriptor);

  extern int db_dget (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value);
  extern int db_dput (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value);

  extern int db_dsend (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, ...);

  extern int db_dsend_arglist (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, DB_VALUE_LIST * args);

  extern int db_dsend_argarray (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, DB_VALUE ** args);

  extern int db_dsend_quick (DB_OBJECT * obj, DB_METHDESC * method, DB_VALUE * returnval, int nargs, DB_VALUE ** args);

  extern int dbt_dput (DB_OTMPL * def, DB_ATTDESC * attribute, DB_VALUE * value);

/* SQL/M API function*/
  extern char *db_get_vclass_ldb_name (DB_OBJECT * op);

  extern int db_add_query_spec (DB_OBJECT * vclass, const char *query);
  extern int db_drop_query_spec (DB_OBJECT * vclass, const int query_no);
  extern DB_NAMELIST *db_get_object_id (DB_OBJECT * vclass);

  extern int db_namelist_add (DB_NAMELIST ** list, const char *name);
  extern int db_namelist_append (DB_NAMELIST ** list, const char *name);
  extern void db_namelist_free (DB_NAMELIST * list);

  extern int db_is_vclass (DB_OBJECT * op);

  extern DB_OBJLIST *db_get_all_vclasses_on_ldb (void);
  extern DB_OBJLIST *db_get_all_vclasses (void);

  extern DB_QUERY_SPEC *db_get_query_specs (DB_OBJECT * obj);
  extern DB_QUERY_SPEC *db_query_spec_next (DB_QUERY_SPEC * query_spec);
  extern const char *db_query_spec_string (DB_QUERY_SPEC * query_spec);
  extern int db_change_query_spec (DB_OBJECT * vclass, const char *new_query, const int query_no);

  extern int db_validate (DB_OBJECT * vclass);
  extern int db_validate_query_spec (DB_OBJECT * vclass, const char *query_spec);
  extern int db_is_real_instance (DB_OBJECT * obj);
  extern DB_OBJECT *db_real_instance (DB_OBJECT * obj);
  extern int db_instance_equal (DB_OBJECT * obj1, DB_OBJECT * obj2);
  extern int db_is_updatable_object (DB_OBJECT * obj);
  extern int db_is_updatable_attribute (DB_OBJECT * obj, const char *attr_name);

  extern int db_check_single_query (DB_SESSION * session);

/* query pre-processing functions */
  extern int db_get_query_format (const char *CSQL_query, DB_QUERY_TYPE ** type_list, DB_QUERY_ERROR * query_error);
  extern DB_QUERY_TYPE *db_query_format_next (DB_QUERY_TYPE * query_type);
  extern DB_COL_TYPE db_query_format_col_type (DB_QUERY_TYPE * query_type);
  extern char *db_query_format_name (DB_QUERY_TYPE * query_type);
  extern DB_TYPE db_query_format_type (DB_QUERY_TYPE * query_type);
  extern void db_query_format_free (DB_QUERY_TYPE * query_type);
  extern DB_DOMAIN *db_query_format_domain (DB_QUERY_TYPE * query_type);
  extern char *db_query_format_attr_name (DB_QUERY_TYPE * query_type);
  extern char *db_query_format_spec_name (DB_QUERY_TYPE * query_type);
  extern char *db_query_format_original_name (DB_QUERY_TYPE * query_type);
  extern const char *db_query_format_class_name (DB_QUERY_TYPE * query_type);
  extern int db_query_format_is_non_null (DB_QUERY_TYPE * query_type);

/* query processing functions */
  extern int db_get_query_result_format (DB_QUERY_RESULT * result, DB_QUERY_TYPE ** type_list);
  extern int db_query_next_tuple (DB_QUERY_RESULT * result);
  extern int db_query_prev_tuple (DB_QUERY_RESULT * result);
  extern int db_query_first_tuple (DB_QUERY_RESULT * result);
  extern int db_query_last_tuple (DB_QUERY_RESULT * result);
  extern int db_query_get_tuple_value_by_name (DB_QUERY_RESULT * result, char *column_name, DB_VALUE * value);
  extern int db_query_get_tuple_value (DB_QUERY_RESULT * result, int tuple_index, DB_VALUE * value);

  extern int db_query_get_tuple_oid (DB_QUERY_RESULT * result, DB_VALUE * db_value);

  extern int db_query_get_tuple_valuelist (DB_QUERY_RESULT * result, int size, DB_VALUE * value_list);

  extern int db_query_tuple_count (DB_QUERY_RESULT * result);

  extern int db_query_column_count (DB_QUERY_RESULT * result);

  extern int db_query_prefetch_columns (DB_QUERY_RESULT * result, int *columns, int col_count);

  extern int db_query_format_size (DB_QUERY_TYPE * query_type);

  extern int db_query_end (DB_QUERY_RESULT * result);

/* query post-processing functions */
  extern int db_query_plan_dump_file (char *filename);

/* sql query routines */
  extern DB_SESSION *db_open_buffer (const char *buffer);
  extern DB_SESSION *db_open_file (FILE * file);
  extern DB_SESSION *db_open_file_name (const char *name);

  extern int db_statement_count (DB_SESSION * session);

  extern int db_compile_statement (DB_SESSION * session);
  extern void db_rewind_statement (DB_SESSION * session);

  extern DB_SESSION_ERROR *db_get_errors (DB_SESSION * session);

  extern DB_SESSION_ERROR *db_get_next_error (DB_SESSION_ERROR * errors, int *linenumber, int *columnnumber);

  extern DB_SESSION_ERROR *db_get_warnings (DB_SESSION * session);

  extern DB_SESSION_ERROR *db_get_next_warning (DB_SESSION_WARNING * errors, int *linenumber, int *columnnumber);

  extern DB_PARAMETER *db_get_parameters (DB_SESSION * session, int statement_id);
  extern DB_PARAMETER *db_parameter_next (DB_PARAMETER * param);
  extern const char *db_parameter_name (DB_PARAMETER * param);
  extern int db_bind_parameter_name (const char *name, DB_VALUE * value);

  extern DB_QUERY_TYPE *db_get_query_type_list (DB_SESSION * session, int stmt);

  extern int db_number_of_input_markers (DB_SESSION * session, int stmt);
  extern int db_number_of_output_markers (DB_SESSION * session, int stmt);
  extern DB_MARKER *db_get_input_markers (DB_SESSION * session, int stmt);
  extern DB_MARKER *db_get_output_markers (DB_SESSION * session, int stmt);
  extern DB_MARKER *db_marker_next (DB_MARKER * marker);
  extern int db_marker_index (DB_MARKER * marker);
  extern DB_DOMAIN *db_marker_domain (DB_MARKER * marker);
  extern bool db_is_input_marker (DB_MARKER * marker);
  extern bool db_is_output_marker (DB_MARKER * marker);

  extern int db_get_start_line (DB_SESSION * session, int stmt);

  extern int db_get_statement_type (DB_SESSION * session, int stmt);

/* constants for db_include_oid */
  enum
  { DB_NO_OIDS, DB_ROW_OIDS, DB_COLUMN_OIDS /* deprecated constant */  };

  extern void db_include_oid (DB_SESSION * session, int include_oid);

  extern int db_push_values (DB_SESSION * session, int count, DB_VALUE * in_values);

  extern int db_execute (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

  extern int db_execute_oid (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

  extern int db_query_produce_updatable_result (DB_SESSION * session, int stmtid);

  extern int db_execute_statement (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);

  extern int db_execute_and_keep_statement (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);
  extern DB_CLASS_MODIFICATION_STATUS db_has_modified_class (DB_SESSION * session, int stmt_id);

  extern int db_query_set_copy_tplvalue (DB_QUERY_RESULT * result, int copy);

  extern void db_close_session (DB_SESSION * session);
  extern void db_drop_statement (DB_SESSION * session, int stmt_id);

  extern int db_object_describe (DB_OBJECT * obj, int num_attrs, const char **attrs, DB_QUERY_TYPE ** col_spec);

  extern int db_object_fetch (DB_OBJECT * obj, int num_attrs, const char **attrs, DB_QUERY_RESULT ** result);

  extern int db_set_client_cache_time (DB_SESSION * session, int stmt_ndx, CACHE_TIME * cache_time);
  extern bool db_get_jdbccachehint (DB_SESSION * session, int stmt_ndx, int *life_time);
  extern bool db_get_cacheinfo (DB_SESSION * session, int stmt_ndx, bool * use_plan_cache, bool * use_query_cache);

/* These are used by csql but weren't in the 2.0 dbi.h file, added
   it for the PC.  If we don't want them here, they should go somewhere
   else so csql.c doesn't have to have an explicit declaration.
*/
  extern void db_free_query (DB_SESSION * session);
  extern DB_QUERY_TYPE *db_get_query_type_ptr (DB_QUERY_RESULT * result);

/* OBSOLETE FUNCTIONS
 * These functions are no longer supported.
 * New applications should not use any of these functions of structures.
 * Old applications should change to use only the functions and structures
 * published in the CUBRID Application Program Interface Reference Guide.
 */

  extern int db_query_execute (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

  extern int db_list_length (DB_LIST * list);
  extern DB_NAMELIST *db_namelist_copy (DB_NAMELIST * list);

  extern int db_drop_shared_attribute (DB_OBJECT * classobj, const char *name);

  extern int db_add_element_domain (DB_OBJECT * classobj, const char *name, const char *domain);
  extern int db_drop_element_domain (DB_OBJECT * classobj, const char *name, const char *domain);
  extern int db_rename_attribute (DB_OBJECT * classobj, const char *name, int class_attribute, const char *newname);
  extern int db_rename_method (DB_OBJECT * classobj, const char *name, int class_method, const char *newname);
  extern int db_set_argument_domain (DB_OBJECT * classobj, const char *name, int class_method, int arg_index,
				     const char *domain);
  extern int db_set_method_arg_domain (DB_OBJECT * classobj, const char *name, int arg_index, const char *domain);
  extern int db_set_class_method_arg_domain (DB_OBJECT * classobj, const char *name, int arg_index, const char *domain);
  extern DB_NAMELIST *db_namelist_sort (DB_NAMELIST * names);
  extern void db_namelist_remove (DB_NAMELIST ** list, const char *name);
  extern DB_OBJECT *db_objlist_get (DB_OBJLIST * list, int psn);
  extern void db_namelist_print (DB_NAMELIST * list);
  extern void db_objlist_print (DB_OBJLIST * list);

  extern DB_NAMELIST *db_get_attribute_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_shared_attribute_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_ordered_attribute_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_class_attribute_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_method_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_class_method_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_superclass_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_subclass_names (DB_OBJECT * obj);
  extern DB_NAMELIST *db_get_method_file_names (DB_OBJECT * obj);
  extern const char *db_get_method_function (DB_OBJECT * obj, const char *name);

  extern DB_DOMAIN *db_get_attribute_domain (DB_OBJECT * obj, const char *name);
  extern DB_TYPE db_get_attribute_type (DB_OBJECT * obj, const char *name);
  extern DB_OBJECT *db_get_attribute_class (DB_OBJECT * obj, const char *name);

  extern void db_force_method_reload (DB_OBJECT * obj);

  extern DB_ATTRIBUTE *db_get_shared_attribute (DB_OBJECT * obj, const char *name);
  extern DB_ATTRIBUTE *db_get_ordered_attributes (DB_OBJECT * obj);
  extern DB_ATTRIBUTE *db_attribute_ordered_next (DB_ATTRIBUTE * attribute);

  extern int db_print_mop (DB_OBJECT * obj, char *buffer, int maxlen);

  extern int db_get_shared (DB_OBJECT * object, const char *attpath, DB_VALUE * value);

  extern DB_OBJECT *db_copy (DB_OBJECT * sourcemop);
  extern char *db_get_method_source_file (DB_OBJECT * obj, const char *name);

  extern int db_is_indexed (DB_OBJECT * classobj, const char *attname);

/* INTERNAL FUNCTIONS
 * These are part of the interface but are intended only for
 * internal use by CUBRID.  Applications should not use these
 * functions.
 */
  extern DB_IDENTIFIER *db_identifier (DB_OBJECT * obj);
  extern DB_OBJECT *db_object (DB_IDENTIFIER * oid);
  extern int db_chn (DB_OBJECT * obj, DB_FETCH_MODE purpose);

  extern int db_encode_object (DB_OBJECT * object, char *string, int allocated_length, int *actual_length);
  extern int db_decode_object (const char *string, DB_OBJECT ** object);

  extern int db_set_system_parameters (const char *data);
  extern int db_get_system_parameters (char *data, int len);

  extern char *db_get_host_connected (void);
  extern int db_get_ha_server_state (char *buffer, int maxlen);

  extern void db_clear_host_connected (void);
  extern char *db_get_database_version (void);

  extern bool db_enable_trigger (void);
  extern bool db_disable_trigger (void);

  extern void db_clear_host_status (void);
  extern void db_set_host_status (char *hostname, int status);
  extern void db_set_connected_host_status (char *host_connected);
  extern bool db_does_connected_host_have_status (int status);
  extern bool db_need_reconnect (void);
  extern bool db_need_ignore_repl_delay (void);

#ifdef __cplusplus
}
#endif

#endif				/* _DBI_COMPAT_H_ */
