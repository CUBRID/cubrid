/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * trigger_manager.h - Trigger Manager Definitions
 *
 * Note:
 *    The following definitions have been moved to dbdef.h.
 *
 *    DB_TRIGGER_STATUS
 *    DB_TRIGGER_EVENT
 *    DB_TRIGGER_TIME
 *    DB_TRIGGER_ACTION
 */

#ifndef _TRIGGER_MANAGER_H_
#define _TRIGGER_MANAGER_H_

#ident "$Id$"

#include "memory_manager_2.h"
#include "dbtype.h"
#include "dbdef.h"
#include "class_object.h"

/*
 * TR_LOWEST_PRIORITY
 *
 * Note:
 *    This defines the default lowest priority for a trigger.
 *
 */

#define TR_LOWEST_PRIORITY       	0.0

/*
 * TR_INFINITE_RECURSION_LEVEL
 *
 * Note:
 *    This number is used to indicate an infinite recursion level.
 *
 */

#define TR_INFINITE_RECURSION_LEVEL    	-1

/*
 * TR_MAX_PRINT_STRING
 *
 * Note:
 *    Maximum length of string available for use in the PRINT action.
 *
 */

#define TR_MAX_PRINT_STRING 2048

/*
 * TR_MAX_ATTRIBUTE_TRIGGERS
 * TR_MAX_CLASS_TRIGGERS
 *
 * Note:
 *    These are constants based on the definition of DB_TRIGGER_EVENT
 *    which is now defined in dbdef.h.
 *    They are used to determine the ranges of the class & attribute
 *    trigger caches.
 *    The definition of DB_TRIGGER_EVENT is very important since the
 *    numeric constants are used directly as array indexes.
 *    If the DB_TRIGGER_EVENT enumeration changes, these constants
 *    and code in tr.c will also have to be changed.
 *
 */

#define TR_MAX_ATTRIBUTE_TRIGGERS (int)TR_EVENT_STATEMENT_UPDATE + 1
#define TR_MAX_CLASS_TRIGGERS     (int)TR_EVENT_DROP + 1

/*
 * TR_TRIGGER
 *
 * Note:
 *    This is the primary in memory structure used for representint
 *    triggers.  Though triggers are stored in the database as normal
 *    instances of the db_trigger class, when they are cached, they are
 *    converted to one of these structures for fast access.
 *    A table that MAPs trigger MOPs into the corresponding trigger
 *    structure is maintained.
 *
 */

typedef struct tr_trigger
{
  DB_OBJECT *owner;
  DB_OBJECT *object;
  char *name;
  DB_TRIGGER_STATUS status;
  double priority;
  DB_TRIGGER_EVENT event;
  DB_OBJECT *class_mop;
  char *attribute;
  int class_attribute;

  struct tr_activity *condition;
  struct tr_activity *action;

  char *current_refname;
  char *temp_refname;

  /*
   * copy of the "cache coherency number" from the instance, used
   * to detect when the object gets modified and the trigger cache
   * needs to be re-evaluated
   */
  int chn;
} TR_TRIGGER;

/*
 * TR_TRIGLIST
 *
 * Note:
 *    This is used to maintain lists of trigger structures.
 *    It is doubly linked so removals are fast.
 *    In hindsight, this probably doesn't need to be doubly linked but
 *    it doesn't hurt anything right now.
 *
 */

typedef struct tr_triglist
{
  struct tr_triglist *next;
  struct tr_triglist *prev;

  TR_TRIGGER *trigger;
  DB_OBJECT *target;		/* associated target instance */

  /* saved recursion level for a deferred activity */
  int recursion_level;
} TR_TRIGLIST;

