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
 * dbtype_def.h - Definitions related to the memory representations of database
 * attribute values. This is an application interface file. It should contain
 * only definitions available to CUBRID customer applications. It will be exposed
 * as part of the dbi_compat.h file.
 */

#ifndef _DBTYPE_DEF_H_
#define _DBTYPE_DEF_H_

#include "error_code.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* todo: Decide what we do about this!! */
#ifdef __cplusplus
  typedef bool need_clear_type;
#else
  typedef char need_clear_type;
#endif

#if defined (__GNUC__) && defined (NDEBUG)
#define ALWAYS_INLINE always_inline
#else
#define ALWAYS_INLINE
#endif

#if !defined (__GNUC__)
#define __attribute__(X)
#endif

#if defined (__cplusplus)
  class JSON_DOC;
  class JSON_VALIDATOR;
#else
  typedef void JSON_DOC;
  typedef void JSON_VALIDATOR;
#endif

  typedef enum
  {
    SMALL_STRING,
    MEDIUM_STRING,
    LARGE_STRING
  } STRING_STYLE;

  /******************************************/
  /* From cubrid_api.h */

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

  /********************************************************/
  /* From object_accessor.h */
  extern char *obj_Method_error_msg;

  /******************************************/
  /* From dbdef.h */

  /* TODO: there is almost nothing left of current dbdef.h, and the remaining parts don't really belong to dbdef.h, but
   *       rather to other server/client common headers, like storage_common.h;
   *       then we can move all this back to dbdef, include dbdef in dbtype_common and add dbdef to the list of exposed
   *       headers.
   */

#define DB_AUTH_ALL \
  ((DB_AUTH) (DB_AUTH_SELECT | DB_AUTH_INSERT | DB_AUTH_UPDATE | DB_AUTH_DELETE | \
   DB_AUTH_ALTER  | DB_AUTH_INDEX  | DB_AUTH_EXECUTE))

/* It is strongly advised that applications use these macros for access to the fields of the DB_QUERY_ERROR structure */
#define DB_QUERY_ERROR_LINE(error) ((error)->err_lineno)
#define DB_QUERY_ERROR_CHAR(error) ((error)->err_posno)

/* These are the status codes that can be returned by the functions that iterate over statement results. */
#define DB_CURSOR_SUCCESS      0
#define DB_CURSOR_END          1
#define DB_CURSOR_ERROR       -1

#define DB_IS_CONSTRAINT_UNIQUE_FAMILY(c) \
  ( ((c) == DB_CONSTRAINT_UNIQUE || (c) == DB_CONSTRAINT_REVERSE_UNIQUE || (c) == DB_CONSTRAINT_PRIMARY_KEY) ? true : false )

#define DB_IS_CONSTRAINT_INDEX_FAMILY(c) \
  ( (DB_IS_CONSTRAINT_UNIQUE_FAMILY(c) || (c) == DB_CONSTRAINT_INDEX || (c) == DB_CONSTRAINT_REVERSE_INDEX \
     || (c) == DB_CONSTRAINT_FOREIGN_KEY) ? true : false )

#define DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY(c) \
  ( ((DB_CONSTRAINT_TYPE) (c) == DB_CONSTRAINT_REVERSE_UNIQUE || (DB_CONSTRAINT_TYPE) (c) == DB_CONSTRAINT_REVERSE_INDEX) \
    ? true : false )

