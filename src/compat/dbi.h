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
 * dbi.h - Definitions and function prototypes for the CUBRID
 *         Application Program Interface (API).
 */

#ifndef _DBI_H_
#define _DBI_H_

#ident "$Id$"

#include <stdio.h>
#include <time.h>
#include "error_code.h"
#include "dbtype.h"
#include "dbdef.h"
#include "db_date.h"
#include "db_elo.h"
#include "db_query.h"
#include "databases_file.h"

#define db_utime_to_string db_timestamp_to_string
#define db_string_to_utime db_string_to_timestamp

/* the order to connect to db-hosts in databases.txt */
#define DB_CONNECT_ORDER_SEQ         0
#define DB_CONNECT_ORDER_RANDOM      1

/* abnormal DB host status */
#define DB_HS_NORMAL                    0x00000000
#define DB_HS_CONN_TIMEOUT              0x00000001
#define DB_HS_CONN_FAILURE              0x00000002
#define DB_HS_MISMATCHED_RW_MODE        0x00000004
#define DB_HS_HA_DELAYED                0x00000008
#define DB_HS_NON_PREFFERED_HOSTS       0x00000010
#define DB_HS_UNUSABLE_DATABASES        0x00000020

#define DB_HS_RECONNECT_INDICATOR \
  (DB_HS_MISMATCHED_RW_MODE | DB_HS_HA_DELAYED | DB_HS_NON_PREFFERED_HOSTS)

/* host status for marking abnormal host status */
typedef struct db_host_status DB_HOST_STATUS;
struct db_host_status
{
  char hostname[MAXHOSTNAMELEN];
  int status;
};

typedef struct db_host_status_list DB_HOST_STATUS_LIST;
struct db_host_status_list
{
  /* preferred_hosts + db-hosts */
  DB_HOST_STATUS hostlist[MAX_NUM_DB_HOSTS * 2];
  DB_HOST_STATUS *connected_host_status;
  int last_host_idx;
};

/* constants for db_include_oid */
enum
{ DB_NO_OIDS, DB_ROW_OIDS, DB_COLUMN_OIDS };

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
extern void db_set_server_session_key (const char *key);
extern char *db_get_server_session_key (void);
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
extern int db_checkpoint (void);
extern int db_freepgs (const char *vlabel);
extern int db_totalpgs (const char *vlabel);
extern char *db_vol_label (int volid, char *vol_fullname);
extern void db_warnspace (const char *vlabel);
extern int db_add_volume (const char *ext_path, const char *ext_name, const char *ext_comments, const int ext_npages,
			  const DB_VOLPURPOSE ext_purpose);
extern int db_add_volume_ex (DBDEF_VOL_EXT_INFO * ext_info);
#if 0
extern int db_del_volume_ex (VOLID volid, bool clear_cached);
#endif
extern int db_num_volumes (void);
extern int db_last_volume (void);
extern void db_print_stats (void);

extern void db_preload_classes (const char *name1, ...);
extern void db_link_static_methods (DB_METHOD_LINK * methods);
extern void db_unlink_static_methods (DB_METHOD_LINK * methods);
extern void db_flush_static_methods (void);

extern const char *db_error_string (int level);
extern int db_error_code (void);
extern int db_error_init (const char *logfile);

typedef void (*db_error_log_handler_t) (unsigned int);
extern db_error_log_handler_t db_register_error_log_handler (db_error_log_handler_t f);

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

extern int db_set_compare (const DB_VALUE * value1, const DB_VALUE * value2);
extern void db_value_fprint (FILE * fp, const DB_VALUE * value);

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
extern DB_OBJECT *db_dfind_unique (DB_OBJECT * classobj, DB_ATTDESC * attdesc, DB_VALUE * value, DB_FETCH_MODE purpose);
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

/* Class definition */
extern DB_OBJECT *db_create_class (const char *name);
extern DB_OBJECT *db_create_vclass (const char *name);
extern int db_drop_class (DB_OBJECT * classobj);
extern int db_drop_class_ex (DB_OBJECT * classobj, bool is_cascade_constraints);
extern int db_rename_class (DB_OBJECT * classobj, const char *new_name);
extern int db_truncate_class (DB_OBJECT * classobj);

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
extern void db_set_connect_order (int connect_order);
extern int db_get_client_type (void);
extern const char *db_get_type_name (DB_TYPE type_id);
extern DB_TYPE db_type_from_string (const char *name);
extern int db_get_schema_def_dbval (DB_VALUE * result, DB_VALUE * name_val);

