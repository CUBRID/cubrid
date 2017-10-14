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
 * dbtype_common.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications. It will be exposed
 * as part of the dbi_compat.h file.
 */

#ifndef _DBTYPE_COMMON_H_
#define _DBTYPE_COMMON_H_

#include "error_code.h"

#ifdef _DBI_COMPAT_H_		/* Keep the old definitions for api. */



#endif /* DBI_COMPAT_H */

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(SERVER_MODE) || defined(NDEBUG)
#define NO_SERVER_OR_DEBUG_MODE
#endif

  /******************************************/
  /* From cubrid_api.h */
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
    CUBRID_STMT_VACUUM,
    CUBRID_STMT_SET_TIMEZONE,

    CUBRID_MAX_STMT_TYPE
  } CUBRID_STMT_TYPE;

  /******************************************/
  /* From dbdef.h */
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
                                       (c) == DB_CONSTRAINT_FOREIGN_KEY)     \
                                      ? true : false )

#define DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY(c) \
                                    ( ((DB_CONSTRAINT_TYPE) (c) == DB_CONSTRAINT_REVERSE_UNIQUE  || \
                                       (DB_CONSTRAINT_TYPE) (c) == DB_CONSTRAINT_REVERSE_INDEX)     \
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
    DB_LT = -1,			/* canonical less than */
    DB_EQ = 0,			/* equal */
    DB_GT = 1,			/* canonical greater than, */
    DB_NE = 2,			/* not equal because types incomparable */
    DB_SUPERSET = 3		/* strict superset for set types.  */
  } DB_VALUE_COMPARE_RESULT;
#define DB_INT_TO_COMPARE_RESULT(c) ((c) == 0 ? DB_EQ : ((c) > 0 ? DB_GT : DB_LT))

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
    DB_AUTH_REPLACE = DB_AUTH_DELETE | DB_AUTH_INSERT,
    DB_AUTH_INSERT_UPDATE = DB_AUTH_UPDATE | DB_AUTH_INSERT,
    DB_AUTH_UPDATE_DELETE = DB_AUTH_UPDATE | DB_AUTH_DELETE,
    DB_AUTH_INSERT_UPDATE_DELETE = DB_AUTH_INSERT_UPDATE | DB_AUTH_DELETE,
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
    TR_MAX_ATTRIBUTE_TRIGGERS = TR_EVENT_STATEMENT_UPDATE + 1,

    /* class cache events */
    TR_EVENT_DELETE = 2,
    TR_EVENT_STATEMENT_DELETE = 3,
    TR_EVENT_INSERT = 4,
    TR_EVENT_STATEMENT_INSERT = 5,
    TR_EVENT_ALTER = 6,		/* currently unsupported */
    TR_EVENT_DROP = 7,		/* currently unsupported */
    TR_MAX_CLASS_TRIGGERS = TR_EVENT_DROP + 1,

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
  typedef struct sm_function_index_info DB_FUNCTION_INDEX_INFO;

/* Types of constraints that may be applied to attributes.  This type
   is used by the db_add_constraint()/db_drop_constraint() API functions. */
  typedef enum
  {
    DB_CONSTRAINT_NONE = -1,
    DB_CONSTRAINT_UNIQUE = 0,
    DB_CONSTRAINT_INDEX = 1,
    DB_CONSTRAINT_NOT_NULL = 2,
    DB_CONSTRAINT_REVERSE_UNIQUE = 3,
    DB_CONSTRAINT_REVERSE_INDEX = 4,
    DB_CONSTRAINT_PRIMARY_KEY = 5,
    DB_CONSTRAINT_FOREIGN_KEY = 6
  } DB_CONSTRAINT_TYPE;		/* TODO: only one enum for DB_CONSTRAINT_TYPE and SM_CONSTRAINT_TYPE */

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
#define DB_EMPTY_SESSION			0
/* uninitialized value for row count */
#define DB_ROW_COUNT_NOT_SET			-2

  /******************************************/
  typedef enum
  {
    SMALL_STRING,
    MEDIUM_STRING,
    LARGE_STRING
  } STRING_STYLE;

/*
 * DB_MAX_IDENTIFIER_LENGTH -
 * This constant defines the maximum length of an identifier
 * in the database.  An identifier is anything that is passed as a string
 * to the db_ functions (other than user attribute values).  This
 * includes such things as class names, attribute names etc.  This
 * isn't strictly enforced right now but applications must be aware that
 * this will be a requirement.
 */
#define DB_MAX_IDENTIFIER_LENGTH 255

/* Maximum allowable user name.*/
#define DB_MAX_USER_LENGTH 32

#define DB_MAX_PASSWORD_LENGTH 8

/* Maximum allowable schema name. */
#define DB_MAX_SCHEMA_LENGTH DB_MAX_USER_LENGTH

/* Maximum allowable class name. */
#define DB_MAX_CLASS_LENGTH (DB_MAX_IDENTIFIER_LENGTH-DB_MAX_SCHEMA_LENGTH-4)

#define DB_MAX_SPEC_LENGTH       4096

/* Maximum allowable class comment length */
#define DB_MAX_CLASS_COMMENT_LENGTH     2048
/* Maximum allowable comment length */
#define DB_MAX_COMMENT_LENGTH    1024

/* This constant defines the maximum length of a character
   string that can be used as the value of an attribute. */
#define DB_MAX_STRING_LENGTH	0x3fffffff

/* This constant defines the maximum length of a bit string
   that can be used as the value of an attribute. */
#define DB_MAX_BIT_LENGTH 0x3fffffff

/* The maximum precision that can be specified for a numeric domain. */
#define DB_MAX_NUMERIC_PRECISION 38

/* The upper limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_OVERFLOW_LIMIT 1e38

/* The lower limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_UNDERFLOW_LIMIT 1e-38

/* The maximum precision that can be specified for a CHAR(n) domain. */
#define DB_MAX_CHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified
   for a CHARACTER VARYING domain.*/
#define DB_MAX_VARCHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified for a NATIONAL CHAR(n)
   domain.
   This probably isn't restrictive enough.  We may need to define
   this functionally as the maximum precision will depend on the size
   multiplier of the codeset.*/
#define DB_MAX_NCHAR_PRECISION (DB_MAX_STRING_LENGTH/2)

/* The maximum precision that can be specified for a NATIONAL CHARACTER
   VARYING domain.
   This probably isn't restrictive enough.  We may need to define
   this functionally as the maximum precision will depend on the size
   multiplier of the codeset. */
#define DB_MAX_VARNCHAR_PRECISION DB_MAX_NCHAR_PRECISION

/*  The maximum precision that can be specified for a BIT domain. */
#define DB_MAX_BIT_PRECISION DB_MAX_BIT_LENGTH

/* The maximum precision that can be specified for a BIT VARYING domain. */
#define DB_MAX_VARBIT_PRECISION DB_MAX_BIT_PRECISION

/* This constant indicates that the system defined default for
   determining the length of a string is to be used for a DB_VALUE. */
#define DB_DEFAULT_STRING_LENGTH -1

/* This constant indicates that the system defined default for
   precision is to be used for a DB_VALUE. */
#define DB_DEFAULT_PRECISION -1

/* This constant indicates that the system defined default for
   scale is to be used for a DB_VALUE. */
#define DB_DEFAULT_SCALE -1

/* This constant defines the default precision of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_PRECISION 15

/* This constant defines the default scale of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_SCALE 0

/* This constant defines the default scale of result
   of numeric division operation */
#define DB_DEFAULT_NUMERIC_DIVISION_SCALE 9

/* These constants define the size of buffers within a DB_VALUE. */
#define DB_NUMERIC_BUF_SIZE	(2*sizeof(double))
#define DB_SMALL_CHAR_BUF_SIZE	(2*sizeof(double) - 3*sizeof(unsigned char))

/* This constant defines the default precision of DB_TYPE_BIGINT. */
#define DB_BIGINT_PRECISION      19

/* This constant defines the default precision of DB_TYPE_INTEGER. */
#define DB_INTEGER_PRECISION      10

/* This constant defines the default precision of DB_TYPE_SMALLINT. */
#define DB_SMALLINT_PRECISION      5

/* This constant defines the default precision of DB_TYPE_SHORT.*/
#define DB_SHORT_PRECISION      DB_SMALLINT_PRECISION

/* This constant defines the default decimal precision of DB_TYPE_FLOAT. */
#define DB_FLOAT_DECIMAL_PRECISION      7