#define DB_IS_CONSTRAINT_FAMILY(c) \
  ( (DB_IS_CONSTRAINT_UNIQUE_FAMILY(c) || (c) == DB_CONSTRAINT_NOT_NULL || (c) == DB_CONSTRAINT_FOREIGN_KEY) ? true : false )

  /* Volume purposes constants.  These are intended for use by the db_add_volext API function. */
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

  /* Object fetch and locking constants.
   * These are used to specify a lock mode when fetching objects using of the explicit fetch and lock functions.
   */
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

  /* Authorization type identifier constants.  The numeric values of these are defined such that they can be used
   * with the bitwise or operator "|" in order to specify more than one authorization type.
   */
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

  /* object_id type constants used in a db_register_ldb api call to specify whether a local database supports intrinsic object
   * identity or user-defined object identity.
   */
  typedef enum
  {
    DB_OID_INTRINSIC = 1,
    DB_OID_USER_DEFINED
  } DB_OBJECT_ID_TYPE;

  /* These are abstract data type pointers used by the functions that issue SQL statements and return their results. */
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

  /* Structure used to contain information about the position of an error detected while compiling a statement. */
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
   * Currently they map directly onto internal unpublished data structures but that are subject to change. API programs are
   * allowed to use them only for those API functions that return them or accept them as arguments. API functions cannot
   * make direct structure references or make any assumptions about the actual definition of these structures.
   */
  typedef struct sm_attribute DB_ATTRIBUTE;
  typedef struct sm_method DB_METHOD;
  typedef struct sm_method_argument DB_METHARG;
  typedef struct sm_method_file DB_METHFILE;
  typedef struct sm_resolution DB_RESOLUTION;
  typedef struct sm_query_spec DB_QUERY_SPEC;
  typedef struct tp_domain DB_DOMAIN;

  /* These are handles to attribute and method descriptors that can be used for optimized lookup during repeated operations.
   * They are NOT the same as the DB_ATTRIBUTE and DB_METHOD handles.
   */
  typedef struct sm_descriptor DB_ATTDESC;
  typedef struct sm_descriptor DB_METHDESC;

  /* These structures are used for building editing templates on classes and objects. Templates allow the specification of
   * multiple operations to the object that are treated as an atomic unit. If any of the operations in the template fail,
   * none of the operations will be applied to the object. They are defined as abstract data types on top of internal data
   * structures, API programs are not allowed to make assumptions about the contents of these structures.
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
   * If a trigger is ACTIVE, it will be raised when its event is detected. If it is INACTIVE, it will not be raised. If it is
   * INVALID, it indicates that the class associated with the trigger has been deleted.
   */
  typedef enum
  {
    TR_STATUS_INVALID = 0,
    TR_STATUS_INACTIVE = 1,
    TR_STATUS_ACTIVE = 2
  } DB_TRIGGER_STATUS;


  /* These define the possible trigger event types.
   * The system depends on the numeric order of these constants, do not modify this definition without understanding the trigger
   * manager source.
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

  /* These define the possible trigger activity times. Numeric order is important here, don't change without understanding
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

  /* This is the generic pointer to database objects. An object may be either an instance or a class. The actual structure is
   * defined elsewhere and it is not necessary for database applications to understand its contents.
   */
  typedef struct db_object DB_OBJECT, *MOP;

  /* Structure defining the common list link header used by the general list routines. Any structure in the db_ layer that
   * are linked in lists will follow this convention.
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
   *  "object id" functions
   */
  typedef struct db_namelist DB_NAMELIST;

  struct db_namelist
  {
    struct db_namelist *next;
    const char *name;
  };

  /* List structure with additional object pointer field.
   * Might belong in dbtype.h but we rarely use object lists on the server.
   */
  typedef struct db_objlist DB_OBJLIST;
  typedef struct db_objlist *MOPLIST;

  struct db_objlist
  {
    struct db_objlist *next;
    struct db_object *op;
  };

  typedef struct sm_class_constraint DB_CONSTRAINT;
  typedef struct sm_function_index_info DB_FUNCTION_INDEX_INFO;

  /* Types of constraints that may be applied to attributes. This type is used by the db_add_constraint()/db_drop_constraint()
   * API functions.
   */
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
  /*
   * DB_MAX_IDENTIFIER_LENGTH -
   * This constant defines the maximum length of an identifier in the database. An identifier is anything that is passed as
   * a string to the db_ functions (other than user attribute values). This includes such things as class names, attribute names
   * etc. This isn't strictly enforced right now but applications must be aware that this will be a requirement.
   */
#define DB_MAX_IDENTIFIER_LENGTH 255

/* Maximum allowable user name.*/
#define DB_MAX_USER_LENGTH 32

#define DB_MAX_PASSWORD_LENGTH 8

/* Maximum allowable schema name. */
#define DB_MAX_SCHEMA_LENGTH DB_MAX_USER_LENGTH

/* Maximum allowable class name. */
#define DB_MAX_CLASS_LENGTH (DB_MAX_IDENTIFIER_LENGTH-DB_MAX_SCHEMA_LENGTH-4)

#define DB_MAX_SPEC_LENGTH       (0x3FFFFFFF)

/* Maximum allowable class comment length */
#define DB_MAX_CLASS_COMMENT_LENGTH     2048
/* Maximum allowable comment length */
#define DB_MAX_COMMENT_LENGTH    1024

/* This constant defines the maximum length of a character string that can be used as the value of an attribute. */
#define DB_MAX_STRING_LENGTH	0x3fffffff

/* This constant defines the maximum length of a bit string that can be used as the value of an attribute. */
#define DB_MAX_BIT_LENGTH 0x3fffffff

