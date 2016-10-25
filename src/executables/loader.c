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
 * loader.c - Database loader (Optimized version)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#if defined (WINDOWS)
#include <io.h>
#else
#include <unistd.h>
#endif /* !WINDOWS */
#include <errno.h>
#include "porting.h"
#include "utility.h"
#include "dbi.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "network.h"
#include "authenticate.h"
#include "schema_manager.h"
#include "object_accessor.h"

#include "db.h"
#include "loader_object_table.h"
#include "load_object.h"
#include "loader.h"
#include "work_space.h"
#include "message_catalog.h"
#include "locator_cl.h"
#include "elo.h"
#include "intl_support.h"
#include "language_support.h"
#include "environment_variable.h"
#include "set_object.h"
#include "trigger_manager.h"
#include "execute_schema.h"
#include "transaction_cl.h"
#include "locator_cl.h"

/* this must be the last header file included!!! */
#include "dbval.h"

extern bool No_oid_hint;
extern int loader_yylineno;

#define LDR_MAX_ARGS 32

#define LDR_INCREMENT_ERR_TOTAL(context) ldr_increment_err_total(context)
#define LDR_INCREMENT_ERR_COUNT(context, i) \
                                           ldr_increment_err_count(context, i)
#define LDR_CLEAR_ERR_TOTAL(context)     ldr_clear_err_total(context)
#define LDR_CLEAR_ERR_COUNT(context)     ldr_clear_err_count(context)

/* filter out ignorable errid */
#define FILTER_OUT_ERR_INTERNAL(err, expr)                              \
  ( err = ((expr) == NO_ERROR ? NO_ERROR : er_filter_errid(false)) )

#define CHECK_ERR(err, expr)                                            \
  do {                                                                  \
    int inner_err = (expr);                                             \
    if (FILTER_OUT_ERR_INTERNAL(err, inner_err) != NO_ERROR) {          \
      display_error(0);                                                 \
      goto error_exit;                                                  \
    }                                                                   \
    if (inner_err != NO_ERROR && err == NO_ERROR ) {                    \
      skip_current_instance = true;                                     \
    }                                                                   \
  } while (0)

/*
 * CHECK_PARSE_ERR is used by the element setters to output more information
 * about the source token and destination attribute being processed, if
 * an error occurs.
 */
#define CHECK_PARSE_ERR(err, expr, cont, type, str)                     \
  do {                                                                  \
    int inner_err = (expr);                                             \
    if (FILTER_OUT_ERR_INTERNAL(err, inner_err) != NO_ERROR) {          \
      display_error(0);                                                 \
      parse_error(cont, type, str);                                     \
      goto error_exit;                                                  \
    }                                                                   \
    if (inner_err != NO_ERROR && err == NO_ERROR) {                     \
      skip_current_instance = true;                                     \
    }                                                                   \
  } while (0)

#define CHECK_PTR(err, expr)                                            \
  do {                                                                  \
    if ((expr) == NULL) {                                               \
      display_error(0);                                                 \
      if (err == NO_ERROR) {                                            \
        if ((err = er_errid()) == NO_ERROR) { /* need to set errid */   \
          err = ER_GENERIC_ERROR;                                       \
          er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);             \
        }                                                               \
      }                                                                 \
      goto error_exit;                                                  \
    }                                                                   \
  } while (0)

#define CHECK_CONTEXT_VALIDITY(context, expr)                           \
  do {                                                                  \
    if ((expr)) {                                                       \
      LDR_INCREMENT_ERR_COUNT(context, 1);                              \
      context->valid = false;                                           \
    }                                                                   \
  } while (0)

#define RETURN_IF_NOT_VALID(context)                                    \
  do {                                                                  \
    if (!context->valid) return;                                        \
  } while (0)

#define RETURN_IF_NOT_VALID_WITH(context, ret)                          \
  do {                                                                  \
    if (!context->valid) return (ret);                                  \
  } while (0)

#define CHECK_VALIDATION_ONLY(context)                                  \
  do {                                                                  \
    if (context->validation_only) goto error_exit;                      \
  } while (0)

#define GET_DOMAIN(context, domain)                                      \
	    do {                                                         \
	      if (context->collection == NULL && context->valid)         \
	        domain = context->attrs[context->next_attr].att->domain; \
	      else                                                       \
	        domain = context->set_domain;                            \
	     } while (0)

#define  CHECK_SKIP()                        \
         do {                                \
           if (skip_current_class == true) { \
             return;                         \
           }                                 \
         } while (0)

#define  CHECK_SKIP_WITH(ret)                \
         do {                                \
           if (skip_current_class == true) { \
             return (ret);                   \
           }                                 \
         } while (0)

#define IS_OLD_GLO_CLASS(class_name)                    \
	 (strncasecmp ((class_name), "glo", MAX(strlen(class_name), 3)) == 0      || \
	  strncasecmp ((class_name), "glo_name", MAX(strlen(class_name), 8)) == 0  || \
	  strncasecmp ((class_name), "glo_holder", MAX(strlen(class_name), 10)) == 0)

#define FREE_STRING(s) \
do { \
  if ((s)->need_free_val) free_and_init ((s)->val); \
  if ((s)->need_free_self) free_and_init ((s)); \
} while (0)

typedef int (*LDR_SETTER) (LDR_CONTEXT *, const char *, int, SM_ATTRIBUTE *);
typedef int (*LDR_ELEM) (LDR_CONTEXT *, const char *, int, DB_VALUE *);

/*
 * LDR_ATTDESC
 *    Loader attribute description structure.
 *    This contains the description, attribute, and fast setter function
 *    for the attribute.
 *    parser_* holds the parser token information and is used when parsing
 *    the constructor syntax, to simulate the parse phase when creating a
 *    constructor object.
 */

typedef struct LDR_ATTDESC
{

  DB_ATTDESC *attdesc;		/* Attribute descriptor */
  SM_ATTRIBUTE *att;		/* Attribute */
  LDR_SETTER setter[NUM_LDR_TYPES];	/* Setter functions indexed by type */

  char *parser_str;		/* used as a holder to hold the parser strings when parsing method arguments. */
  int parser_str_len;		/* Length of parser token string */
  int parser_buf_len;		/* Length of parser token buffer */
  LDR_TYPE parser_type;		/* Used when parsing method arguments, to store */
  /* the type information. */

  DB_OBJECT *ref_class;		/* Class referenced by object reference */
  int instance_id;		/* Instance id of instance referenced by ref_class object ref */
  TP_DOMAIN *collection_domain;	/* Best domain for collection */

} LDR_ATTDESC;

/*
 * LDR_MOP_TEMPOID_MAP
 * LDR_MOP_TEMPOID_MAPS
 *    Information about a temporary OID created while parsing the load file,
 *    LDR_MOP_TEMPOID_MAP.
 *    An array of these structures will be used to hold MOP -> CLASS_TABLE
 *    entry information, LDR_MOP_TEMPOID_MAPS. The CLASS_TABLE and id will
 *    be used to access the INST_INFO structure to  directly access the
 *    oid pointer in the instances array of the loaders otable.
 *    Prior to flushing and LC_OIDSET is created with the list
 *    of mops in this structure. This set is sent to the server to obtain
 *    permanent OIDs. The loader's otable is updated with the permanent OIDs
 *    sent back from the server.
 */

typedef struct ldr_mop_tempoid_map
{

  MOP mop;			/* Workspace MOP */
  CLASS_TABLE *table;		/* otable class table */
  int id;			/* instance identifier in table */

} LDR_MOP_TEMPOID_MAP;

typedef struct ldr_mop_tempoid_maps
{

  int count;			/* number of LDR_MOP_TEMPOID_MAP maps */
  int index;			/* next available slot */
  int size;			/* size of LDR_MOP_TEMPOID_MAP array */

  LDR_MOP_TEMPOID_MAP *mop_tempoid_maps;	/* array of maps */

} LDR_MOP_TEMPOID_MAPS;

static LDR_MOP_TEMPOID_MAPS *ldr_Mop_tempoid_maps = NULL;

/*
 * LDR_CONTEXT
 *    The loader context, holds the state information for the parser actions
 *    to use when triggered.
 */

struct LDR_CONTEXT
{
  DB_OBJECT *cls;		/* Current class */

  char *class_name;		/* Class name of current class */

  DB_OBJECT *obj;		/* Instance of class */
  MOBJ mobj;			/* Memory object of instance */
  int obj_pin;			/* pins returned when pinning the */
  int class_pin;		/* current class */
  int class_type;		/* not partitioned, partitioned, partition */
  LDR_ATTDESC *attrs;		/* array of attribute descriptors for */
  /* the current class.  */
  int num_attrs;		/* Number of attributes for class */
  int next_attr;		/* Index of current attribute */

  unsigned int instance_started:1;	/* Instance stared flag */
  bool valid;			/* State of the loader.  */
  bool verbose;			/* Verbosity flag */
  int periodic_commit;		/* instances prior to committing */

  int err_count;		/* Current error count for instance */
  int err_total;		/* Total error counter */

  int inst_count;		/* Instance count for this class */
  int inst_total;		/* Total instance count */
  int inst_num;			/* Instance id of current instance */

  int flush_total;		/* Total number of flushes performed */
  /* for the current class */
  int flush_interval;		/* The number of instances before a */
  /* flush is performed */

  CLASS_TABLE *table;		/* Table of instances currently */
  /* accumlated for the class */

  bool validation_only;		/* Syntax checking only flag */

  INTL_LANG lang_id;		/* Current language */

  DB_COLLECTION *collection;	/* Pointer to hang collection from */
  TP_DOMAIN *set_domain;	/* Set domain of current set if */
  /* applicable.  */

  SM_METHOD *constructor;	/* Method constructor */
  int arg_index;		/* Index where arguments start in */
  /* the attr descriptor array.  */
  int maxarg;			/* Maximum number of arguments */
  SM_METHOD_ARGUMENT **args;	/* Pointer to the arguments */
  int arg_count;		/* argument counter */

  DB_OBJECT *id_class;		/* Holds class ptr when processing ids */

  LDR_ATTRIBUTE_TYPE attribute_type;	/* type of attribute if class */
  /* attribute, shared, default */

  int status_count;		/* Count used to indicate number of */
  /* instances committed for internal */
  /* debugging use only */
  int status_counter;		/* Internal debug instance counter */
  int commit_counter;		/* periodic commit counter */
  int default_count;		/* the number of instances with */
  /* values */
  LDR_CONSTANT *cons;		/* constant list for instance line */
};

char **ignore_class_list = NULL;
int ignore_class_num = 0;
bool skip_current_class = false;
bool skip_current_instance = false;

/*
 * ldr_Current_context, ldr_Context
 *    Global variables holding the parser current context information.
 *    This pointer is exported and used by the parser module ld.g
 *
 */
LDR_CONTEXT *ldr_Current_context;
static LDR_CONTEXT ldr_Context;

/*
 * ldr_Hint_locks
 * ldr_Hint_classnames
 * ldr_Hint_subclasses
 * ldr_Hint_flags
 *    Global array used to hold the class read, instance write lock
 *    set up at initialization.  This will be used by ldr_find_class()
 *    This is a temporary solution to fix the client/server deadlock problem.
 *    This needs to be done for all classes, perhaps attached to the otable.
 */
#define LDR_LOCKHINT_COUNT 1
static LOCK ldr_Hint_locks[LDR_LOCKHINT_COUNT];
static const char *ldr_Hint_classnames[LDR_LOCKHINT_COUNT];
static int ldr_Hint_subclasses[LDR_LOCKHINT_COUNT];
static LC_PREFETCH_FLAGS ldr_Hint_flags[LDR_LOCKHINT_COUNT];

/*
 * elem_converter
 *   array to fucntions indexed by attribute type for for setting elements in
 *   in a collection.
 */
static LDR_ELEM elem_converter[NUM_LDR_TYPES];

/*
 * Total_objects
 *    Global total object count.
 */
static int Total_objects = 0;
static int Last_committed_line = 0;
static int Total_fails = 0;

/*
 * ldr_post_commit_handler
 *    Post commit callback function. Called with number of instances
 *    committed.
 */
static LDR_POST_COMMIT_HANDLER ldr_post_commit_handler = NULL;

/*
 * ldr_post_interrupt_handler
 *    Post commit interrupt handler. Called with number of instances
 *    committed.
 */

static LDR_POST_INTERRUPT_HANDLER ldr_post_interrupt_handler = NULL;

/*
 * ldr_Load_interrupted
 *    Global flag which is turned on when an interrupt is received, via
 *    ldr_interrupt_has_occurred()
 *    Values : LDR_INTERRUPT_TYPE :
 *               LDR_NO_INTERRUPT
 *               LDR_STOP_AND_ABORT_INTERRUPT
 *               LDR_STOP_AND_COMMIT_INTERRUPT
 */

static int ldr_Load_interrupted = LDR_NO_INTERRUPT;
static jmp_buf *ldr_Jmp_buf = NULL;

/*
 * Id_map
 * Id_map_size
 *    The global class id map.
 *    The class id map provides a textually shorter way to reference classes.
 *    When the %class <name> <id> statement is parsed, an entry will be made
 *    in this map table so the class can be referenced by id number rather
 *    than continually using the full class name.
 */
static DB_OBJECT **Id_map = NULL;
static int Id_map_size = 0;

/*
 * internal_classes
 *    Global list of internal_classes.
 *    These are the classes that we don't allow to be loaded.
 *    Initialized by clist_init().
 */
static DB_OBJLIST *internal_classes = NULL;

/*
 *                        DB_VALUE TEMPLATES
 *
 *  These templates are used to avoid the overhead of calling
 *  db_value_domain_init incessantly.  Incredibly, that adds up when you're
 *  dealing with thousands of attributes and values.
 *
 *  This is very dangerous from a maintenance standpoint, because it means
 *  that this code now has to know and obey all of the quirks for the various
 *  fields.  If any of the handling in dbtype.h and/or db_macro.c changes,
 *  this code will probably need to change, too.
 */

static DB_VALUE ldr_short_tmpl;
static DB_VALUE ldr_int_tmpl;
static DB_VALUE ldr_bigint_tmpl;
static DB_VALUE ldr_char_tmpl;
static DB_VALUE ldr_varchar_tmpl;
static DB_VALUE ldr_float_tmpl;
static DB_VALUE ldr_double_tmpl;
static DB_VALUE ldr_date_tmpl;
static DB_VALUE ldr_time_tmpl;
static DB_VALUE ldr_timeltz_tmpl;
static DB_VALUE ldr_timetz_tmpl;
static DB_VALUE ldr_timestamp_tmpl;
static DB_VALUE ldr_timestampltz_tmpl;
static DB_VALUE ldr_timestamptz_tmpl;
static DB_VALUE ldr_datetime_tmpl;
static DB_VALUE ldr_datetimeltz_tmpl;
static DB_VALUE ldr_datetimetz_tmpl;
static DB_VALUE ldr_blob_tmpl;
static DB_VALUE ldr_clob_tmpl;
static DB_VALUE ldr_bit_tmpl;

/* default for 64 bit signed big integers, i.e., 9223372036854775807 (0x7FFFFFFFFFFFFFFF) */
#define MAX_DIGITS_FOR_BIGINT   19
/* default for 32 bit signed integers, i.e., 2147483647 (0x7FFFFFFF) */
#define MAX_DIGITS_FOR_INT      10
#define MAX_DIGITS_FOR_SHORT    5

#define ROUND(x) (int)((x) > 0 ? ((x) + .5) : ((x) - .5))

#define PARSE_ELO_STR(str, new_len)           \
do                                            \
  {                                           \
    if (str[0] == '\"')                       \
      str++;                                  \
    new_len = strlen(str);                    \
    if (new_len &&  str[new_len-1] == '\"')   \
      new_len--;                              \
  }                                           \
while (0)

/*
 * presize for mop_tempoid_map  array.
 * Also used as the increment size to grow the maps array.
 */
#define LDR_MOP_TEMPOID_MAPS_PRESIZE 1000
#define LDR_ARG_GROW_SIZE 128
#define ENV_LOADDB_STATUS "LOADDB_STATUS"

