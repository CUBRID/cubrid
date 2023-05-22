/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#ifndef _TRIGGER_MANAGER_H_
#define _TRIGGER_MANAGER_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "memory_alloc.h"
#include "dbtype_def.h"
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

#define TR_MAX_RECURSION_LEVEL    	32

/*
 * TR_MAX_PRINT_STRING
 *
 * Note:
 *    Maximum length of string available for use in the PRINT action.
 *
 */

#define TR_MAX_PRINT_STRING 2048

/* free_and_init routine */
#define tr_free_schema_cache_and_init(cache) \
  do \
    { \
      tr_free_schema_cache ((cache)); \
      (cache) = NULL; \
    } \
  while (0)

typedef struct tr_trigger
{
  DB_OBJECT *owner;
  DB_OBJECT *object;
  char *name;
  double priority;
  DB_TRIGGER_STATUS status;
  DB_TRIGGER_EVENT event;
  DB_OBJECT *class_mop;
  char *attribute;

  struct tr_activity *condition;
  struct tr_activity *action;

  char *current_refname;
  char *temp_refname;
  int class_attribute;

  /*
   * copy of the "cache coherency number" from the instance, used
   * to detect when the object gets modified and the trigger cache
   * needs to be re-evaluated
   */
  int chn;
  const char *comment;
} TR_TRIGGER;



typedef struct tr_triglist
{
  struct tr_triglist *next;
  struct tr_triglist *prev;

  TR_TRIGGER *trigger;
  DB_OBJECT *target;		/* associated target instance */
} TR_TRIGLIST;

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

/* TR_RECURSION_DECISION */
/* When analyzing the stack searching for recursion (which is forbidden),
 * there are three scenarios:
 * 1. no recursive activity found: continue
 * 2. we found a recursive trigger and we must return an error
 * 3. we found a recursive STATEMENT trigger. This is supposed to happen, and
 *    we should not continue the trigger firing chain, but we should allow
 *    the transaction to go on.
 */
typedef enum
{
  TR_DECISION_CONTINUE,
  TR_DECISION_HALT_WITH_ERROR,
  TR_DECISION_DO_NOT_CONTINUE
} TR_RECURSION_DECISION;

/* TRIGGER OBJECT ATTRIBUTES */
/*
 * Names of the trigger class and its attributes.
 */

extern const char *TR_CLASS_NAME;
extern const char *TR_ATT_UNIQUE_NAME;
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
extern const char *TR_ATT_COMMENT;

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

extern DB_OBJECT *tr_create_trigger (const char *name, DB_TRIGGER_STATUS status, double priority,
				     DB_TRIGGER_EVENT event, DB_OBJECT * class_, const char *attribute,
				     DB_TRIGGER_TIME cond_time, const char *cond_source, DB_TRIGGER_TIME action_time,
				     DB_TRIGGER_ACTION action_type, const char *action_source, const char *comment);

/* Trigger location */

extern int tr_find_all_triggers (DB_OBJLIST ** list);
extern DB_OBJECT *tr_find_trigger (const char *name);
extern int tr_find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_, const char *attribute, bool active,
				   DB_OBJLIST ** list);

/* Check access rights */
extern int tr_check_authorization (DB_OBJECT * trigger_object, int alter_flag);

/* Trigger modification */

extern int tr_drop_trigger (DB_OBJECT * obj, bool call_from_api);
extern int tr_rename_trigger (DB_OBJECT * trigger_object, const char *name, bool call_from_api, bool deferred_flush);

extern int tr_set_status (DB_OBJECT * trigger_object, DB_TRIGGER_STATUS status, bool call_from_api);
extern int tr_set_priority (DB_OBJECT * trigger_object, double priority, bool call_from_api);
extern int tr_set_comment (DB_OBJECT * trigger_object, const char *comment, bool call_from_api);

/* Parameters */
extern int tr_get_depth (void);
extern int tr_set_depth (int depth);
extern int tr_get_trace (void);
extern int tr_set_trace (bool trace);

/* Signaling */

extern int tr_prepare_statement (TR_STATE ** state_p, DB_TRIGGER_EVENT event, DB_OBJECT * class_, int attcount,
				 const char **attnames);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int tr_prepare (TR_STATE ** state_p, TR_TRIGLIST * triggers);
#endif
extern int tr_prepare_class (TR_STATE ** state_p, TR_SCHEMA_CACHE * cache, MOP class_Mop, DB_TRIGGER_EVENT event);

