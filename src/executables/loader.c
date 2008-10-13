/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * ldr.c
 *
 * Note :
 *      Database loader (Optimized version)
 *      Updated using design from fast loaddb prototype
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

#include "utility.h"
#include "dbi.h"
#include "memory_manager_2.h"
#include "system_parameter.h"
#include "network.h"
#include "authenticate.h"
#include "schema_manager_3.h"
#include "object_accessor.h"
#include "memory_manager_4.h"

#include "db.h"
#include "loader_object_table.h"
#include "load_object.h"
#include "zzpref.h"
#include "loader.h"
#include "work_space.h"
#include "message_catalog.h"
#include "locator_cl.h"
#include "elo_class.h"
#include "intl.h"
#include "language_support.h"
#include "environment_variable.h"
#include "set_object_1.h"
#include "trigger_manager.h"
#include "execute_schema_8.h"
#include "transaction_cl.h"
#include "locator_cl.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/*
 * Defined somewhere in the antlr internals.   May need to
 * recognize the antlr prefix if we can ever be used in multiple-antlr
 * situations.
 */
extern int zzline;

#define LDR_MAX_ARGS 32

#  define LDR_INCREMENT_ERR_TOTAL(context) ldr_increment_err_total(context)
#  define LDR_INCREMENT_ERR_COUNT(context, i) \
                                           ldr_increment_err_count(context, i)
#  define LDR_CLEAR_ERR_TOTAL(context)     ldr_clear_err_total(context)
#  define LDR_CLEAR_ERR_COUNT(context)     ldr_clear_err_count(context)

/* filter out ignorable errid */
#define FILTER_OUT_ERR_INTERNAL(err, expr)                              \
  ( err = ((expr) == NO_ERROR ? NO_ERROR : er_filter_errid(false)) )

#define CHECK_ERR(err, expr)                                            \
  do {                                                                  \
    if (FILTER_OUT_ERR_INTERNAL(err, expr) != NO_ERROR) {               \
      display_error(0);                                                 \
      goto error_exit;                                                  \
    }                                                                   \
  } while (0)

/*
 * CHECK_PARSE_ERR is used by the element setters to output more information
 * about the source token and destination attribute being processed, if
 * an error occurs.
 */