static void ldr_increment_err_count (LDR_CONTEXT * context, int i);
static void ldr_clear_err_count (LDR_CONTEXT * context);
static void ldr_clear_err_total (LDR_CONTEXT * context);
static const char *ldr_class_name (LDR_CONTEXT * context);
static const char *ldr_attr_name (LDR_CONTEXT * context);
static int select_set_domain (LDR_CONTEXT * context, TP_DOMAIN * domain, TP_DOMAIN ** set_domain_ptr);
static int check_object_domain (LDR_CONTEXT * context, DB_OBJECT * class_, DB_OBJECT ** actual_class);
static int check_class_domain (LDR_CONTEXT * context);
static void idmap_init (void);
static void idmap_final (void);
static int idmap_grow (int size);
static int ldr_assign_class_id (DB_OBJECT * class_, int id);
static DB_OBJECT *ldr_find_class (const char *classname);
static DB_OBJECT *ldr_get_class_from_id (int id);
static void ldr_clear_context (LDR_CONTEXT * context);
static void ldr_clear_and_free_context (LDR_CONTEXT * context);
static void ldr_internal_error (LDR_CONTEXT * context);
static void display_error_line (int adjust);
static void display_error (int adjust);
static void ldr_invalid_class_error (LDR_CONTEXT * context);
static void parse_error (LDR_CONTEXT * context, DB_TYPE token_type, const char *token);
static int clist_init (void);
static void clist_final (void);
static int is_internal_class (DB_OBJECT * class_);
static void ldr_act_elem (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type);
static void ldr_act_meth (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type);
static int ldr_mismatch (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_ignore (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_generic (LDR_CONTEXT * context, DB_VALUE * value);
static int ldr_null_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_null_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_class_attr_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att,
				      DB_VALUE * val);
static void ldr_act_class_attr (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type);
static int ldr_sys_user_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_sys_class_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_int_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_int_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_int_db_bigint (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_int_db_int (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_int_db_short (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_str_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_str_db_char (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_str_db_varchar (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_str_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_bstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_bstr_db_varbit (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_xstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_xstr_db_varbit (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_nstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_nstr_db_varnchar (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_numeric_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_numeric_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_double_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_float_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_real_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_real_db_float (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_real_db_double (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_date_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_date_db_date (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_time_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_time_db_time (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_timetz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timeltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timetz_db_timetz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_timeltz_db_timeltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_timestamp_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timestamp_db_timestamp (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_timestamptz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timestampltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timestamptz_db_timestamptz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_timestampltz_db_timestampltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_datetime_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_datetime_db_datetime (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_datetimetz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_datetimeltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_datetimetz_db_datetimetz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_datetimeltz_db_datetimeltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static void ldr_date_time_conversion_error (const char *token, DB_TYPE type);
static int ldr_check_date_time_conversion (const char *str, LDR_TYPE type);
static int ldr_elo_int_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_elo_int_db_elo (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_elo_ext_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_elo_ext_db_elo (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_mop_tempoid_maps_init (void);
static void ldr_mop_tempoid_maps_final (void);
static int ldr_add_mop_tempoid_map (MOP mop, CLASS_TABLE * table, int id);
static int ldr_assign_all_perm_oids (void);
static int find_instance (LDR_CONTEXT * context, DB_OBJECT * class_, OID * oid, int id);
static int ldr_class_oid_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_class_oid_db_object (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_oid_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_oid_db_object (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_monetary_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_monetary_db_monetary (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_collection_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_collection_db_collection (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att);
static int ldr_reset_context (LDR_CONTEXT * context);
static void ldr_flush (LDR_CONTEXT * context);
static int check_commit (LDR_CONTEXT * context);
static void ldr_restore_pin_and_drop_obj (LDR_CONTEXT * context, bool drop_obj);
static int ldr_finish_context (LDR_CONTEXT * context);
static int ldr_refresh_attrs (LDR_CONTEXT * context);
static int update_default_count (CLASS_TABLE * table, OID * oid);
static int update_default_instances_stats (LDR_CONTEXT * context);
static int insert_instance (LDR_CONTEXT * context);
static MOP construct_instance (LDR_CONTEXT * context);
static int insert_meth_instance (LDR_CONTEXT * context);
static int add_element (void ***elements, int *count, int *max, int grow);
static int add_argument (LDR_CONTEXT * context);
static void invalid_class_id_error (LDR_CONTEXT * context, int id);
static int ldr_init_loader (LDR_CONTEXT * context);
static void ldr_abort (void);
static void ldr_process_object_ref (LDR_OBJECT_REF * ref, int type);
static int ldr_act_add_class_all_attrs (LDR_CONTEXT * context, const char *class_name);

/* default action */
void (*ldr_act) (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type) = ldr_act_attr;

/*
 * ldr_increment_err_total - increment err_total count of the given context
 *    return: void
 *    context(out): context
 */
void
ldr_increment_err_total (LDR_CONTEXT * context)
{
  if (context)
    {
      context->err_total += 1;
    }
}

/*
 * ldr_increment_err_count - increment err_count of the given context
 *    return: void
 *    context(out): context
 *    i(in): count
 */
static void
ldr_increment_err_count (LDR_CONTEXT * context, int i)
{
  if (context)
    {
      context->err_count += i;
    }
}

/*
 * ldr_clear_err_count - clear err_count of a context
 *    return: in
 *    context(out): context
 */
static void
ldr_clear_err_count (LDR_CONTEXT * context)
{
  if (context)
    {
      context->err_count = 0;
    }
}

/*
 * ldr_clear_err_total - clear err_total of a context
 *    return: void
 *    context(out): context
 */
static void
ldr_clear_err_total (LDR_CONTEXT * context)
{
  if (context)
    {
      context->err_total = 0;
    }
}

/*
 * ldr_increment_fails - increment Total_fails count
 *    return: void
 */
void
ldr_increment_fails ()
{
  Total_fails++;
}

/*
 * ldr_class_name - Returns the name of the class currently being loaded.
 *    return: name
 *    context(in): context
 * Note:
 *    Used predominately to get the class name when an error occurs.
 */
static const char *
ldr_class_name (LDR_CONTEXT * context)
{
  static const char *name = NULL;

  if (context)
    {
      if (context->cls)
	{
	  name = db_get_class_name (context->cls);
	}
    }

  return name;
}

/*
 * ldr_attr_name - Returns the name of the attribute we are currently dealing
 * with.
 *    return: name
 *    context(in): context
 * Note:
 *    Used predominately to get the attribute name when an error occurs.
 */
static const char *
ldr_attr_name (LDR_CONTEXT * context)
{
  static const char *name = NULL;

  if (context && context->attrs && context->valid)
    {
      if (context->num_attrs >= context->next_attr)
	{
	  if (context->num_attrs)
	    {
	      name = context->attrs[context->next_attr].att->header.name;
	    }
	  else
	    {
	      /* haven't processed an attribute yet */
	      name = "";
	    }
	}
      else
	{
	  /* should return some kind of string representation for the current method argument */
	  name = "";
	}
    }

  return name;
}

/*
 * select_set_domain - looks through a domain list and selects a domain that
 * is one of the set types.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 *    domain(in): target domain
 *    set_domain_ptr(out): returned set domain
 * Note:
 *    Work functions for ldr_act_add*() functions, when the target type is
 *    collection.
 *    In all current cases, there will be only one element in the target
 *    domain list.
 *    Assuming there were more than one, we should be smarter and select
 *    the most "general" domain, for now, just pick the first one.
 *    NOTE : This is similar to tp_domain_select(), expect we are not dealing
 *    with a value here. The checking here should match the checking peformed
 *    by relevant code in tp_domain_select()
 */
static int
select_set_domain (LDR_CONTEXT * context, TP_DOMAIN * domain, TP_DOMAIN ** set_domain_ptr)
{
  int err = NO_ERROR;
  TP_DOMAIN *best, *d;

  /* 
   * Must pick an appropriate set domain, probably we should pick
   * the most general if there are more than one possibilities.
   * In practice, this won't ever happen until we allow nested
   * sets or union domains.
   */
  best = NULL;
  for (d = domain; d != NULL && best == NULL; d = d->next)
    {
      if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (d)))
	{
	  /* pick the first one */
	  best = d;
	}
    }

  if (best == NULL)
    {
      err = ER_LDR_DOMAIN_MISMATCH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 4, ldr_attr_name (context), ldr_class_name (context),
	      domain->type->name, db_get_type_name (DB_TYPE_SET));
    }
  else
    {
      if (set_domain_ptr != NULL)
	{
	  *set_domain_ptr = best;
	}
    }

  return err;
}

/*
 * check_object_domain - checks the type of an incoming value against the
 * target domain.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 *    class(in): class of incoming object reference
 *    actual_class(out): class to expect (if first arg is NULL)
 * Note:
 *    If they don't match LDR_DOMAIN_MISMATCH is returned.
 *    If "class" is NULL, we try to determine what the class will really
 *    be by examining the domain.  If there is only one possible class
 *    in the domain, we return it through "actual_class".
 *    If there are more than one possible classes in the domain,
 *    we return the LDR_AMBIGUOUS_DOMAIN error.
 */
static int
check_object_domain (LDR_CONTEXT * context, DB_OBJECT * class_, DB_OBJECT ** actual_class)
{
  int err = NO_ERROR;
  TP_DOMAIN *domain, *best, *d;

  GET_DOMAIN (context, domain);

  if (class_ == NULL)
    {
      /* its an object but no domain was specified, see if we can unambiguously select one. */
      if (domain == NULL)
	{
	  err = ER_LDR_AMBIGUOUS_DOMAIN;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, ldr_attr_name (context), ldr_class_name (context));
	}
      else
	{
	  for (d = domain; d != NULL; d = d->next)
	    {
	      if (d->type == tp_Type_object)
		{
		  if (class_ == NULL && d->class_mop != NULL)
		    {
		      class_ = d->class_mop;
		    }
		  else
		    {
		      class_ = NULL;
		      break;
		    }
		}
	    }
	  if (class_ == NULL)
	    {
	      err = ER_LDR_AMBIGUOUS_DOMAIN;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, ldr_attr_name (context), ldr_class_name (context));
	    }
	}
    }
  else
    {
      if (domain != NULL)
	{
	  /* make sure we have a compabile class in the domain list */
	  best = tp_domain_select_type (domain, DB_TYPE_OBJECT, class_, 1);
	  if (best == NULL)
	    {
	      err = ER_LDR_OBJECT_DOMAIN_MISMATCH;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, ldr_attr_name (context), ldr_class_name (context),
		      db_get_class_name (class_));
	    }
	}
    }

  if (actual_class != NULL)
    {
      *actual_class = class_;
    }

  return err;
}

/*
 * check_class_domain - checks the domain for an incoming reference to an
 * actual class object (not an instance).
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 * Note:
 *    For these references, the target domain must contain a wildcard
 *    "object" domain.
 */
static int
check_class_domain (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  TP_DOMAIN *domain, *d;

  GET_DOMAIN (context, domain);

  /* the domain must support "object" */
  if (domain != NULL)
    {
      for (d = domain; d != NULL; d = d->next)
	{
	  if (d->type == tp_Type_object && d->class_mop == NULL)
	    {
	      goto error_exit;	/* we found it */
	    }
	}

      /* 
       * could make this more specific but not worth the trouble
       * right now, can only happen in internal trigger objects
       */
      err = ER_LDR_CLASS_OBJECT_REFERENCE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
    }

error_exit:
  return err;
}

/*
 * idmap_init - Initialize the global class id map.
 *    return: void
 */
static void
idmap_init (void)
{
  Id_map = NULL;
  Id_map_size = 0;
}


/*
 * idmap_final - free the class id map
 *    return: void
 */
static void
idmap_final (void)
{
  if (Id_map != NULL)
    {
      free_and_init (Id_map);
      Id_map_size = 0;
    }
}

/*
 * idmap_grow - This makes sure the class id map is large enough to accomodate
 * the given index.
 *    return: NO_ERROR if successful, error code otherwise
 *    size(in): element index we want to set
 */
static int
idmap_grow (int size)
{
  int err = NO_ERROR;
  int newsize, i;
  DB_OBJECT **id_map_old;

  if (size > Id_map_size)
    {
      newsize = size + 10;	/* some extra for growth */
      id_map_old = Id_map;
      Id_map = (DB_OBJECT **) realloc (Id_map, (sizeof (DB_OBJECT *) * newsize));
      if (Id_map == NULL)
	{
	  /* Prevent leakage if we get a memory problem. */
	  if (id_map_old)
	    {
	      free_and_init (id_map_old);
	    }
	  err = ER_LDR_MEMORY_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	}
      else
	{
	  for (i = Id_map_size; i < newsize; i++)
	    {
	      Id_map[i] = NULL;
	    }

	  Id_map_size = newsize;
	}
    }

  return err;
}

/*
 * ldr_assign_class_id - assign an id to a class
 *    return: NO_ERROR if successful, error code otherwise
 *    class(in): class object
 *    id(in): id for this class
 * Note:
 *    An entry will be made to the global class map.
 */
static int
ldr_assign_class_id (DB_OBJECT * class_, int id)
{
  int err;

  err = idmap_grow (id + 1);
  if (err == NO_ERROR)
    {
      Id_map[id] = class_;
    }

  return err;
}

/*
 * ldr_act_set_id - Assign an id number to a class.
 *    return: void
 *    context(in/out): context
 *    id(in): class id
 */
void
ldr_act_set_id (LDR_CONTEXT * context, int id)
{
  int err = NO_ERROR;

  if (context->id_class != NULL)
    {
      context->inst_num = id;
      CHECK_ERR (err, ldr_assign_class_id (context->id_class, id));
      context->id_class = NULL;
    }
error_exit:
  CHECK_CONTEXT_VALIDITY (context, err != NO_ERROR);
}

/*
 * ldr_find_class - find a class hinting that we need the class for reading and
 * for writing instances.
 *    return: class db_object
 *    classname(in): string
 * Note:
 *   This uses the ldr_Hint* arrays global arrays. The lock that we require
 *   is setup once in ldr_init_loader().
 */
static DB_OBJECT *
ldr_find_class (const char *classname)
{
  LC_FIND_CLASSNAME find;
  DB_OBJECT *class_ = NULL;
  char realname[SM_MAX_IDENTIFIER_LENGTH];
  int err = NO_ERROR;

  /* Check for internal error */
  if (classname == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      display_error (0);
      return NULL;
    }

  sm_downcase_name (classname, realname, SM_MAX_IDENTIFIER_LENGTH);
  ldr_Hint_classnames[0] = realname;

  find =
    locator_lockhint_classes (1, ldr_Hint_classnames, ldr_Hint_locks, ldr_Hint_subclasses, ldr_Hint_flags, 1,
			      NULL_LOCK);

  if (find == LC_CLASSNAME_EXIST)
    {
      class_ = db_find_class (classname);
    }

  ldr_Hint_classnames[0] = NULL;

  return (class_);
}

/*
 * ldr_get_class_from_id - return the class with the given 'id' from global
 * class id map
 *    return: class object
 *    id(in): class id
 */
static DB_OBJECT *
ldr_get_class_from_id (int id)
{
  DB_OBJECT *class_ = NULL;

  if (id <= Id_map_size)
    {
      class_ = Id_map[id];
    }

  return (class_);
}

/*
 * ldr_act_start_id - Begin the specification of a class id assignment.
 *    return: void
 *    context(in/out): context
 *    name(in): class name
 */
void
ldr_act_start_id (LDR_CONTEXT * context, char *name)
{
  DB_OBJECT *class_;
  bool is_ignore_class;

  if (!context->validation_only)
    {
      class_ = ldr_find_class (name);
      if (class_ != NULL)
	{
	  context->id_class = class_;
	}
      else
	{
	  is_ignore_class = ldr_is_ignore_class (name, strlen (name));
	  if (!is_ignore_class)
	    {
	      display_error (0);
	      CHECK_CONTEXT_VALIDITY (context, true);
	    }
	  else if (er_errid () == ER_LC_UNKNOWN_CLASSNAME)
	    {
	      er_clear ();
	    }
	}
    }
}

/*
 * ldr_clear_context - clears any information on the class/attribute/argument
 * definitions that have been made.
 *    return: void
 *    context(out): current context
 */
static void
ldr_clear_context (LDR_CONTEXT * context)
{
  context->cls = NULL;
  context->class_name = NULL;

  context->obj = NULL;
  context->mobj = NULL;
  context->obj_pin = 0;
  context->class_pin = 0;
  context->class_type = DB_NOT_PARTITIONED_CLASS;

  context->attrs = NULL;

  context->num_attrs = 0;
  context->next_attr = 0;

  context->instance_started = 0;
  context->valid = false;

  /* verbose and periodic_commit are should be set out side this function */

  LDR_CLEAR_ERR_COUNT (context);

  /* error_total should not be reset here */

  context->inst_count = 0;
  context->inst_total = 0;
  context->inst_num = -1;

  context->flush_interval = prm_get_integer_value (PRM_ID_LOADDB_FLUSH_INTERVAL);

  context->table = NULL;

  /* validation_only, lang_id flag should be set outside this function */

  context->collection = NULL;
  context->set_domain = NULL;

  context->constructor = NULL;
  context->arg_index = 0;
  context->maxarg = 0;
  context->args = NULL;
  context->arg_count = 0;

  context->id_class = NULL;
  context->attribute_type = LDR_ATTRIBUTE_ANY;
  context->cons = NULL;
}

/*
 * ldr_clear_and_free_context - Frees up the space allocated for the
 * constructor strings, attribute descriptors and args array.
 *    return: void
 *    context(in/out): current context
 * Note:
 *   Called if we have an internal error, or when we finish processing each
 *   class.
 */
static void
ldr_clear_and_free_context (LDR_CONTEXT * context)
{
  int i;

  if (context->attrs)
    {
      for (i = 0; i < (context->num_attrs + context->arg_count); i += 1)
	{
	  if (context->attrs[i].parser_str)
	    {
	      free_and_init (context->attrs[i].parser_str);
	    }
	  if (context->attrs[i].attdesc)
	    {
	      db_free_attribute_descriptor (context->attrs[i].attdesc);
	    }
	  context->attrs[i].attdesc = NULL;
	}
      free_and_init (context->attrs);
    }

  if (context->args)
    {
      free_and_init (context->args);
    }

  if (context->class_name)
    {
      free_and_init (context->class_name);
    }

  ldr_clear_context (context);

  return;
}

/*
 *                        LOADER ERROR NOTIFICATION
 *
 * There are two levels of error handling here.  When a call
 * is made to a ldr_ function, those functions will return a int
 * and use er_set to set the global error state.  For those errors,
 * we simply use db_error_string to get the text and display it.
 *
 * Other errors are detected by the action routines in this file.
 * For these, we don't use er_set since it isn't really necessary.  Instead
 * we just have message catalog entries for the error text we wish
 * to display.
 *
 */

/*
 * ldr_internal_error - handle internal error
 *    return: void
 *    context(in/out): context
 * Note:
 *    This can be called by an upper level when an error is detected
 *    and we need to reset the internal loader state.
 *    Call this when serious errors are encountered and we need to
 *    stop immediately.  Probably we could longjmp out of parser here to
 *    avoid parsing the rest of the file.
 */
static void
ldr_internal_error (LDR_CONTEXT * context)
{
  ldr_clear_and_free_context (context);
  ldr_abort ();
}

/*
 * display_error_line - display error line
 *    return: void
 *    adjust(in): line number adjustor
 * Note:
 *    This will display the line number of the current input file.
 *    It is intended to give error messages context within the file.
 *    Its public because there are a few places in the loader internals
 *    where we need to display messages without propogating
 *    errors back up to the action routine level.
 */
static void
display_error_line (int adjust)
{
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LINE),
	   loader_yylineno + adjust);
}

/*
 * display_error - ldr_ function error handler
 *    return: void
 *    adjust(in): line number adjustor
 * Note:
 *    This is called whenever one of the ldr_ functions returns
 *    an error.  In this case, a standard system error has been set
 *    and the text is obtained through db_error_string.
 */
static void
display_error (int adjust)
{
  const char *msg;

  display_error_line (adjust);
  msg = db_error_string (3);
  fprintf (stderr, msg);
  fprintf (stderr, "\n");
}

/*
 * ldr_invalid_class_error - invalid class error handler
 *    return: void
 *    context(in): context
 * Note:
 *    Called when the name of an invalid class reference was specified.
 */
static void
ldr_invalid_class_error (LDR_CONTEXT * context)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNKNOWN_ATT_CLASS),
	   ldr_attr_name (context), ldr_class_name (context));
}

/*
 * parse_error - parse error handler
 *    return: void
 *    context(in): context
 *    token_type(in): incoming token type
 *    token(in): token string
 * Note:
 *    Called when we have some sort of parsing problem with an incoming
 *    token that made it past parser's initial level of checking.
 *    System errors have not been set.
 *    This is called by serveral of the setters.
 */
static void
parse_error (LDR_CONTEXT * context, DB_TYPE token_type, const char *token)
{
  display_error_line (0);

  /* 
   * This is called when we experience an error when performing a string to
   * DB_TYPE conversion. Called via CHECK_PARSE_ERR() macro.
   */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_PARSE_ERROR), token,
	   db_get_type_name (token_type), ldr_attr_name (context), ldr_class_name (context));
}

/*
 * clist_init - Initializes the list of internal classes.
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    These are the classes that we don't allow to be loaded.
 */
static int
clist_init (void)
{
  DB_OBJECT *class_;

  internal_classes = NULL;

  class_ = db_find_class (AU_ROOT_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
  class_ = db_find_class (AU_USER_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
  class_ = db_find_class (AU_PASSWORD_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
  class_ = db_find_class (AU_AUTH_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }
  return NO_ERROR;
}

/*
 * clist_final - free the list of internal classes
 *    return: void
 */
static void
clist_final (void)
{
  ml_ext_free (internal_classes);
  internal_classes = NULL;
}

/*
 * is_internal_class - test to see if a class is an internal class
 *    return: non-zero if this is an internal class
 *    class(in): class to examine
 */
static int
is_internal_class (DB_OBJECT * class_)
{
  return (ml_find (internal_classes, class_));
}

/*
 *                         LEXER ACTION ROUTINES
 *
 * These routines are, for the most part, just dispatchers.  They allow us
 * factor out the information about the form of the incoming string (i.e.,
 * we know that it's an integer, a quoted string, a numeric, or a real),
 * which allows the the function we dispatch to to make certain significant
 * optimizations (usually based on the domain of the attribute it's known to
 * be setting).
 */

/*
 * ldr_act_attr - Invokes the appropriate setter from a vector of setters
 * indexed by the parser type.
 *    return: void
 *    context(in/out): current context
 *    str(in): parser token
 *    len(in): length of token
 *    type(in): Parser type
 */
void
ldr_act_attr (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type)
{
  LDR_ATTDESC *attdesc;
  int err = NO_ERROR;

  CHECK_SKIP ();

  /* we have reached an invalid state, ignore the tuples */

  RETURN_IF_NOT_VALID (context);

  if (context->next_attr >= context->num_attrs)
    {
      context->valid = false;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_VALUE_OVERFLOW, 1, context->num_attrs);
      CHECK_ERR (err, ER_LDR_VALUE_OVERFLOW);
      goto error_exit;
    }

  if (context->validation_only)
    {
      switch (type)
	{
	  /* 
	   * For validation only simply parse the set elements, by switching
	   * to the element setter
	   */
	case LDR_COLLECTION:
	  /* ending brace of collection */
	  if (str == NULL)
	    {
	      ldr_act = ldr_act_attr;
	      goto error_exit;
	    }
	  else
	    {
	      ldr_act = ldr_act_elem;
	      return;
	    }
	  /* Check validity of date/time/timestamp string during validation */
	case LDR_TIME:
	case LDR_TIMELTZ:
	case LDR_TIMETZ:
	case LDR_DATE:
	case LDR_TIMESTAMP:
	case LDR_TIMESTAMPLTZ:
	case LDR_TIMESTAMPTZ:
	case LDR_DATETIME:
	case LDR_DATETIMELTZ:
	case LDR_DATETIMETZ:
	  CHECK_ERR (err, ldr_check_date_time_conversion (str, type));
	  break;
	default:
	  break;
	}
    }
  else
    {
      attdesc = &context->attrs[context->next_attr];
      CHECK_ERR (err, (*(attdesc->setter[type])) (context, str, len, attdesc->att));
    }

error_exit:
  context->next_attr += 1;
  LDR_INCREMENT_ERR_COUNT (context, (err != NO_ERROR));
}

/*
 * ldr_act_elem - Invokes the appropriate convertor from a set of vectored
 * convertors indexed by the parser type.
 *    return: void
 *    context(in/out): current context
 *    str(in): parser token
 *    len(in): length of token
 *    type(in): Parser type
 * Note:
 *      If an object reference is encoutered we need to use a special
 *      function set_new_element() to generate a collection element, since we
 *      are dealing with DB_TYPE_OIDs not DB_TYPE_OBJECT.
 *      assign_set_value(), called by set_add_element(), calls
 *      tp_domain_check(), and tp_value_cast to determine if the domains are
 *      compatible, for DB_TYPE_OID, these do not return DB_DOMAIN_COMPATIBLE
 *      for DB_OBJECT and DB_OID.
 */
static void
ldr_act_elem (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type)
{
  DB_VALUE tempval;
  int err = NO_ERROR;

  /* we have reached an invalid state, ignore the tuples */

  RETURN_IF_NOT_VALID (context);

  if (context->validation_only)
    {
      switch (type)
	{
	  /* 
	   * For validation only simply parse the set elements, by switching
	   * to the element setter
	   */
	case LDR_COLLECTION:
	  err = ER_LDR_NESTED_SET;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	  display_error (0);
	  break;
	  /* Check validity of date/time/timestamp string during validation */
	case LDR_TIME:
	case LDR_TIMELTZ:
	case LDR_TIMETZ:
	case LDR_DATE:
	case LDR_TIMESTAMP:
	case LDR_TIMESTAMPLTZ:
	case LDR_TIMESTAMPTZ:
	case LDR_DATETIME:
	case LDR_DATETIMELTZ:
	case LDR_DATETIMETZ:
	  CHECK_ERR (err, ldr_check_date_time_conversion (str, type));
	  break;
	default:
	  break;
	}
    }
  else
    {
      CHECK_ERR (err, (*(elem_converter[type])) (context, str, len, &tempval));
      if ((err = set_add_element (context->collection, &tempval)) == ER_SET_DOMAIN_CONFLICT)
	{
	  display_error_line (0);
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_SET_DOMAIN_ERROR),
		   ldr_attr_name (context), ldr_class_name (context), db_get_type_name (db_value_type (&tempval)));
	  CHECK_ERR (err, ER_SET_DOMAIN_CONFLICT);
	}
      else
	CHECK_ERR (err, err);
    }

error_exit:
  LDR_INCREMENT_ERR_COUNT (context, (err != NO_ERROR));
}

/*
 * ldr_act_meth - set up the attr desc for an instance generated via a
 * constructor.
 *    return: void
 *    context(in/out): current context
 *    str(in): parser token
 *    len(in): length of token
 *    type(in): Parser type
 * Note:
 *      This simulates the parse phase here.
 */
static void
ldr_act_meth (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type)
{
  int err = NO_ERROR;
  LDR_ATTDESC *attdesc = NULL;

  /* we have reached an invalid state, ignore the tuples */

  RETURN_IF_NOT_VALID (context);

  if ((context->next_attr) >= (context->num_attrs + context->arg_count))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_UNEXPECTED_ARGUMENT, 1, context->arg_count);
      CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
    }
  CHECK_VALIDATION_ONLY (context);

  attdesc = &context->attrs[context->next_attr];

  /* 
   * Save the parser buffer and type information, this will be
   * used later to feed to the fast setters to populate the
   * constructor generated instance
   * Attempt to reuse the strings buffers.
   */
  if (attdesc->parser_buf_len == 0)
    {
      attdesc->parser_str = (char *) (malloc (len + 1));
      if (attdesc->parser_str == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	}
      CHECK_PTR (err, attdesc->parser_str);
      attdesc->parser_buf_len = len;
    }
  else if (len > attdesc->parser_buf_len)
    {
      char *parser_str_old;
      parser_str_old = attdesc->parser_str;
      /* Prevent leak from realloc call failure */
      attdesc->parser_str = (char *) realloc (attdesc->parser_str, len + 1);
      if (attdesc->parser_str == NULL)
	{
	  /* Prevent leakage if realloc fails */
	  free_and_init (parser_str_old);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	}
      CHECK_PTR (err, attdesc->parser_str);
      attdesc->parser_buf_len = len;
    }
  memcpy (attdesc->parser_str, str, len + 1);
  attdesc->parser_type = type;
  attdesc->parser_str_len = len;

error_exit:
  context->next_attr += 1;
  LDR_INCREMENT_ERR_COUNT (context, (err != NO_ERROR));
}

/*
 * ldr_mismatch -  always error handler.
 *    return: ER_OBJ_DOMAIN_CONFLICT
 *    context(in): not used
 *    str(in): not used
 *    len(in): not used
 *    att(in): attribute
 */
static int
ldr_mismatch (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1, att->header.name);
  CHECK_ERR (err, ER_OBJ_DOMAIN_CONFLICT);
error_exit:
  return err;
}

/*
 * ldr_ignore - always ignoring handler.
 *    return: ER_OBJ_DOMAIN_CONFLICT
 *    context(in): not used
 *    str(in): not used
 *    len(in): not used
 *    att(in): not used
 */
static int
ldr_ignore (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  /* 
   * No need to set an error here, we've already issued a message when we were
   * studying the attribute in ldr_act_add_attr().  Just return an error code
   * so that the caller will increment context->err_count, causing us to
   * skip the row rather than flush it.
   */
  return ER_OBJ_DOMAIN_CONFLICT;
}

/*
 * ldr_generic - set attribute value
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 *    value(in): DB_VALUE
 */
static int
ldr_generic (LDR_CONTEXT * context, DB_VALUE * value)
{
  int err;

  CHECK_ERR (err, obj_desc_set (context->obj, context->attrs[context->next_attr].attdesc, value));
error_exit:
  return err;
}

/*
 * ldr_null_elem - set db value to null
 *    return: NO_ERROR
 *    context(in):
 *    str(in): not used
 *    len(in): not used
 *    val(out): DB_VALUE
 */
static int
ldr_null_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  DB_MAKE_NULL (val);
  return NO_ERROR;
}

/*
 * ldr_null_db_generic - set attribute value to null
 *    return:
 *    context(in): context
 *    str(in): not used
 *    len(in): not used
 *    att(att): memory representation of attribute
 */
static int
ldr_null_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  char *mem;

  if (att->flags & SM_ATTFLAG_NON_NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1, att->header.name);
      CHECK_ERR (err, ER_OBJ_ATTRIBUTE_CANT_BE_NULL);
    }
  else
    {
      mem = context->mobj + att->offset;
      CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, NULL));
      if (!att->domain->type->variable_p)
	OBJ_CLEAR_BOUND_BIT (context->mobj, att->storage_order);
    }