/* The maximum precision that can be specified for a numeric domain. */
#define DB_MAX_NUMERIC_PRECISION 38

/* The upper limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_OVERFLOW_LIMIT 1e38

/* The lower limit for a number that can be represented by a numeric type */
#define DB_NUMERIC_UNDERFLOW_LIMIT 1e-38

/* The maximum precision of CHAR(n) domain that can be specified for an INTL_UTF8_MAX_CHAR_SIZE.
 * We may need to define this functionally as the maximum precision will depend on the size multiplier of the codeset.
 */
#define DB_MAX_CHAR_PRECISION (DB_MAX_STRING_LENGTH/4)

/* The maximum precision that can be specified for a CHARACTER VARYING domain.*/
#define DB_MAX_VARCHAR_PRECISION DB_MAX_STRING_LENGTH

/* The maximum precision that can be specified for a NATIONAL CHAR(n) domain. This probably isn't restrictive enough.
 * We may need to define this functionally as the maximum precision will depend on the size multiplier of the codeset.
 */
#define DB_MAX_NCHAR_PRECISION (DB_MAX_STRING_LENGTH/2)

/* The maximum precision that can be specified for a NATIONAL CHARACTER VARYING domain. This probably isn't restrictive enough.
 * We may need to define this functionally as the maximum precision will depend on the size multiplier of the codeset.
 */
#define DB_MAX_VARNCHAR_PRECISION DB_MAX_NCHAR_PRECISION

/* The maximum precision that can be specified for a BIT domain. */
#define DB_MAX_BIT_PRECISION DB_MAX_BIT_LENGTH

/* The maximum precision that can be specified for a BIT VARYING domain. */
#define DB_MAX_VARBIT_PRECISION DB_MAX_BIT_PRECISION

/* This constant indicates that the system defined default for determining the length of a string is to be used for a DB_VALUE. */
#define DB_DEFAULT_STRING_LENGTH -1

/* This constant indicates that the system defined default for precision is to be used for a DB_VALUE. */
#define DB_DEFAULT_PRECISION -1

/* This constant indicates that the system defined default for scale is to be used for a DB_VALUE. */
#define DB_DEFAULT_SCALE -1

/* This constant defines the default precision of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_PRECISION 15

/* This constant defines the default scale of DB_TYPE_NUMERIC. */
#define DB_DEFAULT_NUMERIC_SCALE 0

/* This constant defines the default scale of result of numeric division operation */
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

/* DB_DATE_MIN and DB_DATE_MAX are calculated by julian_encode function with arguments (1,1,1) and (12,31,9999) respectively. */
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

  /* This defines the basic type identifier constants. These are used in the domain specifications of attributes and method
   * arguments and as value type tags in the DB_VALUE structures.
   */
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
    DB_TYPE_ELO = 9,		/* obsolete... keep for backward compatibility. maybe we can replace with something else */
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
    DB_TYPE_JSON = 40,

    /* aliases */
    DB_TYPE_LIST = DB_TYPE_SEQUENCE,
    DB_TYPE_SMALLINT = DB_TYPE_SHORT,	/* SQL SMALLINT */
    DB_TYPE_VARCHAR = DB_TYPE_STRING,	/* SQL CHAR(n) VARYING values */
    DB_TYPE_UTIME = DB_TYPE_TIMESTAMP,	/* SQL TIMESTAMP */

    DB_TYPE_LAST = DB_TYPE_JSON
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

  /* Structure used for the representation of universal times. These are compatible with the Unix time_t definition. */
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

  /* Definition for the collection descriptor structure. The structures for the collection descriptors and the sequence
   * descriptors are identical internally but not all db_collection functions can be used with sequences and no db_seq functions
   * can be used with sets. It is advisable to recognize the type of set being used, type it appropriately and only call those
   * db_ functions defined for that type.
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

  /* Used in btree_coerce_key and btree_ils_adjust_range to represent min or max values, necessary in index search comparisons */
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
   * This is the run-time state structure for an ELO. The ELO is part of the implementation of large object type and not intended
   * to be used directly by the API.
   */

  typedef struct vpid VPID;	/* REAL PAGE IDENTIFIER */
  struct vpid
  {
    int32_t pageid;		/* Page identifier */
    short volid;		/* Volume identifier where the page resides */
  };

  typedef struct vfid VFID;	/* REAL FILE IDENTIFIER */
  struct vfid
  {
    int32_t fileid;		/* File identifier */
    short volid;		/* Volume identifier where the file resides */
  };

#define VFID_INITIALIZER { NULL_FILEID, NULL_VOLID }

