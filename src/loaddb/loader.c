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
#include <assert.h>
#if defined (WINDOWS)
#include <io.h>
#else
#include <unistd.h>
#endif /* !WINDOWS */
#include <errno.h>

#include "loader.h"

#include "porting.h"
#include "utility.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "network.h"
#include "db.h"
#include "loader_object_table.h"
#include "work_space.h"
#include "message_catalog.h"
#include "elo.h"
#include "intl_support.h"
#include "language_support.h"
#include "environment_variable.h"
#include "set_object.h"
#include "db_json.hpp"

#include "dbtype.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

extern "C"
{
  extern int loader_yylineno;
}

#define FREE_STRING(s) \
do { \
  if ((s)->need_free_val) free_and_init ((s)->val); \
  if ((s)->need_free_self) free_and_init ((s)); \
} while (0)

// TODO CBRD-21654 temporary define SM_ATTRIBUTE_S, SM_METHOD_S and SM_METHOD_ARGUMENT_S types in order to make compiler happy
typedef int SM_ATTRIBUTE_S;
typedef int SM_METHOD_S;
typedef int SM_METHOD_ARGUMENT_S;

typedef int (*LDR_SETTER) (LDR_CONTEXT *, const char *, int, SM_ATTRIBUTE_S *);
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
  SM_ATTRIBUTE_S *att;		/* Attribute */
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

  SM_METHOD_S *constructor;	/* Method constructor */
  int arg_index;		/* Index where arguments start in */
  /* the attr descriptor array.  */
  int maxarg;			/* Maximum number of arguments */
  SM_METHOD_ARGUMENT_S **args;	/* Pointer to the arguments */
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
static DB_VALUE ldr_json_tmpl;


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
static int ldr_mismatch (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_ignore (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_generic (LDR_CONTEXT * context, DB_VALUE * value);
static int ldr_null_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_null_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_class_attr_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att,
				      DB_VALUE * val);
static void ldr_act_class_attr (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type);
static int ldr_sys_user_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_sys_class_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_int_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_int_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_int_db_bigint (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_int_db_int (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_int_db_short (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_str_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_str_db_char (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_str_db_varchar (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_str_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_bstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_bstr_db_varbit (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_xstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_xstr_db_varbit (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_nstr_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_nstr_db_varnchar (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_numeric_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_numeric_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_double_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_float_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_real_db_generic (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_real_db_float (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_real_db_double (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_date_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_date_db_date (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_time_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_time_db_time (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_timetz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timeltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timetz_db_timetz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_timeltz_db_timeltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_timestamp_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timestamp_db_timestamp (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_timestamptz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timestampltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_timestamptz_db_timestamptz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_timestampltz_db_timestampltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_datetime_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_datetime_db_datetime (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_datetimetz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_datetimeltz_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_datetimetz_db_datetimetz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_datetimeltz_db_datetimeltz (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static void ldr_date_time_conversion_error (const char *token, DB_TYPE type);
static int ldr_check_date_time_conversion (const char *str, LDR_TYPE type);
static int ldr_elo_int_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_elo_int_db_elo (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_elo_ext_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_elo_ext_db_elo (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_mop_tempoid_maps_init (void);
static void ldr_mop_tempoid_maps_final (void);
static int ldr_add_mop_tempoid_map (MOP mop, CLASS_TABLE * table, int id);
static int ldr_assign_all_perm_oids (void);
static int find_instance (LDR_CONTEXT * context, DB_OBJECT * class_, OID * oid, int id);
static int ldr_class_oid_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_class_oid_db_object (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_oid_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_oid_db_object (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_monetary_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_monetary_db_monetary (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
static int ldr_collection_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_collection_db_collection (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);
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
static int ldr_json_elem (LDR_CONTEXT * context, const char *str, int len, DB_VALUE * val);
static int ldr_json_db_json (LDR_CONTEXT * context, const char *str, int len, SM_ATTRIBUTE_S * att);

/* default action */
void (*ldr_act) (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type) = ldr_act_attr;


void
ldr_act_set_id (LDR_CONTEXT * context, int id)
{
}

void
ldr_increment_err_total (LDR_CONTEXT * context)
{
}

void
ldr_act_restrict_attributes (LDR_CONTEXT * context, LDR_ATTRIBUTE_TYPE type)
{
}

void
ldr_increment_fails ()
{
}

void
ldr_act_finish (LDR_CONTEXT * context, int parse_error)
{
}

void
ldr_process_constants (LDR_CONSTANT * cons)
{
}

int
ldr_act_set_constructor (LDR_CONTEXT * context, const char *name)
{
  return NO_ERROR;
}

int
ldr_act_check_missing_non_null_attrs (LDR_CONTEXT * context)
{
  return NO_ERROR;
}

void
ldr_act_finish_line (LDR_CONTEXT * context)
{
}

void
ldr_act_add_attr (LDR_CONTEXT * context, const char *attr_name, int len)
{
}

void
ldr_act_init_context (LDR_CONTEXT * context, const char *class_name, int len)
{
}

void
ldr_load_failed_error ()
{
}

void
ldr_act_start_id (LDR_CONTEXT * context, char *name)
{
}

void
ldr_act_attr (LDR_CONTEXT * context, const char *str, int len, LDR_TYPE type)
{
}

void
ldr_act_start_instance (LDR_CONTEXT * context, int id, LDR_CONSTANT * cons)
{
}

void
ldr_act_set_skip_current_class (char *classname, size_t size)
{
}

int
ldr_act_add_argument (LDR_CONTEXT * context, const char *name)
{
}

void
ldr_register_post_commit_handler (LDR_POST_COMMIT_HANDLER handler, void *arg)
{
}

int
ldr_init (bool verbose)
{
  return NO_ERROR;
}

void
ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER handler, void *ldr_jmp_buf)
{
}

int
ldr_init_class_spec (const char *class_name)
{
  return NO_ERROR;
}

int
ldr_final (void)
{
  return NO_ERROR;
}

void
ldr_interrupt_has_occurred (int type)
{
}

int
ldr_update_statistics (void)
{
  return NO_ERROR;
}

int
ldr_start (int periodic_commit)
{
  return NO_ERROR;
}

void
ldr_stats (int *errors, int *objects, int *defaults, int *lastcommit, int *fails)
{
}