error_exit:
  return err;
}

/*
 * ldr_class_attr_db_generic - set attribute of a class
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 *    str(in): not used
 *    len(in): not used
 *    att(in): memory representation of attribute
 *    val(in): value to set
 * Note:
 *    This is a special setter, and is not called via the same process as the
 *    other setters. i.e., ldr_act(). This is called by ldr_act_class_attr().
 */
static int
ldr_class_attr_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att, DB_VALUE * val)
{
  int err = NO_ERROR;

  if (att->header.name_space == ID_SHARED_ATTRIBUTE)
    {
      CHECK_ERR (err, obj_set_shared (context->cls, att->header.name, val));
    }
  else if (att->header.name_space == ID_CLASS_ATTRIBUTE)
    {
      CHECK_ERR (err, obj_set (context->cls, att->header.name, val));
    }
  else if (context->attribute_type == LDR_ATTRIBUTE_DEFAULT)
    {
      CHECK_ERR (err, db_change_default (context->cls, att->header.name, val));
    }

error_exit:
  return err;
}

/*
 * ldr_act_class_attr -
 *    return:
 *    context():
 *    str():
 *    len():
 *    type():
 */
static void
ldr_act_class_attr (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type)
{
  int err = NO_ERROR;
  DB_VALUE src_val, dest_val, *val;
  TP_DOMAIN *domain;

  /* we have reached an invalid state, ignore the tuples */

  RETURN_IF_NOT_VALID (context);

  CHECK_VALIDATION_ONLY (context);

  if (context->next_attr >= context->num_attrs)
    {
      context->valid = false;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_VALUE_OVERFLOW, 1, context->num_attrs);
      CHECK_ERR (err, ER_LDR_VALUE_OVERFLOW);
    }

  if (type == LDR_COLLECTION)
    {
      if (context->attrs[context->next_attr].collection_domain == NULL)
	{
	  CHECK_CONTEXT_VALIDITY (context, true);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
		  context->attrs[context->next_attr].att->header.name);
	  CHECK_ERR (err, ER_OBJ_DOMAIN_CONFLICT);
	}
      CHECK_ERR (err, ldr_collection_db_collection (context, str, len, context->attrs[context->next_attr].att));
    }
  else
    {
      CHECK_ERR (err, (*(elem_converter[type])) (context, str, len, &src_val));
      GET_DOMAIN (context, domain);
      CHECK_ERR (err, db_value_domain_init (&dest_val, TP_DOMAIN_TYPE (domain), domain->precision, domain->scale));

      val = &dest_val;
      /* tp_value_cast does not handle DB_TYPE_OID coersions, simply use the value returned by the elem converter. */
      if (type == LDR_OID || type == LDR_CLASS_OID)
	{
	  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_OBJECT)
	    {
	      val = &src_val;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
		      context->attrs[context->next_attr].att->header.name);
	      CHECK_ERR (err, ER_OBJ_DOMAIN_CONFLICT);
	    }
	}
      else if (tp_value_cast (&src_val, &dest_val, domain, false))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
		  context->attrs[context->next_attr].att->header.name);
	  CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, TP_DOMAIN_TYPE (domain), str);
	}
      CHECK_ERR (err, ldr_class_attr_db_generic (context, str, len, context->attrs[context->next_attr].att, val));
    }

error_exit:
  context->next_attr += 1;
  LDR_INCREMENT_ERR_COUNT (context, (err != NO_ERROR));
}

/*
 * ldr_sys_user_db_generic -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_sys_user_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNAUTHORIZED_CLASS),
	   "db_user");
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  LDR_INCREMENT_ERR_COUNT (context, 1);
  return (ER_GENERIC_ERROR);
}

/*
 * ldr_sys_class_db_generic -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_sys_class_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNAUTHORIZED_CLASS),
	   "*system class*");
  CHECK_CONTEXT_VALIDITY (context, true);

  LDR_INCREMENT_ERR_COUNT (context, 1);
  return (NO_ERROR);
}

/*
 *  INT SETTERS
 *
 *  These functions (ldr_int_db_*) are called when int constants are
 *  processed by the lexer.  They probably only make sense for int, float,
 *  double, and numeric types.  An "int" string is known to consist only of
 *  digits with an optional preceding sign character.
 *
 *  Right now we don't have a special handler for DB_TYPE_SHORT attributes.
 *  We may want to build one if they turn out to be prevalent.  That handler
 *  would have to be on the lookout for overflow situations.  (For that
 *  matter, ldr_int_db_int maybe ought to look out for it too.)
 */

/*
 * ldr_int_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_int_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  int result = 0;

  /* 
   * Watch out for really long digit strings that really are being
   * assigned into a DB_TYPE_NUMERIC attribute; they can hold more than a
   * standard integer can, and calling atol() on that string will lose
   * data.
   * Is there some better way to test for this condition?
   */
  if (len < MAX_DIGITS_FOR_INT || (len == MAX_DIGITS_FOR_INT && (str[0] == '0' || str[0] == '1')))
    {
      val->domain = ldr_int_tmpl.domain;
      result = parse_int (&val->data.i, str, 10);
      if (result != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_INTEGER));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_INTEGER, str);
	}
    }
  else if (len < MAX_DIGITS_FOR_BIGINT || (len == MAX_DIGITS_FOR_BIGINT && str[0] != '9'))
    {
      val->domain = ldr_bigint_tmpl.domain;
      result = parse_bigint (&val->data.bigint, str, 10);
      if (result != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_BIGINT));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_BIGINT, str);
	}
    }
  else
    {
      DB_NUMERIC num;
      DB_BIGINT tmp_bigint;

      numeric_coerce_dec_str_to_num (str, num.d.buf);
      if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint) != NO_ERROR)
	{

	  CHECK_PARSE_ERR (err, db_value_domain_init (val, DB_TYPE_NUMERIC, len, 0), context, DB_TYPE_BIGINT, str);
	  CHECK_PARSE_ERR (err, db_value_put (val, DB_TYPE_C_CHAR, (char *) str, len), context, DB_TYPE_BIGINT, str);
	}
      else
	{
	  val->domain = ldr_bigint_tmpl.domain;
	  val->data.bigint = tmp_bigint;
	}
    }

error_exit:
  return err;
}

/*
 * ldr_int_db_generic -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_int_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_int_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  return err;
}

/*
 * ldr_int_db_bigint -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_int_db_bigint (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  int result = 0;
  DB_VALUE val;

  val.domain = ldr_bigint_tmpl.domain;

  /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
   * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
   * entered this can take the slower route. */
  if (len < MAX_DIGITS_FOR_BIGINT || (len == MAX_DIGITS_FOR_BIGINT && str[0] != '9'))
    {
      result = parse_bigint (&val.data.bigint, str, 10);
      if (result != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_BIGINT));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_BIGINT, str);
	}
    }
  else
    {
      DB_NUMERIC num;
      DB_BIGINT tmp_bigint;

      numeric_coerce_dec_str_to_num (str, num.d.buf);
      if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_BIGINT));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_BIGINT, str);
	}
      else
	{
	  val.data.bigint = tmp_bigint;
	}
    }

  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_int_db_int -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_int_db_int (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  int result = 0;
  DB_VALUE val;
  char *str_ptr;

  val.domain = ldr_int_tmpl.domain;

  /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
   * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
   * entered this can take the slower route. */
  if (len < MAX_DIGITS_FOR_INT || (len == MAX_DIGITS_FOR_INT && (str[0] == '0' || str[0] == '1')))
    {
      result = parse_int (&val.data.i, str, 10);
      if (result != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_INTEGER));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_INTEGER, str);
	}
    }
  else
    {
      double d;
      d = strtod (str, &str_ptr);

      if (str_ptr == str || OR_CHECK_INT_OVERFLOW (d))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_INTEGER));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_INTEGER, str);
	}
      else
	{
	  val.data.i = ROUND (d);
	}
    }

  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_int_db_short -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_int_db_short (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  int result = 0;
  DB_VALUE val;
  char *str_ptr;

  val.domain = ldr_short_tmpl.domain;

  /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
   * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
   * entered this can take the slower route. */
  if (len > MAX_DIGITS_FOR_SHORT)
    {
      double d;
      d = strtod (str, &str_ptr);

      if (str_ptr == str || OR_CHECK_SHORT_OVERFLOW (d))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_SHORT));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_SHORT, str);
	}
      else
	val.data.sh = ROUND (d);
    }
  else
    {
      int i_val;
      result = parse_int (&i_val, str, 10);

      if (result != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_SHORT));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_SHORT, str);
	}
      val.data.sh = (short) i_val;
    }

  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 *  STRING SETTERS
 *
 *  These functions (ldr_str_db_*) are called when quoted strings are
 *  processed by the lexer.  They probably only make sense for char, varchar,
 *  nchar, varnchar, bit, and varbit domains.
 *
 *  WARNING:  these functions cheat and assume a char-is-a-byte model, which
 *  won't work when dealing with non-ASCII (or non-Latin, at least) charsets.
 *  We'll need new versions to cope with those other charsets, but DON'T ADD
 *  CHARSET TESTING INTO THE BODIES OF THESE FUNCTIONS.  Move those tests WAY
 *  out into the initialization code, and initialize function pointers to
 *  point to fast (ASCII) or slow (e.g., S-JIS) versions.
 */