#define VFID_AS_ARGS(vfidp) (vfidp)->volid, (vfidp)->fileid

#define VPID_INITIALIZER { NULL_PAGEID, NULL_VOLID }

#define VPID_AS_ARGS(vpidp) (vpidp)->volid, (vpidp)->pageid

  /* Set a vpid with values of volid and pageid */
#define VPID_SET(vpid_ptr, volid_value, pageid_value) \
  do { \
    (vpid_ptr)->volid  = (volid_value);	\
    (vpid_ptr)->pageid = (pageid_value); \
  } while (0)

  /* Set the vpid to an invalid one */
#define VPID_SET_NULL(vpid_ptr) VPID_SET(vpid_ptr, NULL_VOLID, NULL_PAGEID)

  /* copy a VPID */
#define  VPID_COPY(dest_ptr, src_ptr) \
  do { \
    *(dest_ptr) = *(src_ptr); \
  } while (0)

  /* vpid1 == vpid2 ? */
#define VPID_EQ(vpid_ptr1, vpid_ptr2) \
  ((vpid_ptr1) == (vpid_ptr2) \
   || ((vpid_ptr1)->pageid == (vpid_ptr2)->pageid && (vpid_ptr1)->volid == (vpid_ptr2)->volid))

#define VPID_LT(vpid_ptr1, vpid_ptr2) \
  ((vpid_ptr1) != (vpid_ptr2) \
   && ((vpid_ptr1)->volid < (vpid_ptr2)->volid \
       || ((vpid_ptr1)->volid == (vpid_ptr2)->volid && (vpid_ptr1)->pageid < (vpid_ptr2)->pageid)))

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

  /* This is the memory representation of an internal object identifier. It is in the API only for a few functions that are not
   * intended for general use. An object identifier is NOT a fixed identifier; it cannot be used reliably as an object
   * identifier across database sessions or even across transaction boundaries. API programs are not allowed to make assumptions
   * about the contents of this structure.
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

  /* db_char.sm was formerly db_char.small.  small is an (undocumented) reserved word on NT. */

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
      const char *buf;
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

  typedef struct db_json DB_JSON;
  struct db_json
  {
    const char *schema_raw;
    JSON_DOC *document;
  };

  /* A union of all of the possible basic type values. This is used in the definition of the DB_VALUE which is the fundamental
   * structure used in passing data in and out of the db_ function layer.
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
    DB_JSON json;
  };

  /* This is the primary structure used for passing values in and out of the db_ function layer. Values are always tagged with
   * a datatype so that they can be identified and type checking can be performed.
   */
  typedef struct db_value DB_VALUE;
  struct db_value
  {
    DB_DOMAIN_INFO domain;
    DB_DATA data;
    need_clear_type need_clear;
  };

  /* This is used to chain DB_VALUEs into a list. */
  typedef struct db_value_list DB_VALUE_LIST;
  struct db_value_list
  {
    struct db_value_list *next;
    DB_VALUE val;
  };

  /* This is used to chain DB_VALUEs into a list. It is used as an argument to db_send_arglist. */
  typedef struct db_value_array DB_VALUE_ARRAY;
  struct db_value_array
  {
    int size;
    DB_VALUE *vals;
  };

  /* This is used to gather stats about the workspace.
   * It contains the number of object descriptors used and total number of object descriptors allocated.
   */
  typedef struct db_workspace_stats DB_WORKSPACE_STATS;
  struct db_workspace_stats
  {
    int obj_desc_used;		/* number of object descriptors used */
    int obj_desc_total;		/* total # of object descriptors allocated */
  };

  /* This defines the C language type identifier constants.
   * These are used to describe the types of values used for setting DB_VALUE contents or used to get DB_VALUE contents into.
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
  typedef const char *DB_CONST_C_CHAR;
  typedef char *DB_C_NCHAR;
  typedef const char *DB_CONST_C_NCHAR;
  typedef char *DB_C_BIT;
  typedef const char *DB_CONST_C_BIT;
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

  /* An attribute having valid default expression, must have NULL default value. Currently, we allow simple expressions
   * like SYS_DATE, CURRENT_TIME. Also we allow to_char expression.
   */
  typedef struct db_default_expr DB_DEFAULT_EXPR;
  struct db_default_expr
  {
    DB_DEFAULT_EXPR_TYPE default_expr_type;	/* default expression identifier */
    int default_expr_op;	/* default expression operator */
    const char *default_expr_format;	/* default expression format */
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
  /* From tz_support.h */
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
/********************************************************/

#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* _DBTYPE_DEF_H_ */