extern int tr_before_object (TR_STATE * state, DB_OBJECT * current, DB_OBJECT * temp);
extern int tr_before (TR_STATE * state);
extern int tr_after_object (TR_STATE * state, DB_OBJECT * current, DB_OBJECT * temp);
extern int tr_after (TR_STATE * state);

extern void tr_abort (TR_STATE * state);

/* Transaction manager trigger hooks */

extern int tr_has_user_trigger (bool * has_user_trigger);
extern int tr_check_commit_triggers (DB_TRIGGER_TIME time);
extern void tr_check_rollback_triggers (DB_TRIGGER_TIME time);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void tr_check_timeout_triggers (void);
#endif
extern void tr_check_abort_triggers (void);

#if defined(ENABLE_UNUSED_FUNCTION)
extern int tr_set_savepoint (void *savepoint_id);
extern int tr_abort_to_savepoint (void *savepoint_id);
#endif

/* Deferred activity control */

extern int tr_execute_deferred_activities (DB_OBJECT * trigger_object, DB_OBJECT * target);
extern int tr_drop_deferred_activities (DB_OBJECT * trigger_object, DB_OBJECT * target);

/* Trigger object accessors */

extern int tr_trigger_name (DB_OBJECT * trigger_object, char **name);
extern int tr_trigger_status (DB_OBJECT * trigger_object, DB_TRIGGER_STATUS * status);
extern int tr_trigger_priority (DB_OBJECT * trigger_object, double *priority);
extern int tr_trigger_event (DB_OBJECT * trigger_object, DB_TRIGGER_EVENT * event);
extern int tr_trigger_class (DB_OBJECT * trigger_object, DB_OBJECT ** class_);
extern int tr_trigger_attribute (DB_OBJECT * trigger_object, char **attribute);
extern int tr_trigger_condition (DB_OBJECT * trigger_object, char **condition);
extern int tr_trigger_condition_time (DB_OBJECT * trigger_object, DB_TRIGGER_TIME * tr_time);
extern int tr_trigger_action (DB_OBJECT * trigger_object, char **action);
extern int tr_trigger_action_time (DB_OBJECT * trigger_object, DB_TRIGGER_TIME * tr_time);
extern int tr_trigger_action_type (DB_OBJECT * trigger_object, DB_TRIGGER_ACTION * type);
extern int tr_trigger_comment (DB_OBJECT * trigger_objet, char **comment);
extern int tr_is_trigger (DB_OBJECT * trigger_object, int *status);

/* Special schema functions */

extern TR_SCHEMA_CACHE *tr_make_schema_cache (TR_CACHE_TYPE type, DB_OBJLIST * objects);
extern TR_SCHEMA_CACHE *tr_copy_schema_cache (TR_SCHEMA_CACHE * cache, MOP filter_class);
extern int tr_merge_schema_cache (TR_SCHEMA_CACHE * destination, TR_SCHEMA_CACHE * source);
extern int tr_empty_schema_cache (TR_SCHEMA_CACHE * cache);
extern void tr_free_schema_cache (TR_SCHEMA_CACHE * cache);

extern int tr_get_cache_objects (TR_SCHEMA_CACHE * cache, DB_OBJLIST ** list);
extern int tr_validate_schema_cache (TR_SCHEMA_CACHE * cache, MOP class_mop);

extern int tr_active_schema_cache (MOP class_mop, TR_SCHEMA_CACHE * cache, DB_TRIGGER_EVENT event_type,
				   bool * has_event_type_triggers);
extern int tr_delete_schema_cache (TR_SCHEMA_CACHE * cache, DB_OBJECT * class_object);

extern int tr_add_cache_trigger (TR_SCHEMA_CACHE * cache, DB_OBJECT * trigger_object);
extern int tr_drop_cache_trigger (TR_SCHEMA_CACHE * cache, DB_OBJECT * trigger_object);

extern int tr_delete_triggers_for_class (TR_SCHEMA_CACHE ** cache, DB_OBJECT * class_object);


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
#if defined(ENABLE_UNUSED_FUNCTION)
extern int tr_dump_all_triggers (FILE * fp, bool quoted_id_flag);
#endif
extern void tr_free_trigger_list (TR_TRIGLIST * list);
extern const char *tr_get_class_name (void);

#if defined(ENABLE_UNUSED_FUNCTION)
extern int tr_reset_schema_cache (TR_SCHEMA_CACHE * cache);
extern int tr_downcase_all_trigger_info (void);
#endif

#endif /* _TRIGGER_MANAGER_H_ */