/* This constant defines the default decimal precision of DB_TYPE_DOUBLE. */
#define DB_DOUBLE_DECIMAL_PRECISION      15

/* This constant defines the default decimal precision of DB_TYPE_MONETARY. */
#define DB_MONETARY_DECIMAL_PRECISION DB_DOUBLE_DECIMAL_PRECISION

/* This constant defines the default precision of DB_TYPE_TIME. */
#define DB_TIME_PRECISION      8

/* This constant defines the default precision of DB_TIMETZ_PRECISION. */
#define DB_TIMETZ_PRECISION   DB_TIME_PRECISION

/* This constant defines the default precision of DB_TYPE_DATE. */
#define DB_DATE_PRECISION      10

/* This constant defines the default precision of DB_TYPE_TIMESTAMP. */
#define DB_TIMESTAMP_PRECISION      19

/* This constant defines the default precision of DB_TYPE_TIMESTAMPTZ. */
#define DB_TIMESTAMPTZ_PRECISION   DB_TIMESTAMP_PRECISION

/* This constant defines the default precision of DB_TYPE_DATETIME. */
#define DB_DATETIME_PRECISION      23

/* This constant defines the default precision of DB_TYPE_DATETIMETZ. */
#define DB_DATETIMETZ_PRECISION    DB_DATETIME_PRECISION

/* This constant defines the default scale of DB_TYPE_DATETIME. */
#define DB_DATETIME_DECIMAL_SCALE      3

/* The maximum length of the partition expression after it is processed */
#define DB_MAX_PARTITION_EXPR_LENGTH 2048

/* Defines the state of a value as not being compressable due to its bad compression size or 
 * its uncompressed size being lower than PRIM_MINIMUM_STRING_LENGTH_FOR_COMPRESSION
 */
#define DB_UNCOMPRESSABLE -1

/* Defines the state of a value not being yet prompted for a compression process. */
#define DB_NOT_YET_COMPRESSED 0

#define DB_CURRENCY_DEFAULT db_get_currency_default()

#define db_set db_collection

#define db_make_utime db_make_timestamp

#define DB_MAKE_NULL(value) db_make_null(value)

#define DB_VALUE_CLONE_AS_NULL(src_value, dest_value)                   \
  do {                                                                  \
    if ((db_value_domain_init(dest_value,                               \
                              db_value_domain_type(src_value),          \
                              db_value_precision(src_value),            \
                              db_value_scale(src_value)))               \
        == NO_ERROR)                                                    \
      (void)db_value_put_null(dest_value);                              \
  } while (0)

#define DB_MAKE_INTEGER(value, num) db_make_int(value, num)

#define DB_MAKE_INT DB_MAKE_INTEGER

#define DB_MAKE_BIGINT(value, num) db_make_bigint(value, num)

#define DB_MAKE_BIGINTEGER DB_MAKE_BIGINT

#define DB_MAKE_FLOAT(value, num) db_make_float(value, num)

#define DB_MAKE_DOUBLE(value, num) db_make_double(value, num)

#define DB_MAKE_OBJECT(value, obj) db_make_object(value, obj)

#define DB_MAKE_OBJ DB_MAKE_OBJECT

#define DB_MAKE_SET(value, set) db_make_set(value, set)

#define DB_MAKE_MULTISET(value, set) db_make_multiset(value, set)

/* obsolete */
#define DB_MAKE_MULTI_SET DB_MAKE_MULTISET

#define DB_MAKE_SEQUENCE(value, set) db_make_sequence(value, set)

#define DB_MAKE_LIST DB_MAKE_SEQUENCE

/* obsolete */
#define DB_MAKE_SEQ DB_MAKE_SEQUENCE

/* new preferred interface */
  /*  todo: This following macro had in its previous version another call to 
   *  db_value_domain_init(). Now it has been removed but it needs to be
   *  checked if its still correct!!!.
   */
#define DB_MAKE_OID(value, oid)	\
      (((oid) == NULL) ? ((value)->domain.general_info.is_null = 1, NO_ERROR) : \
          db_make_oid((value), (oid)))

#define DB_GET_OID(value)		(db_get_oid(value))
#define DB_MAKE_COLLECTION(value, col) db_make_collection(value, col)

#define DB_MAKE_MIDXKEY(value, midxkey) db_make_midxkey(value, midxkey)

#define DB_MAKE_ELO(value, type, elo) db_make_elo(value, type, elo)

#define DB_MAKE_TIME(value, hour, minute, second) \
    db_make_time(value, hour, minute, second)

#define DB_MAKE_TIMETZ(value, timetz_value) \
    db_make_timetz(value, timetz_value)

#define DB_MAKE_TIMELTZ(value, time_value) \
    db_make_timeltz(value, time_value)

#define DB_MAKE_ENCODED_TIME(value, time_value) \
    db_value_put_encoded_time(value, time_value)

#define DB_MAKE_DATE(value, month, day, year) \
    db_make_date(value, month, day, year)

#define DB_MAKE_ENCODED_DATE(value, date_value) \
    db_value_put_encoded_date(value, date_value)

#define DB_MAKE_TIMESTAMP(value, timeval) \
    db_make_timestamp(value, timeval)

#define DB_MAKE_UTIME DB_MAKE_TIMESTAMP

#define DB_MAKE_TIMESTAMPTZ(value, ts_tz) \
    db_make_timestamptz(value, ts_tz)

#define DB_MAKE_TIMESTAMPLTZ(value, timeval) \
    db_make_timestampltz(value, timeval)

#define DB_MAKE_MONETARY_AMOUNT(value, amount) \
    db_make_monetary(value, DB_CURRENCY_DEFAULT, amount)

#define DB_MAKE_DATETIME(value, datetime_value) \
    db_make_datetime(value, datetime_value)

#define DB_MAKE_DATETIMETZ(value, datetimetz_value) \
    db_make_datetimetz(value, datetimetz_value)

#define DB_MAKE_DATETIMELTZ(value, datetime_value) \
    db_make_datetimeltz(value, datetime_value)

#define DB_MAKE_MONETARY DB_MAKE_MONETARY_AMOUNT

#define DB_MAKE_MONETARY_TYPE_AMOUNT(value, type, amount) \
    db_make_monetary(value, type, amount)

#define DB_MAKE_POINTER(value, ptr) db_make_pointer(value, ptr)

#define DB_MAKE_ERROR(value, errcode) db_make_error(value, errcode)

#define DB_MAKE_METHOD_ERROR(value, errcode, errmsg) \
           db_make_method_error(value, errcode, errmsg)

#define DB_MAKE_SMALLINT(value, num) db_make_short(value, num)

#define DB_MAKE_SHORT DB_MAKE_SMALLINT

#define DB_MAKE_NUMERIC(value, num, precision, scale) \
        db_make_numeric(value, num, precision, scale)

#define DB_MAKE_BIT(value, bit_length, bit_str, bit_str_bit_size) \
        db_make_bit(value, bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_VARBIT(value, max_bit_length, bit_str, bit_str_bit_size)\
        db_make_varbit(value, max_bit_length, bit_str, bit_str_bit_size)

#define DB_MAKE_CHAR(value, char_length, str, char_str_byte_size, \
		     codeset, collation) \
        db_make_char(value, char_length, str, char_str_byte_size, \
		     codeset, collation)

#define DB_MAKE_VARCHAR(value, max_char_length, str, char_str_byte_size, \
		        codeset, collation) \
        db_make_varchar(value, max_char_length, str, char_str_byte_size, \
			codeset, collation)

#define DB_MAKE_STRING(value, str) db_make_string(value, str)

#define DB_MAKE_NCHAR(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation) \
        db_make_nchar(value, nchar_length, str, nchar_str_byte_size, \
		      codeset, collation)

#define DB_MAKE_VARNCHAR(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)\
        db_make_varnchar(value, max_nchar_length, str, nchar_str_byte_size, \
			 codeset, collation)

#define DB_MAKE_ENUMERATION(value, index, str, size, codeset, collation) \
	db_make_enumeration(value, index, str, size, codeset, collation)

#define DB_MAKE_RESULTSET(value, handle) db_make_resultset(value, handle)

#define db_get_collection db_get_set
#define db_get_utime db_get_timestamp

#define DB_IS_NULL(value)               db_value_is_null(value)

#define DB_VALUE_DOMAIN_TYPE(value)     db_value_domain_type(value)