/*
 * TR_DEFERRED_CONTEXT
 *
 * Note:
 *    This is used to maintain lists of deferred activities.
 *    As trigger activities are deferred, a TR_TRIGLIST is build containing
 *    the necessary information.  These TR_TRIGLISTs are concatenated
 *    with the "triglist" field of the active tr_deferred_context structure
 *    The reason we have another level of indirection with the deferred
 *    activity lists is because they must be grouped according to
 *    savepoint boundaries.  If the user rolls back to a specific savepoint,
 *    all deferred triggers that occurred between now and the selected
 *    savepoint must be removed.  This process is simplified if we maintain
 *    a stack of tr_deferred_context structures for each savepoint.
 *    Each stack entry contains the list of deferred triggers that
 *    were scheduled durint that savepoint.
 *    For transactions without savepoints, there will be only one
 *    element on the tr_deferred_context list.
 *
 */

typedef struct tr_deferred_context
{
  struct tr_deferred_context *next;
  struct tr_deferred_context *prev;

  TR_TRIGLIST *head;
  TR_TRIGLIST *tail;

  void *savepoint_id;
} TR_DEFERRED_CONTEXT;

/*
 * TR_ACTIVITY
 *
 * Note:
 *    This is a substructure of the TR_TRIGGER structure and is used
 *    to hold the state of the condition and action activities.
 *
 */

typedef struct tr_activity TR_ACTIVITY;
struct tr_activity
{
  DB_TRIGGER_ACTION type;
  DB_TRIGGER_TIME time;
  char *source;			/* source text (or string for PRINT) */
  void *parser;			/* parser for statement */
  void *statement;		/* PT_NODE* of statement */
  int exec_cnt;			/* number of executions */
};

/*
 * TR_STATE
 *
 * Note:
 *    This structure is used to pass trigger execution state between
 *    tr_before, tr_after, and tr_abort.
 *
 */

typedef struct tr_state
{
  TR_TRIGLIST *triggers;
} TR_STATE;

/*
 * TR_SCHEMA_CACHE
 *
 * Note:
 *    This structure is attached to class and attribute structures so that
 *    the triggers defined for a class can be quickly determined.
 *    The structure will be allocated of variable length depending on
 *    whether this will be attached to the class or an attribute.
 *    If it is attached to an attribute, we only allocate space for
 *    TR_MAX_ATTRIBUTE_TRIGGERS in the trigger array, otherwise we
 *    allocate TR_MAX_CLASS_TRIGGERS.
 *    Doing it this way simplifies the code because we don't have to
 *    duplicate everything just to handle two different array lengths.
 *
 */

typedef struct tr_schema_cache
{
  struct tr_schema_cache *next;	/* all caches maintained on a global list */
  DB_OBJLIST *objects;		/* flattened object list */
  short compiled;		/* flag set when object list is compiled */
  unsigned short array_length;	/* number of elements in array */

  TR_TRIGLIST *triggers[1];	/* will actually allocate of variable length */
} TR_SCHEMA_CACHE;

/*
 * TR_CACHE_TYPE
 *
 * Note:
 *    This is used by the functions that allocate class caches.  Rather
 *    than passing in the length, just specify the type of cache and the
 *    tr_ module will allocate it of the appropriate length.
 *
 */

typedef enum
{
  TR_CACHE_CLASS,
  TR_CACHE_ATTRIBUTE
} TR_CACHE_TYPE;

/* TRIGGER OBJECT ATTRIBUTES */
/*
 * Names of the trigger class and its attributes.
 */

extern const char *TR_CLASS_NAME;
extern const char *TR_ATT_NAME;
extern const char *TR_ATT_OWNER;
extern const char *TR_ATT_EVENT;
extern const char *TR_ATT_STATUS;
extern const char *TR_ATT_PRIORITY;
extern const char *TR_ATT_CLASS;
extern const char *TR_ATT_ATTRIBUTE;
extern const char *TR_ATT_CLASS_ATTRIBUTE;
extern const char *TR_ATT_CONDITION_TYPE;
extern const char *TR_ATT_CONDITION_TIME;
extern const char *TR_ATT_CONDITION;
extern const char *TR_ATT_ACTION_TYPE;
extern const char *TR_ATT_ACTION_TIME;
extern const char *TR_ATT_ACTION;
extern const char *TR_ATT_ACTION_OLD;
extern const char *TR_ATT_PROPERTIES;