/*
 * ldr_str_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_str_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  DB_MAKE_STRING (val, str);
  return NO_ERROR;
}

/*
 * ldr_str_db_char -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_str_db_char (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int precision;
  int err;
  DB_VALUE val;
  int char_count = 0;

  precision = att->domain->precision;

  intl_char_count ((unsigned char *) str, len, att->domain->codeset, &char_count);

  if (char_count > precision)
    {
      /* 
       * May be a violation, but first we have to check for trailing pad
       * characters that might allow us to successfully truncate the
       * thing.
       */
      int safe;
      const char *p;
      int truncate_size;

      intl_char_size ((unsigned char *) str, precision, att->domain->codeset, &truncate_size);

      for (p = &str[truncate_size], safe = 1; p < &str[len]; p++)
	{
	  if (*p != ' ')
	    {
	      safe = 0;
	      break;
	    }
	}
      if (safe)
	len = truncate_size;
      else
	{
	  /* 
	   * It's a genuine violation; raise an error.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_CHAR));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_CHAR, str);
	}
    }

  val.domain = ldr_char_tmpl.domain;
  val.domain.char_info.length = char_count;
  val.data.ch.info.style = MEDIUM_STRING;
  val.data.ch.info.is_max_string = false;
  val.data.ch.info.compressed_need_clear = false;
  val.data.ch.medium.size = len;
  val.data.ch.medium.buf = (char *) str;
  val.data.ch.medium.compressed_buf = NULL;
  val.data.ch.medium.compressed_size = 0;
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_str_db_varchar -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_str_db_varchar (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int precision;
  int err;
  DB_VALUE val;
  int char_count = 0;

  precision = att->domain->precision;
  intl_char_count ((unsigned char *) str, len, att->domain->codeset, &char_count);

  if (char_count > precision)
    {
      /* 
       * May be a violation, but first we have to check for trailing pad
       * characters that might allow us to successfully truncate the
       * thing.
       */
      int safe;
      const char *p;
      int truncate_size;

      intl_char_size ((unsigned char *) str, precision, att->domain->codeset, &truncate_size);
      for (p = &str[truncate_size], safe = 1; p < &str[len]; p++)
	{
	  if (*p != ' ')
	    {
	      safe = 0;
	      break;
	    }
	}
      if (safe)
	len = truncate_size;
      else
	{
	  /* 
	   * It's a genuine violation; raise an error.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_VARCHAR));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_VARCHAR, str);
	}
    }

  val.domain = ldr_varchar_tmpl.domain;
  val.domain.char_info.length = char_count;
  val.data.ch.medium.size = len;
  val.data.ch.medium.buf = (char *) str;
  val.data.ch.info.style = MEDIUM_STRING;
  val.data.ch.info.is_max_string = false;
  val.data.ch.info.compressed_need_clear = false;
  val.data.ch.medium.compressed_buf = NULL;
  val.data.ch.medium.compressed_size = 0;

  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  /* 
   * No bound bit to be set for a variable length attribute.
   */

error_exit:
  return err;
}

/*
 * ldr_str_db_generic -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_str_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  DB_VALUE val;

  DB_MAKE_STRING (&val, str);
  return ldr_generic (context, &val);
}

/*
 * ldr_bstr_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_bstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  int dest_size;
  char *bstring;
  TP_DOMAIN *domain;
  DB_VALUE temp;
  TP_DOMAIN *domain_ptr, temp_domain;

  dest_size = (len + 7) / 8;

  CHECK_PTR (err, bstring = db_private_alloc (NULL, dest_size + 1));

  if (qstr_bit_to_bin (bstring, dest_size, (char *) str, len) != len)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	      context->attrs[context->next_attr].att->header.name);
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT, str);
    }

  DB_MAKE_VARBIT (&temp, TP_FLOATING_PRECISION_VALUE, bstring, len);
  temp.need_clear = true;

  GET_DOMAIN (context, domain);

  if (domain == NULL)
    {
      CHECK_PARSE_ERR (err, db_value_domain_init (val, DB_TYPE_BIT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE), context,
		       DB_TYPE_BIT, str);
    }
  else
    {
      CHECK_PARSE_ERR (err, db_value_domain_init (val, TP_DOMAIN_TYPE (domain), domain->precision, domain->scale),
		       context, DB_TYPE_BIT, str);
    }
  domain_ptr = tp_domain_resolve_value (val, &temp_domain);
  if (tp_value_cast (&temp, val, domain_ptr, false))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	      context->attrs[context->next_attr].att->header.name);
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT, str);
    }

error_exit:
  /* cleanup */
  db_value_clear (&temp);
  return err;
}

/*
 * ldr_bstr_db_varbit -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_bstr_db_varbit (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_bstr_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  db_value_clear (&val);
  return err;
}

/*
 * ldr_xstr_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_xstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  int dest_size;
  char *bstring = NULL;
  TP_DOMAIN *domain;
  DB_VALUE temp;
  TP_DOMAIN *domain_ptr, temp_domain;

  DB_MAKE_NULL (&temp);

  dest_size = (len + 1) / 2;

  CHECK_PTR (err, bstring = db_private_alloc (NULL, dest_size + 1));

  if (qstr_hex_to_bin (bstring, dest_size, (char *) str, len) != len)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1, ldr_attr_name (context));
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT, str);
    }

  DB_MAKE_VARBIT (&temp, TP_FLOATING_PRECISION_VALUE, bstring, len * 4);
  temp.need_clear = true;

  /* temp takes ownership of this piece of memory */
  bstring = NULL;

  GET_DOMAIN (context, domain);

  if (domain == NULL)
    {
      db_value_domain_init (val, DB_TYPE_BIT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }
  else
    {
      CHECK_PARSE_ERR (err, db_value_domain_init (val, TP_DOMAIN_TYPE (domain), domain->precision, domain->scale),
		       context, DB_TYPE_BIT, str);
    }
  domain_ptr = tp_domain_resolve_value (val, &temp_domain);
  if (tp_value_cast (&temp, val, domain_ptr, false))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	      context->attrs[context->next_attr].att->header.name);
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT, str);
    }

error_exit:
  /* cleanup */
  if (bstring != NULL)
    {
      db_private_free_and_init (NULL, bstring);
    }

  db_value_clear (&temp);
  return err;
}

/*
 * ldr_xstr_db_varbit -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_xstr_db_varbit (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_xstr_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  db_value_clear (&val);
  return err;
}

/*
 * ldr_nstr_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_nstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{

  DB_MAKE_VARNCHAR (val, TP_FLOATING_PRECISION_VALUE, str, len, LANG_SYS_CODESET, LANG_SYS_COLLATION);
  return NO_ERROR;
}

/*
 * ldr_nstr_db_varnchar -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_nstr_db_varnchar (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  DB_VALUE val;

  CHECK_ERR (err, ldr_nstr_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  return err;
}

/*
 *  NUMERIC SETTERS
 *
 *  A "numeric" string is known to have a decimal point in it but *not* to
 *  have any exponent.  It may also have a leading sign character.  Most of
 *  the interesting cases (float and double attributes) will be handled by
 *  the ldr_numeric_db_{float, double} functions below, but this one will
 *  catch assignments into actual numeric attributes.
 */


/*
 * ldr_numeric_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_numeric_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int precision, scale;
  int err = NO_ERROR;

  precision = len - 1 - (str[0] == '+' || str[0] == '-' || str[0] == '.');
  scale = len - (int) strcspn (str, ".") - 1;

  CHECK_PARSE_ERR (err, db_value_domain_init (val, DB_TYPE_NUMERIC, precision, scale), context, DB_TYPE_NUMERIC, str);
  CHECK_PARSE_ERR (err, db_value_put (val, DB_TYPE_C_CHAR, (char *) str, len), context, DB_TYPE_NUMERIC, str);

error_exit:
  return err;
}

/*
 * ldr_numeric_db_generic -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_numeric_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_numeric_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  return err;
}

/*
 * ldr_double_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_double_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  double d;
  char *str_ptr;
  int err = NO_ERROR;

  val->domain = ldr_double_tmpl.domain;
  d = strtod (str, &str_ptr);

  /* The ascii representation should be ok, check for overflow */

  if (str_ptr == str || OR_CHECK_DOUBLE_OVERFLOW (d))
    {
      TP_DOMAIN *domain;

      GET_DOMAIN (context, domain);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (TP_DOMAIN_TYPE (domain)));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, TP_DOMAIN_TYPE (domain), str);
    }
  else
    val->data.d = d;

error_exit:
  return err;
}

/*
 * ldr_float_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_float_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  double d;
  char *str_ptr;
  int err = NO_ERROR;

  val->domain = ldr_float_tmpl.domain;
  d = strtod (str, &str_ptr);

  /* The ascii representation should be ok, check for overflow */

  if (str_ptr == str || OR_CHECK_FLOAT_OVERFLOW (d))
    {
      TP_DOMAIN *domain;

      GET_DOMAIN (context, domain);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (TP_DOMAIN_TYPE (domain)));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, TP_DOMAIN_TYPE (domain), str);
    }
  else
    val->data.f = (float) d;

error_exit:
  return err;
}

/*
 * ldr_real_db_generic -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_real_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_double_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  return err;
}

/*
 * ldr_real_db_float -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_real_db_float (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  DB_VALUE val;
  double d;
  char *str_ptr;

  val.domain = ldr_float_tmpl.domain;
  d = strtod (str, &str_ptr);

  /* The ascii representation should be ok, check for overflow */

  if (str_ptr == str || OR_CHECK_FLOAT_OVERFLOW (d))
    {
      TP_DOMAIN *domain;

      GET_DOMAIN (context, domain);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (TP_DOMAIN_TYPE (domain)));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, TP_DOMAIN_TYPE (domain), str);
    }
  else
    val.data.f = (float) d;

  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_real_db_double -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_real_db_double (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  DB_VALUE val;
  double d;
  char *str_ptr;

  val.domain = ldr_double_tmpl.domain;
  d = strtod (str, &str_ptr);

  /* The ascii representation should be ok, check for overflow */

  if (str_ptr == str || OR_CHECK_DOUBLE_OVERFLOW (d))
    {
      TP_DOMAIN *domain;

      GET_DOMAIN (context, domain);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (TP_DOMAIN_TYPE (domain)));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, TP_DOMAIN_TYPE (domain), str);
    }
  else
    val.data.d = d;

  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 *  DATE/TIME/TIMESTAMP/DATETIME SETTERS
 *
 *  Any of the "date", "time" , "timestamp" or "datetime" strings have already
 *  had the tag and surrounding quotes stripped off.  We know which one we
 *  have by virtue knowing which function has been called.
 */

/*
 * ldr_date_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_date_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_date_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_date (str, &val->data.date), context, DB_TYPE_DATE, str);

error_exit:
  return err;
}

/*
 * ldr_date_db_date -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_date_db_date (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_date_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_time_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_time_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_time_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_time (str, &val->data.time), context, DB_TYPE_TIME, str);

error_exit:
  return err;
}

/*
 * ldr_time_db_time -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_time_db_time (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_time_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_timetz_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_timetz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  bool has_zone;

  val->domain = ldr_timetz_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_timetz (str, &val->data.timetz, &has_zone), context, DB_TYPE_TIMETZ, str);

error_exit:
  return err;
}

/*
 * ldr_timeltz_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_timeltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_timeltz_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_timeltz (str, &val->data.time), context, DB_TYPE_TIMELTZ, str);

error_exit:
  return err;
}

/*
 * ldr_timetz_db_timetz -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_timetz_db_timetz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_timetz_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_timeltz_db_timeltz -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_timeltz_db_timeltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_timeltz_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_timestamp_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_timestamp_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_timestamp_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_timestamp (str, &val->data.utime), context, DB_TYPE_TIMESTAMP, str);

error_exit:
  return err;
}

/*
 * ldr_timestamp_db_timestamp -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_timestamp_db_timestamp (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_timestamp_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_timestamptz_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_timestamptz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  bool has_zone;

  val->domain = ldr_timestamptz_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_timestamptz (str, &val->data.timestamptz, &has_zone), context, DB_TYPE_TIMESTAMPTZ,
		   str);
  /* if no zone text, than it is assumed session timezone */

error_exit:
  return err;
}

/*
 * ldr_timestampltz_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_timestampltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_timestampltz_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_timestampltz (str, &val->data.utime), context, DB_TYPE_TIMESTAMPLTZ, str);
  /* if no zone text, than it is assumed session timezone */

error_exit:
  return err;
}

/*
 * ldr_timestamptz_db_timestamptz -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_timestamptz_db_timestamptz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_timestamptz_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_timestampltz_db_timestampltz -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_timestampltz_db_timestampltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_timestampltz_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_datetime_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_datetime_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_datetime_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_datetime (str, &val->data.datetime), context, DB_TYPE_DATETIME, str);

error_exit:
  return err;
}

/*
 * ldr_datetime_db_datetime -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_datetime_db_datetime (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_datetime_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_datetimetz_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_datetimetz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  bool has_zone;

  val->domain = ldr_datetimetz_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_datetimetz (str, &val->data.datetimetz, &has_zone), context, DB_TYPE_DATETIMETZ,
		   str);
  /* if no zone text, than it is assumed session timezone */

error_exit:
  return err;
}

/*
 * ldr_datetimeltz_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_datetimeltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_datetimeltz_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_datetimeltz (str, &val->data.datetime), context, DB_TYPE_DATETIMELTZ, str);
  /* if no zone text, than it is assumed session timezone */

error_exit:
  return err;
}

/*
 * ldr_datetimetz_db_datetimetz -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_datetimetz_db_datetimetz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_datetimetz_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_datetimeltz_db_datetimeltz -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_datetimeltz_db_datetimeltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_datetimeltz_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_date_time_conversion_error - display date/time validation error
 *    return: void
 *    token(in): string that failed.
 *    type(in): loader type
 */
static void
ldr_date_time_conversion_error (const char *token, DB_TYPE type)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CONVERSION_ERROR), token,
	   db_get_type_name (type));
}

/*
 * ldr_load_failed_error - display load failed error
 *    return: void
 */
void
ldr_load_failed_error ()
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LOAD_FAIL));
}

/*
 * ldr_check_date_time_conversion - check time/date/timestamp string w.r.t. type
 *    return: NO_ERROR if successful, error code otherwise
 *    str(in): string to convert
 *    type(in): loader type
 * Note:
 *   Strings are checked for correctness during the validation only phase
 */
static int
ldr_check_date_time_conversion (const char *str, LDR_TYPE type)
{
  int err = NO_ERROR;
  DB_TIME dummy_time;
  DB_TIMETZ dummy_timetz;
  DB_DATE dummy_date;
  DB_TIMESTAMP dummy_timestamp;
  DB_TIMESTAMPTZ dummy_timestamptz;
  DB_DATETIME dummy_datetime;
  DB_DATETIMETZ dummy_datetimetz;
  DB_TYPE current_type = DB_TYPE_NULL;
  bool has_zone;

  /* 
   * Flag invalid date/time/timestamp strings as errors.
   * e.g., DATE '01///' should be an error, this is not detected by the lexical
   * analysis phase since DATE 'str' is valid.
   * Attempt to do the string to type conversion here.
   */
  switch (type)
    {
    case LDR_TIME:
      current_type = DB_TYPE_TIME;
      err = db_string_to_time (str, &dummy_time);
      break;
    case LDR_TIMELTZ:
      current_type = DB_TYPE_TIMELTZ;
      err = db_string_to_timeltz (str, &dummy_time);
      break;
    case LDR_TIMETZ:
      current_type = DB_TYPE_TIMETZ;
      err = db_string_to_timetz (str, &dummy_timetz, &has_zone);
      break;
    case LDR_DATE:
      current_type = DB_TYPE_DATE;
      err = db_string_to_date (str, &dummy_date);
      break;
    case LDR_TIMESTAMP:
      current_type = DB_TYPE_TIMESTAMP;
      err = db_string_to_timestamp (str, &dummy_timestamp);
      break;
    case LDR_TIMESTAMPLTZ:
      current_type = DB_TYPE_TIMESTAMPLTZ;
      err = db_string_to_timestampltz (str, &dummy_timestamp);
      break;
    case LDR_TIMESTAMPTZ:
      current_type = DB_TYPE_TIMESTAMPTZ;
      err = db_string_to_timestamptz (str, &dummy_timestamptz, &has_zone);
      break;
    case LDR_DATETIME:
      current_type = DB_TYPE_DATETIME;
      err = db_string_to_datetime (str, &dummy_datetime);
      break;
    case LDR_DATETIMELTZ:
      current_type = DB_TYPE_DATETIMELTZ;
      err = db_string_to_datetimeltz (str, &dummy_datetime);
      break;
    case LDR_DATETIMETZ:
      current_type = DB_TYPE_DATETIMETZ;
      err = db_string_to_datetimetz (str, &dummy_datetimetz, &has_zone);
      break;
    default:
      break;
    }

  if (err != NO_ERROR)
    {
      if (err == ER_DATE_CONVERSION)
	ldr_date_time_conversion_error (str, current_type);
      else
	display_error (0);
    }

  return err;
}

/*
 * ldr_elo_int_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_elo_int_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  /* not implemented. should not be called */
  assert (0);
  return ER_FAILED;
}

/*
 * ldr_elo_int_db_elo -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_elo_int_db_elo (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  /* not implemented. should not be called */
  assert (0);
  return ER_FAILED;
}

/*
 * ldr_elo_ext_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_elo_ext_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  DB_ELO elo;
  int err = NO_ERROR;
  int result = 0;
  int new_len;
  INT64 size;
  char *locator = NULL;
  char *meta_data = NULL;

  if (!context->valid)
    {
      err = ER_LDR_INVALID_STATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);

      /* reset the error count by adding -1, since this is not real error */
      LDR_INCREMENT_ERR_COUNT (context, -1);
      return (err);
    }

  PARSE_ELO_STR (str, new_len);

  /* parse elo string: see process_value () in unload_object.c */
  if (new_len > 0)
    {
      DB_TYPE type;
      const char *size_sp, *size_ep;
      const char *locator_sp, *locator_ep;
      const char *meta_sp, *meta_ep;

      /* db type */
      assert (str[0] == 'B' || str[0] == 'C');
      if (str[0] == 'B')
	{
	  type = DB_TYPE_BLOB;
	  val->domain = ldr_blob_tmpl.domain;
	}
      else
	{
	  type = DB_TYPE_CLOB;
	  val->domain = ldr_clob_tmpl.domain;
	}

      /* size */
      size_sp = str + 1;
      size_ep = strchr (size_sp, '|');
      if (size_ep == NULL || size_ep - size_sp == 0)
	{
	  /* FBO TODO: error message --> invalid elo format */
	  err = ER_LDR_ELO_INPUT_FILE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, str);
	  goto error_exit;
	}

      /* locator */
      locator_sp = size_ep + 1;
      locator_ep = strchr (locator_sp, '|');
      if (locator_ep == NULL || locator_ep - locator_sp == 0)
	{
	  /* FBO TODO: error message --> invalid elo format */
	  err = ER_LDR_ELO_INPUT_FILE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, str);
	  goto error_exit;
	}

      /* meta_data */
      meta_ep = meta_sp = locator_ep + 1;
      while (*meta_ep)
	{
	  meta_ep++;
	}

      /* make elo */
      elo_init_structure (&elo);

      result = str_to_int64 (&size, &size_ep, size_sp, 10);
      if (result != 0 || size < 0)
	{
	  err = ER_LDR_ELO_INPUT_FILE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_ELO_INPUT_FILE, 1, str);
	  goto error_exit;
	}

      locator = db_private_alloc (NULL, locator_ep - locator_sp + 1);
      if (locator == NULL)
	{
	  goto error_exit;
	}
      memcpy (locator, locator_sp, locator_ep - locator_sp);
      locator[locator_ep - locator_sp] = '\0';

      if (meta_ep - meta_sp > 0)
	{
	  meta_data = db_private_alloc (NULL, meta_ep - meta_sp + 1);
	  if (meta_data == NULL)
	    {
	      goto error_exit;
	    }
	  memcpy (meta_data, meta_sp, meta_ep - meta_sp);
	  meta_data[meta_ep - meta_sp] = '\0';
	}

      elo.size = size;
      elo.locator = locator;
      elo.meta_data = meta_data;
      elo.type = ELO_FBO;

      err = db_make_elo (val, type, &elo);
      if (err == NO_ERROR)
	{
	  val->need_clear = true;
	  locator = NULL;
	  meta_data = NULL;
	}
    }

error_exit:
  if (locator != NULL)
    {
      db_private_free_and_init (NULL, locator);
    }

  if (meta_data != NULL)
    {
      db_private_free_and_init (NULL, meta_data);
    }

  if (err != NO_ERROR)
    {
      display_error (0);
    }

  return err;
}

/*
 * ldr_elo_ext_db_elo -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_elo_ext_db_elo (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  char *mem;
  DB_VALUE val;
  char name_buf[8196];
  char *name = NULL;
  int new_len;

  db_make_null (&val);

  PARSE_ELO_STR (str, new_len);
  if (new_len >= 8196)
    {
      name = malloc (new_len);

      if (name == NULL)
	{
	  err = ER_LDR_MEMORY_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	  CHECK_CONTEXT_VALIDITY (context, true);
	  ldr_abort ();
	  goto error_exit;
	}
    }
  else
    {
      name = &name_buf[0];
    }

  strncpy (name, str, new_len);
  name[new_len] = '\0';
  CHECK_ERR (err, ldr_elo_ext_elem (context, name, new_len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  /* No bound bit to be set for a variable length attribute. */