/* New preferred interface for DB_GET macros. */
#define DB_GET_INT(v) db_get_int(v)
#define DB_GET_SHORT(v) db_get_short(v)
#define DB_GET_BIGINT(v) db_get_bigint(v)
#define DB_GET_FLOAT(v) db_get_float(v)
#define DB_GET_STRING(v) db_get_string(v)
#define DB_GET_STRING_LENGTH(v) db_get_string_length(v)
#define DB_GET_DOUBLE(v) db_get_double(v)
#define DB_GET_OBJECT(v) db_get_object(v)
#define DB_GET_SET(v) db_get_set(v)
#define DB_GET_MIDXKEY(v) db_get_midxkey(v)
#define DB_GET_POINTER(v) db_get_pointer(v)
#define DB_GET_TIME(v) db_get_time(v)
#define DB_GET_TIMETZ(v) db_get_timetz(v)
#define DB_GET_TIMESTAMP(v) db_get_timestamp(v)
#define DB_GET_TIMESTAMPTZ(v) db_get_timestamptz(v)
#define DB_GET_DATETIME(v) db_get_datetime(v)
#define DB_GET_DATETIMETZ(v) db_get_datetimetz(v)
#define DB_GET_DATE(v) db_get_date(v)
#define DB_GET_MONETARY(v) db_get_monetary(v)
#define DB_GET_ERROR(v) db_get_error(v)
#define DB_GET_ELO(v) db_get_elo(v)
#define DB_GET_NUMERIC(v) db_get_numeric(v)
#define DB_GET_BIT(v, l) db_get_bit(v, l)
#define DB_GET_CHAR(v, l) db_get_char(v, l)
#define DB_GET_NCHAR(v, l) db_get_nchar(v, l)
#define DB_GET_STRING_SIZE(v) db_get_string_size(v)
#define DB_GET_ENUM_SHORT(v) db_get_enum_short(v)
#define DB_GET_ENUM_STRING(v) db_get_enum_string(v)
#define DB_GET_ENUM_STRING_SIZE(v) db_get_enum_string_size(v)
#define DB_GET_METHOD_ERROR_MSG() db_get_method_error_msg()
#define DB_GET_RESULTSET(v) db_get_resultset(v)
#define DB_GET_STRING_CODESET(v) ((INTL_CODESET) db_get_string_codeset(v))
#define DB_GET_STRING_COLLATION(v) db_get_string_collation(v)
#define DB_GET_ENUM_CODESET(v) db_get_enum_codeset(v)
#define DB_GET_ENUM_COLLATION(v) db_get_enum_collation(v)
#define DB_VALUE_TYPE(value) db_value_type(value)
#define DB_VALUE_PRECISION(value) db_value_precision(value)
#define DB_VALUE_SCALE(value) db_value_scale(value)

#define DB_GET_INTEGER(value)           db_get_int(value)
#define DB_GET_BIGINTEGER               DB_GET_BIGINT
#define DB_GET_OBJ DB_GET_OBJECT
#define DB_GET_MULTISET(value)          db_get_set(value)
#define DB_GET_LIST(value)              db_get_set(value)
#define DB_GET_SEQUENCE DB_GET_LIST
#define DB_GET_COLLECTION(value)        db_get_set(value)
#define DB_GET_UTIME DB_GET_TIMESTAMP
#define DB_GET_SMALLINT(value)          db_get_short(value)

#define DB_GET_COMPRESSED_SIZE(value) db_get_compressed_size(value)

#define DB_GET_SEQ DB_GET_SEQUENCE

#define DB_SET_COMPRESSED_STRING(value, compressed_string, compressed_size, compressed_need_clear) \
	db_set_compressed_string(value, compressed_string, compressed_size, compressed_need_clear)

#define DB_TRIED_COMPRESSION(value) (DB_GET_COMPRESSED_SIZE(value) != DB_NOT_YET_COMPRESSED)

#define DB_INT16_MIN   (-(DB_INT16_MAX)-1)
#define DB_INT16_MAX   0x7FFF
#define DB_UINT16_MAX  0xFFFFU
#define DB_INT32_MIN   (-(DB_INT32_MAX)-1)
#define DB_INT32_MAX   0x7FFFFFFF
#define DB_UINT32_MIN  0
#define DB_UINT32_MAX  0xFFFFFFFFU
#if (__WORDSIZE == 64) || defined(_WIN64)
#define DB_BIGINT_MAX  9223372036854775807L
#define DB_BIGINT_MIN  (-DB_BIGINT_MAX - 1L)
#else				/* (__WORDSIZE == 64) || defined(_WIN64) */
#define DB_BIGINT_MAX  9223372036854775807LL
#define DB_BIGINT_MIN  (-DB_BIGINT_MAX - 1LL)
#endif				/* (__WORDSIZE == 64) || defined(_WIN64) */
#define DB_ENUM_ELEMENTS_MAX  512
/* special ENUM index for PT_TO_ENUMERATION_VALUE function */
#define DB_ENUM_OVERFLOW_VAL  0xFFFF

/* DB_DATE_MIN and DB_DATE_MAX are calculated by julian_encode function
   with arguments (1,1,1) and (12,31,9999) respectively. */
#define DB_DATE_ZERO       DB_UINT32_MIN	/* 0 means zero date */
#define DB_DATE_MIN        1721424
#define DB_DATE_MAX        5373484

#define DB_TIME_MIN        DB_UINT32_MIN
#define DB_TIME_MAX        DB_UINT32_MAX

#define DB_UTIME_ZERO      DB_DATE_ZERO	/* 0 means zero date */
#define DB_UTIME_MIN       (DB_UTIME_ZERO + 1)
#define DB_UTIME_MAX       DB_UINT32_MAX

#define NULL_DEFAULT_EXPRESSION_OPERATOR (-1)

#define DB_IS_DATETIME_DEFAULT_EXPR(v) ((v) == DB_DEFAULT_SYSDATE || \
    (v) == DB_DEFAULT_CURRENTTIME || (v) == DB_DEFAULT_CURRENTDATE || \
    (v) == DB_DEFAULT_SYSDATETIME || (v) == DB_DEFAULT_SYSTIMESTAMP || \
    (v) == DB_DEFAULT_UNIX_TIMESTAMP || (v) == DB_DEFAULT_CURRENTDATETIME || \
    (v) == DB_DEFAULT_CURRENTTIMESTAMP || (v) == DB_DEFAULT_SYSTIME)

/* This defines the basic type identifier constants.  These are used in
   the domain specifications of attributes and method arguments and
   as value type tags in the DB_VALUE structures. */
  typedef enum
  {
    DB_TYPE_FIRST = 0,		/* first for iteration */
    DB_TYPE_UNKNOWN = 0,
    DB_TYPE_NULL = 0,
    DB_TYPE_INTEGER = 1,
    DB_TYPE_FLOAT = 2,
    DB_TYPE_DOUBLE = 3,
    DB_TYPE_STRING = 4,
    DB_TYPE_OBJECT = 5,
    DB_TYPE_SET = 6,
    DB_TYPE_MULTISET = 7,
    DB_TYPE_SEQUENCE = 8,
    DB_TYPE_ELO = 9,		/* obsolete... keep for backward compatibility. maybe we can replace with something else
				 */
    DB_TYPE_TIME = 10,
    DB_TYPE_TIMESTAMP = 11,
    DB_TYPE_DATE = 12,
    DB_TYPE_MONETARY = 13,
    DB_TYPE_VARIABLE = 14,	/* internal use only */
    DB_TYPE_SUB = 15,		/* internal use only */
    DB_TYPE_POINTER = 16,	/* method arguments only */
    DB_TYPE_ERROR = 17,		/* method arguments only */
    DB_TYPE_SHORT = 18,
    DB_TYPE_VOBJ = 19,		/* internal use only */
    DB_TYPE_OID = 20,		/* internal use only */
    DB_TYPE_DB_VALUE = 21,	/* special for esql */
    DB_TYPE_NUMERIC = 22,	/* SQL NUMERIC(p,s) values */
    DB_TYPE_BIT = 23,		/* SQL BIT(n) values */
    DB_TYPE_VARBIT = 24,	/* SQL BIT(n) VARYING values */
    DB_TYPE_CHAR = 25,		/* SQL CHAR(n) values */
    DB_TYPE_NCHAR = 26,		/* SQL NATIONAL CHAR(n) values */
    DB_TYPE_VARNCHAR = 27,	/* SQL NATIONAL CHAR(n) VARYING values */
    DB_TYPE_RESULTSET = 28,	/* internal use only */
    DB_TYPE_MIDXKEY = 29,	/* internal use only */
    DB_TYPE_TABLE = 30,		/* internal use only */
    DB_TYPE_BIGINT = 31,
    DB_TYPE_DATETIME = 32,
    DB_TYPE_BLOB = 33,
    DB_TYPE_CLOB = 34,
    DB_TYPE_ENUMERATION = 35,
    DB_TYPE_TIMESTAMPTZ = 36,
    DB_TYPE_TIMESTAMPLTZ = 37,
    DB_TYPE_DATETIMETZ = 38,
    DB_TYPE_DATETIMELTZ = 39,
    /* Disabled types */
    DB_TYPE_TIMETZ = 40,	/* internal use only - RESERVED */
    DB_TYPE_TIMELTZ = 41,	/* internal use only - RESERVED */
    /* end of disabled types */
    DB_TYPE_LIST = DB_TYPE_SEQUENCE,
    DB_TYPE_SMALLINT = DB_TYPE_SHORT,	/* SQL SMALLINT */
    DB_TYPE_VARCHAR = DB_TYPE_STRING,	/* SQL CHAR(n) VARYING values */
    DB_TYPE_UTIME = DB_TYPE_TIMESTAMP,	/* SQL TIMESTAMP */

    DB_TYPE_LAST = DB_TYPE_DATETIMELTZ
  } DB_TYPE;