#define CHECK_PARSE_ERR(err, expr, cont, type, str)                     \
  do {                                                                  \
    if (FILTER_OUT_ERR_INTERNAL(err, expr) != NO_ERROR) {               \
      display_error(0);                                                 \
      parse_error(cont, type, str);                                     \
      goto error_exit;                                                  \
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

#define  CHECK_SKIP()                      \
         do {                              \
           if (skipCurrentclass == true) { \
             return;                       \
           }                               \
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

  char *parser_str;		/* used as a holder to hold the parser
				 * strings when parsing method arguments.
				 */
  int parser_str_len;		/* Length of parser token string */
  int parser_buf_len;		/* Length of parser token buffer */
  LDR_TYPE parser_type;		/* Used when parsing method arguments, to
				 * store
				 */
  /* the type information. */

  DB_OBJECT *ref_class;		/* Class referenced by object reference  */
  int instance_id;		/* Instance id of instance referenced by
				 * ref_class object ref
				 */
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

  DB_OBJECT *cls;		/* Current class                      */
  DB_OBJECT *partition_of;	/* Current class's partition indo.    */
  PARTITION_SELECT_INFO *psi;	/* partition selection information    */
  int key_attr_idx;		/* partition key attr index(of attrs) */

  char *class_name;		/* Class name of current class        */

  DB_OBJECT *obj;		/* Instance of class                  */
  MOBJ mobj;			/* Memory object of instance          */
  int obj_pin;			/* pins returned when pinning the     */
  int class_pin;		/* current class                      */

  LDR_ATTDESC *attrs;		/* array of attribute descriptors for */
  /* the current class.                 */
  int num_attrs;		/* Number of attributes for class     */
  int next_attr;		/* Index of current attribute         */

  unsigned int instance_started:1;	/* Instance stared flag             */
  bool valid;			/* State of the loader.               */
  bool verbose;			/* Verbosity flag                     */
  int periodic_commit;		/* instances prior to committing      */

  int err_count;		/* Current error count for instance   */
  int err_total;		/* Total error counter                */

  int inst_count;		/* Instance count for this class      */
  int inst_total;		/* Total instance count               */
  int inst_num;			/* Instance id of current instance    */

  int flush_total;		/* Total number of flushes performed  */
  /* for the current class              */
  int flush_interval;		/* The number of instances before a   */
  /* flush is performed                 */

  CLASS_TABLE *table;		/* Table of instances currently       */
  /* accumlated for the class           */

  bool validation_only;		/* Syntax checking only flag          */

  INTL_LANG lang_id;		/* Current language                   */

  DB_COLLECTION *collection;	/* Pointer to hang collection from    */
  TP_DOMAIN *set_domain;	/* Set domain of current set if       */
  /* applicable.                        */

  SM_METHOD *constructor;	/* Method constructor                 */
  int arg_index;		/* Index where arguments start in     */
  /* the attr descriptor array.         */
  int maxarg;			/* Maximum number of arguments        */
  SM_METHOD_ARGUMENT **args;	/* Pointer to the arguments           */
  int arg_count;		/* argument counter                   */

  DB_OBJECT *id_class;		/* Holds class ptr when processing ids */

  LDR_ATTRIBUTE_TYPE attribute_type;	/* type of attribute if class       */
  /* attribute, shared, default         */

  int status_count;		/* Count used to indicate number of   */
  /* instances committed for internal   */
  /* debugging use only                 */
  int status_counter;		/* Internal debug instance counter    */
  int commit_counter;		/* periodic commit counter            */
  int default_count;		/* the number of instances with       */
  /* values                             */
};

char **ignoreClasslist = NULL;
int ignoreClassnum = 0;
bool skipCurrentclass = false;

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
 *    Global array used to hold the class read, instance write lock
 *    set up at initialization.  This will be used by ldr_find_class()
 *    This is a temporary solution to fix the client/server deadlock problem.
 *    This needs to be done for all classes, perhaps attached to the otable.
 */
#define LDR_LOCKHINT_COUNT 1
static LOCK ldr_Hint_locks[LDR_LOCKHINT_COUNT];
static const char *ldr_Hint_classnames[LDR_LOCKHINT_COUNT];
static int ldr_Hint_subclasses[LDR_LOCKHINT_COUNT];

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
static int Last_committed_zzline = 0;

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

static DB_VALUE ldr_int_tmpl;
static DB_VALUE ldr_char_tmpl;
static DB_VALUE ldr_varchar_tmpl;
static DB_VALUE ldr_float_tmpl;
static DB_VALUE ldr_double_tmpl;
static DB_VALUE ldr_date_tmpl;
static DB_VALUE ldr_time_tmpl;
static DB_VALUE ldr_timestamp_tmpl;
static DB_VALUE ldr_elo_tmpl;
static DB_VALUE ldr_bit_tmpl;

/* default for 32 bit signed integers, i.e., 2147483647 (0x7FFFFFFF) */
#define MAX_DIGITS_FOR_INT      9
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
static int select_set_domain (LDR_CONTEXT * context, TP_DOMAIN * domain,
			      TP_DOMAIN ** set_domain_ptr);
static int check_object_domain (LDR_CONTEXT * context, DB_OBJECT * class_,
				DB_OBJECT ** actual_class);
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
static void parse_error (LDR_CONTEXT * context, DB_TYPE token_type,
			 const char *token);
static int clist_init (void);
static void clist_final (void);
static int is_internal_class (DB_OBJECT * class_);
static void ldr_act_elem (LDR_CONTEXT * context, const char *str, int len,
			  LDR_TYPE type);
static void ldr_act_meth (LDR_CONTEXT * context, const char *str, int len,
			  LDR_TYPE type);
static int ldr_mismatch (LDR_CONTEXT * context, const char *str, int len,
			 SM_ATTRIBUTE * att);
static int ldr_ignore (LDR_CONTEXT * context, const char *str, int len,
		       SM_ATTRIBUTE * att);
static int ldr_generic (LDR_CONTEXT * context, DB_VALUE * value);
static int ldr_null_elem (LDR_CONTEXT * context, const char *str, int len,
			  DB_VALUE * val);
static int ldr_null_db_generic (LDR_CONTEXT * context, const char *str,
				int len, SM_ATTRIBUTE * att);
static int ldr_class_attr_db_generic (LDR_CONTEXT * context, const char *str,
				      int len, SM_ATTRIBUTE * att,
				      DB_VALUE * val);
static void ldr_act_class_attr (LDR_CONTEXT * context, const char *str,
				int len, LDR_TYPE type);
static int ldr_sys_user_db_generic (LDR_CONTEXT * context, const char *str,
				    int len, SM_ATTRIBUTE * att);
static int ldr_sys_class_db_generic (LDR_CONTEXT * context, const char *str,
				     int len, SM_ATTRIBUTE * att);
static int ldr_int_elem (LDR_CONTEXT * context, const char *str, int len,
			 DB_VALUE * val);
static int ldr_int_db_generic (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_int_db_int (LDR_CONTEXT * context, const char *str, int len,
			   SM_ATTRIBUTE * att);
static int ldr_int_db_short (LDR_CONTEXT * context, const char *str, int len,
			     SM_ATTRIBUTE * att);
static int ldr_str_elem (LDR_CONTEXT * context, const char *str, int len,
			 DB_VALUE * val);
static int ldr_str_db_char (LDR_CONTEXT * context, const char *str, int len,
			    SM_ATTRIBUTE * att);
static int ldr_str_db_varchar (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_str_db_generic (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_bstr_elem (LDR_CONTEXT * context, const char *str, int len,
			  DB_VALUE * val);
static int ldr_bstr_db_varbit (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_xstr_elem (LDR_CONTEXT * context, const char *str, int len,
			  DB_VALUE * val);
static int ldr_xstr_db_varbit (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_nstr_elem (LDR_CONTEXT * context, const char *str, int len,
			  DB_VALUE * val);
static int ldr_nstr_db_varnchar (LDR_CONTEXT * context, const char *str,
				 int len, SM_ATTRIBUTE * att);
static int ldr_numeric_elem (LDR_CONTEXT * context, const char *str, int len,
			     DB_VALUE * val);
static int ldr_numeric_db_generic (LDR_CONTEXT * context, const char *str,
				   int len, SM_ATTRIBUTE * att);
static int ldr_double_elem (LDR_CONTEXT * context, const char *str, int len,
			    DB_VALUE * val);
static int ldr_float_elem (LDR_CONTEXT * context, const char *str, int len,
			   DB_VALUE * val);
static int ldr_real_db_generic (LDR_CONTEXT * context, const char *str,
				int len, SM_ATTRIBUTE * att);
static int ldr_real_db_float (LDR_CONTEXT * context, const char *str, int len,
			      SM_ATTRIBUTE * att);
static int ldr_real_db_double (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_date_elem (LDR_CONTEXT * context, const char *str, int len,
			  DB_VALUE * val);
static int ldr_date_db_date (LDR_CONTEXT * context, const char *str, int len,
			     SM_ATTRIBUTE * att);
static int ldr_time_elem (LDR_CONTEXT * context, const char *str, int len,
			  DB_VALUE * val);
static int ldr_time_db_time (LDR_CONTEXT * context, const char *str, int len,
			     SM_ATTRIBUTE * att);
static int ldr_timestamp_elem (LDR_CONTEXT * context, const char *str,
			       int len, DB_VALUE * val);
static int ldr_timestamp_db_timestamp (LDR_CONTEXT * context, const char *str,
				       int len, SM_ATTRIBUTE * att);
static void ldr_date_time_conversion_error (const char *token, DB_TYPE type);
static int ldr_check_date_time_conversion (const char *str, LDR_TYPE type);
static int ldr_elo_int_elem (LDR_CONTEXT * context, const char *str, int len,
			     DB_VALUE * val);
static int ldr_elo_int_db_elo (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_elo_ext_elem (LDR_CONTEXT * context, const char *str, int len,
			     DB_VALUE * val);
static int ldr_elo_ext_db_elo (LDR_CONTEXT * context, const char *str,
			       int len, SM_ATTRIBUTE * att);
static int ldr_mop_tempoid_maps_init (void);
static void ldr_mop_tempoid_maps_final (void);
static int ldr_add_mop_tempoid_map (MOP mop, CLASS_TABLE * table, int id);
static int ldr_assign_all_perm_oids (void);
static int find_instance (LDR_CONTEXT * context, DB_OBJECT * class_,
			  OID * oid, int id);
static int ldr_class_oid_elem (LDR_CONTEXT * context, const char *str,
			       int len, DB_VALUE * val);
static int ldr_class_oid_db_object (LDR_CONTEXT * context, const char *str,
				    int len, SM_ATTRIBUTE * att);
static int ldr_oid_elem (LDR_CONTEXT * context, const char *str, int len,
			 DB_VALUE * val);
static int ldr_oid_db_object (LDR_CONTEXT * context, const char *str, int len,
			      SM_ATTRIBUTE * att);
static int ldr_monetary_elem (LDR_CONTEXT * context, const char *str, int len,
			      DB_VALUE * val);
static int ldr_monetary_db_monetary (LDR_CONTEXT * context, const char *str,
				     int len, SM_ATTRIBUTE * att);
static int ldr_collection_elem (LDR_CONTEXT * context, const char *str,
				int len, DB_VALUE * val);
static int ldr_collection_db_collection (LDR_CONTEXT * context,
					 const char *str, int len,
					 SM_ATTRIBUTE * att);
static int ldr_reset_context (LDR_CONTEXT * context);
static void ldr_flush (LDR_CONTEXT * context);
static int check_commit (LDR_CONTEXT * context);
static void ldr_restore_pin_and_drop_obj (LDR_CONTEXT * context,
					  bool drop_obj);
static int ldr_finish_context (LDR_CONTEXT * context);
static int ldr_partition_info (LDR_CONTEXT * context);
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
////static function

/* default action */
void (*ldr_act) (LDR_CONTEXT * context, const char *str, int len,
		 LDR_TYPE type) = ldr_act_attr;

/*
 * ldr_increment_err_total - increment err_total count of the given context
 *    return: void
 *    context(out): context
 */
void
ldr_increment_err_total (LDR_CONTEXT * context)
{
  if (context)
    context->err_total += 1;
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
    context->err_count += i;
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
    context->err_count = 0;
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
    context->err_total = 0;
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
    if (context->cls)
      name = db_get_class_name (context->cls);

  return (name);
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
	    name = context->attrs[context->next_attr].att->header.name;
	  else
	    /* haven't processed an attribute yet */
	    name = "";
	}
      else
	/* should return some kind of string representation for
	   the current method argument */
	name = "";
    }
  return (name);
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
select_set_domain (LDR_CONTEXT * context,
		   TP_DOMAIN * domain, TP_DOMAIN ** set_domain_ptr)
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
      if (TP_IS_SET_TYPE (d->type->id))
	/* pick the first one */
	best = d;
    }

  if (best == NULL)
    {
      err = ER_LDR_DOMAIN_MISMATCH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 4,
	      ldr_attr_name (context), ldr_class_name (context),
	      domain->type->name, db_get_type_name (DB_TYPE_SET));
    }
  else
    {
      if (set_domain_ptr != NULL)
	*set_domain_ptr = best;
    }
  return err;
}


/*
 * check_object_domain - checks the type of an incomming value against the
 * target domain.
 *    return: NO_ERROR if successful, error code otherwise
 *    context(in): context
 *    class(in): class of incomming object reference
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
check_object_domain (LDR_CONTEXT * context,
		     DB_OBJECT * class_, DB_OBJECT ** actual_class)
{
  int err = NO_ERROR;
  TP_DOMAIN *domain, *best, *d;

  GET_DOMAIN (context, domain);

  if (class_ == NULL)
    {
      /* its an object but no domain was specified, see if we can unambiguously
         select one. */
      if (domain == NULL)
	{
	  err = ER_LDR_AMBIGUOUS_DOMAIN;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2,
		  ldr_attr_name (context), ldr_class_name (context));
	}
      else
	{
	  for (d = domain; d != NULL; d = d->next)
	    {
	      if (d->type == tp_Type_object)
		{
		  if (class_ == NULL && d->class_mop != NULL)
		    class_ = d->class_mop;
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2,
		      ldr_attr_name (context), ldr_class_name (context));
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3,
		      ldr_attr_name (context),
		      ldr_class_name (context), db_get_class_name (class_));
	    }
	}
    }

  if (actual_class != NULL)
    *actual_class = class_;

  return err;
}


/*
 * check_class_domain - checks the domain for an incomming reference to an
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
	if (d->type == tp_Type_object && d->class_mop == NULL)
	  goto error_exit;	/* we found it */

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
      Id_map = NULL;
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
      if ((Id_map =
	   (DB_OBJECT **) realloc (Id_map,
				   sizeof (DB_OBJECT *) * newsize)) == NULL)
	{
	  /* Prevent leakage if we get a memory problem. */
	  if (id_map_old)
	    free_and_init (id_map_old);
	  err = ER_LDR_MEMORY_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	}
      else
	{
	  for (i = Id_map_size; i < newsize; i++)
	    Id_map[i] = NULL;

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

  if (!(err = idmap_grow (id + 1)))
    Id_map[id] = class_;

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
  if (!classname)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      CHECK_ERR (err, ER_GENERIC_ERROR);
    }

  sm_downcase_name (classname, realname, SM_MAX_IDENTIFIER_LENGTH);
  ldr_Hint_classnames[0] = realname;

  find = locator_lockhint_classes (1, ldr_Hint_classnames, ldr_Hint_locks,
				   ldr_Hint_subclasses, 1);

  if (find == LC_CLASSNAME_EXIST)
    class_ = db_find_class (classname);

  ldr_Hint_classnames[0] = NULL;

error_exit:
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
    class_ = Id_map[id];

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
      if ((class_ = ldr_find_class (name)) != NULL)
	context->id_class = class_;
      else
	{
          is_ignore_class =
            ldr_is_ignore_class (name, strlen(name));
          if (!is_ignore_class) {
	    display_error (0);
	    CHECK_CONTEXT_VALIDITY (context, true);
          }
          else if (er_errid() == ER_LC_UNKNOWN_CLASSNAME)
          {
            er_clear();
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
  context->partition_of = NULL;
  context->psi = NULL;
  context->class_name = NULL;

  context->obj = NULL;
  context->mobj = NULL;
  context->obj_pin = 0;
  context->class_pin = 0;

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

  context->flush_interval = PRM_LOADDB_FLUSH_INTERVAL;

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
	    free_and_init (context->attrs[i].parser_str);
	  context->attrs[i].parser_str = NULL;
	  if (context->attrs[i].attdesc)
	    db_free_attribute_descriptor (context->attrs[i].attdesc);
	  context->attrs[i].attdesc = NULL;
	}
      free_and_init (context->attrs);
    }

  if (context->args)
    free_and_init (context->args);

  if (context->class_name)
    free_and_init (context->class_name);

  if (context->psi)
    do_clear_partition_select (context->psi);

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
 *    and we need to reset the interal loader state.
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LINE),
	   zzline + adjust);
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNKNOWN_ATT_CLASS),
	   ldr_attr_name (context), ldr_class_name (context));
}