error_exit:
  if (name != NULL && name != &name_buf[0])
    {
      free (name);
    }
  db_value_clear (&val);

  return err;
}

/*
 * LDR_MOP_TEMPOID_MAP MAINTENANCE
 */

/*
 * ldr_mop_tempoid_maps_init - initialize ldr_Mop_tempoid_maps
 *    return: NO_ERROR if successful, error code otherwise
 */
static int
ldr_mop_tempoid_maps_init (void)
{
  int err = NO_ERROR;
  int presize = 0;

  if (ldr_Mop_tempoid_maps)
    {
      ldr_Mop_tempoid_maps->count = 0;
      ldr_Mop_tempoid_maps->index = 0;
      return (err);
    }
  ldr_Mop_tempoid_maps = NULL;

  if ((ldr_Mop_tempoid_maps = (LDR_MOP_TEMPOID_MAPS *) malloc (sizeof (LDR_MOP_TEMPOID_MAPS))) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
      return ER_LDR_MEMORY_ERROR;
    }

  presize = LDR_MOP_TEMPOID_MAPS_PRESIZE;

  if ((ldr_Mop_tempoid_maps->mop_tempoid_maps =
       (LDR_MOP_TEMPOID_MAP *) malloc (presize * sizeof (LDR_MOP_TEMPOID_MAP))) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
      err = ER_LDR_MEMORY_ERROR;
      ldr_Mop_tempoid_maps->count = -1;
      ldr_Mop_tempoid_maps->index = -1;
      ldr_Mop_tempoid_maps->size = 0;
    }
  else
    {
      int i = 0;
      while (i < presize)
	{
	  ldr_Mop_tempoid_maps->mop_tempoid_maps[i].mop = NULL;
	  ldr_Mop_tempoid_maps->mop_tempoid_maps[i].table = NULL;
	  ldr_Mop_tempoid_maps->mop_tempoid_maps[i].id = 0;
	  i += 1;
	}
      ldr_Mop_tempoid_maps->count = 0;
      ldr_Mop_tempoid_maps->index = 0;
      ldr_Mop_tempoid_maps->size = presize;
    }

  return err;
}

/*
 * ldr_mop_tempoid_maps_final - Frees ldr_Mop_tempoid_maps.
 *    return: NO_ERROR if successful, error code otherwise
 */
static void
ldr_mop_tempoid_maps_final (void)
{
  if (!ldr_Mop_tempoid_maps)
    return;

  if (ldr_Mop_tempoid_maps->mop_tempoid_maps)
    free_and_init (ldr_Mop_tempoid_maps->mop_tempoid_maps);
  free_and_init (ldr_Mop_tempoid_maps);

  return;
}

/*
 * ldr_add_mop_tempoid_map - add a temporary oid mop to CLASS_TABLE mapping
 * to ldr_Mop_tempoid_maps.
 *    return: NO_ERROR if successful, error code otherwise
 *    mop(in): MOP to add
 *    table(out): CLASS_TABLE
 *    id(in): instance id.
 */
static int
ldr_add_mop_tempoid_map (MOP mop, CLASS_TABLE * table, int id)
{
  int err = NO_ERROR;

  if (ldr_Mop_tempoid_maps == NULL || ldr_Mop_tempoid_maps->mop_tempoid_maps == NULL)
    {
      err = ldr_mop_tempoid_maps_init ();
      if (err != NO_ERROR)
	{
	  display_error (0);
	  return err;
	}
    }

  ldr_Mop_tempoid_maps->mop_tempoid_maps[ldr_Mop_tempoid_maps->index].mop = mop;
  ldr_Mop_tempoid_maps->mop_tempoid_maps[ldr_Mop_tempoid_maps->index].table = table;
  ldr_Mop_tempoid_maps->mop_tempoid_maps[ldr_Mop_tempoid_maps->index].id = id;
  ldr_Mop_tempoid_maps->count += 1;
  ldr_Mop_tempoid_maps->index += 1;

  /* Grow array if required */

  if (ldr_Mop_tempoid_maps->index == ldr_Mop_tempoid_maps->size)
    {
      LDR_MOP_TEMPOID_MAP *mop_tempoid_maps_old;

      mop_tempoid_maps_old = ldr_Mop_tempoid_maps->mop_tempoid_maps;

      ldr_Mop_tempoid_maps->mop_tempoid_maps =
	(LDR_MOP_TEMPOID_MAP *) realloc (ldr_Mop_tempoid_maps->mop_tempoid_maps,
					 sizeof (LDR_MOP_TEMPOID_MAP) * (ldr_Mop_tempoid_maps->size +
									 LDR_MOP_TEMPOID_MAPS_PRESIZE));

      if (ldr_Mop_tempoid_maps->mop_tempoid_maps == NULL)
	{
	  /* Prevent realloc memory leak, if error occurs. */
	  if (mop_tempoid_maps_old)
	    {
	      free_and_init (mop_tempoid_maps_old);
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	  return ER_LDR_MEMORY_ERROR;
	}
      else
	{
	  ldr_Mop_tempoid_maps->size += LDR_MOP_TEMPOID_MAPS_PRESIZE;
	}
    }

  return err;
}

/*
 * ldr_assign_all_perm_oids - traverses ldr_Mop_tempoid_maps and creates a
 * list LC_OIDSET, to send to the server to get permanent oids.
 *    return: NO_ERROR if successful, error code otherwise
 */
static int
ldr_assign_all_perm_oids (void)
{
  int err = NO_ERROR;
  LC_OIDSET *oidset = NULL;
  LDR_MOP_TEMPOID_MAP *mop_tempoid_map = NULL;
  INST_INFO *inst;
  int i;

  if (!ldr_Mop_tempoid_maps)
    return (err);

  /* No permanent oids to assign */
  if (!ldr_Mop_tempoid_maps->count)
    return (NO_ERROR);

  oidset = locator_make_oid_set ();
  if (oidset == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
    }
  else
    {
      i = 0;
      while (i < ldr_Mop_tempoid_maps->count)
	{
	  mop_tempoid_map = &(ldr_Mop_tempoid_maps->mop_tempoid_maps[i]);
	  if (mop_tempoid_map->mop && OID_ISTEMP (WS_REAL_OID (mop_tempoid_map->mop)))
	    {
	      if (locator_add_oidset_object (oidset, mop_tempoid_map->mop) == NULL)
		CHECK_ERR (err, er_errid ());
	    }
	  i += 1;
	  if (oidset->total_oids > OID_BATCH_SIZE)
	    {
	      CHECK_ERR (err, locator_assign_oidset (oidset, NULL));
	      locator_clear_oid_set (NULL, oidset);
	    }
	}
      /* call locator_assign_oidset(). This will make a server call to get permanent oids. */
      if (oidset->total_oids)
	{
	  CHECK_ERR (err, locator_assign_oidset (oidset, NULL));
	}

      /* At this point the mapping between mop -> permanent oid should be complete. Update the otable oids via the oid
       * pointer obtained from the CLASS_TABLE and id. */

      if (!No_oid_hint)
	{
	  i = 0;
	  while (i < ldr_Mop_tempoid_maps->count)
	    {
	      mop_tempoid_map = &(ldr_Mop_tempoid_maps->mop_tempoid_maps[i]);
	      CHECK_PTR (err, inst = otable_find (mop_tempoid_map->table, mop_tempoid_map->id));
	      COPY_OID (&(inst->oid), WS_REAL_OID (mop_tempoid_map->mop));
	      mop_tempoid_map->mop = NULL;
	      mop_tempoid_map->table = NULL;
	      mop_tempoid_map->id = 0;
	      i += 1;
	    }
	}

      ldr_Mop_tempoid_maps->index = 0;
      ldr_Mop_tempoid_maps->count = 0;
    }
error_exit:
  if (oidset)
    locator_free_oid_set (NULL, oidset);

  return err;
}

/*
 * find_instance - locates an instance OID given the class and an instance id.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 *    class(in): class object
 *    oid(out): instance OID (returned)
 *    id(in): instance identifier
 * Note:
 *    If the instance does not exist, an OID is reserved for this
 *    instance.
 *    Note : the oid references return are temporary OIDs. This will be
 *    when the objects are packed to be sent to the server, using a batch
 *    oid set.
 *    The oids in the workspace to which the references point to are updated
 *    via the call ldr_assign_all_perm_oids()
 */
static int
find_instance (LDR_CONTEXT * context, DB_OBJECT * class_, OID * oid, int id)
{
  int err = NO_ERROR;
  CLASS_TABLE *table;
  INST_INFO *inst;

  table = otable_find_class (class_);

  if (table == NULL)
    {
      OID_SET_NULL (oid);
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
    }
  else
    {
      inst = otable_find (table, id);
      if (inst != NULL)
	{
	  /* Backward reference */
	  *oid = inst->oid;
	}
      else
	{			/* Forward reference */
	  if ((class_ == context->cls) && (context->inst_num == id))
	    {
	      /* 
	       * We have a reference to the current object being processed.
	       * Simply use the oid of the object.
	       */
	      COPY_OID (oid, WS_REAL_OID (context->obj));
	    }
	  else
	    {
	      /* Forward reference */
	      OID_SET_NULL (oid);

	      /* sigh, should try to catch this at a higher level */
	      if (is_internal_class (class_))
		{
		  err = ER_LDR_INTERNAL_REFERENCE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, db_get_class_name (class_));
		}
	      else
		{
		  MOP mop;
		  INST_INFO *inst;
		  /* 
		   * Create object, this will be used used when the instance
		   * is defined in the load file.
		   * We do not mark the MOP as released yet. This will be done
		   * when we encounter the real instance.
		   */
		  CHECK_PTR (err, mop = db_create_internal (context->attrs[context->next_attr].ref_class));
		  COPY_OID (oid, WS_REAL_OID (mop));
		  CHECK_ERR (err, otable_reserve (table, oid, id));
		  CHECK_PTR (err, inst = otable_find (table, id));
		  /* 
		   * Mark forward references to class attributes so that we do
		   * not cull the objects they point to after we encounter them.
		   */
		  if (context->attribute_type != LDR_ATTRIBUTE_ANY)
		    otable_class_att_ref (inst);

		  /* 
		   * Add to the list of mops for which we need a permanent oid
		   * for
		   */
		  CHECK_ERR (err, ldr_add_mop_tempoid_map (mop, table, id));
		}
	    }
	}
    }
error_exit:
  return (err);
}

/*
 * ldr_class_oid_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_class_oid_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  if (context->attrs[context->next_attr].ref_class == NULL)
    {
      ldr_internal_error (context);
      goto error_exit;
    }

  CHECK_ERR (err, check_class_domain (context));
  DB_MAKE_OBJECT (val, context->attrs[context->next_attr].ref_class);

error_exit:
  return err;
}

/*
 * ldr_class_oid_db_object -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_class_oid_db_object (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  DB_VALUE val;
  char *mem;

  CHECK_ERR (err, ldr_class_oid_elem (context, str, len, &val));

  /* 
   * We need to treat shared attributes in the generic way.
   * There is a problem when setting the bound bit for shared attributes.
   */
  if (att->header.name_space == ID_SHARED_ATTRIBUTE)
    ldr_generic (context, &val);
  else
    {
      mem = context->mobj + att->offset;
      CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
      OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);
    }

error_exit:
  LDR_INCREMENT_ERR_COUNT (context, (err != NO_ERROR));
  return err;
}

/*
 * ldr_oid_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_oid_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  DB_OBJECT *actual_class;
  OID oid;

  if (context->attrs[context->next_attr].ref_class == NULL)
    {
      ldr_internal_error (context);
      goto error_exit;
    }

  CHECK_ERR (err, check_object_domain (context, context->attrs[context->next_attr].ref_class, &actual_class));
  err = find_instance (context, actual_class, &oid, context->attrs[context->next_attr].instance_id);
  if (err == ER_LDR_INTERNAL_REFERENCE)
    {
      DB_MAKE_NULL (val);
    }
  else
    {
      DB_OBJECT *mop;

      if ((mop = ws_mop (&oid, context->attrs[context->next_attr].ref_class)) == NULL)
	{
	  if ((err = er_errid ()) == NO_ERROR)
	    {
	      err = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	    }
	  CHECK_ERR (err, err);
	}

      DB_MAKE_OBJECT (val, mop);
    }

error_exit:
  LDR_INCREMENT_ERR_COUNT (context, (err != NO_ERROR));
  return err;
}

/*
 * ldr_oid_db_object -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_oid_db_object (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  DB_VALUE val;
  char *mem;

  CHECK_ERR (err, ldr_oid_elem (context, str, len, &val));

  mem = context->mobj + att->offset;

  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return (err);
}

/*
 * ldr_monetary_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_monetary_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  const unsigned char *p = (const unsigned char *) str;
  const unsigned char *token = (const unsigned char *) str;
  char *str_ptr;
  double amt;
  int err = NO_ERROR;
  int symbol_size = 0;
  DB_CURRENCY currency_type = DB_CURRENCY_NULL;

  if (len >= 2
      && intl_is_currency_symbol ((const char *) p, &currency_type, &symbol_size,
				  CURRENCY_CHECK_MODE_ESC_ISO | CURRENCY_CHECK_MODE_GRAMMAR))
    {
      token += symbol_size;
    }

  if (currency_type == DB_CURRENCY_NULL)
    {
      currency_type = DB_CURRENCY_DOLLAR;
    }

  amt = strtod ((const char *) token, &str_ptr);

  if (str == str_ptr || OR_CHECK_DOUBLE_OVERFLOW (amt))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, db_get_type_name (DB_TYPE_MONETARY));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_MONETARY, str);
    }
  else
    db_make_monetary (val, currency_type, amt);

error_exit:
  return (err);
}

/*
 * ldr_monetary_db_monetary -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_monetary_db_monetary (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  char *mem;
  DB_VALUE val;

  CHECK_ERR (err, ldr_monetary_elem (context, str, len, &val));
  mem = context->mobj + att->offset;
  CHECK_ERR (err, PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
  OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);

error_exit:
  return err;
}

/*
 * ldr_collection_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_collection_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val)
{
  int err = ER_LDR_NESTED_SET;
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
  display_error (0);
  (void) ldr_null_elem (context, str, len, val);
  return err;
}

/*
 * ldr_collection_db_collection -
 *    return:
 *    context():
 *    str():
 *    len():
 *    att():
 */
static int
ldr_collection_db_collection (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  LDR_ATTDESC *attdesc;

  attdesc = &context->attrs[context->next_attr];

  /* Set the approriate action function to deal with collection elements */

  ldr_act = ldr_act_elem;

  if (context->collection == NULL)
    {
      /* 
       * This kind of bites:  we need to avoid advancing the next_attr
       * counter until we actually hit the closing brace.  Since ldr_act_attr
       * (which has called this function) will increment the counter
       * unconditionally, we decrement it here to compensate.  I with there
       * were a better way of dealing with this.
       */
      context->next_attr -= 1;

      /* 
       * We've just seen the leading brace of a collection, and we need to
       * create the "holding" collection.
       */
      context->collection = db_col_create (TP_DOMAIN_TYPE (att->domain), 0, attdesc->collection_domain);

      if (context->collection == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  err = er_errid ();
	}
      else
	{
	  context->set_domain = attdesc->collection_domain->setdomain;
	}
    }
  else
    {
      /* 
       * We've seen the trailing brace that ends the collection, and it's
       * now time to assign the collection to the instance.
       */
      DB_VALUE tmp;

      DB_MAKE_COLLECTION (&tmp, context->collection);

      context->collection = NULL;
      context->set_domain = NULL;

      /* We finished dealing with elements of a collection, set the action function to deal with normal attributes. */
      if (context->attribute_type == LDR_ATTRIBUTE_ANY)
	{
	  ldr_act = ldr_act_attr;
	  err = ldr_generic (context, &tmp);
	}
      else
	{
	  ldr_act = ldr_act_class_attr;
	  err = ldr_class_attr_db_generic (context, str, len, context->attrs[context->next_attr].att, &tmp);
	}
    }
  return err;
}

/*
 * ldr_reset_context -
 *    return:
 *    context():
 */
static int
ldr_reset_context (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  INST_INFO *inst = NULL;

  /* 
   * Check that we are not dealing with class attributes, use attribute_type
   * Do not create instances for class attributes.
   */
  if (context->cls && context->attribute_type == LDR_ATTRIBUTE_ANY && context->constructor == NULL)
    {

      /* Check that this instance was not a forward reference */
      if (context->table && context->inst_num > -1)
	inst = otable_find (context->table, context->inst_num);

      if (inst && (inst->flags & INST_FLAG_RESERVED))
	{
	  /* 
	   * This instance was already referenced and a workspace MOP created.
	   */
	  context->obj = ws_mop (&(inst->oid), context->cls);
	  if (context->obj == NULL)
	    {
	      err = er_filter_errid (false);
	      display_error (0);
	      goto error_exit;
	    }
	  /* 
	   * If this was a forward reference from a class attribute than we
	   * do not want to mark it as releasable.
	   */
	  if (!(inst->flags & INST_FLAG_CLASS_ATT))
	    {
	      ws_release_instance (context->obj);
	    }
	}
      else
	{
	  context->obj = db_create_internal (context->cls);
	  if (context->obj == NULL)
	    {
	      err = er_filter_errid (false);
	      display_error (0);
	      goto error_exit;
	    }

	  /* set pruning type to be performed on this object */
	  context->obj->pruning_type = context->class_type;

	  /* 
	   * Mark this mop as released so that we can cull it when we
	   * complete inserting this instance.
	   */
	  ws_release_instance (context->obj);
	}
      CHECK_ERR (err,
		 au_fetch_instance (context->obj, &context->mobj, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE));
      ws_pin_instance_and_class (context->obj, &context->obj_pin, &context->class_pin);
      ws_class_has_object_dependencies (context->cls);
    }
  context->next_attr = 0;
  LDR_CLEAR_ERR_COUNT (context);

error_exit:
  CHECK_CONTEXT_VALIDITY (context, err != NO_ERROR);
  return err;
}

/*
 * ldr_flush -
 *    return:
 *    context():
 */
static void
ldr_flush (LDR_CONTEXT * context)
{
  if (context->cls)
    {
      ws_intern_instances (context->cls);
    }
  context->flush_total += 1;
  context->inst_count = 0;
}

/*
 * check_commit - check interrupt and commit w.r.t. commit period
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 * Note:
 *    Checks to see if the interrupt flag was raised. If it was we check
 *    the interrupt type. We abort or commit based on the interrupt type.
 *    The interrupt type is determined by whether logging was enabled or not.
 *    Checks the state of the periodic commit counters.
 *    If this is enabled and the counter goes to zero, we commit
 *    the transaction.
 */