extern int tr_Current_depth;
extern int tr_Maximum_depth;
extern bool tr_Invalid_transaction;
extern char tr_Invalid_transaction_trigger[SM_MAX_IDENTIFIER_LENGTH + 2];

extern bool tr_Trace;

extern TR_DEFERRED_CONTEXT *tr_Deferred_activities;
extern TR_DEFERRED_CONTEXT *tr_Deferred_activities_tail;

extern int tr_Recursion_level;
extern int tr_Recursion_level_max;

extern TR_TRIGLIST *tr_Deferred_triggers;
extern TR_TRIGLIST *tr_Deferred_triggers_tail;

/* INTERFACE FUNCTIONS */

/* Module control */

extern void tr_init (void);
extern void tr_final (void);
extern void tr_dump (FILE * fpp);	/* debug status */
extern int tr_install (void);

/* Global trigger firing state : enable/disable functions */

extern bool tr_get_execution_state (void);
extern bool tr_set_execution_state (bool new_state);

/* Trigger creation */

extern DB_OBJECT *tr_create_trigger (const char *name,
				     DB_TRIGGER_STATUS status,
				     double priority, DB_TRIGGER_EVENT event,
				     DB_OBJECT * class_,
				     const char *attribute,
				     DB_TRIGGER_TIME cond_time,
				     const char *cond_source,
				     DB_TRIGGER_TIME action_time,
				     DB_TRIGGER_ACTION action_type,
				     const char *action_source);

/* Trigger location */

extern int tr_find_all_triggers (DB_OBJLIST ** list);
extern DB_OBJECT *tr_find_trigger (const char *name);
extern int tr_find_event_triggers (DB_TRIGGER_EVENT event,
				   DB_OBJECT * class_, const char *attribute,
				   bool active, DB_OBJLIST ** list);

/* Check access rights */
extern int tr_check_authorization (DB_OBJECT * trigger_object,
				   int alter_flag);

/* Trigger modification */

extern int tr_drop_trigger (DB_OBJECT * obj, bool call_from_api);
extern int tr_rename_trigger (DB_OBJECT * trigger_object,
			      const char *name, bool call_from_api);

extern int tr_set_status (DB_OBJECT * trigger_object,
			  DB_TRIGGER_STATUS status, bool call_from_api);
extern int tr_set_priority (DB_OBJECT * trigger_object, double priority,
			    bool call_from_api);

/* Parameters */
extern int tr_get_depth (void);
extern int tr_set_depth (int depth);
extern int tr_get_trace (void);
extern int tr_set_trace (int trace);

/* Signaling */

extern int tr_prepare_statement (TR_STATE ** state_p,
				 DB_TRIGGER_EVENT event,
				 DB_OBJECT * class_, int attcount,
				 const char **attnames);
extern int tr_prepare (TR_STATE ** state_p, TR_TRIGLIST * triggers);
extern int tr_prepare_class (TR_STATE ** state_p,
			     TR_SCHEMA_CACHE * cache, DB_TRIGGER_EVENT event);

extern int tr_before_object (TR_STATE * state, DB_OBJECT * current,
			     DB_OBJECT * temp);
extern int tr_before (TR_STATE * state);
extern int tr_after_object (TR_STATE * state, DB_OBJECT * current,
			    DB_OBJECT * temp);
extern int tr_after (TR_STATE * state);

extern void tr_abort (TR_STATE * state);

/* Transaction manager trigger hooks */

extern int tr_check_commit_triggers (DB_TRIGGER_TIME time);
extern void tr_check_rollback_triggers (DB_TRIGGER_TIME time);
extern void tr_check_timeout_triggers (void);
extern void tr_check_abort_triggers (void);

extern int tr_set_savepoint (void *savepoint_id);
extern int tr_abort_to_savepoint (void *savepoint_id);

/* Deferred activity control */

extern int tr_execute_deferred_activities (DB_OBJECT * trigger_object,
					   DB_OBJECT * target);