/*
 * parse_error - parse error handler
 *    return: void
 *    context(in): context
 *    token_type(in): incomming token type
 *    token(in): token string
 * Note:
 *    Called when we have some sort of parsing problem with an incomming
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_PARSE_ERROR), token,
	   db_get_type_name (token_type), ldr_attr_name (context),
	   ldr_class_name (context));
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
	return er_errid ();
    }
  class_ = db_find_class (AU_USER_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	return er_errid ();
    }
  class_ = db_find_class (AU_PASSWORD_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	return er_errid ();
    }
  class_ = db_find_class (AU_AUTH_CLASS_NAME);
  if (class_ != NULL)
    {
      if (ml_ext_add (&internal_classes, class_, NULL))
	return er_errid ();
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
  char *mem;
  DB_VALUE curval;
  MOP retobj;

  CHECK_SKIP();

  /* we have reached an invalid state, ignore the tuples */

  RETURN_IF_NOT_VALID (context);

  if (context->next_attr >= context->num_attrs)
    {
      context->valid = false;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_VALUE_OVERFLOW, 1,
	      context->num_attrs);
      CHECK_ERR (err, ER_LDR_VALUE_OVERFLOW);
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
	case LDR_DATE:
	case LDR_TIMESTAMP:
	  CHECK_ERR (err, ldr_check_date_time_conversion (str, type));
	  break;
	default:
	  break;
	}
    }
  else
    {
      attdesc = &context->attrs[context->next_attr];
      CHECK_ERR (err,
		 (*(attdesc->setter[type])) (context, str, len,
					     attdesc->att));
      if (context->partition_of)
	{
	  if (context->psi &&
	      context->key_attr_idx >= 0 &&
	      context->key_attr_idx == context->next_attr)
	    {
	      if (type == LDR_NULL)
		{
		  DB_MAKE_NULL (&curval);
		}
	      else
		{
		  mem = context->mobj + attdesc->att->offset;
		  CHECK_ERR (err,
			     PRIM_GETMEM_NOCOPY (attdesc->att->domain->type,
						 attdesc->att->domain, mem,
						 &curval));
		}
	      CHECK_ERR (err,
			 do_select_partition (context->psi, &curval,
					      &retobj));
	      if (ws_mop_compare (context->cls, retobj) != 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_PARTITION_WORK_FAILED, 0);
		  CHECK_ERR (err, ER_PARTITION_WORK_FAILED);
		}
	    }
	}
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
	case LDR_DATE:
	case LDR_TIMESTAMP:
	  CHECK_ERR (err, ldr_check_date_time_conversion (str, type));
	  break;
	default:
	  break;
	}
    }
  else
    {
      CHECK_ERR (err,
		 (*(elem_converter[type])) (context, str, len, &tempval));
      if ((err =
	   set_add_element (context->collection,
			    &tempval)) == ER_SET_DOMAIN_CONFLICT)
	{
	  display_error_line (0);
	  fprintf (stderr,
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_SET_DOMAIN_ERROR),
		   ldr_attr_name (context), ldr_class_name (context),
		   db_get_type_name (db_value_type (&tempval)));
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
  LDR_ATTDESC *attdesc;

  /* we have reached an invalid state, ignore the tuples */

  RETURN_IF_NOT_VALID (context);

  if ((context->next_attr) >= (context->num_attrs + context->arg_count))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_UNEXPECTED_ARGUMENT, 1,
	      context->arg_count);
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
      CHECK_PTR (err, attdesc->parser_str = (char *) (malloc (len + 1)));
      attdesc->parser_buf_len = len;
    }
  else if (len > attdesc->parser_buf_len)
    {
      char *parser_str_old;
      parser_str_old = attdesc->parser_str;
      /* Prevent leak from realloc call failure */
      if ((attdesc->parser_str = (char *) (realloc (attdesc->parser_str,
						    len + 1))) == NULL)
	/* Prevent leakage if realloc fails */
	free_and_init (parser_str_old);
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
ldr_mismatch (LDR_CONTEXT * context,
	      const char *str, int len, SM_ATTRIBUTE * att)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	  att->header.name);
  display_error (0);
  return ER_OBJ_DOMAIN_CONFLICT;
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
ldr_ignore (LDR_CONTEXT * context,
	    const char *str, int len, SM_ATTRIBUTE * att)
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
  /*
   * This will, unfortunately, do the au_fetch_instance dance again.  It
   * would be nice to bypass that, but we don't have a convenient entry
   * point in obj.c, and it's not worth providing one until someone can
   * prove that this is a performance-limiting case.
   */
  CHECK_ERR (err, obj_desc_set (context->obj,
				context->attrs[context->next_attr].attdesc,
				value));
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
ldr_null_elem (LDR_CONTEXT * context,
	       const char *str, int len, DB_VALUE * val)
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
ldr_null_db_generic (LDR_CONTEXT * context,
		     const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  char *mem;

  if (att->flags & SM_ATTFLAG_NON_NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL,
	      1, att->header.name);
      CHECK_ERR (err, ER_OBJ_ATTRIBUTE_CANT_BE_NULL);
    }
  else
    {
      mem = context->mobj + att->offset;
      CHECK_ERR (err,
		 PRIM_SETMEM (att->domain->type, att->domain, mem, NULL));
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
ldr_class_attr_db_generic (LDR_CONTEXT * context,
			   const char *str,
			   int len, SM_ATTRIBUTE * att, DB_VALUE * val)
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
      CHECK_ERR (err,
		 db_change_default (context->cls, att->header.name, val));
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
ldr_act_class_attr (LDR_CONTEXT * context,
		    const char *str, int len, LDR_TYPE type)
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_VALUE_OVERFLOW, 1,
	      context->num_attrs);
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
      CHECK_ERR (err, ldr_collection_db_collection (context, str, len,
						    context->attrs[context->
								   next_attr].
						    att));
    }
  else
    {
      CHECK_ERR (err,
		 (*(elem_converter[type])) (context, str, len, &src_val));
      GET_DOMAIN (context, domain);
      CHECK_ERR (err, db_value_domain_init (&dest_val, domain->type->id,
					    domain->precision,
					    domain->scale));

      val = &dest_val;
      /* tp_value_cast does not handle DB_TYPE_OID coersions, simply use the
       * value returned by the elem converter.
       */
      if (type == LDR_OID || type == LDR_CLASS_OID)
	{
	  if (domain->type->id == DB_TYPE_OBJECT)
	    {
	      val = &src_val;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_DOMAIN_CONFLICT, 1,
		      context->attrs[context->next_attr].att->header.name);
	      CHECK_ERR (err, ER_OBJ_DOMAIN_CONFLICT);
	    }
	}
      else if (tp_value_cast (&src_val, &dest_val, domain, false))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
		  context->attrs[context->next_attr].att->header.name);
	  CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT,
			   context, domain->type->id, str);
	}
      CHECK_ERR (err, ldr_class_attr_db_generic (context, str, len,
						 context->attrs[context->
								next_attr].
						 att, val));
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
ldr_sys_user_db_generic (LDR_CONTEXT * context,
			 const char *str, int len, SM_ATTRIBUTE * att)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNAUTHORIZED_CLASS), "db_user");
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
ldr_sys_class_db_generic (LDR_CONTEXT * context,
			  const char *str, int len, SM_ATTRIBUTE * att)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNAUTHORIZED_CLASS),
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
  /*
   * Watch out for really long digit strings that really are being
   * assigned into a DB_TYPE_NUMERIC attribute; they can hold more than a
   * standard integer can, and calling atol() on that string will lose
   * data.
   * Is there some better way to test for this condition?
   */
  if (len > MAX_DIGITS_FOR_INT)
    {
      CHECK_PARSE_ERR (err,
		       db_value_domain_init (val, DB_TYPE_NUMERIC, len, 0),
		       context, DB_TYPE_INTEGER, str);
      CHECK_PARSE_ERR (err,
		       db_value_put (val, DB_TYPE_C_CHAR, (char *) str, len),
		       context, DB_TYPE_INTEGER, str);
    }
  else
    {
      val->domain = ldr_int_tmpl.domain;
      val->data.i = atol (str);
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
ldr_int_db_generic (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_int_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

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
ldr_int_db_int (LDR_CONTEXT * context,
		const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  DB_VALUE val;
  char *str_ptr;

  val.domain = ldr_int_tmpl.domain;

  /* Let try take the fastest path here, if we know that number we are
   * getting fits into a long, use strtol, else we need to convert it
   * to a double and coerce it, checking for overflow.
   * Note if integers with leading zeros are entered this can take the
   * slower route.
   */
  if (len > MAX_DIGITS_FOR_INT)
    {
      double d;
      d = strtod (str, &str_ptr);

      if (str_ptr == str || OR_CHECK_INT_OVERFLOW (d))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		  db_get_type_name (DB_TYPE_INTEGER));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_INTEGER,
			   str);
	}
      else
	val.data.i = ROUND (d);
    }
  else
    {
      val.data.i = atol (str);
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
ldr_int_db_short (LDR_CONTEXT * context,
		  const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int err;
  DB_VALUE val;
  char *str_ptr;

  val.domain = ldr_int_tmpl.domain;

  /* Let try take the fastest path here, if we know that number we are
   * getting fits into a long, use strtol, else we need to convert it
   * to a double and coerce it, checking for overflow.
   * Note if integers with leading zeros are entered this can take the
   * slower route.
   */
  if (len > MAX_DIGITS_FOR_SHORT)
    {
      double d;
      d = strtod (str, &str_ptr);

      if (str_ptr == str || OR_CHECK_SHORT_OVERFLOW (d))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		  db_get_type_name (DB_TYPE_SHORT));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_SHORT,
			   str);
	}
      else
	val.data.sh = ROUND (d);
    }
  else
    {
      val.data.sh = atoi (str);
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
ldr_str_db_char (LDR_CONTEXT * context,
		 const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int precision;
  int err;
  DB_VALUE val;

  precision = att->domain->precision;

  if (len > precision)
    {
      /*
       * May be a violation, but first we have to check for trailing pad
       * characters that might allow us to successfully truncate the
       * thing.
       */
      int safe;
      const char *p;
      for (p = &str[precision], safe = 1; p < &str[len]; p++)
	{
	  if (*p != ' ')
	    {
	      safe = 0;
	      break;
	    }
	}
      if (safe)
	len = precision;
      else
	{
	  /*
	   * It's a genuine violation; raise an error.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		  db_get_type_name (DB_TYPE_CHAR));
	  CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_CHAR,
			   str);
	}
    }

  val.domain = ldr_char_tmpl.domain;
  val.domain.char_info.length = len;
  val.data.ch.info.style = MEDIUM_STRING;
  val.data.ch.medium.size = len;
  val.data.ch.medium.buf = (char *) str;

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
ldr_str_db_varchar (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
{
  char *mem;
  int precision;
  int err;
  DB_VALUE val;

  precision = att->domain->precision;

  if (len > precision)
    {
      /*
       * May be a violation, but first we have to check for trailing pad
       * characters that might allow us to successfully truncate the
       * thing.
       */
      int safe;
      const char *p;
      for (p = &str[precision], safe = 1; p < &str[len]; p++)
	{
	  if (*p != ' ')
	    {
	      safe = 0;
	      break;
	    }
	}
      if (safe)
	len = precision;
      else
	{
	  /*
	   * It's a genuine violation; raise an error.
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  CHECK_ERR (err, ER_GENERIC_ERROR);
	}
    }

  val.domain = ldr_varchar_tmpl.domain;
  val.domain.char_info.length = len;
  val.data.ch.medium.size = len;
  val.data.ch.medium.buf = (char *) str;
  val.data.ch.info.style = MEDIUM_STRING;

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
ldr_str_db_generic (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_bstr_elem (LDR_CONTEXT * context,
	       const char *str, int len, DB_VALUE * val)
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
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT,
		       str);
    }

  DB_MAKE_VARBIT (&temp, TP_FLOATING_PRECISION_VALUE, bstring, len);
  temp.need_clear = true;

  GET_DOMAIN (context, domain);

  if (domain == NULL)
    {
      CHECK_PARSE_ERR (err, db_value_domain_init (val, DB_TYPE_BIT,
						  DB_DEFAULT_PRECISION,
						  DB_DEFAULT_SCALE),
		       context, DB_TYPE_BIT, str);
    }
  else
    {
      CHECK_PARSE_ERR (err, db_value_domain_init (val,
						  domain->type->id,
						  domain->precision,
						  domain->scale),
		       context, DB_TYPE_BIT, str);
    }
  domain_ptr = tp_domain_resolve_value (val, &temp_domain);
  if (tp_value_cast (&temp, val, domain_ptr, false))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	      context->attrs[context->next_attr].att->header.name);
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT,
		       str);
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
ldr_bstr_db_varbit (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_xstr_elem (LDR_CONTEXT * context,
	       const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;
  int dest_size;
  char *bstring;
  TP_DOMAIN *domain;
  DB_VALUE temp;
  TP_DOMAIN *domain_ptr, temp_domain;

  dest_size = (len + 1) / 2;

  CHECK_PTR (err, bstring = db_private_alloc (NULL, dest_size + 1));

  if (qstr_hex_to_bin (bstring, dest_size, (char *) str, len) != len)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	      ldr_attr_name (context));
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT,
		       str);
    }

  DB_MAKE_VARBIT (&temp, TP_FLOATING_PRECISION_VALUE, bstring, len * 4);
  temp.need_clear = true;

  GET_DOMAIN (context, domain);

  if (domain == NULL)
    {
      db_value_domain_init (val, DB_TYPE_BIT, DB_DEFAULT_PRECISION,
			    DB_DEFAULT_SCALE);
    }
  else
    {
      CHECK_PARSE_ERR (err, db_value_domain_init (val,
						  domain->type->id,
						  domain->precision,
						  domain->scale),
		       context, DB_TYPE_BIT, str);
    }
  domain_ptr = tp_domain_resolve_value (val, &temp_domain);
  if (tp_value_cast (&temp, val, domain_ptr, false))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1,
	      context->attrs[context->next_attr].att->header.name);
      CHECK_PARSE_ERR (err, ER_OBJ_DOMAIN_CONFLICT, context, DB_TYPE_BIT,
		       str);
    }

error_exit:
  /* cleanup */
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
ldr_xstr_db_varbit (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_nstr_elem (LDR_CONTEXT * context,
	       const char *str, int len, DB_VALUE * val)
{

  DB_MAKE_VARNCHAR (val, TP_FLOATING_PRECISION_VALUE, str, len);
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
ldr_nstr_db_varnchar (LDR_CONTEXT * context,
		      const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_numeric_elem (LDR_CONTEXT * context,
		  const char *str, int len, DB_VALUE * val)
{
  int precision, scale;
  int err = NO_ERROR;

  precision = len - 1 - (str[0] == '+' || str[0] == '-' || str[0] == '.');
  scale = len - strcspn (str, ".") - 1;

  CHECK_PARSE_ERR (err, db_value_domain_init (val, DB_TYPE_NUMERIC, precision,
					      scale),
		   context, DB_TYPE_NUMERIC, str);
  CHECK_PARSE_ERR (err, db_value_put (val, DB_TYPE_C_CHAR, (char *) str, len),
		   context, DB_TYPE_NUMERIC, str);

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
ldr_numeric_db_generic (LDR_CONTEXT * context,
			const char *str, int len, SM_ATTRIBUTE * att)
{
  int err;
  DB_VALUE val;

  CHECK_ERR (err, ldr_numeric_elem (context, str, len, &val));
  CHECK_ERR (err, ldr_generic (context, &val));

error_exit:
  return err;
}


/*
 *  REAL SETTERS
 *
 *  A "real" string is known to have an exponent part (e.g., "e+4") following
 *  a mantissa part that may or may not have a decimal point or a sign.  If
 *  know that we're assigning into a float or double attribute, we can
 *  exploit that knowledge by calling atod() and going to PRIM_SETMEM
 *  directly.  If not, we just build a DB_TYPE_DOUBLE DB_VALUE and use the
 *  generic setter.
 *
 *  The parser sees a real literal and passes back the following sub types
 *
 * TYPE     LOADER TYPE   Description
 * ----     -----------   -----------
 *
 * DOUBLE   LDR_DOUBLE    real with exponent part i.e., 'e' or 'E'
 * FLOAT    LDR_FLOAT     real with 'f' or 'F' suffix
 * NUMERIC  LDR_NUMERIC   default if no additional information provided in
 *                        real value.
 *
 * The loader will perform the following conversions based on the source
 * (loader type) and destination (database type) types :
 *
 *         +--------+---------------+-------------+---------------+--------+
 *         |DEFAULT | DB_TYPE_DOUBLE|DB_TYPE_FLOAT|DB_TYPE_NUMERIC|SET ELEM|
 *  -------+--------+---------------+-------------+---------------+--------+
 *  LDR    |real->db| real->db dbl  |real->db flt |real->db       | db dbl |
 *  DOUBLE |generic |               |             |generic        | elem   |
 *  -------+--------+---------------+-------------+---------------+--------+
 *  LDR    |real->db| real->db dbl  |real->db flt |real->db       | db flt |
 *  FLOAT  |generic |               |             |generic        | elem   |
 *  -------+--------+---------------+-------------+---------------+--------+
 *  LDR    |real->db| real->db dbl  |real->db flt |numeric->db    | db     |
 *  NUMERIC|generic |               |             |generic        | numeric|
 *  -------+--------+---------------+-------------+---------------+--------+
 *  LDR    |int->db | real->db dbl  |real->db flt |int->db        | db int |
 *  INT    |generic |               |             |generic        | elem   |
 *  -------+--------+---------------+-------------+---------------+--------+
 *
 *  Notice that because strtod() parses the syntax for both "numeric" and
 *  "real" strings, we only need one converter each for float and double
 *  attributes (i.e., if we had ldr_numeric_db_{float, double} they would be
 *  exactly the same as ldr_real_db_{float, double}).
 */


/*
 * ldr_double_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_double_elem (LDR_CONTEXT * context,
		 const char *str, int len, DB_VALUE * val)
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
	      db_get_type_name (domain->type->id));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, domain->type->id,
		       str);
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
ldr_float_elem (LDR_CONTEXT * context,
		const char *str, int len, DB_VALUE * val)
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
	      db_get_type_name (domain->type->id));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, domain->type->id,
		       str);
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
ldr_real_db_generic (LDR_CONTEXT * context,
		     const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_real_db_float (LDR_CONTEXT * context,
		   const char *str, int len, SM_ATTRIBUTE * att)
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
	      db_get_type_name (domain->type->id));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, domain->type->id,
		       str);
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
ldr_real_db_double (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
	      db_get_type_name (domain->type->id));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, domain->type->id,
		       str);
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
 *  DATE/TIME/TIMESTAMP SETTERS
 *
 *  Any of the "date", "time" or "timestamp" strings have already had the tag
 *  and surrounding quotes stripped off.  We know which one we have by virtue
 *  knowing which function has been called.
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
ldr_date_elem (LDR_CONTEXT * context,
	       const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_date_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_date (str, &val->data.date),
		   context, DB_TYPE_DATE, str);

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
ldr_date_db_date (LDR_CONTEXT * context,
		  const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_time_elem (LDR_CONTEXT * context,
	       const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_time_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_time (str, &val->data.time),
		   context, DB_TYPE_TIME, str);

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
ldr_time_db_time (LDR_CONTEXT * context,
		  const char *str, int len, SM_ATTRIBUTE * att)
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
 * ldr_timestamp_elem -
 *    return:
 *    context():
 *    str():
 *    len():
 *    val():
 */
static int
ldr_timestamp_elem (LDR_CONTEXT * context,
		    const char *str, int len, DB_VALUE * val)
{
  int err = NO_ERROR;

  val->domain = ldr_timestamp_tmpl.domain;
  CHECK_PARSE_ERR (err, db_string_to_timestamp (str, &val->data.utime),
		   context, DB_TYPE_TIMESTAMP, str);

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
ldr_timestamp_db_timestamp (LDR_CONTEXT * context,
			    const char *str, int len, SM_ATTRIBUTE * att)
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
 * ldr_date_time_conversion_error - display date/time validation error
 *    return: void
 *    token(in): string that failed.
 *    type(in): loader type
 */
static void
ldr_date_time_conversion_error (const char *token, DB_TYPE type)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_CONVERSION_ERROR), token,
	   db_get_type_name (type));
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
  DB_DATE dummy_date;
  DB_TIMESTAMP dummy_timestamp;
  DB_TYPE current_type = DB_TYPE_NULL;

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
    case LDR_DATE:
      current_type = DB_TYPE_DATE;
      err = db_string_to_date (str, &dummy_date);
      break;
    case LDR_TIMESTAMP:
      current_type = DB_TYPE_TIMESTAMP;
      err = db_string_to_timestamp (str, &dummy_timestamp);
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
ldr_elo_int_elem (LDR_CONTEXT * context,
		  const char *str, int len, DB_VALUE * val)
{
  DB_ELO *elo;
  int err = NO_ERROR;
  char name[PATH_MAX + 8];
  int new_len;
  const char *filename;
  int fd;

  if (!context->valid)
    {
      err = ER_LDR_INVALID_STATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      /* reset the error count, by adding -1 since this is not real error */
      LDR_INCREMENT_ERR_COUNT (context, -1);
      return (err);
    }

  PARSE_ELO_STR (str, new_len);

  if (new_len)
    {
      strncpy (name, str, new_len);
      name[new_len] = '\0';
      val->domain = ldr_elo_tmpl.domain;
      if ((elo = elo_create (name)) == NULL)
	err = er_errid ();
      else
	{
	  /*
	   * change the type to LO even though we have path name,
	   * since I^ was specified
	   */
	  elo->type = ELO_LO;
	  err = db_make_elo (val, elo);
	  if (!err && elo->pathname != NULL)
	    {
	      filename = elo->pathname;
	      fd = open (elo->pathname, O_RDONLY, 0);
	      if (fd > 0)
		{
		  close (fd);
		  ws_free_string ((char *) elo->original_pathname);
		  elo->pathname = NULL;
		  elo->original_pathname = NULL;
		  err = lo_migrate_in (&elo->loid, filename);
		  ws_free_string ((char *) filename);
		}
	      else
		{
		  err = ER_LDR_ELO_INPUT_FILE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1,
			  elo->pathname);
		}
	    }
	}
    }
  if (err != NO_ERROR)
    display_error (0);
  return (err);
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
ldr_elo_int_db_elo (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  char *mem;
  DB_VALUE val;
  char name[PATH_MAX + 8];
  int new_len;

  PARSE_ELO_STR (str, new_len);

  if (new_len)
    {
      strncpy (name, str, new_len);
      name[new_len] = '\0';
      CHECK_ERR (err, ldr_elo_int_elem (context, name, new_len, &val));
      mem = context->mobj + att->offset;
      CHECK_ERR (err,
		 PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
      OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);
    }

error_exit:
  return err;
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
ldr_elo_ext_elem (LDR_CONTEXT * context,
		  const char *str, int len, DB_VALUE * val)
{
  DB_ELO *elo;
  int err = NO_ERROR;
  char name[PATH_MAX + 8];
  int new_len;
  const char *filename;
  int fd;

  if (!context->valid)
    {
      err = ER_LDR_INVALID_STATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      /* reset the error count by adding -1, since this is not real error */
      LDR_INCREMENT_ERR_COUNT (context, -1);
      return (err);
    }

  PARSE_ELO_STR (str, new_len);

  if (new_len)
    {
      strncpy (name, str, new_len);
      name[new_len] = '\0';
      val->domain = ldr_elo_tmpl.domain;
      if ((elo = elo_create (name)) == NULL)
	err = er_errid ();
      else
	{
	  err = db_make_elo (val, elo);
	  if (!err && (elo->pathname != NULL) && (elo->type == ELO_LO))
	    {
	      if ((fd = open (elo->pathname, O_RDONLY, 0) > 0))
		{
		  close (fd);
		  filename = elo->pathname;
		  ws_free_string ((char *) elo->original_pathname);
		  elo->pathname = NULL;
		  elo->original_pathname = NULL;
		  err = lo_migrate_in (&elo->loid, filename);
		  ws_free_string ((char *) filename);
		}
	      else
		{
		  err = ER_LDR_ELO_INPUT_FILE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1,
			  elo->pathname);
		}
	    }
	}
    }
  if (err != NO_ERROR)
    display_error (0);
  return (err);
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
ldr_elo_ext_db_elo (LDR_CONTEXT * context,
		    const char *str, int len, SM_ATTRIBUTE * att)
{
  int err = NO_ERROR;
  char *mem;
  DB_VALUE val;
  char name[PATH_MAX + 8];
  int new_len;

  PARSE_ELO_STR (str, new_len);

  if (new_len && str[new_len - 1] == '\"')
    new_len--;
  if (new_len)
    {
      strncpy (name, str, new_len);
      name[new_len] = '\0';
      CHECK_ERR (err, ldr_elo_ext_elem (context, name, new_len, &val));
      mem = context->mobj + att->offset;
      CHECK_ERR (err,
		 PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
      OBJ_SET_BOUND_BIT (context->mobj, att->storage_order);
    }

error_exit:
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

  if ((ldr_Mop_tempoid_maps =
       (LDR_MOP_TEMPOID_MAPS *) malloc (sizeof (LDR_MOP_TEMPOID_MAPS))) ==
      NULL)
    {
      err = er_errid ();
      return (err);
    }

  presize = LDR_MOP_TEMPOID_MAPS_PRESIZE;

  if ((ldr_Mop_tempoid_maps->mop_tempoid_maps =
       (LDR_MOP_TEMPOID_MAP *) malloc (presize *
				       sizeof (LDR_MOP_TEMPOID_MAP))) == NULL)
    {
      err = er_errid ();
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
  ldr_Mop_tempoid_maps = NULL;

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

  if (!ldr_Mop_tempoid_maps)
    CHECK_ERR (err, ldr_mop_tempoid_maps_init ());

  ldr_Mop_tempoid_maps->mop_tempoid_maps[ldr_Mop_tempoid_maps->index].mop =
    mop;
  ldr_Mop_tempoid_maps->mop_tempoid_maps[ldr_Mop_tempoid_maps->index].table =
    table;
  ldr_Mop_tempoid_maps->mop_tempoid_maps[ldr_Mop_tempoid_maps->index].id = id;
  ldr_Mop_tempoid_maps->count += 1;
  ldr_Mop_tempoid_maps->index += 1;

  /* Grow array if required */

  if (ldr_Mop_tempoid_maps->index == ldr_Mop_tempoid_maps->size)
    {
      LDR_MOP_TEMPOID_MAP *mop_tempoid_maps_old;

      mop_tempoid_maps_old = ldr_Mop_tempoid_maps->mop_tempoid_maps;

      ldr_Mop_tempoid_maps->mop_tempoid_maps =
	(LDR_MOP_TEMPOID_MAP *) realloc (ldr_Mop_tempoid_maps->
					 mop_tempoid_maps,
					 sizeof (LDR_MOP_TEMPOID_MAP) *
					 (ldr_Mop_tempoid_maps->size +
					  LDR_MOP_TEMPOID_MAPS_PRESIZE));

      if (!ldr_Mop_tempoid_maps->mop_tempoid_maps)
	{
	  /* Prevent realloc memory leak, if error occurs. */
	  if (mop_tempoid_maps_old)
	    free_and_init (mop_tempoid_maps_old);
	  return (er_errid ());
	}
      else
	ldr_Mop_tempoid_maps->size += LDR_MOP_TEMPOID_MAPS_PRESIZE;
    }

error_exit:
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
    err = er_errid ();
  else
    {
      i = 0;
      while (i < ldr_Mop_tempoid_maps->count)
	{
	  mop_tempoid_map = &(ldr_Mop_tempoid_maps->mop_tempoid_maps[i]);
	  if (mop_tempoid_map->mop &&
	      OID_ISTEMP (WS_REAL_OID (mop_tempoid_map->mop)))
	    {
	      if (locator_add_oidset_object
		  (oidset, mop_tempoid_map->mop) == NULL)
		CHECK_ERR (err, er_errid ());
	    }
	  i += 1;
	  if (oidset->total_oids > OID_BATCH_SIZE)
	    {
	      CHECK_ERR (err, locator_assign_oidset (oidset, NULL));
	      locator_clear_oid_set (NULL, oidset);
	    }
	}
      /* call locator_assign_oidset(). This will make a server call to get permanent
       * oids.
       */
      if (oidset->total_oids)
	{
	  CHECK_ERR (err, locator_assign_oidset (oidset, NULL));
	}

      /* At this point the mapping between mop -> permanent oid should be
       * complete. Update the otable oids via the oid pointer obtained from
       * the CLASS_TABLE and id.
       */
      i = 0;
      while (i < ldr_Mop_tempoid_maps->count)
	{
	  mop_tempoid_map = &(ldr_Mop_tempoid_maps->mop_tempoid_maps[i]);
	  CHECK_PTR (err, inst = otable_find (mop_tempoid_map->table,
					      mop_tempoid_map->id));
	  COPY_OID (&(inst->oid), WS_REAL_OID (mop_tempoid_map->mop));
	  mop_tempoid_map->mop = NULL;
	  mop_tempoid_map->table = NULL;
	  mop_tempoid_map->id = 0;
	  i += 1;
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
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1,
			  db_get_class_name (class_));
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
		  CHECK_PTR (err, mop =
			     db_create_internal (context->
						 attrs[context->next_attr].
						 ref_class));
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
ldr_class_oid_elem (LDR_CONTEXT * context,
		    const char *str, int len, DB_VALUE * val)
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
ldr_class_oid_db_object (LDR_CONTEXT * context,
			 const char *str, int len, SM_ATTRIBUTE * att)
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
      CHECK_ERR (err,
		 PRIM_SETMEM (att->domain->type, att->domain, mem, &val));
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

  CHECK_ERR (err, check_object_domain (context,
				       context->attrs[context->next_attr].
				       ref_class, &actual_class));
  err =
    find_instance (context, actual_class, &oid,
		   context->attrs[context->next_attr].instance_id);
  if (err == ER_LDR_INTERNAL_REFERENCE)
    {
      DB_MAKE_NULL (val);
    }
  else
    {
      DB_OBJECT *mop;

      if ((mop = ws_mop (&oid, context->attrs[context->next_attr].ref_class))
	  == NULL)
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
ldr_oid_db_object (LDR_CONTEXT * context,
		   const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_monetary_elem (LDR_CONTEXT * context,
		   const char *str, int len, DB_VALUE * val)
{
  const unsigned char *p = (const unsigned char *) str;
  const unsigned char *token = (const unsigned char *) str;
  char *str_ptr;
  double amt;
  int err = NO_ERROR;
  DB_CURRENCY currency_type = DB_CURRENCY_DOLLAR;

  if (p[0] == 0xa1 && p[1] == 0xef)
    {
      token += 2;
      currency_type = DB_CURRENCY_YEN;
    }
  else if (p[0] == '\\')
    {
      token += 1;
      currency_type = DB_CURRENCY_WON;
    }
  else if (p[0] == 0xa3 && p[1] == 0xdc)
    {
      token += 2;
      currency_type = DB_CURRENCY_WON;
    }
  else if (p[0] == '$')
    {
      token += 1;
      currency_type = DB_CURRENCY_DOLLAR;
    }

  amt = strtod ((const char *) token, &str_ptr);

  if (str == str_ptr || OR_CHECK_DOUBLE_OVERFLOW (amt))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
	      db_get_type_name (DB_TYPE_MONETARY));
      CHECK_PARSE_ERR (err, ER_IT_DATA_OVERFLOW, context, DB_TYPE_MONETARY,
		       str);
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
ldr_monetary_db_monetary (LDR_CONTEXT * context,
			  const char *str, int len, SM_ATTRIBUTE * att)
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
ldr_collection_elem (LDR_CONTEXT * context,
		     const char *str, int len, DB_VALUE * val)
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
ldr_collection_db_collection (LDR_CONTEXT * context,
			      const char *str, int len, SM_ATTRIBUTE * att)
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
      context->collection = db_col_create (att->domain->type->id,
					   0, attdesc->collection_domain);

      if (context->collection == NULL)
	err = er_errid ();
      else
	context->set_domain = attdesc->collection_domain->setdomain;
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

      /* We finished dealing with elements of a collection, set the
       * action function to deal with normal attributes.
       */
      if (context->attribute_type == LDR_ATTRIBUTE_ANY)
	{
	  ldr_act = ldr_act_attr;
	  err = ldr_generic (context, &tmp);
	}
      else
	{
	  ldr_act = ldr_act_class_attr;
	  err = ldr_class_attr_db_generic (context, str, len,
					   context->attrs[context->next_attr].
					   att, &tmp);
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
  if (context->cls &&
      context->attribute_type == LDR_ATTRIBUTE_ANY &&
      context->constructor == NULL)
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
	  /*
	   * If this was a forward reference from a class attribute than we
	   * do not want to mark it as releasable.
	   */
	  if (!(inst->flags & INST_FLAG_CLASS_ATT))
	    ws_release_instance (context->obj);
	}
      else
	{
	  if ((context->obj = db_create_internal (context->cls)) == NULL)
	    CHECK_ERR (err, er_errid ());
	  /*
	   * Mark this mop as released so that we can cull it when we
	   * complete inserting this instance.
	   */
	  ws_release_instance (context->obj);
	}
      CHECK_ERR (err, au_fetch_instance (context->obj, &context->mobj,
					 AU_FETCH_UPDATE, AU_UPDATE));
      ws_pin_instance_and_class (context->obj, &context->obj_pin,
				 &context->class_pin);
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
    ws_intern_instances (context->cls);
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
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_LOADDB,
					   LOADDB_MSG_INTERRUPTED_ABORT));
	  if (context->periodic_commit
	      && Total_objects >= context->periodic_commit)
	    committed_instances =
	      Total_objects - (context->periodic_commit -
			       context->commit_counter);
	  else
	    committed_instances = 0;
	}
      else
	{
	  if (context->cls != NULL)
	    {
	      CHECK_ERR (err, ldr_assign_all_perm_oids ());
	      CHECK_ERR (err, db_commit_transaction ());
	      Last_committed_zzline = zzline - 1;
	      committed_instances = Total_objects + 1;
	      display_error_line (-1);
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_INTERRUPTED_COMMIT));
	    }
	}

      /* Invoke post interrupt callback function */
      if (ldr_post_interrupt_handler != NULL)
	(*ldr_post_interrupt_handler) (committed_instances);

      if (ldr_Jmp_buf != NULL)
	longjmp (*ldr_Jmp_buf, 1);
      else
	return (err);
    }

  if (context->periodic_commit)
    {
      context->commit_counter--;
      if (context->commit_counter <= 0)
	{
	  if (context->cls != NULL)
	    {
	      print_log_msg (context->verbose,
			     msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_COMMITTING));
	      CHECK_ERR (err, ldr_assign_all_perm_oids ());
	      CHECK_ERR (err, db_commit_transaction ());
	      Last_committed_zzline = zzline - 1;
	      context->commit_counter = context->periodic_commit;

	      /* Invoke post commit callback function */
	      if (ldr_post_commit_handler != NULL)
		(*ldr_post_commit_handler) ((Total_objects + 1));

	      /* After a commit we need to ensure that our attributes and
	       * attribute descriptors are updated. The commit process
	       * can pull these from under us if another client is also
	       * updating the class.
	       */
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

  CHECK_SKIP();
  if (context->valid)
    {
      if (context->next_attr < (context->num_attrs + context->arg_count))
	{
	  if (context->arg_count
	      && (context->next_attr >= context->arg_index))
	    {
	      err = ER_LDR_MISSING_ARGUMENT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2,
		      context->arg_count,
		      context->next_attr - context->arg_index);
	    }
	  else
	    {
	      err = ER_LDR_MISSING_ATTRIBUTES;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2,
		      context->num_attrs, context->next_attr);
	    }
	  LDR_INCREMENT_ERR_COUNT (context, 1);
	}
    }

  if (context->partition_of)
    {				/* partition table load */
      /* partitioned parent class-not allowed
         partition key column must be specified */
      if (!context->psi || context->key_attr_idx < 0)
	{
	  err = ER_PARTITION_WORK_FAILED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	  LDR_INCREMENT_ERR_COUNT (context, 1);
	}
    }

  ldr_restore_pin_and_drop_obj (context, ((context->err_count != 0) ||
					  (err != NO_ERROR)));
  CHECK_ERR (err, err);

  if (context->valid && !context->err_count)
    {
      if (context->constructor)
	err = insert_meth_instance (context);
      else if (context->attribute_type == LDR_ATTRIBUTE_ANY)
	err = insert_instance (context);

      if (err == NO_ERROR)
	{
	  if (context->flush_interval &&
	      context->inst_count >= context->flush_interval)
	    {
	      err = ldr_assign_all_perm_oids ();
	      if (err == NO_ERROR)
		{
		  if (!context->validation_only)
		    {
		      ldr_flush (context);
		      err = er_filter_errid (true);	/* ignore warning */
		      CHECK_ERR (err, err);
		    }
		}
	      else
		LDR_INCREMENT_ERR_COUNT (context, 1);
	    }
	}
      else
	LDR_INCREMENT_ERR_COUNT (context, 1);
    }

error_exit:
  if (context->err_count || (err != NO_ERROR))
    ldr_abort ();
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
  int err = NO_ERROR;

  if (err)
    CHECK_CONTEXT_VALIDITY (context, true);
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
	  /* Need to flush the class now, for class attributes.
	   * Class objects are flushed during a commit, if ws_intern_instances()
	   * is called and culls object references from within a class_object
	   * the class_object will loose these references.
	   */
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
	    fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_INSTANCE_COUNT),
		     context->inst_total);
	}
    }

  /* Reset the action function to deal with attributes */
  ldr_act = ldr_act_attr;

#if defined(CUBRID_DEBUG)
  if ((err == NO_ERROR) && (envvar_get ("LOADER_DEBUG") != NULL))
    {
      if (context->inst_total)
	printf ("%d %s %s inserted in %d %s\n",
		context->inst_total, ldr_class_name (context),
		context->inst_total == 1 ? "instance" : "instances",
		context->flush_total,
		context->flush_total == 1 ? "flush" : "flushes");

      if (context->err_total)
	printf ("%d %s %s ignored because of errors\n",
		context->err_total, ldr_class_name (context),
		context->err_total == 1 ? "instance" : "instances");
    }
#endif /* CUBRID_DEBUG */

  ldr_clear_and_free_context (context);

error_exit:
  if (err != NO_ERROR)
    ldr_abort ();
  return (err);
}

/*
 * ldr_partition_info -
 *    return:
 *    context():
 */
static int
ldr_partition_info (LDR_CONTEXT * context)
{
  SM_CLASS *smclass;
  DB_VALUE pclassof, pname, classobj;
  int error = NO_ERROR;
  int au_save;

  if (!context || !context->cls)
    return -1;
  AU_DISABLE (au_save);
  if (au_fetch_class (context->cls, &smclass,
		      AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      er_clear ();		/* not a partition class */
      AU_ENABLE (au_save);
      return NO_ERROR;
    }
  if (smclass->partition_of && context->partition_of != smclass->partition_of)
    {
      context->partition_of = smclass->partition_of;
      context->psi = NULL;
      context->key_attr_idx = -1;
      DB_MAKE_NULL (&pclassof);
      DB_MAKE_NULL (&pname);
      DB_MAKE_NULL (&classobj);
      if ((error = db_get (smclass->partition_of, PARTITION_ATT_CLASSOF,
			   &pclassof)) != NO_ERROR)
	goto error_return;
      if ((error = db_get (smclass->partition_of, PARTITION_ATT_PNAME,
			   &pname)) != NO_ERROR)
	goto error_return;
      if (!DB_IS_NULL (&pname))
	{
	  if ((error =
	       db_get (db_get_object (&pclassof), PARTITION_ATT_CLASSOF,
		       &classobj)) != NO_ERROR)
	    goto error_return;
	  error =
	    do_init_partition_select (db_get_object (&classobj),
				      &context->psi);
	}
    }
  else
    {
      AU_ENABLE (au_save);
      return NO_ERROR;
    }
error_return:
  AU_ENABLE (au_save);
  pr_clear_value (&pclassof);
  pr_clear_value (&pname);
  pr_clear_value (&classobj);
  return error;
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
#if !defined(DISABLE_TTA_FIX)
  DB_OBJECT *class_mop;
#endif /* !DISABLE_TTA_FIX */

  CHECK_SKIP();
  if ((err = ldr_finish_context (context) != NO_ERROR))
    {
      display_error (-1);
      return;
    }
  if (class_name)
    {
#if !defined (DISABLE_TTA_FIX)
#else /* DISABLE_TTA_FIX */
      if (!context->validation_only)
	{
#endif /* !DISABLE_TTA_FIX */
	  if ((class_mop = ldr_find_class (class_name)) == NULL)
	    {
	      display_error_line (0);
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_UNKNOWN_CLASS),
		       class_name);
	      CHECK_CONTEXT_VALIDITY (context, true);
	      ldr_abort ();
	      goto error_exit;
	    }
#if !defined (DISABLE_TTA_FIX)
	  if (!context->validation_only)
	    {
	      context->cls = class_mop;
#else /* DISABLE_TTA_FIX */
#endif /* !DISABLE_TTA_FIX */
	  /*
	   * Cache the class name. This will be used if we have a periodic
	   * commit and need to refresh the class
	   * This is a temporary fix, we will have to cache all the class
	   * names that we deal with, and refetch the classes and locks
	   * after a periodic commit.
	   */
	  if ((context->class_name = (char *) malloc (len + 1)) == NULL)
	    {
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
	  if (context->psi)
	    do_clear_partition_select (context->psi);
	  if (ldr_partition_info (context) != NO_ERROR)
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
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_LOADDB,
				       LOADDB_MSG_CLASS_TITLE), class_name);
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
  DB_VALUE ele;

  CHECK_SKIP();
  RETURN_IF_NOT_VALID (context);

  if (context->validation_only)
    {
      context->num_attrs += 1;
      goto error_exit;
    }

  n = context->num_attrs + context->arg_count;

  attrs_old = context->attrs;
  if ((context->attrs = (LDR_ATTDESC *) realloc (context->attrs,
						 (n +
						  1) *
						 sizeof (context->
							 attrs[0]))) == NULL)
    free_and_init (attrs_old);	/* Prevent leakage if realloc fails */

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
      if (context->constructor->signatures->num_args &&
	  (context->arg_count >= context->constructor->signatures->num_args))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LDR_UNEXPECTED_ARGUMENT, 1,
		  context->constructor->signatures->num_args);
	  CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
	}
      return;
    }

  CHECK_ERR (err, db_get_attribute_descriptor (context->cls,
					       attr_name,
					       context->attribute_type ==
					       LDR_ATTRIBUTE_CLASS, true,
					       &attdesc->attdesc));
  CHECK_ERR (err, sm_get_descriptor_component (context->cls, attdesc->attdesc, 1,	/* for update */
					       &class_,
					       (SM_COMPONENT **) & attdesc->
					       att));

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
      if (attdesc->att->domain->type->id == DB_TYPE_SET ||
	  attdesc->att->domain->type->id == DB_TYPE_MULTISET ||
	  attdesc->att->domain->type->id == DB_TYPE_SEQUENCE)
	{
	  attdesc->setter[LDR_COLLECTION] = &ldr_collection_db_collection;
	  CHECK_ERR (err, select_set_domain (context, attdesc->att->domain,
					     &(attdesc->collection_domain)));
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_INVALID_CLASS_ATTR,
		  2, ldr_attr_name (context), ldr_class_name (context));
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

  switch (attdesc->att->domain->type->id)
    {
    case DB_TYPE_CHAR:
      attdesc->setter[LDR_STR] = &ldr_str_db_char;
      break;

    case DB_TYPE_VARCHAR:
      attdesc->setter[LDR_STR] = &ldr_str_db_varchar;
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

    case DB_TYPE_TIMESTAMP:
      attdesc->setter[LDR_STR] = &ldr_str_db_generic;
      attdesc->setter[LDR_TIMESTAMP] = &ldr_timestamp_db_timestamp;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      attdesc->setter[LDR_COLLECTION] = &ldr_collection_db_collection;
      CHECK_ERR (err, select_set_domain (context, attdesc->att->domain,
					 &(attdesc->collection_domain)));
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

    case DB_TYPE_ELO:
      attdesc->setter[LDR_ELO_EXT] = &ldr_elo_ext_db_elo;
      attdesc->setter[LDR_ELO_INT] = &ldr_elo_int_db_elo;
      break;

    case DB_TYPE_MONETARY:
      attdesc->setter[LDR_MONETARY] = &ldr_monetary_db_monetary;
      break;

    default:
      break;
    }
  if (context->partition_of && context->psi)
    {
      if ((err = set_get_element (context->psi->pattr->data.set, 0, &ele))
	  != NO_ERROR)
	goto error_exit;
      if (intl_mbs_casecmp (attr_name, db_get_string (&ele)) == 0)
	{
	  context->key_attr_idx = n;
	}
      pr_clear_value (&ele);
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

  if ((context->cls = ldr_find_class (context->class_name)) == NULL)
    {
      CHECK_ERR (err, er_errid ());
    }

  for (i = 0; i < context->num_attrs; i += 1)
    {
      attdesc = &(context->attrs[i]);
      CHECK_ERR (err, db_get_attribute_descriptor (context->cls,
						   attdesc->attdesc->name,
						   context->attribute_type ==
						   LDR_ATTRIBUTE_CLASS,
						   true, &db_attdesc));
      /* Free existing descriptor */
      db_free_attribute_descriptor (attdesc->attdesc);
      attdesc->attdesc = db_attdesc;
      /* Get refreshed attribute */
      CHECK_ERR (err, sm_get_descriptor_component (context->cls, attdesc->attdesc, 1,	/* for update */
						   &class_,
						   (SM_COMPONENT **) &
						   attdesc->att));
    }
  CHECK_ERR (err, ldr_partition_info (context));
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

  CHECK_SKIP();
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
    ldr_clear_and_free_context (context);
  else
    {
      finish_error = ldr_finish_context (context);
      if (!finish_error && !context->validation_only && !(context->err_total))
	finish_error = update_default_instances_stats (context);
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
  if (ldr_finish (context, parse_error))
    display_error (-1);

  /* check errors */

  if (parse_error)
    printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			    MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_STOPPED));
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
	      inst = otable_find (context->table, context->inst_num);
	      if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
		{
		  CHECK_ERR (err, otable_insert (context->table,
						 WS_REAL_OID (context->obj),
						 context->inst_num));
		  CHECK_PTR (err, inst = otable_find (context->table,
						      context->inst_num));
		  CHECK_ERR (err, ldr_add_mop_tempoid_map (context->obj,
							   context->table,
							   context->
							   inst_num));
		}
	      else
		{
		  err = otable_update (context->table, context->inst_num);
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
    ldr_internal_error (context);
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
#if !defined(DISABLE_TTA_FIX)
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_INSTANCE_COUNT_EX),
		       context->inst_total);
#else /* DISABLE_TTA_FIX */
	      fprintf (stdout, "%d ", context->inst_total);
#endif /* !DISABLE_TTA_FIX */
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
ldr_act_start_instance (LDR_CONTEXT * context, int id)
{
  CHECK_SKIP();
  if (context->valid)
    {

      context->inst_num = id;

      if (ldr_reset_context (context) != NO_ERROR)
	display_error (-1);

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

  for (i = 0, a = context->arg_index;
       i < context->arg_count && err == NO_ERROR && i < LDR_MAX_ARGS;
       i++, a++)
    {
      err = (*(elem_converter[context->attrs[a].parser_type])) (context,
								context->
								attrs[a].
								parser_str,
								context->
								attrs[a].
								parser_str_len,
								&vals[i]);
      meth_args[i] = &(vals[i]);
    }

  meth_args[i] = NULL;

  err = db_send_argarray (context->cls, context->constructor->header.name,
			  &retval, meth_args);

  if (!err && DB_VALUE_TYPE (&retval) == DB_TYPE_OBJECT)
    {
      obj = DB_GET_OBJECT (&retval);
      context->obj = obj;
      if (!
	  (err =
	   au_fetch_instance (context->obj, &context->mobj, AU_FETCH_UPDATE,
			      AU_UPDATE)))
	{
	  ws_pin_instance_and_class (context->obj,
				     &context->obj_pin, &context->class_pin);
	  ws_class_has_object_dependencies (context->cls);
	}

      /* now we have to initialize the instance with the supplied values */
      for (context->next_attr = 0;
	   context->next_attr < context->arg_index && !err;
	   context->next_attr++)
	{
	  attdesc = &context->attrs[context->next_attr];

	  err = sm_get_descriptor_component (context->cls,
					     attdesc->attdesc,
					     1,
					     &class_,
					     (SM_COMPONENT **) & attdesc->
					     att);

	  if (!err)
	    err = (*(attdesc->setter[attdesc->parser_type])) (context,
							      attdesc->
							      parser_str,
							      attdesc->
							      parser_str_len,
							      attdesc->att);
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
	  /*
	   * Build a real-live instance using a constructor method.
	   * Since we go through the usual object creation mechanism
	   * for these, we don't have to register unique values in a separate
	   * operation.
	   * We still however must remember the instance OID in the
	   * class table for later references.
	   *
	   * For now, we don't allow forward references to instances that
	   * have a constructor.  This is because we don't know how
	   * to pre-create the referenced instance.  Could fix this someday
	   * but it would require the ability to either "create with oid"
	   * or something else equally complex.
	   */
	  CHECK_PTR (err, real_obj = construct_instance (context));
	  if (real_obj == NULL)
	    CHECK_ERR (err, er_errid ());
	  else
	    {
	      ws_release_instance (real_obj);
	      inst = otable_find (context->table, context->inst_num);
	      if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
		{
		  CHECK_ERR (err,
			     otable_insert (context->table, WS_OID (real_obj),
					    context->inst_num));
		  CHECK_PTR (err, inst =
			     otable_find (context->table, context->inst_num));
		  CHECK_ERR (err,
			     ldr_add_mop_tempoid_map (real_obj,
						      context->table,
						      context->inst_num));
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LDR_FORWARD_CONSTRUCTOR, 0);
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
    ldr_internal_error (context);
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
#if !defined (DISABLE_TTA_FIX)
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_INSTANCE_COUNT_EX),
		       context->inst_total);
#else /* DISABLE_TTA_FIX */
	      fprintf (stdout, "%d ", context->inst_total);
#endif /* !DISABLE_TTA_FIX */
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

  CHECK_SKIP();
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LDR_INVALID_CONSTRUCTOR, 2, name,
		  ldr_class_name (context));
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

      if ((*elements =
	   (void **) realloc (*elements, sizeof (void *) * resize)) == NULL)
	free_and_init (*elements_old);	/* Prevent leak if realloc fails */

      CHECK_PTR (err, *elements);

      for (i = *count; i < resize; i++)
	(*elements)[i] = NULL;

      *max = resize;
    }
  new_ = *count;
  *count = new_ + 1;

error_exit:
  if (err != NO_ERROR || *elements == NULL)
    return (-1);
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

  index = add_element ((void ***) (&(context->args)), &(context->arg_count),
		       &(context->maxarg), LDR_ARG_GROW_SIZE);
  return index;
}


/*
 * ldr_act_add_argument - spefify paramters to an instance constructor method
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

  CHECK_SKIP();
  if (context->validation_only)
    {
      context->arg_count += 1;
      goto error_exit;
    }

  if (context->valid)
    {
      if (context->constructor == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LDR_UNEXPECTED_ARGUMENT, 1, 0);
	  CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
	}
      else
	{
	  sig = context->constructor->signatures;
	  /* arg count of zero currently means "variable", not good */
	  if (sig->num_args && (context->arg_count) >= sig->num_args)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LDR_UNEXPECTED_ARGUMENT, 1, sig->num_args);
	      CHECK_ERR (err, ER_LDR_UNEXPECTED_ARGUMENT);
	    }
	  else
	    {
	      /* Locate the argument descriptor, remember to adjust for
	         1 based argument numbering.
	       */
	      arg =
		classobj_find_method_arg (&sig->args, context->arg_count, 0);
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
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNKNOWN_CLASS_ID), id,
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

  CHECK_SKIP();
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
  CHECK_SKIP();
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

  CHECK_SKIP();
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
  RETURN_IF_NOT_VALID (context);

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

  /*
   * Definitely *don't* want to use oid preflushing in this app; it just
   * gives us some extra overhead that we don't care about.
   */
  tm_Use_OID_preflush = false;

  /* Set the appropriate action function for normal attribute values */

  ldr_act = ldr_act_attr;

  /*
   * Don't gc if we have to grow the mop blocks; just grow them without
   * thinking.
   */
  ws_gc_disable ();

  /*
   * Optimization to avoid calling db_value_domain_init all of the time
   * during loading; we can simply copy these templates much more cheaply.
   */
  db_make_int (&ldr_int_tmpl, 0);
  db_make_char (&ldr_char_tmpl, 1, (char *) "a", 1);
  db_make_varchar (&ldr_varchar_tmpl, 1, (char *) "a", 1);
  db_make_float (&ldr_float_tmpl, (float) 0.0);
  db_make_double (&ldr_double_tmpl, (double) 0.0);
  db_make_date (&ldr_date_tmpl, 1, 1, 1996);
  db_make_time (&ldr_time_tmpl, 0, 0, 0);
  db_make_timestamp (&ldr_timestamp_tmpl, 0);
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
  elem_converter[LDR_TIMESTAMP] = &ldr_timestamp_elem;
  elem_converter[LDR_COLLECTION] = &ldr_collection_elem;
  elem_converter[LDR_BSTR] = &ldr_bstr_elem;
  elem_converter[LDR_XSTR] = &ldr_xstr_elem;
  elem_converter[LDR_NSTR] = &ldr_nstr_elem;
  elem_converter[LDR_MONETARY] = &ldr_monetary_elem;
  elem_converter[LDR_ELO_EXT] = &ldr_elo_ext_elem;
  elem_converter[LDR_ELO_INT] = &ldr_elo_int_elem;

  /* Set up the lockhint array. Used by ldr_find_class() when locating a class.
   */
  ldr_Hint_locks[0] = locator_fetch_mode_to_lock (DB_FETCH_CLREAD_INSTWRITE,
						  LC_CLASS);
  ldr_Hint_classnames[0] = NULL;
  ldr_Hint_subclasses[0] = 0;

  Total_objects = 0;
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
ldr_init (int verbose)
{
  LDR_CONTEXT *context;

  ldr_Current_context = &ldr_Context;
  context = ldr_Current_context;

  if (ldr_init_loader (ldr_Current_context))
    return er_errid ();

  idmap_init ();

  if (otable_init ())
    return er_errid ();

  if (clist_init ())
    return er_errid ();

  context->validation_only = 1;
  context->verbose = verbose;

  context->status_count = 0;
  /* used to monitor the number of insertions performed */
  context->status_counter = 0;

#if !defined (DISABLE_TTA_FIX)
  context->status_count = 10;
#else /* DISABLE_TTA_FIX */
  /* hack, let an environment variable determine the status counter */
  env = envvar_get (ENV_LOADDB_STATUS);
  if (env != NULL)
    context->status_count = atol (env);
#endif /* !DISABLE_TTA_FIX */

  (void) tr_set_execution_state (false);
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
    ldr_Current_context = &ldr_Context;

  context = ldr_Current_context;

  ldr_clear_context (context);

  /*
   * Initialize the mop -> temporary_oid, table used to obtain a mapping
   * between permanent OID and workspace mop, when permanent OIDs are obtained.
   */
  if ((err = ldr_mop_tempoid_maps_init ()) != NO_ERROR)
    return err;

  context->validation_only = 0;

  if (periodic_commit <= 0)
    context->periodic_commit = 0;
  else
    {
      context->periodic_commit = periodic_commit;
      context->commit_counter = periodic_commit;
    }

  /* make sure we reset this to get accurate statistics */
  Total_objects = 0;

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
ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER handler,
				     void *ldr_jmp_buf)
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
 */