/* Domain information stored in DB_VALUE structures. */
  typedef union db_domain_info DB_DOMAIN_INFO;
  union db_domain_info
  {
    struct general_info
    {
      unsigned char is_null;
      unsigned char type;
    } general_info;
    struct numeric_info
    {
      unsigned char is_null;
      unsigned char type;
      unsigned char precision;
      unsigned char scale;
    } numeric_info;
    struct char_info
    {
      unsigned char is_null;
      unsigned char type;
      int length;
      int collation_id;
    } char_info;
  };

/* types used for the representation of bigint values. */
  typedef int64_t DB_BIGINT;

/* Structure used for the representation of time values. */
  typedef unsigned int DB_TIME;

  typedef unsigned int TZ_ID;
  typedef struct db_timetz DB_TIMETZ;
  struct db_timetz
  {
    DB_TIME time;
    TZ_ID tz_id;		/* zone id */
  };

/* Structure used for the representation of universal times.
   These are compatible with the Unix time_t definition. */
  typedef unsigned int DB_TIMESTAMP;

  typedef DB_TIMESTAMP DB_UTIME;

  typedef struct db_timestamptz DB_TIMESTAMPTZ;
  struct db_timestamptz
  {
    DB_TIMESTAMP timestamp;	/* Unix timestamp */
    TZ_ID tz_id;		/* zone id */
  };

/* Structure used for the representation of date values. */
  typedef unsigned int DB_DATE;

  typedef struct db_datetime DB_DATETIME;
  struct db_datetime
  {
    unsigned int date;		/* date */
    unsigned int time;		/* time */
  };

  typedef struct db_datetimetz DB_DATETIMETZ;
  struct db_datetimetz
  {
    DB_DATETIME datetime;
    TZ_ID tz_id;		/* zone id */
  };

/* Structure used for the representation of numeric values. */
  typedef struct db_numeric DB_NUMERIC;
  struct db_numeric
  {
    union
    {
      unsigned char *digits;
      unsigned char buf[DB_NUMERIC_BUF_SIZE];
    } d;
  };

/* Structure used for the representation of monetary amounts. */
  typedef enum
  {
    DB_CURRENCY_DOLLAR,
    DB_CURRENCY_YEN,
    DB_CURRENCY_BRITISH_POUND,
    DB_CURRENCY_WON,
    DB_CURRENCY_TL,
    DB_CURRENCY_CAMBODIAN_RIEL,
    DB_CURRENCY_CHINESE_RENMINBI,
    DB_CURRENCY_INDIAN_RUPEE,
    DB_CURRENCY_RUSSIAN_RUBLE,
    DB_CURRENCY_AUSTRALIAN_DOLLAR,
    DB_CURRENCY_CANADIAN_DOLLAR,
    DB_CURRENCY_BRASILIAN_REAL,
    DB_CURRENCY_ROMANIAN_LEU,
    DB_CURRENCY_EURO,
    DB_CURRENCY_SWISS_FRANC,
    DB_CURRENCY_DANISH_KRONE,
    DB_CURRENCY_NORWEGIAN_KRONE,
    DB_CURRENCY_BULGARIAN_LEV,
    DB_CURRENCY_VIETNAMESE_DONG,
    DB_CURRENCY_CZECH_KORUNA,
    DB_CURRENCY_POLISH_ZLOTY,
    DB_CURRENCY_SWEDISH_KRONA,
    DB_CURRENCY_CROATIAN_KUNA,
    DB_CURRENCY_SERBIAN_DINAR,
    DB_CURRENCY_NULL
  } DB_CURRENCY;

  typedef struct db_monetary DB_MONETARY;
  struct db_monetary
  {
    double amount;
    DB_CURRENCY type;
  };

/* Definition for the collection descriptor structure. The structures for
 * the collection descriptors and the sequence descriptors are identical
 * internally but not all db_collection functions can be used with sequences
 * and no db_seq functions can be used with sets. It is advisable to
 * recognize the type of set being used, type it appropriately and only
 * call those db_ functions defined for that type.
 */

  typedef struct db_set DB_COLLECTION;
  typedef DB_COLLECTION DB_MULTISET;
  typedef DB_COLLECTION DB_SEQ;
  typedef DB_COLLECTION DB_SET;


  enum special_column_type
  {
    MIN_COLUMN = 0,
    MAX_COLUMN = 1
  };
  typedef enum special_column_type MIN_MAX_COLUMN_TYPE;

/* Used in btree_coerce_key and btree_ils_adjust_range to represent
 * min or max values, necessary in index search comparisons
 */
  typedef struct special_column MIN_MAX_COLUMN_INFO;
  struct special_column
  {
    int position;		/* position in the list of columns */
    MIN_MAX_COLUMN_TYPE type;	/* MIN or MAX column */
  };

  typedef struct db_midxkey DB_MIDXKEY;
  struct db_midxkey
  {
    int size;			/* size of buf */
    int ncolumns;		/* # of elements */
    DB_DOMAIN *domain;		/* MIDXKEY domain */
    char *buf;			/* key structure */
    MIN_MAX_COLUMN_INFO min_max_val;	/* info about coerced column */
  };

/*
 * DB_ELO
 * This is the run-time state structure for an ELO. The ELO is part of
 * the implementation of large object type and not intended to be used
 * directly by the API.
 */

  typedef struct vpid VPID;	/* REAL PAGE IDENTIFIER */
  struct vpid
  {
    int32_t pageid;		/* Page identifier */
    short volid;		/* Volume identifier where the page resides */
  };
#define VPID_INITIALIZER \
  { NULL_PAGEID, NULL_VOLID }

#define VPID_AS_ARGS(vpidp) (vpidp)->volid, (vpidp)->pageid

/* Set a vpid with values of volid and pageid */
#define VPID_SET(vpid_ptr, volid_value, pageid_value)	      \
  do {							      \
    (vpid_ptr)->volid  = (volid_value);			      \
    (vpid_ptr)->pageid = (pageid_value);		      \
  } while(0)

/* Set the vpid to an invalid one */
#define VPID_SET_NULL(vpid_ptr) VPID_SET(vpid_ptr, NULL_VOLID, NULL_PAGEID)

/* copy a VPID */
#define  VPID_COPY(dest_ptr, src_ptr)                      \
  do {							   \
    *(dest_ptr) = *(src_ptr);				   \
  } while (0)

/* vpid1 == vpid2 ? */
#define VPID_EQ(vpid_ptr1, vpid_ptr2)                         \
  ((vpid_ptr1) == (vpid_ptr2) ||                              \
   ((vpid_ptr1)->pageid == (vpid_ptr2)->pageid &&             \
    (vpid_ptr1)->volid  == (vpid_ptr2)->volid))

/* Is vpid NULL ? */
#define VPID_ISNULL(vpid_ptr) ((vpid_ptr)->pageid == NULL_PAGEID)

  typedef struct vsid VSID;	/* REAL SECTOR IDENTIFIER */
  struct vsid
  {
    int32_t sectid;		/* Sector identifier */
    short volid;		/* Volume identifier where the sector resides */
  };