static int
check_commit (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  int committed_instances = 0;

  /* Check interrupt flag */
  if (ldr_Load_interrupted)
    {
      if (ldr_Load_interrupted == LDR_STOP_AND_ABORT_INTERRUPT)
	{
	  CHECK_ERR (err, db_abort_transaction ());
	  display_error_line (-1);
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INTERRUPTED_ABORT));
	  if (context->periodic_commit && Total_objects >= context->periodic_commit)
	    {
	      committed_instances = Total_objects - (context->periodic_commit - context->commit_counter);
	    }
	  else
	    {
	      committed_instances = 0;
	    }
	}
      else
	{
	  if (context->cls != NULL)
	    {
	      CHECK_ERR (err, ldr_assign_all_perm_oids ());
	      CHECK_ERR (err, db_commit_transaction ());
	      Last_committed_line = loader_yylineno - 1;
	      committed_instances = Total_objects + 1;
	      display_error_line (-1);
	      fprintf (stderr,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INTERRUPTED_COMMIT));
	    }
	}

      /* Invoke post interrupt callback function */
      if (ldr_post_interrupt_handler != NULL)
	{
	  (*ldr_post_interrupt_handler) (committed_instances);
	}

      if (ldr_Jmp_buf != NULL)
	{
	  longjmp (*ldr_Jmp_buf, 1);
	}
      else
	{
	  return (err);
	}
    }

  if (context->periodic_commit)
    {
      context->commit_counter--;
      if (context->commit_counter <= 0)
	{
	  if (context->cls != NULL)
	    {
	      print_log_msg (context->verbose,
			     msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_COMMITTING));
	      CHECK_ERR (err, ldr_assign_all_perm_oids ());
	      CHECK_ERR (err, db_commit_transaction ());
	      Last_committed_line = loader_yylineno - 1;
	      context->commit_counter = context->periodic_commit;

	      /* Invoke post commit callback function */
	      if (ldr_post_commit_handler != NULL)
		{
		  (*ldr_post_commit_handler) ((Total_objects + 1));
		}

	      /* After a commit we need to ensure that our attributes and attribute descriptors are updated. The commit 
	       * process can pull these from under us if another client is also updating the class. */
	      CHECK_ERR (err, ldr_refresh_attrs (context));
	    }
	}
    }

error_exit:
  if (err != NO_ERROR)
    {
      display_error_line (-1);
      fprintf (stderr, "%s\n", db_error_string (3));
      committed_instances = (-1);
      CHECK_CONTEXT_VALIDITY (context, true);
      LDR_INCREMENT_ERR_COUNT (context, 1);
    }
  return err;
}

/*
 * ldr_restore_pin_and_drop_obj - restore pin flag of a object and delete the
 * object optionally
 *    return: in
 *    context(in/out):
 *    drop_obj(in): if set, delete object also
 */
static void
ldr_restore_pin_and_drop_obj (LDR_CONTEXT * context, bool drop_obj)
{
  if (context->obj)
    {
      ws_restore_pin (context->obj, context->obj_pin, context->class_pin);
      if (drop_obj)
	{
	  db_drop (context->obj);
	  context->obj = NULL;
	}
    }
}

/*
 * ldr_act_finish_line - Completes an instance line.
 *    return: void
 *    context(in/out): current context.
 * Note:
 *   If there are missing attributes/arguments an error is flagged. The
 *   instance is inserted if it is not a class attribute modification. If
 *   the flush interval has been reached, the current set of instances created
 *   are flushed.
 */
void
ldr_act_finish_line (LDR_CONTEXT * context)
{
  int err = NO_ERROR;

  CHECK_SKIP ();
  if (context->valid)
    {
      if (context->next_attr < (context->num_attrs + context->arg_count))
	{
	  if (context->arg_count && (context->next_attr >= context->arg_index))
	    {
	      err = ER_LDR_MISSING_ARGUMENT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, context->arg_count,
		      context->next_attr - context->arg_index);
	    }
	  else
	    {
	      err = ER_LDR_MISSING_ATTRIBUTES;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, context->num_attrs, context->next_attr);
	    }
	  LDR_INCREMENT_ERR_COUNT (context, 1);
	}
    }

  ldr_restore_pin_and_drop_obj (context, ((context->err_count != 0) || (err != NO_ERROR)) || skip_current_instance);
  CHECK_ERR (err, err);

  if (context->valid && !context->err_count && !skip_current_instance)
    {
      if (context->constructor)
	{
	  err = insert_meth_instance (context);
	}
      else if (context->attribute_type == LDR_ATTRIBUTE_ANY)
	{
	  err = insert_instance (context);
	}

      if (err == NO_ERROR)
	{
	  if (context->flush_interval && context->inst_count >= context->flush_interval)
	    {
	      err = ldr_assign_all_perm_oids ();
	      if (err == NO_ERROR)
		{
		  if (!context->validation_only)
		    {
		      ldr_flush (context);
		      err = er_errid ();
		      if (err != NO_ERROR)
			{
			  /* flush failed */
			  err = er_filter_errid (true);	/* ignore warning */
			  if (err == NO_ERROR)
			    {
			      /* Flush error was ignored. Objects in workspace must be decached for later flush */
			      ws_decache_all_instances (context->cls);
			    }
			}
		      CHECK_ERR (err, er_errid ());
		    }
		}
	      else
		{
		  LDR_INCREMENT_ERR_COUNT (context, 1);
		}
	    }
	}
      else
	{
	  LDR_INCREMENT_ERR_COUNT (context, 1);
	}
    }

error_exit:
  if (context->err_count || (err != NO_ERROR))
    {
      ldr_abort ();
    }
  context->instance_started = 0;
  return;
}

/*
 * ldr_fini_context - finish parse context
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 */
static int
ldr_finish_context (LDR_CONTEXT * context)
{
  int err = er_filter_errid (false);

  if (err != NO_ERROR)
    {
      CHECK_CONTEXT_VALIDITY (context, true);
    }
  else
    {
      if (!context->validation_only)
	{
	  /* 
	   * Get permanent oids for the current class,
	   * Note : we have to this even if the instance total is 0 since
	   * we might have had forward object references, specified from
	   * class DEFAULT, SHARED or CLASS attributes
	   */
	  CHECK_ERR (err, ldr_assign_all_perm_oids ());
	  /* Need to flush the class now, for class attributes. Class objects are flushed during a commit, if
	   * ws_intern_instances() is called and culls object references from within a class_object the class_object
	   * will loose these references. */
	  if (context->attribute_type != LDR_ATTRIBUTE_ANY)
	    {
	      if (locator_flush_class (context->cls) != NO_ERROR)
		{
		  CHECK_ERR (err, er_errid ());
		}
	    }
	}
      if (context->inst_total && context->valid)
	{
	  if (!context->validation_only)
	    {
	      ldr_flush (context);
	      CHECK_ERR (err, er_errid ());
	    }
	  if (context->verbose)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INSTANCE_COUNT),
		       context->inst_total);
	    }
	}
    }

  /* Reset the action function to deal with attributes */
  ldr_act = ldr_act_attr;

#if defined(CUBRID_DEBUG)
  if ((err == NO_ERROR) && (envvar_get ("LOADER_DEBUG") != NULL))
    {
      if (context->inst_total)
	{
	  printf ("%d %s %s inserted in %d %s\n", context->inst_total, ldr_class_name (context),
		  context->inst_total == 1 ? "instance" : "instances", context->flush_total,
		  context->flush_total == 1 ? "flush" : "flushes");
	}

      if (context->err_total)
	{
	  printf ("%d %s %s ignored because of errors\n", context->err_total, ldr_class_name (context),
		  context->err_total == 1 ? "instance" : "instances");
	}
    }
#endif /* CUBRID_DEBUG */

  ldr_clear_and_free_context (context);

error_exit:
  if (err != NO_ERROR)
    {
      ldr_abort ();
    }
  return (err);
}

/*
 * ldr_act_init_context -
 *    return:
 *    context():
 *    class_name():
 *    len():
 */
void
ldr_act_init_context (LDR_CONTEXT * context, const char *class_name, int len)
{
  int err = NO_ERROR;
  DB_OBJECT *class_mop;

  CHECK_SKIP ();

  er_clear ();

  err = ldr_finish_context (context);
  if (err != NO_ERROR)
    {
      display_error (-1);
      return;
    }
  if (class_name)
    {
      if (intl_identifier_lower_string_size (class_name) >= SM_MAX_IDENTIFIER_LENGTH)
	{
	  display_error_line (0);
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_EXCEED_MAX_LEN),
		   SM_MAX_IDENTIFIER_LENGTH - 1);
	  CHECK_CONTEXT_VALIDITY (context, true);
	  ldr_abort ();
	  goto error_exit;
	}

      class_mop = ldr_find_class (class_name);
      if (class_mop == NULL)
	{
	  display_error_line (0);
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNKNOWN_CLASS),
		   class_name);
	  CHECK_CONTEXT_VALIDITY (context, true);
	  ldr_abort ();
	  goto error_exit;
	}
      if (!context->validation_only)
	{
	  context->cls = class_mop;
	  /* 
	   * Cache the class name. This will be used if we have a periodic
	   * commit and need to refresh the class
	   * This is a temporary fix, we will have to cache all the class
	   * names that we deal with, and refetch the classes and locks
	   * after a periodic commit.
	   */
	  context->class_name = (char *) malloc (len + 1);
	  if (context->class_name == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	      CHECK_CONTEXT_VALIDITY (context, true);
	      ldr_abort ();
	      goto error_exit;
	    }
	  strcpy (context->class_name, class_name);

	  if (is_internal_class (context->cls))
	    {
	      err = ER_LDR_SYSTEM_CLASS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, class_name);
	      CHECK_CONTEXT_VALIDITY (context, true);
	      display_error (-1);
	      ldr_abort ();
	      CHECK_ERR (err, err);
	    }
	  err = sm_partitioned_class_type (class_mop, &context->class_type, NULL, NULL);
	  if (err != NO_ERROR)
	    {
	      CHECK_CONTEXT_VALIDITY (context, true);
	      ldr_abort ();
	      goto error_exit;
	    }
	}
    }

  context->valid = true;
  if (context->verbose)
    {
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CLASS_TITLE),
	       class_name);
      fflush (stdout);
    }

  CHECK_VALIDATION_ONLY (context);

  if (context->cls)
    {
      if ((context->table = otable_find_class (context->cls)) == NULL)
	{
	  err = ER_LDR_MEMORY_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	  display_error (-1);
	  ldr_internal_error (context);
	  CHECK_CONTEXT_VALIDITY (context, true);
	}
    }

error_exit:
  return;
}

/*
 * ldr_act_check_missing_non_null_attrs - xxx
 *    return:
 *    context(in): current context
 * Note:
 *      xxx
 */
int
ldr_act_check_missing_non_null_attrs (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  int i;
  DB_ATTDESC *db_attdesc;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  CHECK_SKIP_WITH (err);
  RETURN_IF_NOT_VALID_WITH (context, err);

  if (context->validation_only)
    {
      goto error_exit;
    }

  err = au_fetch_class (context->cls, &class_, AU_FETCH_READ, AU_SELECT);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  switch (context->attribute_type)
    {
    case LDR_ATTRIBUTE_CLASS:
      att = class_->class_attributes;
      break;
    case LDR_ATTRIBUTE_SHARED:
      att = class_->shared;
      break;
    case LDR_ATTRIBUTE_ANY:
    case LDR_ATTRIBUTE_DEFAULT:
      att = class_->attributes;
      break;
    default:
      att = NULL;		/* impossible case */
      break;
    }
  if (att == NULL)
    {
      goto error_exit;		/* do nothing */
    }

  for (; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->flags & SM_ATTFLAG_NON_NULL)
	{
	  for (i = 0; i < context->num_attrs; i++)
	    {
	      db_attdesc = context->attrs[i].attdesc;
	      if (db_attdesc->name_space == att->header.name_space
		  && intl_identifier_casecmp (db_attdesc->name, att->header.name) == 0)
		{
		  break;
		}
	    }

	  /* not found */
	  if (i >= context->num_attrs)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1, att->header.name);
	      CHECK_ERR (err, ER_OBJ_ATTRIBUTE_CANT_BE_NULL);
	    }
	}
    }

error_exit:
  if (err != NO_ERROR)
    {
      CHECK_CONTEXT_VALIDITY (context, true);
      ldr_abort ();
    }
  return err;
}

/*
 * ldr_act_add_attr - Sets up the appropriate setters for the dealing with the
 * attributes.
 *    return: void
 *    context(in/out): current context
 *    attr_name(in): attribute name token
 *    len(in): length of token
 * Note:
 *      Determines the target type and sets the setter accordingly
 */
void
ldr_act_add_attr (LDR_CONTEXT * context, const char *attr_name, int len)
{
  int err = NO_ERROR;
  int i, n;
  LDR_ATTDESC *attdesc, *attrs_old;
  SM_CLASS *class_;
  SM_COMPONENT **comp_ptr = NULL;

  CHECK_SKIP ();
  RETURN_IF_NOT_VALID (context);

  if (context->validation_only)
    {
      context->num_attrs += 1;
      goto error_exit;
    }

  n = context->num_attrs + context->arg_count;

  attrs_old = context->attrs;
  context->attrs = (LDR_ATTDESC *) realloc (context->attrs, (n + 1) * sizeof (context->attrs[0]));
  if (context->attrs == NULL)
    {
      free_and_init (attrs_old);	/* Prevent leakage if realloc fails */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
    }

  CHECK_PTR (err, context->attrs);
  attdesc = &context->attrs[n];

  attdesc->attdesc = NULL;
  attdesc->att = NULL;
  attdesc->parser_str_len = 0;
  attdesc->parser_buf_len = 0;
  attdesc->parser_str = NULL;
  attdesc->ref_class = NULL;
  attdesc->collection_domain = NULL;

  /* 
   * Initialize the setters to something that hopefully won't crash and
   * burn if either of the following initialization calls fails.
   */
  for (i = 0; i < NUM_LDR_TYPES; i++)
    {
      attdesc->setter[i] = &ldr_ignore;
    }

  if (context->constructor)
    {
      /* 
       * At this point the parser has seen the CONSTRUCTOR syntax.
       * We are dealing with arguments for the constructor.
       * There is no attribute descriptor for method arguments so a check
       * to see that the number of args is within the number of args specified
       * is made and the function returns.
       */
      if (context->constructor->signatures->num_args
	  && (context->arg_count >= context->constructor->signatures->num_args))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_UNEXPECTED_ARGUMENT, 1,
		  context->constructor->signatures->num_args);
	  CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
	}
      return;
    }

  CHECK_ERR (err,
	     db_get_attribute_descriptor (context->cls, attr_name, context->attribute_type == LDR_ATTRIBUTE_CLASS, true,
					  &attdesc->attdesc));
  comp_ptr = (void *) (&attdesc->att);
  CHECK_ERR (err, sm_get_descriptor_component (context->cls, attdesc->attdesc, 1,	/* for update */
					       &class_, comp_ptr));

  context->num_attrs += 1;

  /* 
   * When dealing with class attributes the normal setters are not used, since
   * they rely on the use of an empty instance.
   */
  if (context->attribute_type != LDR_ATTRIBUTE_ANY)
    {
      int invalid_attr = 0;
      /* 
       * Set elements need to be gathhers in a collection value bucket
       * We can use the existing setter for this.
       */
      if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (attdesc->att->domain)))
	{
	  attdesc->setter[LDR_COLLECTION] = &ldr_collection_db_collection;
	  CHECK_ERR (err, select_set_domain (context, attdesc->att->domain, &(attdesc->collection_domain)));
	}
      if (context->attribute_type == LDR_ATTRIBUTE_SHARED)
	{
	  if (attdesc->att->header.name_space != ID_SHARED_ATTRIBUTE)
	    invalid_attr = 1;
	}
      else if (context->attribute_type == LDR_ATTRIBUTE_CLASS)
	{
	  if (attdesc->att->header.name_space != ID_CLASS_ATTRIBUTE)
	    invalid_attr = 1;
	}
      if (invalid_attr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INVALID_CLASS_ATTR, 2, ldr_attr_name (context),
		  ldr_class_name (context));
	  CHECK_ERR (err, ER_LDR_INVALID_CLASS_ATTR);
	}
      goto error_exit;
    }

  /* 
   * Now that we know we really have an attribute descriptor and a
   * SM_ATTRIBUTE, we can go ahead and initialize our setters to
   * something that can exploit them.
   */
  for (i = 0; i < NUM_LDR_TYPES; i++)
    {
      attdesc->setter[i] = &ldr_mismatch;
    }

  attdesc->setter[LDR_NULL] = &ldr_null_db_generic;
  attdesc->setter[LDR_INT] = &ldr_int_db_generic;
  attdesc->setter[LDR_NUMERIC] = &ldr_numeric_db_generic;
  attdesc->setter[LDR_DOUBLE] = &ldr_real_db_generic;
  attdesc->setter[LDR_FLOAT] = &ldr_real_db_generic;

  /* 
   * These two system object setters are setup to return an
   * appropriate error message to the user.
   */

  attdesc->setter[LDR_SYS_USER] = &ldr_sys_user_db_generic;
  attdesc->setter[LDR_SYS_CLASS] = &ldr_sys_class_db_generic;

  switch (TP_DOMAIN_TYPE (attdesc->att->domain))
    {
    case DB_TYPE_CHAR:
      attdesc->setter[LDR_STR] = &ldr_str_db_char;
      break;

    case DB_TYPE_VARCHAR:
      attdesc->setter[LDR_STR] = &ldr_str_db_varchar;
      break;

    case DB_TYPE_BIGINT:
      attdesc->setter[LDR_INT] = &ldr_int_db_bigint;
      break;

    case DB_TYPE_INTEGER:
      attdesc->setter[LDR_INT] = &ldr_int_db_int;
      break;

    case DB_TYPE_SHORT:
      attdesc->setter[LDR_INT] = &ldr_int_db_short;
      break;

    case DB_TYPE_FLOAT:
      attdesc->setter[LDR_INT] = &ldr_real_db_float;
      attdesc->setter[LDR_NUMERIC] = &ldr_real_db_float;
      attdesc->setter[LDR_DOUBLE] = &ldr_real_db_float;
      attdesc->setter[LDR_FLOAT] = &ldr_real_db_float;
      break;

    case DB_TYPE_DOUBLE:
      attdesc->setter[LDR_INT] = &ldr_real_db_double;
      attdesc->setter[LDR_NUMERIC] = &ldr_real_db_double;
      attdesc->setter[LDR_DOUBLE] = &ldr_real_db_double;
      attdesc->setter[LDR_FLOAT] = &ldr_real_db_double;
      break;

    case DB_TYPE_NUMERIC:
      attdesc->setter[LDR_INT] = &ldr_int_db_generic;
      attdesc->setter[LDR_NUMERIC] = &ldr_numeric_db_generic;
      attdesc->setter[LDR_DOUBLE] = &ldr_real_db_generic;
      attdesc->setter[LDR_FLOAT] = &ldr_real_db_generic;
      break;

    case DB_TYPE_DATE:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_DATE] = &ldr_date_db_date;
      break;

    case DB_TYPE_TIME:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIME] = &ldr_time_db_time;
      break;

    case DB_TYPE_TIMELTZ:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIMELTZ] = &ldr_timeltz_db_timeltz;
      break;

    case DB_TYPE_TIMETZ:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIMETZ] = &ldr_timetz_db_timetz;
      break;

    case DB_TYPE_TIMESTAMP:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIMESTAMP] = &ldr_timestamp_db_timestamp;
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIMESTAMPLTZ] = &ldr_timestampltz_db_timestampltz;
      break;

    case DB_TYPE_TIMESTAMPTZ:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIMESTAMPTZ] = &ldr_timestamptz_db_timestamptz;
      break;

    case DB_TYPE_DATETIME:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_DATETIME] = &ldr_datetime_db_datetime;
      break;

    case DB_TYPE_DATETIMELTZ:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_DATETIMELTZ] = &ldr_datetimeltz_db_datetimeltz;
      break;

    case DB_TYPE_DATETIMETZ:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_DATETIMETZ] = &ldr_datetimetz_db_datetimetz;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      attdesc->setter[LDR_COLLECTION] = &ldr_collection_db_collection;
      CHECK_ERR (err, select_set_domain (context, attdesc->att->domain, &(attdesc->collection_domain)));
      break;

    case DB_TYPE_OBJECT:
    case DB_TYPE_VOBJ:
      attdesc->setter[LDR_CLASS_OID] = &ldr_class_oid_db_object;
      attdesc->setter[LDR_OID] = &ldr_oid_db_object;
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      attdesc->setter[LDR_BSTR] = &ldr_bstr_db_varbit;
      attdesc->setter[LDR_XSTR] = &ldr_xstr_db_varbit;
      break;

    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      attdesc->setter[LDR_NSTR] = &ldr_nstr_db_varnchar;
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      attdesc->setter[LDR_ELO_EXT] = &ldr_elo_ext_db_elo;
      attdesc->setter[LDR_ELO_INT] = &ldr_elo_int_db_elo;
      break;

    case DB_TYPE_MONETARY:
      attdesc->setter[LDR_MONETARY] = &ldr_monetary_db_monetary;
      break;

    default:
      break;
    }