void
ldr_stats (int *errors, int *objects, int *defaults, int *lastcommit)
{
  if (errors != NULL)
    *errors = ldr_Current_context->err_total;

  if (objects != NULL)
    *objects = Total_objects;

  if (defaults != NULL)
    *defaults = ldr_Current_context->default_count;

  if (lastcommit != NULL)
    *lastcommit = Last_committed_zzline;
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

  for (table = Classes; table != NULL && !err; table = table->next)
    {
      if (table->total_inserts)
	{
	  if (ldr_Current_context->verbose)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_CLASS_TITLE),
		       sm_class_name (table->class_));
	      fflush (stdout);
	    }
	  err = sm_update_statistics (table->class_);
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
    (*ldr_post_interrupt_handler) (-1);
  if (ldr_Jmp_buf != NULL)
    longjmp (*ldr_Jmp_buf, 1);
}

/*
 * print_parser_lineno - print parse line number
 *    return: void
 *    fp(in): FILE *
 */
void
print_parser_lineno (FILE * fp)
{
  if (fp)
    fprintf (fp, "%d\n", zzline);
  else
    printf ("%d\n", zzline);
}


void ldr_act_set_skipCurrentclass (char *classname, size_t size)
{
  skipCurrentclass = ldr_is_ignore_class (classname, size);
}

bool ldr_is_ignore_class (char *classname, size_t size)
{
  int i;
  char **p;

  if (classname == NULL)
    {
      return false;
    }
  
  if (ignoreClasslist != NULL) {
    for (i=0, p=ignoreClasslist ; i<ignoreClassnum ; i++, p++) {
      if (strncasecmp(*p, classname, size) == 0) {
        return true;
      }
    }
  }

  return false;
}