extern int tr_drop_deferred_activities (DB_OBJECT * trigger_object,
					DB_OBJECT * target);

/* Trigger object accessors */

extern int tr_trigger_name (DB_OBJECT * trigger_object, char **name);
extern int tr_trigger_status (DB_OBJECT * trigger_object,
			      DB_TRIGGER_STATUS * status);
extern int tr_trigger_priority (DB_OBJECT * trigger_object, double *priority);
extern int tr_trigger_event (DB_OBJECT * trigger_object,
			     DB_TRIGGER_EVENT * event);
extern int tr_trigger_class (DB_OBJECT * trigger_object, DB_OBJECT ** class_);
extern int tr_trigger_attribute (DB_OBJECT * trigger_object,
				 char **attribute);
extern int tr_trigger_condition (DB_OBJECT * trigger_object,
				 char **condition);
extern int tr_trigger_condition_time (DB_OBJECT * trigger_object,
				      DB_TRIGGER_TIME * tr_time);
extern int tr_trigger_action (DB_OBJECT * trigger_object,
			      char **action);
extern int tr_trigger_action_time (DB_OBJECT * trigger_object,
				   DB_TRIGGER_TIME * tr_time);
extern int tr_trigger_action_type (DB_OBJECT * trigger_object,
				   DB_TRIGGER_ACTION * type);
extern int tr_is_trigger (DB_OBJECT * trigger_object, int *status);

/* Special schema functions */

extern TR_SCHEMA_CACHE *tr_make_schema_cache (TR_CACHE_TYPE type,
					      DB_OBJLIST * objects);
extern TR_SCHEMA_CACHE *tr_copy_schema_cache (TR_SCHEMA_CACHE * cache,
					      MOP filter_class);
extern int tr_merge_schema_cache (TR_SCHEMA_CACHE * destination,
				  TR_SCHEMA_CACHE * source);
extern int tr_empty_schema_cache (TR_SCHEMA_CACHE * cache);
extern void tr_free_schema_cache (TR_SCHEMA_CACHE * cache);

extern void tr_gc_schema_cache (TR_SCHEMA_CACHE * cache,
				void (*gcmarker) (MOP));
extern int tr_get_cache_objects (TR_SCHEMA_CACHE * cache, DB_OBJLIST ** list);
extern int tr_validate_schema_cache (TR_SCHEMA_CACHE * cache);

extern int tr_active_schema_cache (TR_SCHEMA_CACHE * cache);
extern int tr_delete_schema_cache (TR_SCHEMA_CACHE * cache,
				   DB_OBJECT * class_object);

extern int tr_add_cache_trigger (TR_SCHEMA_CACHE * cache,
				 DB_OBJECT * trigger_object);
extern int tr_drop_cache_trigger (TR_SCHEMA_CACHE * cache,
				  DB_OBJECT * trigger_object);


/* Shouldn't be external any more ? */

extern TR_TRIGGER *tr_map_trigger (DB_OBJECT * object, int fetch);
extern int tr_unmap_trigger (TR_TRIGGER * trigger);

/* Cache control */

extern int tr_update_user_cache (void);
extern void tr_invalidate_user_cache (void);

/* Migration and information functions */

extern const char *tr_time_as_string (DB_TRIGGER_TIME time);
extern const char *tr_event_as_string (DB_TRIGGER_EVENT event);
extern const char *tr_status_as_string (DB_TRIGGER_STATUS status);
extern int tr_dump_trigger (DB_OBJECT * trigger_object, FILE * fp,
			    int quoted_id_flag);
extern int tr_dump_all_triggers (FILE * fp, int quoted_id_flag);
extern int tr_dump_selective_triggers (FILE * fp, int quoted_id_flag,
				       DB_OBJLIST * classes);

extern void tr_free_trigger_list (TR_TRIGLIST * list);

#if 0
extern int tr_reset_schema_cache (TR_SCHEMA_CACHE * cache);
#endif /* 0 */

extern int tr_downcase_all_trigger_info (void);

#endif /* _TRIGGER_MANAGER_H_ */