error_exit:
  if (err != NO_ERROR)
    {
      CHECK_CONTEXT_VALIDITY (context, true);
      ldr_abort ();
    }
  return;
}

/*
 * ldr_refresh_attrs - refresh the attributes and attribute descriptors for
 * a class.
 *    return: error code
 *    context(in): context
 * Note:
 *   This is called after a periodic commit to refresh the attributes and
 *   attribute descriptors for a class. These may have been pulled from under
 *   us if another client updated the class.
 *   The attrs, LDR_ATTRS array, is traversed and each attribute, and
 *   attribute descriptor is refreshed for the current class.
 */
static int
ldr_refresh_attrs (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  DB_ATTDESC *db_attdesc;
  LDR_ATTDESC *attdesc;
  int i;
  SM_CLASS *class_;
  SM_COMPONENT **comp_ptr = NULL;

  context->cls = ldr_find_class (context->class_name);
  if (context->cls == NULL)
    {
      display_error (0);
      return er_filter_errid (false);
    }

  for (i = 0; i < context->num_attrs; i += 1)
    {
      attdesc = &(context->attrs[i]);
      CHECK_ERR (err,
		 db_get_attribute_descriptor (context->cls, attdesc->attdesc->name,
					      context->attribute_type == LDR_ATTRIBUTE_CLASS, true, &db_attdesc));
      /* Free existing descriptor */
      db_free_attribute_descriptor (attdesc->attdesc);
      attdesc->attdesc = db_attdesc;
      /* Get refreshed attribute */
      comp_ptr = (void *) &attdesc->att;
      CHECK_ERR (err, sm_get_descriptor_component (context->cls, attdesc->attdesc, 1,	/* for update */
						   &class_, comp_ptr));
    }

error_exit:
  return err;
}

/*
 * ldr_act_restrict_attributes - called after ldr_start_class to indicate that
 * the attributes being added are class/shared attributes rather than normal
 * instance attributes.
 *    return: none
 *    context(in):
 *    type(in): type of attributes to expect
 */
void
ldr_act_restrict_attributes (LDR_CONTEXT * context, LDR_ATTRIBUTE_TYPE type)
{

  CHECK_SKIP ();
  RETURN_IF_NOT_VALID (context);

  context->attribute_type = type;

  if (!context->validation_only)
    {
      /* Set the appropriate functions to handle class attributes */
      ldr_act = ldr_act_class_attr;
    }

  return;
}

/*
 * update_default_count - Updates the default instances counter.
 *    return: none
 *    table(out):
 *    oid(in): not used
 */
static int
update_default_count (CLASS_TABLE * table, OID * oid)
{
  ldr_Current_context->default_count++;
  table->total_inserts++;
  return NO_ERROR;
}

/*
 * update_default_instances_stats - Update the statics of the number of
 * instances referenced but never actually defined in the loader input file.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 * Note:
 *    These will be inserted with all default values.
 */
static int
update_default_instances_stats (LDR_CONTEXT * context)
{
  int err;

  context->default_count = 0;

  /* 
   * note that if we run out of space, the context->valid flag will get
   * turned off, insert_default_instance needs to check this
   */
  err = otable_map_reserved (update_default_count, 1);

  return err;
}

/*
 * ldr_finish -
 *    return:
 *    context():
 *    err():
 */
int
ldr_finish (LDR_CONTEXT * context, int err)
{
  int finish_error = NO_ERROR;

  if (err)
    {
      ldr_clear_and_free_context (context);
    }
  else
    {
      finish_error = ldr_finish_context (context);
      if (!finish_error && !context->validation_only && !(context->err_total))
	{
	  finish_error = update_default_instances_stats (context);
	}
    }

  return finish_error;
}

/*
 * ldr_act_finish -
 *    return:
 *    context():
 *    parse_error():
 */
void
ldr_act_finish (LDR_CONTEXT * context, int parse_error)
{
  /* do not display duplicate error msg */
  int err = er_filter_errid (false);
  if (ldr_finish (context, parse_error) && err == NO_ERROR)
    {
      display_error (-1);
    }

  /* check errors */

  if (parse_error)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_STOPPED));
    }
}

/*
 * insert_instance - Adds the instance to the loader internal oid table,
 * otable, and adds the mop to the list of mops that require permanent oids,
 * prior to flushing.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 */
static int
insert_instance (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  INST_INFO *inst;

  if (!context->validation_only)
    {
      if (context->obj)
	{
	  /* 
	   * Note : instances without ids are not inserted in the otable as
	   * there can not be referenced from the load file.
	   */
	  if (context->inst_num >= 0)
	    {
	      if (No_oid_hint)
		{
		  CHECK_ERR (err, ldr_add_mop_tempoid_map (context->obj, context->table, context->inst_num));
		}
	      else
		{
		  inst = otable_find (context->table, context->inst_num);

		  if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
		    {
		      CHECK_ERR (err, otable_insert (context->table, WS_REAL_OID (context->obj), context->inst_num));
		      CHECK_PTR (err, inst = otable_find (context->table, context->inst_num));
		      CHECK_ERR (err, ldr_add_mop_tempoid_map (context->obj, context->table, context->inst_num));
		    }
		  else
		    {
		      err = otable_update (context->table, context->inst_num);
		    }

		}
	    }
	  if (!err)
	    {
	      context->obj = NULL;
	      context->table->total_inserts++;
	      err = check_commit (context);
	    }
	}
    }

  if (err)
    {
      ldr_internal_error (context);
    }
  else
    {
      context->inst_count++;
      context->inst_total += 1;
      Total_objects++;

      if (context->verbose && context->status_count)
	{
	  context->status_counter++;
	  if (context->status_counter >= context->status_count)
	    {
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INSTANCE_COUNT_EX),
		       context->inst_total);
	      fflush (stdout);
	      context->status_counter = 0;
	    }
	}
    }

error_exit:
  return err;
}

/*
 * ldr_act_start_instance - Finishes off the previous instance and resets the
 * context to deal with a new instance.
 *    return: void
 *    context(in/out): current context
 *    id(in): id of current instance.
 * Note:
 *    This is called when a new instance if found by the parser.
 */
void
ldr_act_start_instance (LDR_CONTEXT * context, int id, LDR_CONSTANT * cons)
{
  CHECK_SKIP ();
  if (context->valid)
    {

      context->inst_num = id;
      if (cons)
	{
	  context->cons = cons;
	}

      if (ldr_reset_context (context) != NO_ERROR)
	{
	  display_error (-1);
	}

      context->instance_started = 1;
    }
}

/*
 * construct_instance - called to insert an instance using the current
 * constructor method.
 *    return: object pointer
 *    context(in/out):
 * Note:
 *    This simulates the token parsing here.
 */
static MOP
construct_instance (LDR_CONTEXT * context)
{
  DB_VALUE *meth_args[LDR_MAX_ARGS + 1];
  DB_VALUE retval;
  int err = NO_ERROR;
  MOP obj = NULL;
  DB_VALUE vals[LDR_MAX_ARGS];
  int i, a;
  LDR_ATTDESC *attdesc;
  SM_CLASS *class_;
  SM_COMPONENT **comp_ptr = NULL;

  for (i = 0, a = context->arg_index; i < context->arg_count && err == NO_ERROR && i < LDR_MAX_ARGS; i++, a++)
    {
      err =
	(*(elem_converter[context->attrs[a].parser_type])) (context, context->attrs[a].parser_str,
							    context->attrs[a].parser_str_len, &vals[i]);
      meth_args[i] = &(vals[i]);
    }

  meth_args[i] = NULL;

  err = db_send_argarray (context->cls, context->constructor->header.name, &retval, meth_args);

  if (!err && DB_VALUE_TYPE (&retval) == DB_TYPE_OBJECT)
    {
      obj = DB_GET_OBJECT (&retval);
      context->obj = obj;
      err = au_fetch_instance (context->obj, &context->mobj, AU_FETCH_UPDATE, LC_FETCH_MVCC_VERSION, AU_UPDATE);
      if (err == NO_ERROR)
	{
	  ws_pin_instance_and_class (context->obj, &context->obj_pin, &context->class_pin);
	  ws_class_has_object_dependencies (context->cls);
	}

      /* now we have to initialize the instance with the supplied values */
      for (context->next_attr = 0; context->next_attr < context->arg_index && !err; context->next_attr++)
	{
	  attdesc = &context->attrs[context->next_attr];

	  comp_ptr = (void *) &attdesc->att;
	  err = sm_get_descriptor_component (context->cls, attdesc->attdesc, 1, &class_, comp_ptr);

	  if (!err)
	    {
	      err =
		(*(attdesc->setter[attdesc->parser_type])) (context, attdesc->parser_str, attdesc->parser_str_len,
							    attdesc->att);
	    }
	}
    }
  else
    {
      CHECK_ERR (err, ER_GENERIC_ERROR);
      ldr_internal_error (context);
    }

error_exit:
  return context->obj;
}

/*
 * insert_meth_instance - Inserts the pending instance generated for by a
 * method call into the database.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 */
static int
insert_meth_instance (LDR_CONTEXT * context)
{
  int err = NO_ERROR;
  INST_INFO *inst;
  MOP real_obj;

  if (!context->validation_only)
    {
      if (context->constructor != NULL)
	{
	  CHECK_PTR (err, real_obj = construct_instance (context));
	  if (real_obj == NULL)
	    {
	      CHECK_ERR (err, er_errid ());
	    }
	  else
	    {
	      ws_release_instance (real_obj);
	      inst = otable_find (context->table, context->inst_num);
	      if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
		{
		  CHECK_ERR (err, otable_insert (context->table, WS_OID (real_obj), context->inst_num));
		  CHECK_PTR (err, inst = otable_find (context->table, context->inst_num));
		  CHECK_ERR (err, ldr_add_mop_tempoid_map (real_obj, context->table, context->inst_num));
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_FORWARD_CONSTRUCTOR, 0);
		  CHECK_ERR (err, ER_LDR_FORWARD_CONSTRUCTOR);
		}
	    }
	}
      else
	{
	  err = ER_GENERIC_ERROR;
	}
    }

  if (err)
    {
      ldr_internal_error (context);
    }
  else
    {
      context->inst_count++;
      context->inst_total += 1;
      Total_objects++;

      if (context->verbose && context->status_count)
	{
	  context->status_counter++;
	  if (context->status_counter >= context->status_count)
	    {
	      fprintf (stdout,
		       msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_INSTANCE_COUNT_EX),
		       context->inst_total);
	      fflush (stdout);
	      context->status_counter = 0;
	    }
	}
    }
error_exit:
  return err;
}

/*
 * ldr_act_set_constructor - Specifies a constructor method for the instances
 * of the current class.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 *    name(in): method name
 * Note:
 *    The name must be the name of a class method that when called will return
 *    an instance.  After this function, the loader will expect zero or more
 *    calls to ldr_act_add_argument to specify any arguments that must be
 *    passed to the constructor method.
 */
int
ldr_act_set_constructor (LDR_CONTEXT * context, const char *name)
{
  int err = NO_ERROR;
  SM_METHOD *meth;

  CHECK_SKIP_WITH (err);
  if (context->validation_only)
    {
      context->arg_index = context->num_attrs;

      /* setup the appropriate constructor handling functions */

      ldr_act = ldr_act_meth;
      goto error_exit;
    }

  if (context->valid)
    {
      meth = db_get_class_method (context->cls, name);
      if (meth == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INVALID_CONSTRUCTOR, 2, name, ldr_class_name (context));
	  CHECK_ERR (err, ER_LDR_INVALID_CONSTRUCTOR);
	}
      else
	{
	  /* for now, a method can have exactly one signature */

	  context->constructor = meth;
	  context->arg_index = context->num_attrs;

	  /* setup the appropriate constructor handling functions */

	  ldr_act = ldr_act_meth;
	}
    }

error_exit:
  if (err != NO_ERROR)
    {
      CHECK_CONTEXT_VALIDITY (context, true);
      ldr_abort ();
    }
  return err;
}

/*
 * add_element - add an element to either the loader's argument array
 *    return: success code (non-zero if ok)
 *    elements(in): pointer to an array of pointers
 *    count(in/out): current index (and returned new index)
 *    max(in/out): maximum size (and returned maximum size)
 *    grow(in): amount to grow if necessary
 * Note:
 *    We first check to see if *count is less than *max, if so we simply
 *    increment count.  If not, we extend the array, return the incremented
 *    count and also returned the new max size.
 */
static int
add_element (void ***elements, int *count, int *max, int grow)
{
  int new_ = -1, resize, i;
  int err = NO_ERROR;
  void ***elements_old;

  if (*count >= *max)
    {

      resize = *max + grow;

      elements_old = elements;

      *elements = (void **) realloc (*elements, sizeof (void *) * resize);
      if (*elements == NULL)
	{
	  free_and_init (*elements_old);	/* Prevent leak if realloc fails */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	}
      CHECK_PTR (err, *elements);

      for (i = *count; i < resize; i++)
	{
	  (*elements)[i] = NULL;
	}
      *max = resize;
    }
  new_ = *count;
  *count = new_ + 1;

error_exit:
  if (err != NO_ERROR || *elements == NULL)
    {
      return (-1);
    }
  return new_;
}

/*
 * add_argument - Extends the loader's argument array.
 *    return: next argument index
 *    context(out): context
 */
static int
add_argument (LDR_CONTEXT * context)
{
  int index;

  index = add_element ((void ***) (&(context->args)), &(context->arg_count), &(context->maxarg), LDR_ARG_GROW_SIZE);
  return index;
}

/*
 * ldr_act_add_argument - specify parameters to an instance constructor method
 * previously specified with ldr_act_set_constructor.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 *    name(in): argument name
 * Note:
 *    The name isn't really important here since method argumetns don't
 *    have domains.  It is however important to specify as many arguments
 *    as the method expects because the domain validation will be
 *    done positionally according to the method signature in the schema.
 */
int
ldr_act_add_argument (LDR_CONTEXT * context, const char *name)
{
  int err = NO_ERROR;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;
  int index;

  CHECK_SKIP_WITH (err);
  if (context->validation_only)
    {
      context->arg_count += 1;
      goto error_exit;
    }

  if (context->valid)
    {
      if (context->constructor == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_UNEXPECTED_ARGUMENT, 1, 0);
	  CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
	}
      else
	{
	  sig = context->constructor->signatures;
	  /* arg count of zero currently means "variable", not good */
	  if (sig->num_args && (context->arg_count) >= sig->num_args)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_UNEXPECTED_ARGUMENT, 1, sig->num_args);
	      CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
	    }
	  else
	    {
	      /* Locate the argument descriptor, remember to adjust for 1 based argument numbering. */
	      arg = classobj_find_method_arg (&sig->args, context->arg_count, 0);
	      /* This is called to setup the LDR_ATTDESC structure */
	      ldr_act_add_attr (context, name, strlen (name));
	      index = add_argument (context);
	      if (index >= 0)
		context->args[index] = arg;
	      else
		{
		  err = ER_LDR_MEMORY_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
		  ldr_internal_error (context);
		  CHECK_ERR (err, err);
		}
	    }
	}
    }
error_exit:
  if (err != NO_ERROR)
    {
      CHECK_CONTEXT_VALIDITY (context, true);
      ldr_abort ();
    }
  return err;
}

/*
 * invalid_class_id_error - called when invalid class id is met.
 *    return:
 *    context(in):
 *    id(in): class id
 */
static void
invalid_class_id_error (LDR_CONTEXT * context, int id)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNKNOWN_CLASS_ID), id,
	   ldr_attr_name (context), ldr_class_name (context));
  CHECK_CONTEXT_VALIDITY (context, true);
}

/*
 * ldr_act_set_ref_class - Find the class that is about to be used in an
 * instance reference.
 *    return: void
 *    context(in/out): context
 *    name(in): class name
 */
void
ldr_act_set_ref_class (LDR_CONTEXT * context, char *name)
{
  DB_OBJECT *mop;

  CHECK_SKIP ();
  RETURN_IF_NOT_VALID (context);

  if (!context->validation_only)
    {
      mop = db_find_class (name);
      if (mop != NULL)
	{
	  context->attrs[context->next_attr].ref_class = mop;
	}
      else
	{
	  ldr_invalid_class_error (context);
	}
    }
}

/*
 * ldr_act_set_instance_id - Set the instance to be used from the reference
 * class
 *    return: void
 *    context(in/out): context
 *    id(in): instance id for ref_class already set
 */
void
ldr_act_set_instance_id (LDR_CONTEXT * context, int id)
{
  CHECK_SKIP ();
  RETURN_IF_NOT_VALID (context);

  if (!context->validation_only)
    {
      if (context->attrs[context->next_attr].ref_class != NULL)
	context->attrs[context->next_attr].instance_id = id;
      else
	{
	  CHECK_CONTEXT_VALIDITY (context, true);
	}
    }
}

/*
 * ldr_act_set_ref_class_id - Find the class that is about to be used in an
 * instance reference
 *    return: void
 *    context(in/out): context
 *    id(in): class id
 * Note:
 *    Unlike act_set_ref_class, the class is identified through a previously
 *    assigned id number.
 */
void
ldr_act_set_ref_class_id (LDR_CONTEXT * context, int id)
{
  DB_OBJECT *class_;

  CHECK_SKIP ();
  RETURN_IF_NOT_VALID (context);

  if (!context->validation_only)
    {
      class_ = ldr_get_class_from_id (id);
      if (class_ != NULL)
	{
	  context->attrs[context->next_attr].ref_class = class_;
	}
      else
	{
	  invalid_class_id_error (context, id);
	}
    }
}

DB_OBJECT *
ldr_act_get_ref_class (LDR_CONTEXT * context)
{
  RETURN_IF_NOT_VALID_WITH (context, NULL);

  if (!context->validation_only)
    {
      return context->attrs[context->next_attr].ref_class;
    }

  return NULL;
}

/*
 * ldr_init_loader - Initializes the global loader state.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in/out): context
 * Note:
 *    This should be called once prior to loader operation.
 */
