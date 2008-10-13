/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 *      ldr.c: Loader definitions. Updated using design from fast loaddb 
 *             prototype
 */

#ifndef _LDR_H_
#define _LDR_H_

#ident "$Id$"

typedef struct LDR_CONTEXT LDR_CONTEXT;

/*
 * These are the "types" of strings that the lexer recognizes.  The
 * loader can specialize on each type.
 * These values are used to set up a vector of type setting functions, based
 * on information about each attribute parsed in the %CLASS line. 
 * The setter functions are invoked using the enumerated type as an index into 
 * the function vector. This gives us a significant saving when processing 
 * values in the instance line, over the previous loader.
 */

typedef enum
{
  LDR_NULL,
  LDR_INT,
  LDR_STR,
  LDR_NSTR,
  LDR_NUMERIC,			/* Default real */
  LDR_DOUBLE,			/* Reals specified with scientific notation, 'e', or 'E' */
  LDR_FLOAT,			/* Reals specified with C 'f' or 'F' notation */
  LDR_OID,			/* Object references */
  LDR_CLASS_OID,		/* Class object reference */
  LDR_DATE,
  LDR_TIME,
  LDR_TIMESTAMP,
  LDR_COLLECTION,
  LDR_ELO_INT,			/* Internal ELO's */
  LDR_ELO_EXT,			/* External ELO's */
  LDR_SYS_USER,
  LDR_SYS_CLASS,		/* This type is not allowed currently. */
  LDR_MONETARY,
  LDR_BSTR,			/* Binary bit strings     */
  LDR_XSTR,			/* Hexidecimal bit strings */

  NUM_LDR_TYPES
} LDR_TYPE;

typedef void (*LDR_POST_COMMIT_HANDLER) (int);
typedef void (*LDR_POST_INTERRUPT_HANDLER) (int);



/*
 * LDR_ATTRIBUTE_TYPE
 *
 * attribute type identifiers for ldr_act_restrict_attributes().
 * These attributes are handled specially since there modify the class object
 * directly.
 */

typedef enum ldr_attr_types
{
  LDR_ATTRIBUTE_ANY = 0,
  LDR_ATTRIBUTE_SHARED,
  LDR_ATTRIBUTE_CLASS,
  LDR_ATTRIBUTE_DEFAULT
} LDR_ATTRIBUTE_TYPE;

typedef enum ldr_interrupt_types
{
  LDR_NO_INTERRUPT,
  LDR_STOP_AND_ABORT_INTERRUPT,
  LDR_STOP_AND_COMMIT_INTERRUPT
} LDR_INTERRUPT_TYPE;

extern char **ignoreClasslist;
extern int ignoreClassnum;
extern bool skipCurrentclass;

extern LDR_CONTEXT *ldr_Current_context;

/* Loader initialization and shutdown functions */

extern int ldr_init (int verbose);
extern int ldr_start (int periodic_commit);
extern int ldr_final (void);
extern int ldr_finish (LDR_CONTEXT * context, int err);

/* Action to initialize the parser context to deal with a new class */

extern void ldr_act_init_context (LDR_CONTEXT * context,
				  const char *class_name, int len);
extern void ldr_increment_err_total (LDR_CONTEXT * context);

/*
 * Action to deal with instance attributes, arguments for
 * constructors and set elements
 * ldr_act is set to appropriate function depending on the context.
 */

extern void (*ldr_act) (LDR_CONTEXT * context, const char *str, int len,
			LDR_TYPE type);

extern void ldr_act_attr (LDR_CONTEXT * context, const char *str, int len,
			  LDR_TYPE type);

/* Action to deal with attribute names and argument names */

extern void ldr_act_add_attr (LDR_CONTEXT * context, const char *str,
			      int len);

/*
 * Action to finish normal instances, constructor instances, and
 * updates to class/default/shared values.
 */

extern void ldr_act_finish_line (LDR_CONTEXT * context);

/* Actions for %ID command */

extern void ldr_act_start_id (LDR_CONTEXT * context, char *name);
extern void ldr_act_set_id (LDR_CONTEXT * context, int id);

/* Actions for object references */

extern void ldr_act_set_ref_class_id (LDR_CONTEXT * context, int id);
extern void ldr_act_set_ref_class (LDR_CONTEXT * context, char *name);
extern void ldr_act_set_instance_id (LDR_CONTEXT * context, int id);

/* Special action for class, shared, default attributes */

extern void ldr_act_restrict_attributes (LDR_CONTEXT * context,
					 LDR_ATTRIBUTE_TYPE type);

/* Action for cleaning up and finish the parse phase */

extern void ldr_act_finish (LDR_CONTEXT * context, int parse_error);

/* Actions for constructor syntax */

extern int ldr_act_set_constructor (LDR_CONTEXT * context, const char *name);
extern int ldr_act_add_argument (LDR_CONTEXT * context, const char *name);

/* Action to start a new instance */

extern void ldr_act_start_instance (LDR_CONTEXT * context, int id);

/* Statistics updating/retrieving functions */

#if defined (LDR_OLD_LOADDB)
extern void ldr_stats (int *errors, int *objects, int *defaults);
#else /* !LDR_OLD_LOADDB */
extern void ldr_stats (int *errors, int *objects, int *defaults,
		       int *lastcommit);
#endif /* LDR_OLD_LOADDB */
extern int ldr_update_statistics (void);
extern void print_parser_lineno (FILE * fp);

/* Callback functions  */

extern void ldr_register_post_commit_handler (LDR_POST_COMMIT_HANDLER handler,
					      void *arg);
extern void ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER
						 handler, void *ldr_jmp_buf);
extern void ldr_interrupt_has_occurred (int type);

extern void ldr_act_set_skipCurrentclass (char *classname, size_t size);
extern bool ldr_is_ignore_class (char *classname, size_t size);
/* log functions */
extern void print_log_msg (int verbose, const char *fmt, ...);

#endif /* _LDR_H_ */