#define VSID_INITIALIZER { NULL_SECTID, NULL_VOLID }
#define VSID_AS_ARGS(vsidp) (vsidp)->volid, (vsidp)->sectid

  typedef struct vfid VFID;	/* REAL FILE IDENTIFIER */
  struct vfid
  {
    int32_t fileid;		/* File identifier */
    short volid;		/* Volume identifier where the file resides */
  };
#define VFID_INITIALIZER \
  { NULL_FILEID, NULL_VOLID }

#define VFID_AS_ARGS(vfidp) (vfidp)->volid, (vfidp)->fileid

  typedef struct db_elo DB_ELO;

  enum db_elo_type
  {
    ELO_NULL,			/* do we need this anymore? */
    ELO_FBO
  };
  typedef enum db_elo_type DB_ELO_TYPE;

  struct db_elo
  {
    int64_t size;
    char *locator;
    char *meta_data;
    DB_ELO_TYPE type;
    int es_type;
  };

/* This is the memory representation of an internal object
 * identifier.  It is in the API only for a few functions that
 * are not intended for general use.
 * An object identifier is NOT a fixed identifier; it cannot be used
 * reliably as an object identifier across database sessions or even
 * across transaction boundaries.  API programs are not allowed
 * to make assumptions about the contents of this structure.
 */
  typedef struct db_identifier DB_IDENTIFIER;
  struct db_identifier
  {
    int pageid;
    short slotid;
    short volid;
  };

  typedef DB_IDENTIFIER OID;

/* Structure used for the representation of char, nchar and bit values. */
  typedef struct db_large_string DB_LARGE_STRING;

/* db_char.sm was formerly db_char.small.  small is an (undocumented)
 * reserved word on NT. */

  typedef union db_char DB_CHAR;
  union db_char
  {
    struct
    {
      unsigned char style;
      unsigned char codeset;
      unsigned char is_max_string;
      unsigned char compressed_need_clear;
    } info;
    struct
    {
      unsigned char style;
      unsigned char codeset;
      unsigned char is_max_string;
      unsigned char compressed_need_clear;
      unsigned char size;
      char buf[DB_SMALL_CHAR_BUF_SIZE];
    } sm;
    struct
    {
      unsigned char style;
      unsigned char codeset;
      unsigned char is_max_string;
      unsigned char compressed_need_clear;
      int size;
      char *buf;
      int compressed_size;
      char *compressed_buf;
    } medium;
    struct
    {
      unsigned char style;
      unsigned char codeset;
      unsigned char is_max_string;
      unsigned char compressed_need_clear;
      DB_LARGE_STRING *str;
    } large;
  };

  typedef DB_CHAR DB_NCHAR;
  typedef DB_CHAR DB_BIT;

  typedef int DB_RESULTSET;

/* Structure for an ENUMERATION element */
  typedef struct db_enum_element DB_ENUM_ELEMENT;
  struct db_enum_element
  {
    unsigned short short_val;	/* element index */
    DB_CHAR str_val;		/* element string */
  };

/* Structure for an ENUMERATION */
  typedef struct db_enumeration DB_ENUMERATION;
  struct db_enumeration
  {
    DB_ENUM_ELEMENT *elements;	/* array of enumeration elements */
    int collation_id;		/* collation */
    unsigned short count;	/* count of enumeration elements */
  };

/* A union of all of the possible basic type values.  This is used in the
 * definition of the DB_VALUE which is the fundamental structure used
 * in passing data in and out of the db_ function layer.
 */

  typedef union db_data DB_DATA;
  union db_data
  {
    int i;
    short sh;
    DB_BIGINT bigint;
    float f;
    double d;
    void *p;
    DB_OBJECT *op;
    DB_TIME time;
    DB_TIMETZ timetz;
    DB_DATE date;
    DB_TIMESTAMP utime;
    DB_TIMESTAMPTZ timestamptz;
    DB_DATETIME datetime;
    DB_DATETIMETZ datetimetz;
    DB_MONETARY money;
    DB_COLLECTION *set;
    DB_COLLECTION *collect;
    DB_MIDXKEY midxkey;
    DB_ELO elo;
    int error;
    DB_IDENTIFIER oid;
    DB_NUMERIC num;
    DB_CHAR ch;
    DB_RESULTSET rset;
    DB_ENUM_ELEMENT enumeration;
  };

/* This is the primary structure used for passing values in and out of
 * the db_ function layer. Values are always tagged with a datatype
 * so that they can be identified and type checking can be performed.
 */

  typedef struct db_value DB_VALUE;
  struct db_value
  {
    DB_DOMAIN_INFO domain;
    DB_DATA data;
    bool need_clear;
  };

/* This is used to chain DB_VALUEs into a list. */
  typedef struct db_value_list DB_VALUE_LIST;
  struct db_value_list
  {
    struct db_value_list *next;
    DB_VALUE val;
  };

/* This is used to chain DB_VALUEs into a list.  It is used as an argument
   to db_send_arglist. */
  typedef struct db_value_array DB_VALUE_ARRAY;
  struct db_value_array
  {
    int size;
    DB_VALUE *vals;
  };

/* This is used to gather stats about the workspace.
 * It contains the number of object descriptors used and
 * total number of object descriptors allocated
 */
  typedef struct db_workspace_stats DB_WORKSPACE_STATS;
  struct db_workspace_stats
  {
    int obj_desc_used;		/* number of object descriptors used */
    int obj_desc_total;		/* total # of object descriptors allocated */
  };

/* This defines the C language type identifier constants.
 * These are used to describe the types of values used for setting
 * DB_VALUE contents or used to get DB_VALUE contents into.
 */
  typedef enum
  {
    DB_TYPE_C_DEFAULT = 0,
    DB_TYPE_C_FIRST = 100,	/* first for iteration */
    DB_TYPE_C_INT,
    DB_TYPE_C_SHORT,
    DB_TYPE_C_LONG,
    DB_TYPE_C_FLOAT,
    DB_TYPE_C_DOUBLE,
    DB_TYPE_C_CHAR,
    DB_TYPE_C_VARCHAR,
    DB_TYPE_C_NCHAR,
    DB_TYPE_C_VARNCHAR,
    DB_TYPE_C_BIT,
    DB_TYPE_C_VARBIT,
    DB_TYPE_C_OBJECT,
    DB_TYPE_C_SET,
    DB_TYPE_C_ELO,
    DB_TYPE_C_TIME,
    DB_TYPE_C_DATE,
    DB_TYPE_C_TIMESTAMP,
    DB_TYPE_C_MONETARY,
    DB_TYPE_C_NUMERIC,
    DB_TYPE_C_POINTER,
    DB_TYPE_C_ERROR,
    DB_TYPE_C_IDENTIFIER,
    DB_TYPE_C_DATETIME,
    DB_TYPE_C_BIGINT,
    DB_TYPE_C_LAST,		/* last for iteration */
    DB_TYPE_C_UTIME = DB_TYPE_C_TIMESTAMP
  } DB_TYPE_C;

  typedef DB_BIGINT DB_C_BIGINT;
  typedef int DB_C_INT;
  typedef short DB_C_SHORT;
  typedef long DB_C_LONG;
  typedef float DB_C_FLOAT;
  typedef double DB_C_DOUBLE;
  typedef char *DB_C_CHAR;
  typedef char *DB_C_NCHAR;
  typedef char *DB_C_BIT;
  typedef DB_OBJECT DB_C_OBJECT;
  typedef DB_COLLECTION DB_C_SET;
  typedef DB_COLLECTION DB_C_COLLECTION;
  typedef DB_ELO DB_C_ELO;
  typedef struct db_c_time DB_C_TIME;
  struct db_c_time
  {
    int hour;
    int minute;
    int second;
  };

  typedef struct db_c_date DB_C_DATE;
  struct db_c_date
  {
    int year;
    int month;
    int day;
  };

/* identifiers for the default expression */
  typedef enum
  {
    DB_DEFAULT_NONE = 0,
    DB_DEFAULT_SYSDATE = 1,
    DB_DEFAULT_SYSDATETIME = 2,
    DB_DEFAULT_SYSTIMESTAMP = 3,
    DB_DEFAULT_UNIX_TIMESTAMP = 4,
    DB_DEFAULT_USER = 5,
    DB_DEFAULT_CURR_USER = 6,
    DB_DEFAULT_CURRENTDATETIME = 7,
    DB_DEFAULT_CURRENTTIMESTAMP = 8,
    DB_DEFAULT_CURRENTTIME = 9,
    DB_DEFAULT_CURRENTDATE = 10,
    DB_DEFAULT_SYSTIME = 11,
    DB_DEFAULT_FORMATTED_SYSDATE = 12,
  } DB_DEFAULT_EXPR_TYPE;