static int
ldr_init_loader (LDR_CONTEXT * context)
{
  int i;
  int err = NO_ERROR;
  DB_TIMETZ timetz;
  DB_DATETIME datetime;
  DB_DATETIMETZ datetimetz;
  DB_TIMESTAMPTZ timestamptz;
  DB_ELO *null_elo = NULL;

  /* 
   * Definitely *don't* want to use oid preflushing in this app; it just
   * gives us some extra overhead that we don't care about.
   */
  tm_Use_OID_preflush = false;

  /* Set the appropriate action function for normal attribute values */

  ldr_act = ldr_act_attr;

  /* 
   * Optimization to avoid calling db_value_domain_init all of the time
   * during loading; we can simply copy these templates much more cheaply.
   */
  db_make_short (&ldr_short_tmpl, 0);
  db_make_int (&ldr_int_tmpl, 0);
  db_make_bigint (&ldr_bigint_tmpl, 0);
  db_make_char (&ldr_char_tmpl, 1, (char *) "a", 1, LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (&ldr_varchar_tmpl, 1, (char *) "a", 1, LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_float (&ldr_float_tmpl, (float) 0.0);
  db_make_double (&ldr_double_tmpl, (double) 0.0);
  db_make_date (&ldr_date_tmpl, 1, 1, 1996);
  db_make_time (&ldr_time_tmpl, 0, 0, 0);
  timetz.time = 0;
  timetz.tz_id = 0;
  db_make_timeltz (&ldr_timeltz_tmpl, &(timetz.time));
  db_make_timetz (&ldr_timetz_tmpl, &timetz);
  db_make_timestamp (&ldr_timestamp_tmpl, 0);
  timestamptz.timestamp = 0;
  timestamptz.tz_id = 0;
  db_make_timestampltz (&ldr_timestampltz_tmpl, 0);
  db_make_timestamptz (&ldr_timestamptz_tmpl, &timestamptz);
  db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);
  db_make_datetime (&ldr_datetime_tmpl, &datetime);
  datetimetz.datetime = datetime;
  datetimetz.tz_id = 0;
  db_make_datetimeltz (&ldr_datetimeltz_tmpl, &(datetimetz.datetime));
  db_make_datetimetz (&ldr_datetimetz_tmpl, &datetimetz);
  db_make_elo (&ldr_blob_tmpl, DB_TYPE_BLOB, null_elo);
  db_make_elo (&ldr_clob_tmpl, DB_TYPE_CLOB, null_elo);
  db_make_bit (&ldr_bit_tmpl, 1, "0", 1);

  /* 
   * Set up the conversion functions for collection elements.  These
   * don't need to be done on a per-attribute basis (well, I suppose they
   * could if we were really worried about it, but for now we let the
   * usual domain coercion stuff take care of the dirty work).
   */
  for (i = 0; i < NUM_LDR_TYPES; i++)
    {
      elem_converter[i] = &ldr_null_elem;
    }
  elem_converter[LDR_NULL] = &ldr_null_elem;
  elem_converter[LDR_INT] = &ldr_int_elem;
  elem_converter[LDR_STR] = &ldr_str_elem;
  elem_converter[LDR_NUMERIC] = &ldr_numeric_elem;
  elem_converter[LDR_DOUBLE] = &ldr_double_elem;
  elem_converter[LDR_FLOAT] = &ldr_float_elem;
  elem_converter[LDR_OID] = &ldr_oid_elem;
  elem_converter[LDR_CLASS_OID] = &ldr_class_oid_elem;
  elem_converter[LDR_DATE] = &ldr_date_elem;
  elem_converter[LDR_TIME] = &ldr_time_elem;
  elem_converter[LDR_TIMELTZ] = &ldr_timeltz_elem;
  elem_converter[LDR_TIMETZ] = &ldr_timetz_elem;
  elem_converter[LDR_TIMESTAMP] = &ldr_timestamp_elem;
  elem_converter[LDR_TIMESTAMPLTZ] = &ldr_timestampltz_elem;
  elem_converter[LDR_TIMESTAMPTZ] = &ldr_timestamptz_elem;
  elem_converter[LDR_DATETIME] = &ldr_datetime_elem;
  elem_converter[LDR_DATETIMELTZ] = &ldr_datetimeltz_elem;
  elem_converter[LDR_DATETIMETZ] = &ldr_datetimetz_elem;
  elem_converter[LDR_COLLECTION] = &ldr_collection_elem;
  elem_converter[LDR_BSTR] = &ldr_bstr_elem;
  elem_converter[LDR_XSTR] = &ldr_xstr_elem;
  elem_converter[LDR_NSTR] = &ldr_nstr_elem;
  elem_converter[LDR_MONETARY] = &ldr_monetary_elem;
  elem_converter[LDR_ELO_EXT] = &ldr_elo_ext_elem;
  elem_converter[LDR_ELO_INT] = &ldr_elo_int_elem;

  /* Set up the lockhint array. Used by ldr_find_class() when locating a class. */
  ldr_Hint_locks[0] = locator_fetch_mode_to_lock (DB_FETCH_CLREAD_INSTWRITE, LC_CLASS, LC_FETCH_CURRENT_VERSION);
  ldr_Hint_classnames[0] = NULL;
  ldr_Hint_subclasses[0] = 0;
  ldr_Hint_flags[0] = LC_PREF_FLAG_LOCK;

  Total_objects = 0;
  Total_fails = 0;
  ldr_clear_context (context);
  ldr_act_init_context (context, NULL, 0);
  ldr_Current_context = context;
  LDR_CLEAR_ERR_TOTAL (context);
  context->validation_only = 0;
  context->valid = true;
  context->flush_total = 0;

  return (err);
}

/*
 * ldr_init - This prepares the loader for use.
 *    return: none
 *    verbose(in): non-zero to enable printing of status messages
 * Note:
 *    If the verbose flag is on, status messages will be displayed
 *    as the load proceeds.
 *    The loader is initially configured for validation mode.
 *    Call ldr_start() to clear the current state and enter
 *    insertion mode.
 */
int
ldr_init (bool verbose)
{
  LDR_CONTEXT *context;

  ldr_Current_context = &ldr_Context;
  context = ldr_Current_context;

  if (ldr_init_loader (ldr_Current_context))
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  idmap_init ();

  if (otable_init ())
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (clist_init ())
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  context->validation_only = true;
  context->verbose = verbose;

  context->status_count = 0;
  /* used to monitor the number of insertions performed */
  context->status_counter = 0;

  context->status_count = 10;

  obt_enable_unique_checking (false);

  return NO_ERROR;
}

/*
 * ldr_start - This prepares the loader for actual insertion of data.
 *    return: none
 *    periodic_commit(in): periodic_commit interval value
 * Note:
 *    It is commonly called after the input file has been processed
 *    once for syntax checking.  It can also be called immediately
 *    after ldr_init() to skip the syntax check and go right into
 *    insertion mode.
 */
int
ldr_start (int periodic_commit)
{
  int err;
  LDR_CONTEXT *context;

  if (!ldr_Current_context)
    {
      ldr_Current_context = &ldr_Context;
    }

  context = ldr_Current_context;

  ldr_clear_context (context);

  /* 
   * Initialize the mop -> temporary_oid, table used to obtain a mapping
   * between permanent OID and workspace mop, when permanent OIDs are obtained.
   */
  err = ldr_mop_tempoid_maps_init ();
  if (err != NO_ERROR)
    {
      return err;
    }

  context->validation_only = 0;

  if (periodic_commit <= 0)
    {
      context->periodic_commit = 0;
    }
  else
    {
      context->periodic_commit = periodic_commit;
      context->commit_counter = periodic_commit;
    }

  /* make sure we reset this to get accurate statistics */
  Total_objects = 0;
  Total_fails = 0;
  return NO_ERROR;
}

/*
 * ldr_final - shut down the loader.
 *    return: void
 * Note:
 *    Since there may be a pending instance that has not yet been inserted,
 *    you must always call ldr_finish to make sure this occurrs.
 *    If the caller detects an error and wishes to abort the load immediate,
 *    pass in non-zero and the pending instance will not be inserted.
 *    After this call, the loader cannot be used and the session
 *    must begin again with a ldr_init() call.
 */
int
ldr_final (void)
{
  int shutdown_error = NO_ERROR;
  LDR_CONTEXT *context;

  context = ldr_Current_context;

  shutdown_error = ldr_finish (context, 1);

  clist_final ();
  idmap_final ();
  otable_final ();
  ldr_mop_tempoid_maps_final ();

  return shutdown_error;
}

/*
 * ldr_register_post_interrupt_handler - registers a post interrupt callback
 * function.
 *    return: void return, sets ldr_post_interrupt_handler
 *    handler(in): interrupt handler
 *    ldr_jmp_buf(in): jump buffer
 */
void
ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER handler, void *ldr_jmp_buf)
{
  ldr_Jmp_buf = (jmp_buf *) ldr_jmp_buf;
  ldr_post_interrupt_handler = handler;
}

/*
 * ldr_register_post_commit_handler - registers a post commit callback
 * function.
 *    return: void return, sets ldr_post_commit_handler
 *    handler(in): handler
 *    arg(in): not used
 */
void
ldr_register_post_commit_handler (LDR_POST_COMMIT_HANDLER handler, void *arg)
{
  ldr_post_commit_handler = handler;
}

/*
 * ldr_interrupt_has_occurred - set interrupt type
 *    return: void return.
 *    type(in): interrupt type
 */
void
ldr_interrupt_has_occurred (int type)
{
  ldr_Load_interrupted = type;
}

/*
 * ldr_stats - This is used to access the statistics maintained during loading.
 *    return: void
 *    errors(out): return error count
 *    objects(out): return object count
 *    defaults(out): return default object count
 *    lastcommit(out):
 *    fails(out): return fail count
 */
void
ldr_stats (int *errors, int *objects, int *defaults, int *lastcommit, int *fails)
{
  if (errors != NULL)
    {
      *errors = ldr_Current_context->err_total;
    }

  if (objects != NULL)
    {
      *objects = Total_objects;
    }

  if (defaults != NULL)
    {
      *defaults = ldr_Current_context->default_count;
    }

  if (lastcommit != NULL)
    {
      *lastcommit = Last_committed_line;
    }
  if (fails != NULL)
    {
      *fails = Total_fails;
    }
}

/*
 * ldr_update_statistics - update statistics
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    This can be called after loading has finished but BEFORE ldr_final
 *    to update the statistics for the classes that were involved
 *    in the load.
 */
int
ldr_update_statistics (void)
{
  int err = NO_ERROR;
  CLASS_TABLE *table;
  const char *class_name = NULL;

  for (table = Classes; table != NULL && !err; table = table->next)
    {
      if (table->total_inserts)
	{
	  if (ldr_Current_context->verbose)
	    {
	      class_name = sm_get_ch_name (table->class_);
	      if (class_name == NULL)
		{
		  err = er_errid ();
		  assert (err != NO_ERROR);
		  break;
		}

	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_CLASS_TITLE),
		       class_name);
	      fflush (stdout);
	    }
	  err = sm_update_statistics (table->class_, STATS_WITH_SAMPLING);
	}
    }
  return err;
}

/*
 * ldr_abort - immediately error exit function
 *    return: void
 */
static void
ldr_abort (void)
{
  db_abort_transaction ();
  if (ldr_post_interrupt_handler != NULL)
    {
      (*ldr_post_interrupt_handler) (-1);
    }
  if (ldr_Jmp_buf != NULL)
    {
      longjmp (*ldr_Jmp_buf, 1);
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * print_parser_lineno - print parse line number
 *    return: void
 *    fp(in): FILE *
 */
void
print_parser_lineno (FILE * fp)
{
  if (fp)
    {
      fprintf (fp, "%d\n", loader_yylineno);
    }
  else
    {
      printf ("%d\n", loader_yylineno);
    }
}
#endif

void
ldr_act_set_skipCurrentclass (char *classname, size_t size)
{
  skip_current_class = ldr_is_ignore_class (classname, size);

  if (skip_current_class && ldr_Current_context->validation_only != true)
    {
      print_log_msg (1, "Class %s is ignored.\n", classname);
    }
}

bool
ldr_is_ignore_class (const char *classname, size_t size)
{
  int i;
  char **p;

  if (classname == NULL)
    {
      return false;
    }

  if (IS_OLD_GLO_CLASS (classname))
    {
      return true;
    }

  if (ignore_class_list != NULL)
    {
      for (i = 0, p = ignore_class_list; i < ignore_class_num; i++, p++)
	{
	  if (intl_identifier_ncasecmp (*p, classname, (int) MAX (strlen (*p), size)) == 0)
	    {
	      return true;
	    }
	}
    }

  return false;
}


void
ldr_process_constants (LDR_CONSTANT * cons)
{
  LDR_CONSTANT *c, *save;

  CHECK_SKIP ();

  if (cons != NULL && ldr_Current_context->num_attrs == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDB_NO_CLASS_OR_NO_ATTRIBUTE, 0);
      /* diplay the msg 1 time for each class */
      if (ldr_Current_context->valid)
	{
	  display_error (0);
	}
      CHECK_CONTEXT_VALIDITY (ldr_Current_context, true);
      ldr_abort ();
      return;
    }

  for (c = cons; c; c = save)
    {
      save = c->next;

      switch (c->type)
	{
	case LDR_NULL:
	  (*ldr_act) (ldr_Current_context, NULL, 0, LDR_NULL);
	  break;

	case LDR_INT:
	case LDR_FLOAT:
	case LDR_DOUBLE:
	case LDR_NUMERIC:
	case LDR_DATE:
	case LDR_TIME:
	case LDR_TIMELTZ:
	case LDR_TIMETZ:
	case LDR_TIMESTAMP:
	case LDR_TIMESTAMPLTZ:
	case LDR_TIMESTAMPTZ:
	case LDR_DATETIME:
	case LDR_DATETIMELTZ:
	case LDR_DATETIMETZ:
	case LDR_STR:
	case LDR_NSTR:
	  {
	    LDR_STRING *str = (LDR_STRING *) c->val;

	    (*ldr_act) (ldr_Current_context, str->val, str->size, c->type);
	    FREE_STRING (str);
	  }
	  break;

	case LDR_MONETARY:
	  {
	    LDR_MONETARY_VALUE *mon = (LDR_MONETARY_VALUE *) c->val;
	    LDR_STRING *str = (LDR_STRING *) mon->amount;
	    /* buffer size for monetary : numeric size + grammar currency symbol + string terminator */
	    char full_mon_str[NUM_BUF_SIZE + 3 + 1];
	    char *full_mon_str_p = full_mon_str;
	    /* In Loader grammar always print symbol before value (position of currency symbol is not localized) */
	    char *curr_str = intl_get_money_esc_ISO_symbol (mon->currency_type);
	    unsigned int full_mon_str_len = (strlen (str->val) + strlen (curr_str));

	    if (full_mon_str_len >= sizeof (full_mon_str))
	      {
		full_mon_str_p = (char *) malloc (full_mon_str_len + 1);
	      }

	    strcpy (full_mon_str_p, curr_str);
	    strcat (full_mon_str_p, str->val);

	    (*ldr_act) (ldr_Current_context, full_mon_str_p, strlen (full_mon_str_p), c->type);
	    if (full_mon_str_p != full_mon_str)
	      {
		free_and_init (full_mon_str_p);
	      }
	    FREE_STRING (str);
	    free_and_init (mon);
	  }
	  break;

	case LDR_BSTR:
	case LDR_XSTR:
	case LDR_ELO_INT:
	case LDR_ELO_EXT:
	case LDR_SYS_USER:
	case LDR_SYS_CLASS:
	  {
	    LDR_STRING *str = (LDR_STRING *) c->val;

	    (*ldr_act) (ldr_Current_context, str->val, strlen (str->val), c->type);
	    FREE_STRING (str);
	  }
	  break;

	case LDR_OID:
	case LDR_CLASS_OID:
	  ldr_process_object_ref ((LDR_OBJECT_REF *) c->val, c->type);
	  break;

	case LDR_COLLECTION:
	  (*ldr_act) (ldr_Current_context, "{", 1, LDR_COLLECTION);
	  ldr_process_constants ((LDR_CONSTANT *) c->val);
	  ldr_act_attr (ldr_Current_context, NULL, 0, LDR_COLLECTION);
	  break;

	default:
	  break;
	}

      if (c->need_free)
	{
	  free_and_init (c);
	}
    }
}

static void
ldr_process_object_ref (LDR_OBJECT_REF * ref, int type)
{
  bool ignore_class = false;
  const char *class_name;
  DB_OBJECT *ref_class = NULL;

  if (ref->class_id && ref->class_id->val)
    {
      ldr_act_set_ref_class_id (ldr_Current_context, atoi (ref->class_id->val));
    }
  else
    {
      ldr_act_set_ref_class (ldr_Current_context, ref->class_name->val);
    }

  if (ref->instance_number && ref->instance_number->val)
    {
      ldr_act_set_instance_id (ldr_Current_context, atoi (ref->instance_number->val));
    }
  else
    {
      /* ldr_act_set_instance_id(ldr_Current_context, 0); *//* right?? */
    }

  ref_class = ldr_act_get_ref_class (ldr_Current_context);
  if (ref_class != NULL)
    {
      class_name = db_get_class_name (ref_class);
      ignore_class = ldr_is_ignore_class (class_name, ((class_name) ? strlen (class_name) : 0));
    }

  if (type == LDR_OID)
    {
      (*ldr_act) (ldr_Current_context, ref->instance_number->val,
		  ((ref->instance_number->val == NULL) ? 0 : ref->instance_number->size),
		  (ignore_class) ? LDR_NULL : LDR_OID);
    }
  else
    {
      /* right ?? */
      if (ref->class_name)
	{
	  (*ldr_act) (ldr_Current_context, ref->class_name->val, ref->class_name->size,
		      (ignore_class) ? LDR_NULL : LDR_CLASS_OID);
	}
      else
	{
	  (*ldr_act) (ldr_Current_context, ref->class_id->val, ((ref->class_id->val == NULL) ? 0 : ref->class_id->size),
		      (ignore_class) ? LDR_NULL : LDR_CLASS_OID);
	}
    }

  if (ref->class_id)
    {
      FREE_STRING (ref->class_id);
    }

  if (ref->class_name)
    {
      FREE_STRING (ref->class_name);
    }

  if (ref->instance_number)
    {
      FREE_STRING (ref->instance_number);
    }

  free_and_init (ref);
}

int
ldr_init_class_spec (const char *class_name)
{
  ldr_act_init_context (ldr_Current_context, class_name, strlen (class_name));

  /* 
   * If there is no class or not authorized,
   * Error message is printed and ER_FAILED is returned.
   */
  return ldr_act_add_class_all_attrs (ldr_Current_context, class_name);
}

static int
ldr_act_add_class_all_attrs (LDR_CONTEXT * context, const char *class_name)
{
  int err = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  DB_OBJECT *class_mop;

  RETURN_IF_NOT_VALID_WITH (context, ER_FAILED);

  if (context->validation_only)
    {
      assert (context->cls == NULL);

      class_mop = ldr_find_class (class_name);
      if (class_mop == NULL)
	{
	  display_error_line (0);
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_UNKNOWN_CLASS),
		   class_name);
	  err = er_filter_errid (false);
	  goto error_exit;
	}
    }
  else
    {
      class_mop = context->cls;
    }

  CHECK_ERR (err, au_fetch_class (class_mop, &class_, AU_FETCH_READ, AU_SELECT));

  for (att = class_->ordered_attributes; att != NULL; att = att->order_link)
    {
      ldr_act_add_attr (ldr_Current_context, att->header.name, strlen (att->header.name));
    }

  return NO_ERROR;

error_exit:
  if (err != NO_ERROR)
    {
      CHECK_CONTEXT_VALIDITY (context, true);
      ldr_abort ();
    }

  return ER_FAILED;
}