extern void db_clear_host_status (void);
extern void db_set_host_status (char *hostname, int status);
extern void db_set_connected_host_status (char *host_connected);
extern bool db_need_reconnect (void);
extern bool db_need_ignore_repl_delay (void);
extern bool db_does_connected_host_have_status (int status);
extern int db_get_host_list_with_given_status (char **hostlist, int list_size, int status);
extern int db_get_delayed_hosts_count (void);
extern void db_clear_delayed_hosts_count (void);
extern void db_set_max_num_delayed_hosts_lookup (int max_num_delayed_hosts_lookup);
extern int db_get_max_num_delayed_hosts_lookup (void);

extern bool db_enable_trigger (void);
extern bool db_disable_trigger (void);

extern DB_OBJECT *db_find_class_of_index (const char *const index, const DB_CONSTRAINT_TYPE type);
extern DB_OBJECT *db_find_class (const char *name);
extern DB_OBJECT *db_find_class_with_purpose (const char *name, bool for_update);
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

extern int db_class_has_instance (DB_OBJECT * classobj);
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
extern const char *db_attribute_comment (DB_ATTRIBUTE * attribute);
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
extern int db_domain_collation_id (const DB_DOMAIN * domain);

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
extern DB_CONSTRAINT_TYPE db_constraint_type (const DB_CONSTRAINT * constraint);
extern const char *db_constraint_name (DB_CONSTRAINT * constraint);
extern DB_ATTRIBUTE **db_constraint_attributes (DB_CONSTRAINT * constraint);
extern const int *db_constraint_asc_desc (DB_CONSTRAINT * constraint);
extern const int *db_constraint_prefix_length (DB_CONSTRAINT * constraint);

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
extern int db_find_event_triggers (DB_TRIGGER_EVENT event, DB_OBJECT * class_obj, const char *attr, DB_OBJLIST ** list);
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
extern DB_CTMPL *dbt_copy_class (const char *new_name, const char *existing_name, SM_CLASS ** class_);
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
extern int dbt_reset_query_spec (DB_CTMPL * def);
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

/* query post-processing functions */
extern int db_query_plan_dump_file (char *filename);

/* sql query routines */
extern DB_SESSION *db_open_buffer (const char *buffer);
extern DB_SESSION *db_open_file (FILE * file);
extern DB_SESSION *db_open_file_name (const char *name);

extern int db_statement_count (DB_SESSION * session);

extern int db_compile_statement (DB_SESSION * session);
extern void db_rewind_statement (DB_SESSION * session);
extern int db_session_is_last_statement (DB_SESSION * session);

extern DB_SESSION_ERROR *db_get_errors (DB_SESSION * session);

extern DB_SESSION_ERROR *db_get_next_error (DB_SESSION_ERROR * errors, int *linenumber, int *columnnumber);

extern DB_SESSION_ERROR *db_get_warnings (DB_SESSION * session);

extern DB_SESSION_ERROR *db_get_next_warning (DB_SESSION_WARNING * errors, int *linenumber, int *columnnumber);
extern void db_session_set_holdable (DB_SESSION * session, bool holdable);
extern void db_session_set_xasl_cache_pinned (DB_SESSION * session, bool is_pinned, bool recompile);
extern void db_session_set_return_generated_keys (DB_SESSION * session, bool return_generated_keys);
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

extern void db_include_oid (DB_SESSION * session, int include_oid);

extern int db_push_values (DB_SESSION * session, int count, DB_VALUE * in_values);

extern int db_execute (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

extern int db_execute_oid (const char *CSQL_query, DB_QUERY_RESULT ** result, DB_QUERY_ERROR * query_error);

extern int db_query_produce_updatable_result (DB_SESSION * session, int stmtid);

extern int db_execute_statement (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);

extern int db_execute_and_keep_statement (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);
extern DB_CLASS_MODIFICATION_STATUS db_has_modified_class (DB_SESSION * session, int stmt_id);

extern void db_invalidate_mvcc_snapshot_before_statement (void);

extern void db_set_read_fetch_instance_version (LC_FETCH_VERSION_TYPE read_Fetch_Instance_Version);

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
extern int db_set_system_parameters_for_ha_repl (const char *data);
extern int db_reset_system_parameters_from_assignments (const char *data);
extern int db_get_system_parameters (char *data, int len);

extern char *db_get_host_connected (void);
extern int db_get_ha_server_state (char *buffer, int maxlen);

extern void db_clear_host_connected (void);
extern char *db_get_database_version (void);
#endif /* _DBI_H_ */