/*
 * An attribute having valid default expression, must have NULL default value. Currently, we allow simple expressions
 * like SYS_DATE, CURRENT_TIME. Also we allow to_char expression.
 */
  typedef struct db_default_expr DB_DEFAULT_EXPR;
  struct db_default_expr
  {
    DB_DEFAULT_EXPR_TYPE default_expr_type;	/* default expression identifier */
    int default_expr_op;	/* default expression operator */
    char *default_expr_format;	/* default expression format */
  };

  typedef DB_DATETIME DB_C_DATETIME;
  typedef DB_DATETIMETZ DB_C_DATETIMETZ;
  typedef DB_TIMESTAMP DB_C_TIMESTAMP;
  typedef DB_TIMESTAMPTZ DB_C_TIMESTAMPTZ;
  typedef DB_MONETARY DB_C_MONETARY;
  typedef unsigned char *DB_C_NUMERIC;
  typedef void *DB_C_POINTER;
  typedef DB_IDENTIFIER DB_C_IDENTIFIER;

  typedef enum
  {
    V_FALSE = 0,
    V_TRUE = 1,
    V_UNKNOWN = 2,
    V_ERROR = -1
  } DB_LOGICAL;

    /********************************************************/
  /* From intl_support.h */

  enum intl_codeset
  {
    INTL_CODESET_ERROR = -2,
    INTL_CODESET_NONE = -1,
    INTL_CODESET_ASCII,		/* US English charset, ASCII encoding */
    INTL_CODESET_RAW_BITS,	/* Uninterpreted bits, Raw encoding */
    INTL_CODESET_RAW_BYTES,	/* Uninterpreted bytes, Raw encoding */
    INTL_CODESET_ISO88591,	/* Latin 1 charset, ISO 8859 encoding */
    INTL_CODESET_KSC5601_EUC,	/* KSC 5601 1990 charset , EUC encoding */
    INTL_CODESET_UTF8,		/* UNICODE charset, UTF-8 encoding */

    INTL_CODESET_BINARY = INTL_CODESET_RAW_BYTES,

    INTL_CODESET_LAST = INTL_CODESET_UTF8
  };
  typedef enum intl_codeset INTL_CODESET;

  extern int intl_char_count (unsigned char *src, int length_in_bytes, INTL_CODESET src_codeset, int *char_count);
  extern int intl_char_size (unsigned char *src, int length_in_chars, INTL_CODESET src_codeset, int *byte_count);

  /********************************************************/
  /* From db.h */

  /* Argument checking macros */
