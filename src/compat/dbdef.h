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
 * dbdef.h -Supporting definitions for the CUBRID API functions.
 *
 */

#ifndef _DBDEF_H_
#define _DBDEF_H_

#ident "$Id$"

#include "cubrid_api.h"

#define TRAN_ASYNC_WS_BIT                        0x10	/*        1  0000 */
#define TRAN_ISO_LVL_BITS                        0x0F	/*        0  1111 */

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

  DISK_PERMVOL_DATA_PURPOSE = 0,
  DISK_PERMVOL_INDEX_PURPOSE = 1,
  DISK_PERMVOL_GENERIC_PURPOSE = 2,
  DISK_PERMVOL_TEMP_PURPOSE = 3,

  DISK_TEMPVOL_TEMP_PURPOSE = 4,	/* internal use only */
  DISK_UNKNOWN_PURPOSE = 5,	/* internal use only: Does not mean anything */
  DISK_EITHER_TEMP_PURPOSE = 6	/* internal use only:
				 * Either pervol_temp or tempvol_tmp.. Used
				 * only to select a volume
				 */
} DB_VOLPURPOSE;


/* These are the status codes that can be returned by db_value_compare. */
typedef enum
{

  DB_SUBSET = -3,		/* strict subset for set types.         */
  DB_UNK = -2,			/* unknown                              */
  DB_LT = -1,			/* canonical less than                 */
  DB_EQ = 0,			/* equal                                */
  DB_GT = 1,			/* canonical greater than,             */
  DB_NE = 2,			/* not equal because types incomparable */
  DB_SUPERSET = 3		/* strict superset for set types.       */
} DB_VALUE_COMPARE_RESULT;

/* Object fetch and locking constants.  These are used to specify
   a lock mode when fetching objects using of the explicit fetch and
   lock functions. */
typedef enum
{
  DB_FETCH_READ = 0,		/* Read an object (class or instance)   */
  DB_FETCH_WRITE = 1,		/* Update an object (class or instance) */
  DB_FETCH_DIRTY = 2,		/* Does not care about the state
				 * of the object (class or instance). Get
				 * it even if it is obsolete or if it
				 * becomes obsolete.
				 * INTERNAL USE ONLY
				 */
  DB_FETCH_CLREAD_INSTREAD = 3,	/* Read the class and read an instance of
				 * class.
				 * This is to access an instance in shared
				 * mode
				 * Note class must be given
				 * INTERNAL USE ONLY
				 */
  DB_FETCH_CLREAD_INSTWRITE = 4,	/* Read the class and update an instance
					 * of the class.
					 * Note class must be given
					 * This is for creation of instances
					 * INTERNAL USE ONLY
					 */
  DB_FETCH_QUERY_READ = 5,	/* Read the class and query (read) all
				 * instances of the class.
				 * Note class must be given
				 * This is for SQL select
				 * INTERNAL USE ONLY
				 */
  DB_FETCH_QUERY_WRITE = 6,	/* Read the class and query (read) all
				 * instances of the class and update some
				 * of those instances.
				 * Note class must be given
				 * This is for Query update (SQL update)
				 * or Query delete (SQL delete)
				 * INTERNAL USE ONLY
				 */
  DB_FETCH_SCAN = 7,		/* Read the class for scan purpose
				 * The lock of the lock should be kept
				 * since the actual access happens later.
				 * This is for loading an index.
				 * INTERNAL USE ONLY
				 */
  DB_FETCH_EXCLUSIVE_SCAN = 8	/* Read the class for exclusive scan purpose
				 * The lock of the lock should be kept
				 * since the actual access happens later.
				 * This is for loading an index.
				 * INTERNAL USE ONLY
				 */
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
  TR_EVENT_ABORT = 10,		/* currently unsupported */
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
#define DB_EMPTY_SESSION			0
/* uninitialized value for row count */
#define DB_ROW_COUNT_NOT_SET			-2
#define SERVER_SESSION_KEY_SIZE			8

typedef struct session_key SESSION_KEY;
struct session_key
{
  SESSION_ID id;		/* hash key for a session. */
  int fd;			/* the socket(file) descriptor of
				 * the associated connection.
				 */
};
typedef struct dbdef_vol_ext_info DBDEF_VOL_EXT_INFO;
struct dbdef_vol_ext_info
{
  char *path;			/* Directory where the volume extension is created.
				 *  If NULL, is given, it defaults to the system parameter. */
  const char *name;		/* Name of the volume extension
				 * If NULL, system generates one like "db".ext"volid" where
				 * "db" is the database name and "volid" is the volume
				 * identifier to be assigned to the volume extension. */
  const char *comments;		/* Comments which are included in the volume extension header. */
  int max_npages;		/* Maximum pages of this volume */
  int extend_npages;		/* Number of pages to extend - used for generic volume only */
  int max_writesize_in_sec;	/* the amount of volume written per second */
  DB_VOLPURPOSE purpose;	/* The purpose of the volume extension. One of the following:
				 * - DISK_PERMVOL_DATA_PURPOSE,
				 * - DISK_PERMVOL_INDEX_PURPOSE,
				 * - DISK_PERMVOL_GENERIC_PURPOSE,
				 * - DISK_PERMVOL_TEMP_PURPOSE, */
  bool overwrite;
};

typedef enum
{
  DB_PARTITION_HASH = 0,
  DB_PARTITION_RANGE,
  DB_PARTITION_LIST
} DB_PARTITION_TYPE;

typedef enum
{
  DB_NOT_PARTITIONED_CLASS = 0,
  DB_PARTITIONED_CLASS = 1,
  DB_PARTITION_CLASS = 2
} DB_CLASS_PARTITION_TYPE;

#endif /* _DBDEF_H_ */