#define CHECK_1ARG_RETURN_EXPR(obj, expr)                                      \
  do {                                                                         \
    if((obj) == NULL) {                                                        \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_2ARGS_RETURN_EXPR(obj1, obj2, expr)                              \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL) {                                     \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, expr)                        \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL || (obj3) == NULL) {                   \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_1ARG_NULL(obj)        \
  CHECK_1ARG_RETURN_EXPR(obj, NULL)

#define CHECK_2ARGS_NULL(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2,NULL)

#define CHECK_3ARGS_NULL(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1,obj2,obj3,NULL)

#define CHECK_1ARG_FALSE(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,false)

#define CHECK_1ARG_TRUE(obj)   \
  CHECK_1ARG_RETURN_EXPR(obj, true)

#define CHECK_1ARG_ERROR(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ERROR_WITH_TYPE(obj, TYPE)  \
  CHECK_1ARG_RETURN_EXPR(obj,(TYPE)ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_MINUSONE(obj) \
  CHECK_1ARG_RETURN_EXPR(obj,-1)

#define CHECK_2ARGS_ERROR(obj1, obj2)   \
  CHECK_2ARGS_RETURN_EXPR(obj1, obj2, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_3ARGS_ERROR(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ZERO(obj)     \
  CHECK_1ARG_RETURN_EXPR(obj, 0)

#define CHECK_1ARG_ZERO_WITH_TYPE(obj1, RETURN_TYPE)     \
  CHECK_1ARG_RETURN_EXPR(obj1, (RETURN_TYPE) 0)

#define CHECK_2ARGS_ZERO(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2, 0)

#define CHECK_1ARG_UNKNOWN(obj1)        \
  CHECK_1ARG_RETURN_EXPR(obj1, DB_TYPE_UNKNOWN)

  /********************************************************/
  /* From object_accessor.h */
  extern char *obj_Method_error_msg;

  /********************************************************/
  /* From storage_common.h */
  /* LIMITS AND NULL VALUES ON DISK RELATED DATATYPES */

#define NULL_VOLID  (-1)	/* Value of an invalid volume identifier */
#define NULL_SECTID (-1)	/* Value of an invalid sector identifier */
#define NULL_PAGEID (-1)	/* Value of an invalid page identifier */
#define NULL_SLOTID (-1)	/* Value of an invalid slot identifier */
#define NULL_OFFSET (-1)	/* Value of an invalid offset */
#define NULL_FILEID (-1)	/* Value of an invalid file identifier */

#define VOLID_MAX       SHRT_MAX
#define PAGEID_MAX      INT_MAX
#define SECTID_MAX      INT_MAX
#define PGLENGTH_MAX    SHRT_MAX
#define VOL_MAX_NPAGES(page_size) \
  ((sizeof(off_t) == 4) ? (INT_MAX / (page_size)) : INT_MAX)

#define LOGPAGEID_MAX   0x7fffffffffffLL	/* 6 bytes length */

  /********************************************************/
  /* From oid.h */
#define OID_ISNULL(oidp)        ((oidp)->pageid == NULL_PAGEID)

  /********************************************************/
  /* From object_domain.h */
  /*
   * We probably should make this 0 rather than -1 so that we can more easily
   * represent precisions with unsigned integers.  Zero is not a valid
   * precision.
   */
#define TP_FLOATING_PRECISION_VALUE -1
  /********************************************************/
  /* From object_primitive.h */

  extern int pr_clone_value (const DB_VALUE * src, DB_VALUE * dest);

  /********************************************************/
  /* From language_support.h */

  enum
  {
    LANG_COLL_ISO_BINARY = 0,
    LANG_COLL_UTF8_BINARY = 1,
    LANG_COLL_ISO_EN_CS = 2,
    LANG_COLL_ISO_EN_CI = 3,
    LANG_COLL_UTF8_EN_CS = 4,
    LANG_COLL_UTF8_EN_CI = 5,
    LANG_COLL_UTF8_TR_CS = 6,
    LANG_COLL_UTF8_KO_CS = 7,
    LANG_COLL_EUCKR_BINARY = 8,
    LANG_COLL_BINARY = 9
  };

#define LANG_GET_BINARY_COLLATION(c) (((c) == INTL_CODESET_UTF8) \
  ? LANG_COLL_UTF8_BINARY :					 \
  (((c) == INTL_CODESET_KSC5601_EUC) ? LANG_COLL_EUCKR_BINARY :  \
  (((c) == INTL_CODESET_ISO88591) ? LANG_COLL_ISO_BINARY :	 \
    LANG_COLL_BINARY)))


  /* collation and charset do be used by system : */
#define LANG_SYS_COLLATION  (LANG_GET_BINARY_COLLATION(lang_charset()))

  extern INTL_CODESET lang_charset (void);
#define LANG_SYS_CODESET  lang_charset()

  /********************************************************/
  /* From object_representation.h */

  /*
   * SETOBJ
   *    This is the primitive set object header.
   */
  typedef struct setobj SETOBJ;

  typedef struct db_set SETREF;
  struct db_set
  {
    /*
     * a garbage collector ticket is not required for the "owner" field as
     * the entire set references area is registered for scanning in area_grow.
     */
    struct db_object *owner;
    struct db_set *ref_link;
    struct setobj *set;
    char *disk_set;
    DB_DOMAIN *disk_domain;
    int attribute;
    int ref_count;
    int disk_size;
    bool need_clear;
  };
  /********************************************************/
  /* From set_object.h */
  struct setobj
  {

    DB_TYPE coltype;
    int size;			/* valid indexes from 0 to size -1 aka the number of represented values in the
				 * collection */
    int lastinsert;		/* the last value insertion point 0 to size. */
    int topblock;		/* maximum index of an allocated block. This is the maximum non-NULL db_value pointer
				 * index of array. array[topblock] should be non-NULL. array[topblock+1] will be a NULL
				 * pointer for future expansion. */
    int arraytop;		/* maximum indexable pointer in array the valid indexes for array are 0 to arraytop
				 * inclusive Generally this may be greater than topblock */
    int topblockcount;		/* This is the max index of the top block Since it may be shorter than a standard sized
				 * block for space efficiency. */
    DB_VALUE **array;

    /* not stored on disk, attached at run time by the schema */
    struct tp_domain *domain;

    /* external reference list */
    DB_COLLECTION *references;

    /* clear if we can't guarantee sort order, always on for sequences */
    unsigned sorted:1;

    /* set if we can't guarantee that there are no temporary OID's in here */
    unsigned may_have_temporary_oids:1;
  };

  /*
   * struct setobj
   * The internal structure of a setobj data struct is private to this module.
   * all access to this structure should be encapsulated via function calls.
   */
  typedef SETOBJ COL;

  extern DB_TYPE setobj_type (COL * set);
  /********************************************************/
  /* From elo.h */

  extern void elo_init_structure (DB_ELO * elo);
  /********************************************************/
  /* From db_date.h */

  extern int db_date_encode (DB_DATE * date, int month, int day, int year);
  extern int db_time_encode (DB_TIME * timeval, int hour, int minute, int second);
  /********************************************************/


  extern DB_VALUE *db_value_create (void);
  extern DB_VALUE *db_value_copy (DB_VALUE * value);
  extern int db_value_clone (DB_VALUE * src, DB_VALUE * dest);
  extern int db_value_clear (DB_VALUE * value);
  extern int db_value_free (DB_VALUE * value);
  extern int db_value_clear_array (DB_VALUE_ARRAY * value_array);
  extern void db_value_print (const DB_VALUE * value);
  extern void db_value_fprint (FILE * fp, const DB_VALUE * value);
  extern int db_value_coerce (const DB_VALUE * src, DB_VALUE * dest, const DB_DOMAIN * desired_domain);

  extern int db_value_equal (const DB_VALUE * value1, const DB_VALUE * value2);
  extern int db_value_compare (const DB_VALUE * value1, const DB_VALUE * value2);
  extern int db_value_domain_init (DB_VALUE * value, DB_TYPE type, const int precision, const int scale);
  extern int db_value_domain_min (DB_VALUE * value, DB_TYPE type, const int precision, const int scale,
				  const int codeset, const int collation_id, const DB_ENUMERATION * enumeration);
  extern int db_value_domain_max (DB_VALUE * value, DB_TYPE type, const int precision, const int scale,
				  const int codeset, const int collation_id, const DB_ENUMERATION * enumeration);
  extern int db_value_domain_default (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale,
				      const int codeset, const int collation_id, DB_ENUMERATION * enumeration);
  extern int db_value_domain_zero (DB_VALUE * value, const DB_TYPE type, const int precision, const int scale);
  extern int db_string_truncate (DB_VALUE * value, const int max_precision);
  extern DB_TYPE db_value_domain_type (const DB_VALUE * value);
  extern int db_value_put_null (DB_VALUE * value);
  extern int db_value_put (DB_VALUE * value, const DB_TYPE_C c_type, void *input, const int input_length);
  extern bool db_value_type_is_collection (const DB_VALUE * value);
  extern bool db_value_type_is_numeric (const DB_VALUE * value);
  extern bool db_value_type_is_bit (const DB_VALUE * value);
  extern bool db_value_type_is_char (const DB_VALUE * value);
  extern bool db_value_type_is_internal (const DB_VALUE * value);
  extern bool db_value_is_null (const DB_VALUE * value);
  extern int db_value_get (DB_VALUE * value, const DB_TYPE_C type, void *buf, const int buflen, int *transferlen,
			   int *outputlen);
  extern int db_value_size (const DB_VALUE * value, DB_TYPE_C type, int *size);
  extern int db_value_char_size (const DB_VALUE * value, int *size);
  extern DB_CURRENCY db_value_get_monetary_currency (const DB_VALUE * value);
  extern double db_value_get_monetary_amount_as_double (const DB_VALUE * value);
  extern int db_value_put_monetary_currency (DB_VALUE * value, const DB_CURRENCY type);
  extern int db_value_put_monetary_amount_as_double (DB_VALUE * value, const double amount);
  extern int db_value_alter_type (DB_VALUE * value, DB_TYPE type);

/*
 * DB_MAKE_ value constructors.
 * These macros are provided to make the construction of DB_VALUE
 * structures easier.  They will fill in the fields from the supplied
 * arguments. It is not necessary to use these macros but is usually more
 * convenient.
 */
  extern int db_value_put_encoded_time (DB_VALUE * value, const DB_TIME * time_value);
  extern int db_value_put_encoded_date (DB_VALUE * value, const DB_DATE * date_value);
  extern int db_value_put_numeric (DB_VALUE * value, DB_C_NUMERIC num);
  extern int db_value_put_bit (DB_VALUE * value, DB_C_BIT str, int size);
  extern int db_value_put_varbit (DB_VALUE * value, DB_C_BIT str, int size);
  extern int db_value_put_char (DB_VALUE * value, DB_C_CHAR str, int size);
  extern int db_value_put_varchar (DB_VALUE * value, DB_C_CHAR str, int size);
  extern int db_value_put_nchar (DB_VALUE * value, DB_C_NCHAR str, int size);
  extern int db_value_put_varnchar (DB_VALUE * value, DB_C_NCHAR str, int size);

  extern DB_CURRENCY db_get_currency_default (void);

/* Collection functions */
  extern DB_COLLECTION *db_col_create (DB_TYPE type, int size, DB_DOMAIN * domain);
  extern DB_COLLECTION *db_col_copy (DB_COLLECTION * col);
  extern int db_col_filter (DB_COLLECTION * col);
  extern int db_col_free (DB_COLLECTION * col);
  extern int db_col_coerce (DB_COLLECTION * col, DB_DOMAIN * domain);

  extern int db_col_size (DB_COLLECTION * col);
  extern int db_col_cardinality (DB_COLLECTION * col);
  extern DB_TYPE db_col_type (DB_COLLECTION * col);
  extern DB_DOMAIN *db_col_domain (DB_COLLECTION * col);
  extern int db_col_ismember (DB_COLLECTION * col, DB_VALUE * value);
  extern int db_col_find (DB_COLLECTION * col, DB_VALUE * value, int starting_index, int *found_index);
  extern int db_col_add (DB_COLLECTION * col, DB_VALUE * value);
  extern int db_col_drop (DB_COLLECTION * col, DB_VALUE * value, int all);
  extern int db_col_drop_element (DB_COLLECTION * col, int element_index);

  extern int db_col_drop_nulls (DB_COLLECTION * col);

  extern int db_col_get (DB_COLLECTION * col, int element_index, DB_VALUE * value);
  extern int db_col_put (DB_COLLECTION * col, int element_index, DB_VALUE * value);
  extern int db_col_insert (DB_COLLECTION * col, int element_index, DB_VALUE * value);

  extern int db_col_print (DB_COLLECTION * col);
  extern int db_col_fprint (FILE * fp, DB_COLLECTION * col);

/* Set and sequence functions.
   These are now obsolete. Please use the generic collection functions
   "db_col*" instead */
  extern int db_set_compare (const DB_VALUE * value1, const DB_VALUE * value2);
  extern DB_COLLECTION *db_set_create (DB_OBJECT * classobj, const char *name);
  extern DB_COLLECTION *db_set_create_basic (DB_OBJECT * classobj, const char *name);
  extern DB_COLLECTION *db_set_create_multi (DB_OBJECT * classobj, const char *name);
  extern DB_COLLECTION *db_seq_create (DB_OBJECT * classobj, const char *name, int size);
  extern int db_set_free (DB_COLLECTION * set);
  extern int db_set_filter (DB_COLLECTION * set);
  extern int db_set_add (DB_COLLECTION * set, DB_VALUE * value);
  extern int db_set_get (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_set_drop (DB_COLLECTION * set, DB_VALUE * value);
  extern int db_set_size (DB_COLLECTION * set);
  extern int db_set_cardinality (DB_COLLECTION * set);
  extern int db_set_ismember (DB_COLLECTION * set, DB_VALUE * value);
  extern int db_set_isempty (DB_COLLECTION * set);
  extern int db_set_has_null (DB_COLLECTION * set);
  extern int db_set_print (DB_COLLECTION * set);
  extern DB_TYPE db_set_type (DB_COLLECTION * set);
  extern DB_COLLECTION *db_set_copy (DB_COLLECTION * set);
  extern int db_seq_get (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_seq_put (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_seq_insert (DB_COLLECTION * set, int element_index, DB_VALUE * value);
  extern int db_seq_drop (DB_COLLECTION * set, int element_index);
  extern int db_seq_size (DB_COLLECTION * set);
  extern int db_seq_cardinality (DB_COLLECTION * set);
  extern int db_seq_print (DB_COLLECTION * set);
  extern int db_seq_find (DB_COLLECTION * set, DB_VALUE * value, int element_index);
  extern int db_seq_free (DB_SEQ * seq);
  extern int db_seq_filter (DB_SEQ * seq);
  extern DB_SEQ *db_seq_copy (DB_SEQ * seq);

  extern DB_DOMAIN *db_type_to_db_domain (DB_TYPE type);
  extern const char *db_default_expression_string (DB_DEFAULT_EXPR_TYPE default_expr_type);

#ifndef SERVER_MODE
  extern int db_get_int (const DB_VALUE * value);
  extern DB_C_SHORT db_get_short (const DB_VALUE * value);
  extern DB_BIGINT db_get_bigint (const DB_VALUE * value);
  extern DB_C_CHAR db_get_string (const DB_VALUE * value);
  extern DB_C_FLOAT db_get_float (const DB_VALUE * value);
  extern DB_C_DOUBLE db_get_double (const DB_VALUE * value);
  extern DB_OBJECT *db_get_object (const DB_VALUE * value);
  extern DB_COLLECTION *db_get_set (const DB_VALUE * value);
  extern DB_MIDXKEY *db_get_midxkey (const DB_VALUE * value);
  extern DB_C_POINTER db_get_pointer (const DB_VALUE * value);
  extern DB_TIME *db_get_time (const DB_VALUE * value);
  extern DB_TIMETZ *db_get_timetz (const DB_VALUE * value);
  extern DB_TIMESTAMP *db_get_timestamp (const DB_VALUE * value);
  extern DB_TIMESTAMPTZ *db_get_timestamptz (const DB_VALUE * value);
  extern DB_DATETIME *db_get_datetime (const DB_VALUE * value);
  extern DB_DATETIMETZ *db_get_datetimetz (const DB_VALUE * value);
  extern DB_DATE *db_get_date (const DB_VALUE * value);
  extern DB_MONETARY *db_get_monetary (const DB_VALUE * value);
  extern int db_get_error (const DB_VALUE * value);
  extern DB_ELO *db_get_elo (const DB_VALUE * value);
  extern DB_C_NUMERIC db_get_numeric (const DB_VALUE * value);
  extern DB_C_BIT db_get_bit (const DB_VALUE * value, int *length);
  extern DB_C_CHAR db_get_char (const DB_VALUE * value, int *length);
  extern DB_C_NCHAR db_get_nchar (const DB_VALUE * value, int *length);
  extern int db_get_string_size (const DB_VALUE * value);
  extern unsigned short db_get_enum_short (const DB_VALUE * value);
  extern DB_C_CHAR db_get_enum_string (const DB_VALUE * value);
  extern int db_get_enum_string_size (const DB_VALUE * value);
  extern DB_C_CHAR db_get_method_error_msg (void);
  extern DB_RESULTSET db_get_resultset (const DB_VALUE * value);
  extern int db_get_string_codeset (const DB_VALUE * value);
  extern int db_get_string_collation (const DB_VALUE * value);
  extern int db_get_enum_codeset (const DB_VALUE * value);
  extern int db_get_enum_collation (const DB_VALUE * value);
  extern OID *db_get_oid (const DB_VALUE * value);
  extern DB_TYPE db_value_type (const DB_VALUE * value);
  extern int db_value_precision (const DB_VALUE * value);
  extern int db_value_scale (const DB_VALUE * value);

  extern int db_make_db_char (DB_VALUE * value, INTL_CODESET codeset, const int collation_id, const char *str,
			      const int size);

  extern int db_make_null (DB_VALUE * value);
  extern int db_make_int (DB_VALUE * value, const int num);
  extern int db_make_float (DB_VALUE * value, const DB_C_FLOAT num);
  extern int db_make_double (DB_VALUE * value, const DB_C_DOUBLE num);
  extern int db_make_object (DB_VALUE * value, DB_C_OBJECT * obj);
  extern int db_make_midxkey (DB_VALUE * value, DB_MIDXKEY * midxkey);
  extern int db_make_timetz (DB_VALUE * value, const DB_TIMETZ * timetz_value);
  extern int db_make_timeltz (DB_VALUE * value, const DB_TIME * time_value);
  extern int db_make_timestamp (DB_VALUE * value, const DB_C_TIMESTAMP timeval);
  extern int db_make_timestampltz (DB_VALUE * value, const DB_C_TIMESTAMP ts_val);
  extern int db_make_timestamptz (DB_VALUE * value, const DB_C_TIMESTAMPTZ * ts_tz_val);
  extern int db_make_datetime (DB_VALUE * value, const DB_DATETIME * datetime);
  extern int db_make_datetimeltz (DB_VALUE * value, const DB_DATETIME * datetime);
  extern int db_make_datetimetz (DB_VALUE * value, const DB_DATETIMETZ * datetimetz);
  extern int db_make_monetary (DB_VALUE * value, const DB_CURRENCY type, const double amount);
  extern int db_make_pointer (DB_VALUE * value, DB_C_POINTER ptr);
  extern int db_make_error (DB_VALUE * value, const int errcode);
  extern int db_make_method_error (DB_VALUE * value, const int errcode, const char *errmsg);
  extern int db_make_short (DB_VALUE * value, const DB_C_SHORT num);
  extern int db_make_bigint (DB_VALUE * value, const DB_BIGINT num);
  extern int db_make_numeric (DB_VALUE * value, const DB_C_NUMERIC num, const int precision, const int scale);
  extern int db_make_bit (DB_VALUE * value, const int bit_length, const DB_C_BIT bit_str, const int bit_str_bit_size);
  extern int db_make_varbit (DB_VALUE * value, const int max_bit_length, const DB_C_BIT bit_str,
			     const int bit_str_bit_size);
  extern int db_make_char (DB_VALUE * value, const int char_length, const DB_C_CHAR str, const int char_str_byte_size,
			   const int codeset, const int collation_id);
  extern int db_make_varchar (DB_VALUE * value, const int max_char_length, const DB_C_CHAR str,
			      const int char_str_byte_size, const int codeset, const int collation_id);
  extern int db_make_nchar (DB_VALUE * value, const int nchar_length, const DB_C_NCHAR str,
			    const int nchar_str_byte_size, const int codeset, const int collation_id);
  extern int db_make_varnchar (DB_VALUE * value, const int max_nchar_length, const DB_C_NCHAR str,
			       const int nchar_str_byte_size, const int codeset, const int collation_id);
  extern int db_make_enumeration (DB_VALUE * value, unsigned short index, DB_C_CHAR str, int size,
				  unsigned char codeset, const int collation_id);
  extern int db_make_resultset (DB_VALUE * value, const DB_RESULTSET handle);

  extern int db_make_string (DB_VALUE * value, const char *str);
  extern int db_make_string_copy (DB_VALUE * value, const char *str);

  extern int db_make_oid (DB_VALUE * value, const OID * oid);

  extern int db_make_set (DB_VALUE * value, DB_C_SET * set);
  extern int db_make_multiset (DB_VALUE * value, DB_C_SET * set);
  extern int db_make_sequence (DB_VALUE * value, DB_C_SET * set);
  extern int db_make_collection (DB_VALUE * value, DB_C_SET * set);

  extern int db_make_elo (DB_VALUE * value, DB_TYPE type, const DB_ELO * elo);

  extern int db_make_time (DB_VALUE * value, const int hour, const int minute, const int second);
  extern int db_make_date (DB_VALUE * value, const int month, const int day, const int year);
#endif

#ifdef __cplusplus
}
#endif				/* __cplusplus */

extern int db_get_compressed_size (DB_VALUE * value);
extern void db_set_compressed_string (DB_VALUE * value, char *compressed_string,
				      int compressed_size, bool compressed_need_clear);

extern int db_string_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);
extern int db_enum_put_cs_and_collation (DB_VALUE * value, const int codeset, const int collation_id);

extern int valcnv_convert_value_to_string (DB_VALUE * value);

#endif /* dbtype_common.h */
