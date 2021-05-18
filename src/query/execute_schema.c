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

/*
 * execute_schema.c
 */

#ident "$Id$"

#include "config.h"

#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#include "authenticate.h"
#include "error_manager.h"
#include "parser.h"
#include "parser_message.h"
#include "db.h"
#include "dbi.h"
#include "semantic_check.h"
#include "execute_schema.h"
#include "execute_statement.h"
#include "schema_manager.h"
#include "transaction_cl.h"
#include "system_parameter.h"
#if defined(WINDOWS)
#include "misc_string.h"
#endif /* WINDOWS */
#include "semantic_check.h"
#include "xasl_generation.h"
#include "memory_alloc.h"
#include "transform.h"
#include "set_object.h"
#include "object_accessor.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "memory_hash.h"
#include "locator_cl.h"
#include "network_interface_cl.h"
#include "view_transform.h"
#include "xasl_to_stream.h"
#include "parser_support.h"
#include "dbtype.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define UNIQUE_SAVEPOINT_ADD_ATTR_MTHD "aDDaTTRmTHD"
#define UNIQUE_SAVEPOINT_CREATE_ENTITY "cREATEeNTITY"
#define UNIQUE_SAVEPOINT_DROP_ENTITY "dROPeNTITY"
#define UNIQUE_SAVEPOINT_REPLACE_VIEW "rEPlACE"
#define UNIQUE_SAVEPOINT_RENAME "rENAME"
#define UNIQUE_SAVEPOINT_MULTIPLE_ALTER "mULTIPLEaLTER"
#define UNIQUE_SAVEPOINT_TRUNCATE "tRUnCATE"
#define UNIQUE_SAVEPOINT_CHANGE_ATTR "cHANGEaTTR"
#define UNIQUE_SAVEPOINT_ALTER_INDEX "aLTERiNDEX"
#define UNIQUE_SAVEPOINT_CHANGE_DEF_COLL "cHANGEdEFAULTcOLL"
#define UNIQUE_SAVEPOINT_CHANGE_TBL_COMMENT "cHANGEtBLcOMMENT"
#define UNIQUE_SAVEPOINT_CHANGE_COLUMN_COMMENT "cHANGEcOLUMNcOMMENT"
#define UNIQUE_SAVEPOINT_CREATE_USER_ENTITY "cREATEuSEReNTITY"
#define UNIQUE_SAVEPOINT_DROP_USER_ENTITY "dROPuSEReNTITY"
#define UNIQUE_SAVEPOINT_ALTER_USER_ENTITY "aLTERuSEReNTITY"
#define UNIQUE_SAVEPOINT_GRANT_USER "gRANTuSER"
#define UNIQUE_SAVEPOINT_REVOKE_USER "rEVOKEuSER"

#define QUERY_MAX_SIZE	1024 * 1024
#define MAX_FILTER_PREDICATE_STRING_LENGTH 255

typedef enum
{
  DO_INDEX_CREATE, DO_INDEX_DROP
} DO_INDEX;

typedef enum
{
  SM_ATTR_CHG_NOT_NEEDED = 0,
  SM_ATTR_CHG_ONLY_SCHEMA = 1,
  SM_ATTR_CHG_WITH_ROW_UPDATE = 2,
  SM_ATTR_CHG_BEST_EFFORT = 3	/* same as SM_ATTR_CHG_WITH_ROW_UPDATE, but there is a significant chance that the
				 * operation will fail */
} SM_ATTR_CHG_SOL;

/* The ATT_CHG_XXX enum bit flags describe the status of an attribute specific
 * property (sm_attr_properties_chg). Each property is initialized with
 * 'ATT_CHG_PROPERTY_NOT_CHECKED' value, and keeps it until is marked as
 * checked (by setting to zero) and then set corresponding bits.
 * '_OLD' and '_NEW' flags track simple presence of property in the attribute
 * existing schema and new definition, while the upper values flags are set
 * upon more cross-checkings. Some flags applies only to certain properties
 * (like '.._TYPE_..' for domain of attribute)
 * !! Values in enum should be kept in this order as some internal checks
 * rely on the order !!*/
enum
{
  /* property present in existing schema */
  ATT_CHG_PROPERTY_PRESENT_OLD = 0x1,
  /* property present in new attribute definition */
  ATT_CHG_PROPERTY_PRESENT_NEW = 0x2,
  /* present in OLD , lost in NEW */
  ATT_CHG_PROPERTY_LOST = 0x4,
  /* not present in OLD , gained in NEW */
  ATT_CHG_PROPERTY_GAINED = 0x8,
  /* property is not changed (not present in both current schema or new defition or present in both but not affected in
   * any way) */
  ATT_CHG_PROPERTY_UNCHANGED = 0x10,
  /* property is changed (i.e.: both present in old an new , but different) */
  ATT_CHG_PROPERTY_DIFF = 0x20,
  /* type : precision increase : varchar(2) -> varchar (10) */
  ATT_CHG_TYPE_PREC_INCR = 0x100,
  /* type : for COLLECTION or OBJECT (Class) type : the new SET is more general (the new OBJECT is a super-class) */
  ATT_CHG_TYPE_SET_CLS_COMPAT = 0x200,
  /* type : upgrade : int -> bigint */
  ATT_CHG_TYPE_UPGRADE = 0x400,
  /* type : changed, but needs checking if new domain supports all existing values , i.e : int -> char(3) */
  ATT_CHG_TYPE_NEED_ROW_CHECK = 0x800,
  /* type : pseudo-upgrade : datetime -> time: this is succesful as a cast, but may fail due to unique constraint */
  ATT_CHG_TYPE_PSEUDO_UPGRADE = 0x1000,
  /* type : not supported with existing configuration */
  ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG = 0x2000,
  /* type : upgrade : not supported */
  ATT_CHG_TYPE_NOT_SUPPORTED = 0x4000,
  /* property was not checked needs to be the highest value in enum */
  ATT_CHG_PROPERTY_NOT_CHECKED = 0x10000
};

/* Enum to access array from 'sm_attr_properties_chg' struct
 */
enum
{
  P_NAME = 0,			/* name of attribute */
  P_NOT_NULL,			/* constraint NOT NULL */
  P_DEFAULT_VALUE,		/* DEFAULT VALUE of attribute */
  P_ON_UPDATE_EXPR,		/* ON UPADTE default of attribute */
  P_CONSTR_CHECK,		/* constraint CHECK */
  P_DEFFERABLE,			/* DEFFERABLE */
  P_ORDER,			/* ORDERING definition */
  P_AUTO_INCR,			/* has AUTO INCREMENT */
  P_CONSTR_FK,			/* constraint FOREIGN KEY */
  P_S_CONSTR_PK,		/* constraint PRIMARY KEY only on one single column : the checked attribute */
  P_M_CONSTR_PK,		/* constraint PRIMARY KEY on more columns, including checked attribute */
  P_S_CONSTR_UNI,		/* constraint UNIQUE only on one single column : the checked attribute */
  P_M_CONSTR_UNI,		/* constraint UNIQUE on more columns, including checked attribute */
  P_CONSTR_NON_UNI,		/* has a non-unique index defined on it, should apply only for existing schema (as you
				 * cannot add a non-index with ALTER CHANGE) */
  P_PREFIX_INDEX,		/* has a prefix index defined on it, should apply only for existing schema (as you
				 * cannot add a prefix index with ALTER CHANGE) */
  P_TYPE,			/* type (domain) change */
  P_IS_PARTITION_COL,		/* class has partitions */
  P_COMMENT,			/* has comment */
  NUM_ATT_CHG_PROP
};

/* sm_attr_properties_chg :
 * structure used for checking existing attribute definition (from schema)
 * and new attribute definition
 * Array is accessed using enum values define above.
 */
typedef struct sm_attr_properties_chg SM_ATTR_PROP_CHG;
struct sm_attr_properties_chg
{
  int p[NUM_ATT_CHG_PROP];	/* 'change' property */
  SM_CONSTRAINT_INFO *constr_info;
  SM_CONSTRAINT_INFO *new_constr_info;
  int att_id;
  SM_NAME_SPACE name_space;	/* class, shared or normal attribute */
  SM_NAME_SPACE new_name_space;	/* class, shared or normal attribute */
  bool class_has_subclass;	/* if class is part of a hierarchy and if it has subclasses */
};

/* sm_partition_alter_info :
 * helper object for altering partitioning
 */
typedef struct sm_partition_alter_info SM_PARTITION_ALTER_INFO;
struct sm_partition_alter_info
{
  MOP root_op;			/* MOP of the root class */
  DB_CTMPL *root_tmpl;		/* template of the root class */
  char keycol[DB_MAX_IDENTIFIER_LENGTH];	/* partition key column */
  char **promoted_names;	/* promoted partition names */
  int promoted_count;		/* number of promoted partition */
};

/* part_class_info :
 * structure used for creating partitions for a class
 */
typedef struct part_class_info PART_CLASS_INFO;
struct part_class_info
{
  char *pname;			/* partition name */
  DB_CTMPL *temp;		/* partition class template for */
  DB_OBJECT *obj;		/* partition class obj */
  PART_CLASS_INFO *next;
};

/* db_value_slist :
 * partition range definition list
 */
typedef struct db_value_slist DB_VALUE_SLIST;
struct db_value_slist
{
  struct db_value_slist *next;
  SM_PARTITION *partition;	/* partition info */
  MOP class_obj;		/* class object */
  DB_VALUE *min;		/* min range for partition */
  DB_VALUE *max;		/* max range for partition */
};

static int drop_class_name (const char *name, bool is_cascade_constraints);

static int do_alter_one_clause_with_template (PARSER_CONTEXT * parser, PT_NODE * alter);
static int do_alter_clause_rename_entity (PARSER_CONTEXT * const parser, PT_NODE * const alter);
static int do_alter_clause_add_index (PARSER_CONTEXT * const parser, PT_NODE * const alter);
static int do_alter_clause_drop_index (PARSER_CONTEXT * const parser, PT_NODE * const alter);
static int do_alter_change_auto_increment (PARSER_CONTEXT * const parser, PT_NODE * const alter);

static int do_rename_internal (const char *const old_name, const char *const new_name);
static DB_CONSTRAINT_TYPE get_reverse_unique_index_type (const bool is_reverse, const bool is_unique);
static int create_or_drop_index_helper (PARSER_CONTEXT * parser, const char *const constraint_name,
					const bool is_reverse, const bool is_unique, PT_NODE * spec,
					PT_NODE * column_names, PT_NODE * column_prefix_length,
					PT_NODE * filter_predicate, int func_index_pos, int func_index_args_count,
					PT_NODE * function_expr, PT_NODE * comment, DB_OBJECT * const obj,
					SM_INDEX_STATUS index_status, DO_INDEX do_index);
static int update_locksets_for_multiple_rename (const char *class_name, int *num_mops, MOP * mop_set, int *num_names,
						char **name_set, bool error_on_misssing_class);
static int acquire_locks_for_multiple_rename (const PT_NODE * statement);

static int validate_attribute_domain (PARSER_CONTEXT * parser, PT_NODE * attribute, const bool check_zero_precision);
static SM_FOREIGN_KEY_ACTION map_pt_to_sm_action (const PT_MISC_TYPE action);
static int add_foreign_key (DB_CTMPL * ctemplate, const PT_NODE * cnstr, const char **att_names);

static int add_union_query (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * query);
static int add_query_to_virtual_class (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * queries);
static int do_copy_indexes (PARSER_CONTEXT * parser, MOP classmop, SM_CLASS * src_class);

static int do_recreate_renamed_class_indexes (const PARSER_CONTEXT * parser, const char *const old_class_name,
					      const char *const class_name);

static int do_alter_clause_change_attribute (PARSER_CONTEXT * const parser, PT_NODE * const alter);

static int do_alter_change_owner (PARSER_CONTEXT * const parser, PT_NODE * const alter);

static int do_alter_change_default_cs_coll (PARSER_CONTEXT * const parser, PT_NODE * const alter);

static int do_alter_change_tbl_comment (PARSER_CONTEXT * const parser, PT_NODE * const alter);
static int do_alter_change_col_comment (PARSER_CONTEXT * const parser, PT_NODE * const alter);

static int do_change_att_schema_only (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attribute,
				      PT_NODE * old_name_node, PT_NODE * constraints, SM_ATTR_PROP_CHG * attr_chg_prop,
				      SM_ATTR_CHG_SOL * change_mode);

static int build_attr_change_map (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attr_def,
				  PT_NODE * attr_old_name, PT_NODE * constraints,
				  SM_ATTR_PROP_CHG * attr_chg_properties);

static int build_att_type_change_map (TP_DOMAIN * curr_domain, TP_DOMAIN * req_domain,
				      SM_ATTR_PROP_CHG * attr_chg_properties);
static int build_att_coll_change_map (TP_DOMAIN * curr_domain, TP_DOMAIN * req_domain,
				      SM_ATTR_PROP_CHG * attr_chg_properties);

static int check_att_chg_allowed (const char *att_name, const PT_TYPE_ENUM t, const SM_ATTR_PROP_CHG * attr_chg_prop,
				  SM_ATTR_CHG_SOL chg_how, bool log_error_allowed, bool * new_attempt);

static bool is_att_property_structure_checked (const SM_ATTR_PROP_CHG * attr_chg_properties);

static bool is_att_change_needed (const SM_ATTR_PROP_CHG * attr_chg_properties);

static void reset_att_property_structure (SM_ATTR_PROP_CHG * attr_chg_properties);

static bool is_att_prop_set (const int prop, const int value);

static int get_att_order_from_def (PT_NODE * attribute, bool * ord_first, const char **ord_after_name);

static int check_default_on_update_clause (PARSER_CONTEXT * parser, PT_NODE * attribute);

static int get_att_default_from_def (PARSER_CONTEXT * parser, PT_NODE * attribute, DB_VALUE ** default_value,
				     const char *classname);

static int do_update_new_notnull_cols_without_default (PARSER_CONTEXT * parser, PT_NODE * alter, MOP class_mop);

static int do_update_new_cols_with_default_expression (PARSER_CONTEXT * parser, PT_NODE * alter, MOP class_mop);

static int do_run_update_query_for_new_notnull_fields (PARSER_CONTEXT * parser, PT_NODE * alter, PT_NODE * attr_list,
						       int attr_count, MOP class_mop);

static bool is_attribute_primary_key (const char *class_name, const char *attr_name);

static const char *get_hard_default_for_type (PT_TYPE_ENUM type);

static int do_run_upgrade_instances_domain (PARSER_CONTEXT * parser, OID * p_class_oid, int att_id);

static int do_drop_att_constraints (MOP class_mop, SM_CONSTRAINT_INFO * constr_info_list);

static int do_recreate_att_constraints (MOP class_mop, SM_CONSTRAINT_INFO * constr_info_list);

static int check_change_attribute (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attribute,
				   PT_NODE * old_name_node, PT_NODE ** pointer_constraints,
				   SM_ATTR_PROP_CHG * attr_chg_prop, SM_ATTR_CHG_SOL * change_mode);

static int check_change_class_collation (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_ALTER_INFO * alter,
					 bool * need_update, int *collation_id);

static int sort_constr_info_list (SM_CONSTRAINT_INFO ** source);
static int save_constraint_info_from_pt_node (SM_CONSTRAINT_INFO ** save_info, const PT_NODE * const pt_constr);

static int do_run_update_query_for_class (char *query, MOP class_mop, int *row_count);
static SM_FUNCTION_INFO *pt_node_to_function_index (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * sort_spec,
						    DO_INDEX do_index);

static int do_create_partition_constraints (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_create_partition_constraint (PT_NODE * alter, SM_CLASS * root_class, SM_CLASS_CONSTRAINT * constraint,
					   SM_PARTITION_ALTER_INFO * pinfo);
static int do_alter_partitioning_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_alter_partitioning_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_create_partition (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_remove_partition_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_remove_partition_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_coalesce_partition_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_coalesce_partition_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_reorganize_partition_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_reorganize_partition_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_promote_partition_list (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
static int do_promote_partition_by_name (const char *class_name, const char *part_num, char **partition_name);
static int do_promote_partition (SM_CLASS * class_);
#if defined (ENABLE_UNUSED_FUNCTION)
static int do_analyze_partition (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo);
#endif
static int do_redistribute_partitions_data (const char *class_name, const char *keyname, char **promoted,
					    int promoted_count, PT_ALTER_CODE alter_op, bool should_update,
					    bool should_insert);
static SM_FUNCTION_INFO *compile_partition_expression (PARSER_CONTEXT * parser, PT_NODE * entity_name, PT_NODE * pinfo);
static PT_NODE *replace_names_alter_chg_attr (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
					      int *continue_walk);
static PT_NODE *pt_replace_names_index_expr (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg,
					     int *continue_walk);
static int adjust_partition_range (DB_OBJLIST * objs);
static int adjust_partition_size (MOP class_, DB_CTMPL * tmpl);

static const char *get_attr_name (PT_NODE * attribute);
static int do_add_attribute (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * atts, PT_NODE * constraints,
			     bool error_on_not_normal);
static int do_add_attribute_from_select_column (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, DB_QUERY_TYPE * column);
static DB_QUERY_TYPE *query_get_column_with_name (DB_QUERY_TYPE * query_columns, const char *name);
static PT_NODE *get_attribute_with_name (PT_NODE * atts, const char *name);
static PT_NODE *create_select_to_insert_into (PARSER_CONTEXT * parser, const char *class_name, PT_NODE * create_select,
					      PT_CREATE_SELECT_ACTION create_select_action,
					      DB_QUERY_TYPE * query_columns);
static int execute_create_select_query (PARSER_CONTEXT * parser, const char *const class_name, PT_NODE * create_select,
					PT_CREATE_SELECT_ACTION create_select_action, DB_QUERY_TYPE * query_columns,
					PT_NODE * flagged_statement);

static int do_find_auto_increment_serial (MOP * auto_increment_obj, const char *class_name, const char *attr_name);
static int do_check_fk_constraints_internal (DB_CTMPL * ctemplate, PT_NODE * constraints, bool is_partitioned);

static int get_index_type_qualifiers (MOP obj, bool * is_reverse, bool * is_unique, const char *index_name);
static SM_PARTITION *pt_node_to_partition_info (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * entity_name,
						char *class_name, char *partition_name, DB_VALUE * minval);
static int do_save_all_indexes (MOP classmop, SM_CONSTRAINT_INFO ** saved_index_info_listpp);
static int do_drop_saved_indexes (MOP classmop, SM_CONSTRAINT_INFO * index_save_info);
static int do_recreate_saved_indexes (MOP classmop, SM_CONSTRAINT_INFO * index_save_info);

static int do_alter_index_status (PARSER_CONTEXT * parser, const PT_NODE * statement);

int ib_thread_count = 0;

/*
 * Function Group :
 * DO functions for alter statement
 *
 */

/*
 * do_alter_one_clause_with_template() - Executes the operations required by a
 *                                       single ALTER clause.
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a single clause of an ALTER statement. Not
 *                  all possible clauses are handled by this function; see the
 *                  note below and the do_alter() function.
 *
 * Note: This function handles clauses that require class template operations.
 *       It always calls dbt_edit_class(). Other ALTER clauses might have
 *       dedicated processing functions. See do_alter() for details.
 */
static int
do_alter_one_clause_with_template (PARSER_CONTEXT * parser, PT_NODE * alter)
{
  const char *entity_name, *new_query;
  const char *attr_name, *mthd_name, *mthd_file, *attr_mthd_name;
  const char *new_name, *old_name, *domain;
  DB_CTMPL *ctemplate = NULL;
  DB_OBJECT *vclass, *sup_class;
  int error = NO_ERROR;
  DB_ATTRIBUTE *found_attr, *def_attr;
  DB_METHOD *found_mthd;
  TP_DOMAIN *def_domain;
  DB_VALUE src_val, dest_val;
  DB_TYPE db_desired_type;
  int query_no, class_attr;
  PT_NODE *vlist, *p, *n, *d;
  PT_NODE *node, *nodelist;
  PT_NODE *data_type, *data_default, *path;
  PT_NODE *slist;
  PT_TYPE_ENUM pt_desired_type;
  PT_NODE *temp_val, *def_val, *initial_def_val = NULL;
#if 0
  HFID *hfid;
#endif
  SM_PARTITION_ALTER_INFO pinfo;
  bool partition_savepoint = false;
  const PT_ALTER_CODE alter_code = alter->info.alter.code;
#if defined (ENABLE_RENAME_CONSTRAINT)
  SM_CONSTRAINT_FAMILY constraint_family;
#endif
  unsigned int save_custom;
  PT_NODE *super_node = NULL;
  MOP super_class;
  DB_DEFAULT_EXPR default_expr;

  entity_name = alter->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      ERROR1 (error, ER_UNEXPECTED, "Expecting a class or virtual class name.");
      return error;
    }

  vclass = db_find_class (entity_name);
  if (vclass == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  db_make_null (&src_val);
  db_make_null (&dest_val);

  ctemplate = dbt_edit_class (vclass);
  if (ctemplate == NULL)
    {
      /* when dbt_edit_class fails (e.g. because the server unilaterally aborts us), we must record the associated
       * error message into the parser.  Otherwise, we may get a confusing error msg of the form: "so_and_so is not a
       * class". */
      pt_record_error (parser, parser->statement_number - 1, alter->line_number, alter->column_number, er_msg (), NULL);
      return er_errid ();
    }

  switch (alter_code)
    {
    case PT_ADD_QUERY:
      error = do_add_queries (parser, ctemplate, alter->info.alter.alter_clause.query.query);
      if (error != NO_ERROR)
	{
	  break;
	}

      /* set vclass comment if it exists */
      if (alter->info.alter.alter_clause.query.view_comment != NULL)
	{
	  error = do_alter_change_tbl_comment (parser, alter);
	}
      break;

    case PT_DROP_QUERY:
      vlist = alter->info.alter.alter_clause.query.query_no_list;
      if (vlist == NULL)
	{
	  error = dbt_drop_query_spec (ctemplate, 1);
	}
      else if (vlist->next == NULL)
	{			/* ie, only one element in list */
	  error = dbt_drop_query_spec (ctemplate, vlist->info.value.data_value.i);
	}
      else
	{
	  slist = pt_sort_in_desc_order (vlist);
	  for (; slist; slist = slist->next)
	    {
	      error = dbt_drop_query_spec (ctemplate, slist->info.value.data_value.i);
	      if (error != NO_ERROR)
		{
		  break;
		}
	    }
	}
      break;

    case PT_MODIFY_QUERY:
      if (alter->info.alter.alter_clause.query.query_no_list)
	{
	  query_no = alter->info.alter.alter_clause.query.query_no_list->info.value.data_value.i;
	}
      else
	{
	  query_no = 1;
	}
      save_custom = parser->custom_print;
      parser->custom_print |= PT_CHARSET_COLLATE_FULL;
      new_query = parser_print_tree_with_quotes (parser, alter->info.alter.alter_clause.query.query);
      parser->custom_print = save_custom;
      error = dbt_change_query_spec (ctemplate, new_query, query_no);
      if (error != NO_ERROR)
	{
	  break;
	}

      /* set vclass comment if it exists */
      if (alter->info.alter.alter_clause.query.view_comment != NULL)
	{
	  error = do_alter_change_tbl_comment (parser, alter);
	}
      break;

    case PT_ADD_ATTR_MTHD:
#if 0
      /* we currently core dump when adding a unique constraint at the same time as an attribute, whether the unique
       * constraint is on the new attribute or another. Therefore we temporarily disallow adding a unique constraint
       * and an attribute in the same alter statement if the class has or has had any instances. Note that we should be
       * checking for instances in the entire subhierarchy, not just the current class. */
      if ((hfid = sm_get_ch_heap (vclass)) && !HFID_IS_NULL (hfid)
	  && alter->info.alter.alter_clause.attr_mthd.attr_def_list)
	{
	  for (p = alter->info.alter.constraint_list; p != NULL; p = p->next)
	    {
	      if (p->info.constraint.type == PT_CONSTRAIN_UNIQUE)
		{
		  error = ER_DO_ALTER_ADD_WITH_UNIQUE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  (void) dbt_abort_class (ctemplate);
		  return error;
		}
	    }
	  PT_END;
	}
#endif
      error = tran_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
      if (error == NO_ERROR)
	{
	  error =
	    do_add_attributes (parser, ctemplate, alter->info.alter.alter_clause.attr_mthd.attr_def_list,
			       alter->info.alter.constraint_list, NULL);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }


	  vclass = dbt_finish_class (ctemplate);
	  if (vclass == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      dbt_abort_class (ctemplate);
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  ctemplate = dbt_edit_class (vclass);
	  if (ctemplate == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  error = do_add_constraints (ctemplate, alter->info.alter.constraint_list);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  error = do_check_fk_constraints (ctemplate, alter->info.alter.constraint_list);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      (void) tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  if (alter->info.alter.alter_clause.attr_mthd.mthd_def_list != NULL)
	    {
	      error = do_add_methods (parser, ctemplate, alter->info.alter.alter_clause.attr_mthd.mthd_def_list);
	    }
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  if (alter->info.alter.alter_clause.attr_mthd.mthd_file_list != NULL)
	    {
	      error = do_add_method_files (parser, ctemplate, alter->info.alter.alter_clause.attr_mthd.mthd_file_list);
	    }
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	      return error;
	    }

	  assert (alter->info.alter.create_index == NULL);
	}
      break;

    case PT_RESET_QUERY:
      {
	DB_ATTRIBUTE *cur_attr = db_get_attributes (vclass);

	assert (db_get_subclasses (vclass) == NULL);
	assert (db_get_superclasses (vclass) == NULL);

	/* drop all attributes */
	while (cur_attr)
	  {
	    assert (cur_attr->header.name != NULL);
	    error = dbt_drop_attribute (ctemplate, cur_attr->header.name);
	    if (error != NO_ERROR)
	      {
		goto reset_query_error;
	      }
	    cur_attr = db_attribute_next (cur_attr);
	  }

	/* also drop any query specs there may have been */
	error = dbt_reset_query_spec (ctemplate);
	if (error != NO_ERROR)
	  {
	    goto reset_query_error;
	  }

	/* add the new attributes */
	error =
	  do_add_attributes (parser, ctemplate, alter->info.alter.alter_clause.query.attr_def_list,
			     alter->info.alter.constraint_list, NULL);
	if (error != NO_ERROR)
	  {
	    goto reset_query_error;
	  }

	/* and add the single query spec we allow */
	error = do_add_queries (parser, ctemplate, alter->info.alter.alter_clause.query.query);
	if (error != NO_ERROR)
	  {
	    goto reset_query_error;
	  }

	/* set vclass comment if it exists */
	if (alter->info.alter.alter_clause.query.view_comment != NULL)
	  {
	    error = do_alter_change_tbl_comment (parser, alter);
	  }
	if (error != NO_ERROR)
	  {
	    goto reset_query_error;
	  }

	break;

      reset_query_error:
	dbt_abort_class (ctemplate);
	return error;
      }
      break;

    case PT_DROP_ATTR_MTHD:
      p = alter->info.alter.alter_clause.attr_mthd.attr_mthd_name_list;
      for (; p && p->node_type == PT_NAME; p = p->next)
	{
	  attr_mthd_name = p->info.name.original;
	  if (p->info.name.meta_class == PT_META_ATTR)
	    {
	      found_attr = db_get_class_attribute (vclass, attr_mthd_name);
	      if (found_attr)
		{
		  error = dbt_drop_class_attribute (ctemplate, attr_mthd_name);
		}
	      else
		{
		  found_mthd = db_get_class_method (vclass, attr_mthd_name);
		  if (found_mthd)
		    {
		      error = dbt_drop_class_method (ctemplate, attr_mthd_name);
		    }
		}
	    }
	  else
	    {
	      found_attr = db_get_attribute (vclass, attr_mthd_name);
	      if (found_attr)
		{
		  error = dbt_drop_attribute (ctemplate, attr_mthd_name);
		}
	      else
		{
		  found_mthd = db_get_method (vclass, attr_mthd_name);
		  if (found_mthd)
		    {
		      error = dbt_drop_method (ctemplate, attr_mthd_name);
		    }
		}
	    }

	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      return error;
	    }
	}

      p = alter->info.alter.alter_clause.attr_mthd.mthd_file_list;
      for (;
	   p && p->node_type == PT_FILE_PATH && (path = p->info.file_path.string) != NULL && path->node_type == PT_VALUE
	   && (path->type_enum == PT_TYPE_VARCHAR || path->type_enum == PT_TYPE_CHAR || path->type_enum == PT_TYPE_NCHAR
	       || path->type_enum == PT_TYPE_VARNCHAR); p = p->next)
	{
	  mthd_file = (char *) path->info.value.data_value.str->bytes;
	  error = dbt_drop_method_file (ctemplate, mthd_file);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      return error;
	    }
	}

      break;

    case PT_MODIFY_ATTR_MTHD:
      p = alter->info.alter.alter_clause.attr_mthd.attr_def_list;
      for (; p && p->node_type == PT_ATTR_DEF; p = p->next)
	{
	  attr_name = p->info.attr_def.attr_name->info.name.original;
	  if (p->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      class_attr = 1;
	    }
	  else
	    {
	      class_attr = 0;
	    }
	  data_type = p->data_type;

	  domain = pt_node_to_db_domain_name (p);
	  error = dbt_change_domain (ctemplate, attr_name, class_attr, domain);

	  if (data_type && pt_is_set_type (p))
	    {
	      nodelist = data_type->data_type;
	      for (node = nodelist; node != NULL; node = node->next)
		{
		  domain = pt_data_type_to_db_domain_name (node);
		  error = dbt_add_set_attribute_domain (ctemplate, attr_name, class_attr, domain);
		  if (error != NO_ERROR)
		    {
		      dbt_abort_class (ctemplate);
		      return error;
		    }
		}
	    }

	  data_default = p->info.attr_def.data_default;
	  if (data_default != NULL && data_default->node_type == PT_DATA_DEFAULT)
	    {
	      pt_desired_type = p->type_enum;

	      if (pt_desired_type == PT_TYPE_BLOB || pt_desired_type == PT_TYPE_CLOB)
		{
		  error = ER_INTERFACE_NOT_SUPPORTED_OPERATION;
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
		  break;
		}

	      /* try to coerce the default value into the attribute's type */
	      d = data_default->info.data_default.default_value;
	      d = pt_semantic_check (parser, d);
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  dbt_abort_class (ctemplate);
		  return er_errid ();
		}

	      if (d != NULL)
		{
		  error = pt_coerce_value (parser, d, d, pt_desired_type, p->data_type);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		}

	      pt_evaluate_tree (parser, d, &dest_val, 1);
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  dbt_abort_class (ctemplate);
		  return er_errid ();
		}

	      error = dbt_change_default (ctemplate, attr_name, class_attr, &dest_val);
	      if (error != NO_ERROR)
		{
		  dbt_abort_class (ctemplate);
		  return error;
		}
	    }
	}

      /* the order in which methods are defined will change; currently there's no way around this problem. */
      p = alter->info.alter.alter_clause.attr_mthd.mthd_def_list;
      for (; p && p->node_type == PT_METHOD_DEF; p = p->next)
	{
	  mthd_name = p->info.method_def.method_name->info.name.original;
	  error = dbt_drop_method (ctemplate, mthd_name);
	  if (error == NO_ERROR)
	    {
	      error = do_add_methods (parser, ctemplate, alter->info.alter.alter_clause.attr_mthd.mthd_def_list);
	    }
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      return error;
	    }
	}
      break;

    case PT_ADD_SUPCLASS:
      /* here we need to check if the super classes are partitioned, as it is not allowed to inherit from partition
       * tables. */
      assert (error == NO_ERROR);
      super_node = alter->info.alter.super.sup_class_list;

      while (super_node)
	{
	  super_class = db_find_class (super_node->info.name.original);

	  if (super_class == NULL)
	    {
	      error = er_errid ();
	      assert (error != NO_ERROR);
	    }
	  else
	    {
	      error = sm_is_partitioned_class (super_class);
	      if (error > 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INHERIT_FROM_PARTITION_TABLE, 0);
		  error = er_errid ();
		}
	    }

	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (ctemplate);
	      return error;
	    }

	  super_node = super_node->next;
	}

      error = do_add_supers (parser, ctemplate, alter->info.alter.super.sup_class_list);
      break;

    case PT_DROP_SUPCLASS:
      for (p = alter->info.alter.super.sup_class_list; p && p->node_type == PT_NAME; p = p->next)
	{
	  sup_class = db_find_class (p->info.name.original);
	  if (sup_class == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      error = dbt_drop_super (ctemplate, sup_class);
	    }
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}
      break;

    case PT_DROP_RESOLUTION:
      for (p = alter->info.alter.super.resolution_list; p && p->node_type == PT_RESOLUTION; p = p->next)
	{
	  sup_class = db_find_class (p->info.resolution.of_sup_class_name->info.name.original);
	  attr_mthd_name = p->info.resolution.attr_mthd_name->info.name.original;
	  error = dbt_drop_resolution (ctemplate, sup_class, attr_mthd_name);
	  if (error != NO_ERROR)
	    {
	      (void) dbt_abort_class (ctemplate);
	      return error;
	    }
	}
      break;

    case PT_MODIFY_DEFAULT:
    case PT_ALTER_DEFAULT:
      n = alter->info.alter.alter_clause.ch_attr_def.attr_name_list;
      d = alter->info.alter.alter_clause.ch_attr_def.data_default_list;
      for (; n && d; n = n->next, d = d->next)
	{
	  /* try to coerce the default value into the attribute's type */
	  d = pt_semantic_check (parser, d);
	  if (d == NULL)
	    {
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  error = er_errid ();
		}
	      else
		{
		  error = ER_GENERIC_ERROR;
		}
	      break;
	    }

	  attr_name = n->info.name.original;
	  if (n->info.name.meta_class == PT_META_ATTR)
	    {
	      def_attr = db_get_class_attribute (vclass, attr_name);
	    }
	  else
	    {
	      def_attr = db_get_attribute (vclass, attr_name);
	    }
	  if (!def_attr || !(def_domain = db_attribute_domain (def_attr)))
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      break;
	    }
	  db_desired_type = TP_DOMAIN_TYPE (def_domain);
	  pt_desired_type = (PT_TYPE_ENUM) pt_db_to_type_enum (db_desired_type);
	  data_type = pt_domain_to_data_type (parser, def_domain);
	  if (data_type == NULL)
	    {
	      PT_ERRORm (parser, n, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      error = ER_FAILED;
	      break;
	    }

	  def_val = d->info.data_default.default_value;
	  if (d->info.data_default.default_expr_type == DB_DEFAULT_NONE)
	    {
	      initial_def_val = parser_copy_tree (parser, def_val);
	      if (initial_def_val == NULL)
		{
		  error = ER_FAILED;
		  break;
		}
	      error = pt_coerce_value_for_default_value (parser, def_val, def_val, pt_desired_type, data_type,
							 d->info.data_default.default_expr_type);
	      if (error != NO_ERROR)
		{
		  if (pt_has_error (parser))
		    {
		      /* forget previous one to set the better error */
		      pt_reset_error (parser);
		    }

		  if (error == ER_IT_DATA_OVERFLOW)
		    {
		      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OVERFLOW_COERCING_TO,
				   pt_short_print (parser, initial_def_val), pt_short_print (parser, data_type));
		    }
		  else
		    {
		      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
				   pt_short_print (parser, initial_def_val), pt_short_print (parser, data_type));
		    }

		  parser_free_tree (parser, data_type);
		  break;
		}

	      pt_evaluate_tree (parser, d->info.data_default.default_value, &src_val, 1);

	      /* Fix CUBRIDSUS-8035. FOR Primary Key situation, we will throw another ERROR in function
	       * dbt_change_default, so I excluded it from here. */
	      if (DB_IS_NULL (&src_val) && (def_attr->flags & SM_ATTFLAG_NON_NULL)
		  && !(def_attr->flags & SM_ATTFLAG_PRIMARY_KEY))
		{
		  db_value_clear (&src_val);
		  parser_free_tree (parser, data_type);
		  ERROR1 (error, ER_CANNOT_HAVE_NOTNULL_DEFAULT_NULL, attr_name);
		  break;
		}

	      if (n->info.name.meta_class == PT_META_ATTR)
		{
		  error = dbt_change_default (ctemplate, attr_name, 1, &src_val);
		}
	      else
		{
		  error = dbt_change_default (ctemplate, attr_name, 0, &src_val);
		}
	    }
	  else
	    {
	      def_val = pt_semantic_type (parser, def_val, NULL);
	      if (pt_has_error (parser) || def_val == NULL)
		{
		  parser_free_tree (parser, data_type);
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  error = er_errid ();
		  break;
		}

	      pt_evaluate_tree_having_serial (parser, def_val, &src_val, 1);
	      if (pt_has_error (parser))
		{
		  parser_free_tree (parser, data_type);
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  error = er_errid ();
		  break;
		}

	      temp_val = pt_dbval_to_value (parser, &src_val);
	      if (temp_val == NULL)
		{
		  parser_free_tree (parser, data_type);
		  db_value_clear (&src_val);
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		  error = er_errid ();
		  break;
		}

	      error = pt_coerce_value_for_default_value (parser, temp_val, temp_val, pt_desired_type, data_type,
							 d->info.data_default.default_expr_type);
	      db_value_clear (&src_val);
	      temp_val->info.value.db_value_is_in_workspace = 0;
	      parser_free_node (parser, temp_val);
	      if (error != NO_ERROR)
		{
		  if (pt_has_error (parser))
		    {
		      /* forget previous one to set the better error */
		      pt_reset_error (parser);
		    }

		  if (error == ER_IT_DATA_OVERFLOW)
		    {
		      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OVERFLOW_COERCING_TO,
				   pt_short_print (parser, def_val), pt_short_print (parser, data_type));
		    }
		  else
		    {
		      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
				   pt_short_print (parser, def_val), pt_short_print (parser, data_type));
		    }
		  parser_free_tree (parser, data_type);
		  break;
		}

	      pt_get_default_expression_from_data_default_node (parser, d, &default_expr);
	      smt_set_attribute_default (ctemplate, attr_name, 0, &src_val, &default_expr);
	    }
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      error = er_errid ();
	      break;
	    }

	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  parser_free_tree (parser, data_type);
	  pr_clear_value (&src_val);
	  pr_clear_value (&dest_val);
	}
      break;

      /* If merely renaming resolution, will be done after switch statement */
    case PT_RENAME_RESOLUTION:
      break;

    case PT_RENAME_ATTR_MTHD:
      if (alter->info.alter.alter_clause.rename.old_name)
	{
	  old_name = alter->info.alter.alter_clause.rename.old_name->info.name.original;
	}
      else
	{
	  old_name = NULL;
	}

      new_name = alter->info.alter.alter_clause.rename.new_name->info.name.original;

      if (alter->info.alter.alter_clause.rename.meta == PT_META_ATTR)
	{
	  class_attr = 1;
	}
      else
	{
	  class_attr = 0;
	}

      switch (alter->info.alter.alter_clause.rename.element_type)
	{
	case PT_ATTRIBUTE:
	case PT_METHOD:
	  error = dbt_rename (ctemplate, old_name, class_attr, new_name);
	  break;

	case PT_FUNCTION_RENAME:
	  mthd_name = alter->info.alter.alter_clause.rename.mthd_name->info.name.original;
	  error = dbt_change_method_implementation (ctemplate, mthd_name, class_attr, new_name);
	  break;

	  /* the following case is not yet supported, but hey, when it is, there'll code for it :-) */

	  /* There's code now. this drops the old file name and puts the new file name in its place., I took out the
	   * class_attr, since for our purpose we don't need it */

	case PT_FILE_RENAME:
	  {
	    PT_NODE *old_name_node, *new_name_node;

	    old_name_node = alter->info.alter.alter_clause.rename.old_name;
	    new_name_node = alter->info.alter.alter_clause.rename.new_name;

	    old_name = (char *) old_name_node->info.file_path.string->info.value.data_value.str->bytes;
	    new_name = (char *) new_name_node->info.file_path.string->info.value.data_value.str->bytes;

	    error = dbt_rename_method_file (ctemplate, old_name, new_name);
	  }
	  break;

	default:
	  /* actually, it means that a wrong thing is being renamed, and is really an error condition. */
	  assert (false);
	  break;
	}
      break;

    case PT_DROP_CONSTRAINT:
    case PT_DROP_FK_CLAUSE:
    case PT_DROP_PRIMARY_CLAUSE:
      {
	SM_CLASS_CONSTRAINT *cons = NULL;
	const char *constraint_name = NULL;

	if (alter_code == PT_DROP_PRIMARY_CLAUSE)
	  {
	    assert (alter->info.alter.constraint_list == NULL);
	    cons = classobj_find_class_primary_key (ctemplate->current);
	    if (cons != NULL)
	      {
		assert (cons->type == SM_CONSTRAINT_PRIMARY_KEY);
		constraint_name = cons->name;
	      }
	    else
	      {
		/* We set a name to print the error message. */
		constraint_name = "primary key";
	      }
	  }
	else
	  {
	    assert (alter->info.alter.constraint_list->next == NULL);
	    assert (alter->info.alter.constraint_list->node_type == PT_NAME);
	    constraint_name = alter->info.alter.constraint_list->info.name.original;
	    assert (constraint_name != NULL);
	    cons = classobj_find_class_index (ctemplate->current, constraint_name);
	  }

	if (cons != NULL)
	  {
	    const DB_CONSTRAINT_TYPE constraint_type = db_constraint_type (cons);

	    if (alter_code == PT_DROP_FK_CLAUSE && constraint_type != DB_CONSTRAINT_FOREIGN_KEY)
	      {
		er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE, 1, constraint_name);
		error = er_errid ();
	      }
	    else
	      {
		if (alter_code == PT_DROP_FK_CLAUSE && prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
		  {
		    /* We warn the user that dropping a foreign key behaves differently in CUBRID (the associated index
		     * is also dropped while MySQL's associated index is kept and only the foreign key constraint is
		     * dropped). This difference is not important enough to be an error but a warning or a notification
		     * might help. */
		    er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_SM_FK_MYSQL_DIFFERENT, 0);
		  }
		error = dbt_drop_constraint (ctemplate, constraint_type, constraint_name, NULL, 0);
	      }
	  }
	else
	  {
	    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SM_CONSTRAINT_NOT_FOUND, 1, constraint_name);
	    error = er_errid ();
	  }
      }
      break;

    case PT_APPLY_PARTITION:
    case PT_REMOVE_PARTITION:
    case PT_ADD_PARTITION:
    case PT_ADD_HASHPARTITION:
    case PT_COALESCE_PARTITION:
    case PT_REORG_PARTITION:
    case PT_DROP_PARTITION:
    case PT_ANALYZE_PARTITION:
    case PT_PROMOTE_PARTITION:
      /* initialize partition alteration context */
      pinfo.promoted_count = 0;
      pinfo.promoted_names = NULL;
      pinfo.root_tmpl = ctemplate;
      pinfo.root_op = vclass;
      pinfo.keycol[0] = 0;

      error = tran_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_ALTER);
      if (error != NO_ERROR)
	{
	  dbt_abort_class (ctemplate);
	  return error;
	}
      partition_savepoint = true;

      error = do_alter_partitioning_pre (parser, alter, &pinfo);
      break;

#if defined (ENABLE_RENAME_CONSTRAINT)
    case PT_RENAME_CONSTRAINT:
    case PT_RENAME_INDEX:

      old_name = alter->info.alter.alter_clause.rename.old_name->info.name.original;
      new_name = alter->info.alter.alter_clause.rename.new_name->info.name.original;

      if (alter->info.alter.alter_clause.rename.element_type == PT_CONSTRAINT_NAME)
	{
	  constraint_family = SM_CONSTRAINT_NAME;
	}
      else			/* if (alter->info.alter.alter_clause. rename.element_type == PT_INDEX_NAME) */
	{
	  constraint_family = SM_INDEX_NAME;
	}

      error = smt_rename_constraint (ctemplate, old_name, new_name, constraint_family);
      break;
#endif

    default:
      assert (false);
      dbt_abort_class (ctemplate);
      return error;
    }

  if (initial_def_val != NULL)
    {
      parser_free_tree (parser, initial_def_val);
    }

  /* Process resolution list if appropriate */
  if (error == NO_ERROR)
    {
      if (alter->info.alter.super.resolution_list != NULL && alter->info.alter.code != PT_DROP_RESOLUTION)
	{
	  error = do_add_resolutions (parser, ctemplate, alter->info.alter.super.resolution_list);
	}
    }

  if (error != NO_ERROR)
    {
      dbt_abort_class (ctemplate);
      if (partition_savepoint)
	{
	  goto alter_partition_fail;
	}
      return error;
    }

  vclass = dbt_finish_class (ctemplate);

  /* the dbt_finish_class() failed, the template was not freed */
  if (vclass == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      dbt_abort_class (ctemplate);
      if (partition_savepoint)
	{
	  goto alter_partition_fail;
	}
      return error;
    }

  /* If we have an ADD COLUMN x NOT NULL without a default value, the existing rows will be filled with NULL for the
   * new column by default. For compatibility with MySQL, we can auto-fill some column types with "hard defaults", like
   * 0 for integer types. THIS CAN TAKE A LONG TIME (it runs an UPDATE), and can be turned off by setting
   * "add_col_not_null_no_default_behavior" to "cubrid". The parameter is true by default. */
  if (alter_code == PT_ADD_ATTR_MTHD)
    {
      error = do_update_new_notnull_cols_without_default (parser, alter, vclass);
      if (error != NO_ERROR)
	{
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	    }
	  return error;
	}

      /*
       * if we ADD COLUMN with DEFAULT expression the existing rows will be filled with default expression for the
       * new column. That's because we need to set the column value right now (the default expression may be date/time).
       */
      error = do_update_new_cols_with_default_expression (parser, alter, vclass);
      if (error != NO_ERROR)
	{
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ADD_ATTR_MTHD);
	    }
	  return error;
	}
    }

  switch (alter_code)
    {
    case PT_APPLY_PARTITION:
    case PT_REMOVE_PARTITION:
    case PT_ADD_PARTITION:
    case PT_ADD_HASHPARTITION:
    case PT_COALESCE_PARTITION:
    case PT_REORG_PARTITION:
    case PT_DROP_PARTITION:
    case PT_ANALYZE_PARTITION:
    case PT_PROMOTE_PARTITION:
      /* root class template has been edited and finished, update the references in the pinfo object */
      pinfo.root_op = vclass;
      pinfo.root_tmpl = NULL;
      error = do_alter_partitioning_post (parser, alter, &pinfo);

      if (pinfo.promoted_names != NULL)
	{
	  /* cleanup promoted names if any */
	  int i;
	  for (i = 0; i < pinfo.promoted_count; i++)
	    {
	      free_and_init (pinfo.promoted_names[i]);
	    }
	  free_and_init (pinfo.promoted_names);
	}

      if (error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
	{
	  goto alter_partition_fail;
	}
      break;

    default:
      break;
    }
  return error;

alter_partition_fail:
  if (partition_savepoint && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_PARTITION_SAVEPOINT_ALTER);
    }
  return error;
}

/*
 * do_alter_clause_rename_entity() - Executes an ALTER TABLE RENAME TO clause
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_RENAME_ENTITY clause potentially
 *                  followed by the rest of the clauses in the ALTER
 *                  statement.
 * Note: The clauses following the PT_RENAME_ENTITY clause will be updated to
 *       the new name of the class.
 */
static int
do_alter_clause_rename_entity (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  int error_code = NO_ERROR;
  const PT_ALTER_CODE alter_code = alter->info.alter.code;
  const char *const old_name = alter->info.alter.entity_name->info.name.original;
  const char *const new_name = alter->info.alter.alter_clause.rename.new_name->info.name.original;
  PT_NODE *tmp_clause = NULL;

  assert (alter_code == PT_RENAME_ENTITY);
  assert (alter->info.alter.super.resolution_list == NULL);

  error_code = do_rename_internal (old_name, new_name);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  error_code = do_recreate_renamed_class_indexes (parser, old_name, new_name);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  /* We now need to update the current name of the class for the rest of the ALTER clauses. */
  for (tmp_clause = alter->next; tmp_clause != NULL; tmp_clause = tmp_clause->next)
    {
      parser_free_tree (parser, tmp_clause->info.alter.entity_name);
      tmp_clause->info.alter.entity_name = parser_copy_tree (parser, alter->info.alter.alter_clause.rename.new_name);
      if (tmp_clause->info.alter.entity_name == NULL)
	{
	  error_code = ER_FAILED;
	  goto error_exit;
	}
    }

  return error_code;

error_exit:
  return error_code;
}

/*
 * do_alter_clause_add_index() - Executes an ALTER TABLE ADD INDEX clause
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_ADD_INDEX_CLAUSE clause potentially
 *                  followed by the rest of the clauses in the ALTER
 *                  statement.
 * Note: The clauses following the PT_ADD_INDEX_CLAUSE clause are not
 *       affected in any way.
 */
static int
do_alter_clause_add_index (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{

  int error = NO_ERROR;
  PT_NODE *create_index = NULL;

  assert (alter->info.alter.create_index != NULL);
  assert (alter->info.alter.constraint_list == NULL);
  assert (alter->info.alter.alter_clause.attr_mthd.attr_def_list == NULL);
  assert (alter->info.alter.alter_clause.attr_mthd.mthd_def_list == NULL);
  assert (alter->info.alter.alter_clause.attr_mthd.mthd_file_list == NULL);

  create_index = alter->info.alter.create_index;

  for (; create_index != NULL; create_index = create_index->next)
    {
      error = do_create_index (parser, create_index);
      if (error != NO_ERROR)
	{
	  break;
	}
    }

  return error;
}


/*
 * do_alter_clause_drop_index() - Executes an ALTER TABLE DROP INDEX clause
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_DROP_INDEX_CLAUSE clause potentially
 *                  followed by the rest of the clauses in the ALTER
 *                  statement.
 * Note: The clauses following the PT_DROP_INDEX_CLAUSE clause are not
 *       affected in any way.
 */
static int
do_alter_clause_drop_index (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  int error_code = NO_ERROR;
  const PT_ALTER_CODE alter_code = alter->info.alter.code;
  DB_OBJECT *obj = NULL;
  DB_CONSTRAINT_TYPE index_type;
  bool is_reverse;
  bool is_unique;

  assert (alter_code == PT_DROP_INDEX_CLAUSE);
  assert (alter->info.alter.constraint_list != NULL);
  assert (alter->info.alter.constraint_list->next == NULL);
  assert (alter->info.alter.constraint_list->node_type == PT_NAME);

  index_type =
    get_reverse_unique_index_type (alter->info.alter.alter_clause.index.reverse,
				   alter->info.alter.alter_clause.index.unique);

  obj = db_find_class (alter->info.alter.entity_name->info.name.original);
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  if (index_type == DB_CONSTRAINT_INDEX)
    {
      error_code =
	get_index_type_qualifiers (obj, &is_reverse, &is_unique, alter->info.alter.constraint_list->info.name.original);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  else
    {
      is_reverse = alter->info.alter.alter_clause.index.reverse;
      is_unique = alter->info.alter.alter_clause.index.unique;
    }

  error_code =
    create_or_drop_index_helper (parser, alter->info.alter.constraint_list->info.name.original, is_reverse,
				 is_unique, NULL, NULL, NULL, NULL, -1, 0, NULL, NULL, obj,
				 SM_NORMAL_INDEX, DO_INDEX_DROP);
  return error_code;
}


/*
 * do_alter_change_auto_increment() - Executes an
 *               ALTER TABLE ... AUTO_INCREMENT = x statement.
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_CHANGE_AUTO_INCREMENT clause.
 */

static int
do_alter_change_auto_increment (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  const char *entity_name = NULL;
  DB_OBJECT *class_obj = NULL;
  DB_ATTRIBUTE *cur_attr = NULL;
  MOP ai_serial = NULL;
  int error = NO_ERROR;
  int au_save = 0;

  entity_name = alter->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      ERROR1 (error, ER_UNEXPECTED, "Expecting a class name.");
      goto change_ai_error;
    }

  class_obj = db_find_class (entity_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto change_ai_error;
    }

  cur_attr = db_get_attributes (class_obj);

  /* find the attribute that has auto_increment */
  for (cur_attr = db_get_attributes (class_obj); cur_attr != NULL; cur_attr = db_attribute_next (cur_attr))
    {
      if (cur_attr->auto_increment == NULL)
	{
	  continue;
	}
      if (ai_serial != NULL)
	{
	  /* we already found a serial. AMBIGUITY! */
	  ERROR0 (error, ER_AUTO_INCREMENT_SINGLE_COL_AMBIGUITY);
	  goto change_ai_error;
	}
      else
	{
	  ai_serial = cur_attr->auto_increment;
	}
    }

  if (ai_serial == NULL)
    {
      /* we ought to have exactly ONE proper attribute with auto increment */
      ERROR0 (error, ER_AUTO_INCREMENT_SINGLE_COL_AMBIGUITY);
      goto change_ai_error;
    }

  AU_DISABLE (au_save);
  error =
    do_change_auto_increment_serial (parser, ai_serial, alter->info.alter.alter_clause.auto_increment.start_value);
  AU_ENABLE (au_save);


  return error;

change_ai_error:
  return error;
}


/*
 * do_alter() -
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of an alter statement
 */
int
do_alter (PARSER_CONTEXT * parser, PT_NODE * alter)
{
  int error_code = NO_ERROR;
  PT_NODE *crt_clause = NULL;
  bool do_semantic_checks = false;
  bool do_rollback = false;

  CHECK_MODIFICATION_ERROR ();

  /* Multiple alter operations in a single statement need to be atomic. */
  error_code = tran_system_savepoint (UNIQUE_SAVEPOINT_MULTIPLE_ALTER);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }
  do_rollback = true;

  for (crt_clause = alter; crt_clause != NULL; crt_clause = crt_clause->next)
    {
      PT_NODE *const save_next = crt_clause->next;
      const PT_ALTER_CODE alter_code = crt_clause->info.alter.code;

      /* The first ALTER clause has already been checked, we call the semantic check starting with the second clause. */
      if (do_semantic_checks)
	{
	  PT_NODE *crt_result = NULL;

	  crt_clause->next = NULL;
	  crt_result = pt_compile (parser, crt_clause);
	  crt_clause->next = save_next;
	  if (!crt_result || pt_has_error (parser))
	    {
	      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, crt_clause);
	      error_code = er_errid ();
	      goto error_exit;
	    }
	  assert (crt_result == crt_clause);
	}

      switch (alter_code)
	{
	case PT_RENAME_ENTITY:
	  error_code = do_alter_clause_rename_entity (parser, crt_clause);
	  break;
	case PT_ADD_INDEX_CLAUSE:
	  error_code = do_alter_clause_add_index (parser, crt_clause);
	  break;
	case PT_DROP_INDEX_CLAUSE:
	  error_code = do_alter_clause_drop_index (parser, crt_clause);
	  break;
	case PT_CHANGE_AUTO_INCREMENT:
	  error_code = do_alter_change_auto_increment (parser, crt_clause);
	  break;
	case PT_CHANGE_ATTR:
	  error_code = do_alter_clause_change_attribute (parser, crt_clause);
	  break;
	case PT_CHANGE_OWNER:
	  error_code = do_alter_change_owner (parser, crt_clause);
	  break;
	case PT_CHANGE_COLLATION:
	  error_code = do_alter_change_default_cs_coll (parser, crt_clause);
	  break;
	case PT_CHANGE_TABLE_COMMENT:
	  error_code = do_alter_change_tbl_comment (parser, crt_clause);
	  break;
	case PT_CHANGE_COLUMN_COMMENT:
	  error_code = do_alter_change_col_comment (parser, crt_clause);
	  break;
	default:
	  /* This code might not correctly handle a list of ALTER clauses so we keep crt_clause->next to NULL during
	   * its execution just to be on the safe side. */
	  crt_clause->next = NULL;

	  error_code = do_alter_one_clause_with_template (parser, crt_clause);

	  crt_clause->next = save_next;
	}

      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
      do_semantic_checks = true;
    }

  return error_code;

error_exit:
  if (do_rollback && error_code != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_MULTIPLE_ALTER);
    }

  return error_code;
}




/*
 * Function Group :
 * DO functions for user management
 *
 */

#define IS_NAME(n)      ((n)->node_type == PT_NAME)
#define IS_STRING(n)    ((n)->node_type == PT_VALUE &&          \
                         ((n)->type_enum == PT_TYPE_VARCHAR  || \
                          (n)->type_enum == PT_TYPE_CHAR     || \
                          (n)->type_enum == PT_TYPE_VARNCHAR || \
                          (n)->type_enum == PT_TYPE_NCHAR))
#define GET_NAME(n)     ((char *) (n)->info.name.original)
#define GET_STRING(n)   ((char *) (n)->info.value.data_value.str->bytes)

/*
 * do_grant() - Grants priviledges
 *   return: Error code if grant fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a grant statement
 */
int
do_grant (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *user, *user_list;
  DB_OBJECT *user_obj, *class_mop;
  PT_NODE *auth_cmd_list, *auth_list, *auth;
  DB_AUTH db_auth;
  PT_NODE *spec_list, *s_list, *spec;
  PT_NODE *entity_list, *entity;
  int grant_option;
  bool set_savepoint = false;

  CHECK_MODIFICATION_ERROR ();

  user_list = statement->info.grant.user_list;
  auth_cmd_list = statement->info.grant.auth_cmd_list;
  spec_list = statement->info.grant.spec_list;

  if (statement->info.grant.grant_option == PT_GRANT_OPTION)
    {
      grant_option = true;
    }
  else
    {
      grant_option = false;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_GRANT_USER);
  if (error != NO_ERROR)
    {
      return error;
    }
  set_savepoint = true;

  for (user = user_list; user != NULL; user = user->next)
    {
      user_obj = db_find_user (user->info.name.original);
      if (user_obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto end;
	}

      auth_list = auth_cmd_list;
      for (auth = auth_list; auth != NULL; auth = auth->next)
	{
	  db_auth = pt_auth_to_db_auth (auth);

	  s_list = spec_list;
	  for (spec = s_list; spec != NULL; spec = spec->next)
	    {
	      entity_list = spec->info.spec.flat_entity_list;
	      for (entity = entity_list; entity != NULL; entity = entity->next)
		{
		  class_mop = db_find_class (entity->info.name.original);
		  if (class_mop == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      goto end;
		    }

		  error = db_grant (user_obj, class_mop, db_auth, grant_option);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }
		}
	    }
	}
    }

end:
  if (set_savepoint && error != NO_ERROR && !ER_IS_ABORTED_DUE_TO_DEADLOCK (error))
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_GRANT_USER);
    }

  return error;
}

/*
 * do_revoke() - Revokes priviledges
 *   return: Error code if revoke fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a revoke statement
 */
int
do_revoke (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;

  PT_NODE *user, *user_list;
  DB_OBJECT *user_obj, *class_mop;
  PT_NODE *auth_cmd_list, *auth_list, *auth;
  DB_AUTH db_auth;
  PT_NODE *spec_list, *s_list, *spec;
  PT_NODE *entity_list, *entity;
  bool set_savepoint = false;

  CHECK_MODIFICATION_ERROR ();

  user_list = statement->info.revoke.user_list;
  auth_cmd_list = statement->info.revoke.auth_cmd_list;
  spec_list = statement->info.revoke.spec_list;

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_REVOKE_USER);
  if (error != NO_ERROR)
    {
      return error;
    }
  set_savepoint = true;

  for (user = user_list; user != NULL; user = user->next)
    {
      user_obj = db_find_user (user->info.name.original);
      if (user_obj == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto end;
	}

      auth_list = auth_cmd_list;
      for (auth = auth_list; auth != NULL; auth = auth->next)
	{
	  db_auth = pt_auth_to_db_auth (auth);

	  s_list = spec_list;
	  for (spec = s_list; spec != NULL; spec = spec->next)
	    {
	      entity_list = spec->info.spec.flat_entity_list;
	      for (entity = entity_list; entity != NULL; entity = entity->next)
		{
		  class_mop = db_find_class (entity->info.name.original);
		  if (class_mop == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      goto end;
		    }

		  error = db_revoke (user_obj, class_mop, db_auth);
		  if (error != NO_ERROR)
		    {
		      goto end;
		    }
		}
	    }
	}
    }

end:
  if (set_savepoint && error != NO_ERROR && !ER_IS_ABORTED_DUE_TO_DEADLOCK (error))
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_REVOKE_USER);
    }

  return error;
}

/*
 * do_create_user() - Create a user
 *   return: Error code if creation fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a create user statement
 */
int
do_create_user (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *user, *group, *member;
  int exists;
  PT_NODE *node, *node2;
  const char *user_name, *password, *comment;
  const char *group_name, *member_name;
  bool set_savepoint = false;

  CHECK_MODIFICATION_ERROR ();

  if (parser == NULL || statement == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  node = statement->info.create_user.user_name;
  if (node == NULL || node->node_type != PT_NAME || node->info.name.original == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_OR_INVALID_USER, 0);
      return ER_AU_MISSING_OR_INVALID_USER;
    }

  user_name = node->info.name.original;
  if (user_name == NULL)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      return error;
    }

  /* first, check if user_name is in group or member clause */
  for (node = statement->info.create_user.groups; node != NULL; node = node->next)
    {
      if (node == NULL || node->node_type != PT_NAME || node->info.name.original == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_MISSING_OR_INVALID_USER, 0);
	  return ER_AU_MISSING_OR_INVALID_USER;
	}

      group_name = node->info.name.original;
      if (intl_identifier_casecmp (user_name, group_name) == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_MEMBER_CAUSES_CYCLES, 0);
	  return ER_AU_MEMBER_CAUSES_CYCLES;
	}
    }

  for (node = statement->info.create_user.members; node != NULL; node = node->next)
    {
      member_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
      if (member_name == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}

      if (intl_identifier_casecmp (user_name, member_name) == 0
	  || intl_identifier_casecmp (member_name, AU_PUBLIC_USER_NAME) == 0)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_MEMBER_CAUSES_CYCLES, 0);
	  return ER_AU_MEMBER_CAUSES_CYCLES;
	}
    }

  /* second, check if group name is in member clause */
  for (node = statement->info.create_user.groups; node != NULL; node = node->next)
    {
      group_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
      if (group_name == NULL)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}

      for (node2 = statement->info.create_user.members; node2 != NULL; node2 = node2->next)
	{
	  member_name = (node2 && IS_NAME (node2)) ? GET_NAME (node2) : NULL;
	  if (member_name == NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
	      return ER_OBJ_INVALID_ARGUMENTS;
	    }

	  if (intl_identifier_casecmp (group_name, member_name) == 0)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AU_MEMBER_CAUSES_CYCLES, 0);
	      return ER_AU_MEMBER_CAUSES_CYCLES;
	    }
	}
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_CREATE_USER_ENTITY);
  if (error != NO_ERROR)
    {
      return error;
    }
  set_savepoint = true;

  exists = 0;
  user = db_add_user (user_name, &exists);
  if (user == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }
  else if (exists)
    {
      error = ER_AU_USER_EXISTS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, user_name);
      goto end;
    }

  /* Now treats optional password, group, member and comment of the created user */

  /* password */
  node = statement->info.create_user.password;
  password = (node && IS_STRING (node)) ? GET_STRING (node) : NULL;
  if (password != NULL)
    {
      error = au_set_password (user, password);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  /* group */
  node = statement->info.create_user.groups;
  group_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
  if (group_name != NULL)
    {
      do
	{
	  group = db_find_user (group_name);

	  if (group == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      error = db_add_member (group, user);
	    }

	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  node = node->next;
	  group_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
	}
      while (group_name != NULL);
    }

  /* member */
  node = statement->info.create_user.members;
  member_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
  if (member_name != NULL)
    {
      do
	{
	  member = db_find_user (member_name);

	  if (member == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	    }
	  else
	    {
	      error = db_add_member (user, member);
	    }

	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  node = node->next;
	  member_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;
	}
      while (member_name != NULL);
    }

  /* comment */
  node = statement->info.create_user.comment;
  if (node != NULL)
    {
      assert (node->node_type == PT_VALUE);

      comment = (char *) PT_VALUE_GET_BYTES (node);
      error = au_set_user_comment (user, comment);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  if (set_savepoint && error != NO_ERROR && !ER_IS_ABORTED_DUE_TO_DEADLOCK (error))
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CREATE_USER_ENTITY);
    }

  return error;
}

/*
 * do_drop_user() - Drop the user
 *   return: Error code if dropping fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a drop user statement
 */
int
do_drop_user (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *user = NULL;
  PT_NODE *node;
  const char *user_name;
  bool set_savepoint = false;

  CHECK_MODIFICATION_ERROR ();

  if (parser == NULL || statement == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  node = statement->info.create_user.user_name;
  user_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;

  if (user_name == NULL)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      return error;
    }

  error = db_find_user_to_drop (user_name, &user);
  if (error != NO_ERROR)
    {
      return error;
    }

  assert (user != NULL);

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_DROP_USER_ENTITY);
  if (error != NO_ERROR)
    {
      return error;
    }
  set_savepoint = true;

  error = db_drop_user (user);

  if (set_savepoint && error != NO_ERROR && !ER_IS_ABORTED_DUE_TO_DEADLOCK (error))
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_DROP_USER_ENTITY);
    }

  return error;
}

/*
 * do_alter_user() - Change the user's password
 *   return: Error code if alter fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of an alter statement
 */
int
do_alter_user (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *user;
  PT_NODE *node;
  const char *user_name, *password, *comment;
  bool set_savepoint = false;

  CHECK_MODIFICATION_ERROR ();

  if (parser == NULL || statement == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  node = statement->info.alter_user.user_name;
  user_name = (node && IS_NAME (node)) ? GET_NAME (node) : NULL;

  if (user_name == NULL)
    {
      error = ER_AU_INVALID_USER;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, "");
      return error;
    }

  user = db_find_user (user_name);
  if (user == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_ALTER_USER_ENTITY);
  if (error != NO_ERROR)
    {
      return error;
    }
  set_savepoint = true;

  /*
   * here, both password and comment are optional,
   * either password or comment shall exist,
   * csql_grammar denies the error case with the missing of both.
   */

  /* password */
  node = statement->info.alter_user.password;
  if (node != NULL)
    {
      password = IS_STRING (node) ? GET_STRING (node) : NULL;
      error = au_set_password (user, password);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  /* comment */
  node = statement->info.alter_user.comment;
  if (node != NULL)
    {
      assert (node->node_type == PT_VALUE);

      comment = (char *) PT_VALUE_GET_BYTES (node);
      error = au_set_user_comment (user, comment);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  if (set_savepoint && error != NO_ERROR && !ER_IS_ABORTED_DUE_TO_DEADLOCK (error))
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ALTER_USER_ENTITY);
    }

  return error;
}

/*
 * Function Group :
 * Code for dropping a Classes by Parse Tree descriptions.
 *
 */

/*
 * drop_class_name() - This static routine drops a class by name.
 *   return: Error code
 *   name(in): Class name to drop
 *   is_cascade_constraints(in): whether drop relative FK constraints
 */
static int
drop_class_name (const char *name, bool is_cascade_constraints)
{
  DB_OBJECT *class_mop;

  class_mop = db_find_class (name);

  if (class_mop)
    {
      return db_drop_class_ex (class_mop, is_cascade_constraints);
    }
  else
    {
      /* if class is null, return the global error. */
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
}

/*
 * do_drop() - Drops a vclass, class
 *   return: Error code if a vclass is not deleted.
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a drop statement
 */
int
do_drop (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *entity_spec_list, *entity_spec;
  PT_NODE *entity;
  PT_NODE *entity_list;

  CHECK_MODIFICATION_ERROR ();

  /* partitioned sub-class check */
  entity_spec_list = statement->info.drop.spec_list;
  for (entity_spec = entity_spec_list; entity_spec != NULL; entity_spec = entity_spec->next)
    {
      entity_list = entity_spec->info.spec.flat_entity_list;
      for (entity = entity_list; entity != NULL; entity = entity->next)
	{
	  if (do_is_partitioned_subclass (NULL, entity->info.name.original, NULL))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_PARTITION_REQUEST, 0);
	      return er_errid ();
	    }
	}
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_DROP_ENTITY);
  if (error != NO_ERROR)
    {
      return error;
    }

  entity_spec_list = statement->info.drop.spec_list;
  for (entity_spec = entity_spec_list; entity_spec != NULL; entity_spec = entity_spec->next)
    {
      entity_list = entity_spec->info.spec.flat_entity_list;
      for (entity = entity_list; entity != NULL; entity = entity->next)
	{
	  error = drop_class_name (entity->info.name.original, statement->info.drop.is_cascade_constraints);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
    }

  return error;

error_exit:
  if (error != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_DROP_ENTITY);
    }

  return error;
}

/*
 * update_locksets_for_multiple_rename() - Adds a class name to one of the two
 *                                         sets: either names to be reserved
 *                                         or classes to be locked
 *   return: Error code
 *   class_name(in): A class name involved in a rename operation
 *   num_mops(in/out): The number of MOPs
 *   mop_set(in/out): The MOPs to lock before the rename operation
 *   num_names(in/out): The number of class names
 *   name_set(in/out): The class names to reserve before the rename operation
 *   error_on_misssing_class(in/out): Whether to return an error if a class
 *                                    with the given class_name is not found
 */
int
update_locksets_for_multiple_rename (const char *class_name, int *num_mops, MOP * mop_set, int *num_names,
				     char **name_set, bool error_on_misssing_class)
{
  DB_OBJECT *class_mop = NULL;
  char realname[SM_MAX_IDENTIFIER_LENGTH];
  int i = 0;

  sm_downcase_name (class_name, realname, SM_MAX_IDENTIFIER_LENGTH);

  class_mop = db_find_class (realname);
  if (class_mop == NULL && error_on_misssing_class)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (class_mop != NULL)
    {
      /* Classes that exist are locked. */
      /* Duplicates are harmless; they are handled by locator_fetch_set () anyway. */
      mop_set[*num_mops] = class_mop;
      ++(*num_mops);
    }
  else
    {
      /* Class names that don't yet exist are reserved. */
      for (i = 0; i < *num_names; ++i)
	{
	  if (intl_identifier_casecmp (name_set[i], realname) == 0)
	    {
	      /* The class name is used more than once, we ignore its current occurence. */
	      return NO_ERROR;
	    }
	}
      name_set[*num_names] = strdup (realname);
      if (name_set[*num_names] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (strlen (realname) + 1) * sizeof (char));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      ++(*num_names);
    }
  return NO_ERROR;
}

/*
 * acquire_locks_for_multiple_rename() - Performs the necessary locking for an
 *                                       atomic multiple rename operation
 *   return: Error code
 *   statement(in): Parse tree of a rename statement
 *
 * Note: We need to lock all the classes and vclasses involved in the rename
 *       operation. When doing multiple renames we preventively lock all the
 *       names involved in the rename operation. For statements such as:
 *           RENAME A to tmp, B to A, tmp to B;
 *       "A" and "B" will be exclusively locked (locator_fetch_set ())
 *       and the name "tmp" will be reserved for renaming operations
 *       (locator_reserve_class_names ()).
 */
int
acquire_locks_for_multiple_rename (const PT_NODE * statement)
{
  int error = NO_ERROR;
  const PT_NODE *current_rename = NULL;
  int num_rename = 0;
  int num_mops = 0;
  MOP *mop_set = NULL;
  int num_names = 0;
  char **name_set = NULL;
  OID *oid_set = NULL;
  MOBJ fetch_result = NULL;
  LC_FIND_CLASSNAME reserve_result = LC_CLASSNAME_ERROR;
  int i = 0;

  num_rename = pt_length_of_list (statement);

  mop_set = (MOP *) malloc (2 * num_rename * sizeof (MOP));
  if (mop_set == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, 2 * num_rename * sizeof (MOP));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_exit;
    }
  num_mops = 0;

  name_set = (char **) malloc (2 * num_rename * sizeof (char *));
  if (name_set == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, 2 * num_rename * sizeof (char *));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_exit;
    }
  num_names = 0;

  for (current_rename = statement; current_rename != NULL; current_rename = current_rename->next)
    {
      const bool is_first_rename = current_rename == statement ? true : false;
      const char *old_name = current_rename->info.rename.old_name->info.name.original;
      const char *new_name = current_rename->info.rename.new_name->info.name.original;

      bool found = false;

      for (i = 0; i < num_names; i++)
	{
	  if (strcmp (name_set[i], old_name) == 0)
	    {
	      found = true;
	      break;
	    }
	}

      if (!found)
	{
	  error = update_locksets_for_multiple_rename (old_name, &num_mops, mop_set, &num_names, name_set, true);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  if (is_first_rename)
	    {
	      /* We have made sure the first class to be renamed can be locked. */
	      assert (num_mops == 1);
	    }
	}

      error = update_locksets_for_multiple_rename (new_name, &num_mops, mop_set, &num_names, name_set, false);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      if (is_first_rename && num_names != 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_CLASSNAME_EXIST, 1, new_name);
	  error = ER_LC_CLASSNAME_EXIST;
	  goto error_exit;
	}
      /* We have made sure the first name to be used can be reserved. */
    }

  assert (num_mops != 0 && num_names != 0);

  fetch_result = locator_fetch_set (num_mops, mop_set, DB_FETCH_WRITE, DB_FETCH_WRITE, 1);
  if (fetch_result == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CANNOT_GET_LOCK, 0);
      error = ER_CANNOT_GET_LOCK;
      goto error_exit;
    }

  oid_set = (OID *) malloc (num_names * sizeof (OID));
  if (oid_set == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, num_names * sizeof (OID));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_exit;
    }

  for (i = 0; i < num_names; ++i)
    {
      /* Each reserved name will point to the OID of the first class to be renamed. This is ok as the associated
       * transient table entries will only be used for the multiple rename operation. */
      COPY_OID (&oid_set[i], ws_oid (mop_set[0]));
    }

  reserve_result = locator_reserve_class_names (num_names, (const char **) name_set, oid_set);
  if (reserve_result != LC_CLASSNAME_RESERVED)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CANNOT_GET_LOCK, 0);
      error = ER_CANNOT_GET_LOCK;
      goto error_exit;
    }

error_exit:

  if (oid_set != NULL)
    {
      assert (num_names > 0);
      free_and_init (oid_set);
    }
  if (name_set != NULL)
    {
      for (i = 0; i < num_names; ++i)
	{
	  assert (name_set[i] != NULL);
	  free_and_init (name_set[i]);
	}
      free_and_init (name_set);
    }
  if (mop_set != NULL)
    {
      free_and_init (mop_set);
    }
  return error;
}

/*
 * do_rename() - Renames several vclasses or classes
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a rename statement
 */
int
do_rename (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  const PT_NODE *current_rename = NULL;

  CHECK_MODIFICATION_ERROR ();

  /* Renaming operations in a single statement need to be atomic. */
  error = tran_system_savepoint (UNIQUE_SAVEPOINT_RENAME);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (statement->next != NULL)
    {
      error = acquire_locks_for_multiple_rename (statement);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  for (current_rename = statement; current_rename != NULL; current_rename = current_rename->next)
    {
      const char *old_name = current_rename->info.rename.old_name->info.name.original;
      const char *new_name = current_rename->info.rename.new_name->info.name.original;

      error = do_rename_internal (old_name, new_name);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      error = do_recreate_renamed_class_indexes (parser, old_name, new_name);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  return error;

error_exit:
  if (error != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_RENAME);
    }

  return error;
}

static int
do_rename_internal (const char *const old_name, const char *const new_name)
{
  DB_OBJECT *old_class = NULL;

  if (do_is_partitioned_subclass (NULL, old_name, NULL))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_PARTITION_REQUEST, 0);
      return er_errid ();
    }

  old_class = db_find_class (old_name);
  if (old_class == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  return db_rename_class (old_class, new_name);
}

/*
 * Function Group :
 * Parse tree to index commands translation.
 *
 */

static DB_CONSTRAINT_TYPE
get_reverse_unique_index_type (const bool is_reverse, const bool is_unique)
{
  if (is_unique)
    {
      return is_reverse ? DB_CONSTRAINT_REVERSE_UNIQUE : DB_CONSTRAINT_UNIQUE;
    }
  else
    {
      return is_reverse ? DB_CONSTRAINT_REVERSE_INDEX : DB_CONSTRAINT_INDEX;
    }
}

/*
 * create_or_drop_index_helper()
 *   return: Error code
 *   parser(in): Parser context
 *   constraint_name(in): If NULL the default constraint name is used;
 *                        column_names must be non-NULL in this case.
 *   is_reverse(in):
 *   is_unique(in):
 *   column_names(in): Can be NULL if dropping a constraint and providing the
 *                     constraint name.
 *   column_prefix_length(in):
 *   where_predicate(in):
 *   func_index_pos(in):
 *   func_index_args_count(in):
 *   function_expr(in):
 *   comment(in): index comment
 *   obj(in): Class object
 *   do_index(in) : The operation to be performed (creating or dropping)
 */
static int
create_or_drop_index_helper (PARSER_CONTEXT * parser, const char *const constraint_name, const bool is_reverse,
			     const bool is_unique, PT_NODE * spec, PT_NODE * column_names,
			     PT_NODE * column_prefix_length, PT_NODE * where_predicate, int func_index_pos,
			     int func_index_args_count, PT_NODE * function_expr, PT_NODE * comment,
			     DB_OBJECT * const obj, SM_INDEX_STATUS index_status, DO_INDEX do_index)
{
  int error = NO_ERROR;
  int i = 0, nnames = 0;
  DB_CONSTRAINT_TYPE ctype = DB_CONSTRAINT_NONE;
  const PT_NODE *c = NULL, *n = NULL;
  char **attnames = NULL;
  int *asc_desc = NULL;
  int *attrs_prefix_length = NULL;
  char *cname = NULL;
  char const *colname = NULL;
  const char *comment_str = NULL;
  bool mysql_index_name = false;
  bool free_packing_buff = false;
  PRED_EXPR_WITH_CONTEXT *filter_predicate = NULL;
  SM_PREDICATE_INFO pred_index_info = { NULL, NULL, 0, NULL, 0 };
  SM_PREDICATE_INFO *p_pred_index_info = NULL;
  SM_FUNCTION_INFO *func_index_info = NULL;
  int is_partition = DB_NOT_PARTITIONED_CLASS;

  error = sm_partitioned_class_type (obj, &is_partition, NULL, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (is_partition == DB_PARTITION_CLASS)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
      return ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
    }

  if (comment != NULL)
    {
      assert (comment->node_type == PT_VALUE);
      comment_str = (char *) PT_VALUE_GET_BYTES (comment);
    }

  nnames = pt_length_of_list (column_names);

  if (do_index == DO_INDEX_CREATE && nnames == 1 && column_prefix_length)
    {
      n = column_names->info.sort_spec.expr;
      if (n)
	{
	  colname = n->info.name.original;
	}

      if (colname && (sm_att_unique_constrained (obj, colname) || sm_att_fk_constrained (obj, colname)))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INDEX_PREFIX_LENGTH_ON_UNIQUE_FOREIGN, 0);

	  return ER_SM_INDEX_PREFIX_LENGTH_ON_UNIQUE_FOREIGN;
	}
    }

  attnames = (char **) malloc ((nnames + 1) * sizeof (const char *));
  if (attnames == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (nnames + 1) * sizeof (const char *));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  asc_desc = (int *) malloc ((nnames) * sizeof (int));
  if (asc_desc == NULL)
    {
      free_and_init (attnames);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nnames * sizeof (int));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (do_index == DO_INDEX_CREATE)
    {
      attrs_prefix_length = (int *) malloc ((nnames) * sizeof (int));
      if (attrs_prefix_length == NULL)
	{
	  free_and_init (attnames);
	  free_and_init (asc_desc);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nnames * sizeof (int));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  for (c = column_names, i = 0; c != NULL; c = c->next, i++)
    {
      asc_desc[i] = c->info.sort_spec.asc_or_desc == PT_ASC ? 0 : 1;
      /* column name node */
      n = c->info.sort_spec.expr;
      attnames[i] = (char *) n->info.name.original;
      if (do_index == DO_INDEX_CREATE)
	{
	  attrs_prefix_length[i] = -1;
	}
    }
  attnames[i] = NULL;

  if (do_index == DO_INDEX_CREATE && nnames == 1 && attrs_prefix_length && column_prefix_length)
    {
      attrs_prefix_length[0] = column_prefix_length->info.value.data_value.i;
    }

  ctype = get_reverse_unique_index_type (is_reverse, is_unique);

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL && ctype == DB_CONSTRAINT_INDEX
      && constraint_name != NULL && nnames == 0)
    {
      mysql_index_name = true;
    }

  if (function_expr)
    {
      pt_enter_packing_buf ();
      free_packing_buff = true;
      func_index_info = pt_node_to_function_index (parser, spec, function_expr, do_index);
      if (func_index_info == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SM_FUNCTION_INFO));
	  error = ER_FAILED;
	  goto end;
	}
      else
	{
	  func_index_info->col_id = func_index_pos;
	  func_index_info->attr_index_start = nnames - func_index_args_count;
	}
    }

  cname = sm_produce_constraint_name (sm_get_ch_name (obj), ctype, (const char **) attnames, asc_desc, constraint_name);
  if (cname == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
    }
  else
    {
      if (do_index == DO_INDEX_CREATE)
	{
	  if (where_predicate)
	    {
	      PARSER_VARCHAR *filter_expr = NULL;
	      unsigned int save_custom;

	      /* free at parser_free_parser */
	      /* make sure paren_type is 0 so parenthesis are not printed */
	      where_predicate->info.expr.paren_type = 0;
	      save_custom = parser->custom_print;
	      parser->custom_print |= PT_CHARSET_COLLATE_FULL;
	      filter_expr = pt_print_bytes ((PARSER_CONTEXT *) parser, (PT_NODE *) where_predicate);
	      parser->custom_print = save_custom;
	      if (filter_expr)
		{
		  pred_index_info.pred_string = (char *) filter_expr->bytes;
		  if (strlen (pred_index_info.pred_string) > MAX_FILTER_PREDICATE_STRING_LENGTH)
		    {
		      error = ER_SM_INVALID_FILTER_PREDICATE_LENGTH;
		      PT_ERRORmf ((PARSER_CONTEXT *) parser, where_predicate, MSGCAT_SET_ERROR,
				  -(ER_SM_INVALID_FILTER_PREDICATE_LENGTH), MAX_FILTER_PREDICATE_STRING_LENGTH);
		      goto end;
		    }
		}

	      pt_enter_packing_buf ();
	      free_packing_buff = true;
	      filter_predicate =
		pt_to_pred_with_context ((PARSER_CONTEXT *) parser, (PT_NODE *) where_predicate, (PT_NODE *) spec);
	      if (filter_predicate)
		{
		  error =
		    xts_map_filter_pred_to_stream (filter_predicate, &(pred_index_info.pred_stream),
						   &(pred_index_info.pred_stream_size));
		  if (error != NO_ERROR)
		    {
		      PT_ERRORm ((PARSER_CONTEXT *) parser, where_predicate, MSGCAT_SET_PARSER_RUNTIME,
				 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		      goto end;
		    }
		  pred_index_info.att_ids = filter_predicate->attrids_pred;
		  pred_index_info.num_attrs = filter_predicate->num_attrs_pred;
		  p_pred_index_info = &pred_index_info;
		}
	      else
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end;
		}
	    }

	  assert (index_status != SM_NO_INDEX);

	  error = sm_add_constraint (obj, ctype, cname, (const char **) attnames, asc_desc, attrs_prefix_length, false,
				     p_pred_index_info, func_index_info, comment_str, index_status);
	}
      else
	{
	  assert (do_index == DO_INDEX_DROP);
	  error = sm_drop_constraint (obj, ctype, cname, (const char **) attnames, false, mysql_index_name);
	}
    }

end:

  /* free function index info */
  if (func_index_info)
    {
      sm_free_function_index_info (func_index_info);
      db_ws_free (func_index_info);
      func_index_info = NULL;
    }

  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
  if (pred_index_info.pred_stream)
    {
      free_and_init (pred_index_info.pred_stream);
    }

  if (free_packing_buff)
    {
      /* mark the end of another level of xasl packing */
      pt_exit_packing_buf ();
    }

  free_and_init (attnames);
  free_and_init (asc_desc);
  if (attrs_prefix_length)
    {
      free_and_init (attrs_prefix_length);
    }

  if (cname != NULL)
    {
      free_and_init (cname);
    }

  return error;
}

/*
 * do_create_index() - Creates an index
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in) : Parse tree of a create index statement
 */
int
do_create_index (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  PT_NODE *cls;
  DB_OBJECT *obj;
  const char *index_name = NULL;
  int error = NO_ERROR;

  CHECK_MODIFICATION_ERROR ();

  /* class should be already available */
  assert (statement->info.index.indexed_class);

  cls = statement->info.index.indexed_class->info.spec.entity_name;

  obj = db_find_class (cls->info.name.original);
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  index_name = statement->info.index.index_name ? statement->info.index.index_name->info.name.original : NULL;

  if (statement->info.index.index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
    {
      ib_thread_count = statement->info.index.ib_threads;
    }

  error =
    create_or_drop_index_helper (parser, index_name, statement->info.index.reverse, statement->info.index.unique,
				 statement->info.index.indexed_class, statement->info.index.column_names,
				 statement->info.index.prefix_length, statement->info.index.where,
				 statement->info.index.func_pos, statement->info.index.func_no_args,
				 statement->info.index.function_expr, statement->info.index.comment, obj,
				 statement->info.index.index_status, DO_INDEX_CREATE);
  return error;
}

/*
 * do_drop_index() - Drops an index on a class.
 *   return: Error code if it fails
 *   parser(in) : Parser context
 *   statement(in): Parse tree of a drop index statement
 */
int
do_drop_index (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  PT_NODE *cls = NULL;
  DB_OBJECT *obj = NULL;
  const char *index_name = NULL;
  int error_code = NO_ERROR;
  const char *class_name = NULL;
  DB_CONSTRAINT_TYPE index_type;
  bool is_reverse;
  bool is_unique;

  CHECK_MODIFICATION_ERROR ();

  index_name = statement->info.index.index_name ? statement->info.index.index_name->info.name.original : NULL;

  if (index_name == NULL)
    {
      error_code = ER_SM_INVALID_DEF_CONSTRAINT_NAME_PARAMS;
      return error_code;
    }

  if (statement->info.index.indexed_class)
    {
      cls = statement->info.index.indexed_class->info.spec.flat_entity_list;
    }

  assert (cls != NULL);

  index_type = get_reverse_unique_index_type (statement->info.index.reverse, statement->info.index.unique);

  class_name = cls->info.name.resolved;
  obj = db_find_class (class_name);

  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      return error_code;
    }

  if (index_type == DB_CONSTRAINT_INDEX)
    {
      error_code = get_index_type_qualifiers (obj, &is_reverse, &is_unique, index_name);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}
    }
  else
    {
      is_reverse = statement->info.index.reverse;
      is_unique = statement->info.index.unique;
    }

  error_code =
    create_or_drop_index_helper (parser, index_name, is_reverse, is_unique,
				 statement->info.index.indexed_class, statement->info.index.column_names, NULL, NULL,
				 statement->info.index.func_pos, statement->info.index.func_no_args,
				 statement->info.index.function_expr, NULL, obj, statement->info.index.index_status,
				 DO_INDEX_DROP);

  return error_code;
}

/*
 * do_alter_index_rebuild() - Alters an index on a class (drop and create).
 *                            INDEX REBUILD statement ignores any type of the
 *                            qualifier, column, and filter predicate (filtered
 *                            index). The purpose of this feature is that
 *                            reconstructing the corrupted index or improving
 *                            the efficiency of indexes. For the backward
 *                            compatibility, this function supports the
 *                            previous grammar.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a alter index statement
 */
static int
do_alter_index_rebuild (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *obj;
  PT_NODE *cls = NULL;
  int i, nnames;
  DB_CONSTRAINT_TYPE ctype, original_ctype;
  char **attnames = NULL;
  int *asc_desc = NULL;
  int *attrs_prefix_length = NULL;
  SM_CLASS *smcls;
  SM_CLASS_CONSTRAINT *idx = NULL;
  SM_ATTRIBUTE **attp;
  const char *index_name = NULL;
  bool free_pred_string = false;
  bool free_packing_buff = false;
  SM_FUNCTION_INFO *func_index_info = NULL;
  SM_PREDICATE_INFO pred_index_info = { NULL, NULL, 0, NULL, 0 };
  SM_PREDICATE_INFO *p_pred_index_info = NULL;
  const char *class_name = NULL;
  const char *comment_str = NULL;
  bool do_rollback = false;
  SM_INDEX_STATUS saved_index_status = SM_NORMAL_INDEX;

  /* TODO refactor this code, the code in create_or_drop_index_helper and the code in do_drop_index in order to remove
   * duplicate code */

  CHECK_MODIFICATION_ERROR ();

  index_name = statement->info.index.index_name ? statement->info.index.index_name->info.name.original : NULL;

  assert (index_name != NULL);

  if (statement->info.index.indexed_class)
    {
      cls = statement->info.index.indexed_class->info.spec.flat_entity_list;
    }

  assert (cls != NULL);

  class_name = cls->info.name.resolved;

  obj = db_find_class (class_name);
  if (obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }

  if (au_fetch_class (obj, &smcls, AU_FETCH_READ, AU_SELECT) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }

  idx = classobj_find_class_index (smcls, index_name);
  if (idx == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_NO_INDEX, 1, index_name);
      error = ER_SM_NO_INDEX;
      goto error_exit;
    }

  saved_index_status = idx->index_status;

  if (statement->info.index.comment != NULL)
    {
      assert (statement->info.index.comment->node_type == PT_VALUE);
      comment_str = (char *) PT_VALUE_GET_BYTES (statement->info.index.comment);
    }

  /* check the index type */
  ctype = get_reverse_unique_index_type (statement->info.index.reverse, statement->info.index.unique);
  original_ctype = db_constraint_type (idx);
  if (ctype != original_ctype)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE, 1, index_name);
    }

  /* get attributes of the index */
  attp = idx->attributes;
  if (attp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ATTRIBUTE, 1, "unknown");
      error = ER_OBJ_INVALID_ATTRIBUTE;
      goto error_exit;
    }

  nnames = 0;
  while (*attp++)
    {
      nnames++;
    }

  attnames = (char **) malloc ((nnames + 1) * sizeof (const char *));
  if (attnames == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (nnames + 1) * sizeof (const char *));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_exit;
    }

  for (i = 0, attp = idx->attributes; *attp; i++, attp++)
    {
      attnames[i] = strdup ((*attp)->header.name);
      if (attnames[i] == NULL)
	{
	  int j;
	  for (j = 0; j < i; ++j)
	    {
	      free_and_init (attnames[j]);
	    }
	  free_and_init (attnames);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (strlen ((*attp)->header.name) + 1) * sizeof (char));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}
    }
  attnames[i] = NULL;

  if (idx->asc_desc)
    {
      asc_desc = (int *) malloc ((nnames) * sizeof (int));
      if (asc_desc == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nnames * sizeof (int));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}

      for (i = 0; i < nnames; i++)
	{
	  asc_desc[i] = idx->asc_desc[i];
	}
    }

  if (original_ctype == DB_CONSTRAINT_INDEX)
    {
      assert (idx->attrs_prefix_length);

      attrs_prefix_length = (int *) malloc ((nnames) * sizeof (int));
      if (attrs_prefix_length == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, nnames * sizeof (int));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}

      for (i = 0; i < nnames; i++)
	{
	  attrs_prefix_length[i] = idx->attrs_prefix_length[i];
	}
    }

  if (idx->filter_predicate)
    {
      int pred_str_len;
      assert (idx->filter_predicate->pred_string != NULL && idx->filter_predicate->pred_stream != NULL);

      pred_str_len = strlen (idx->filter_predicate->pred_string);

      pred_index_info.pred_string = strdup (idx->filter_predicate->pred_string);
      if (pred_index_info.pred_string == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (strlen (idx->filter_predicate->pred_string) + 1) * sizeof (char));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}
      free_pred_string = true;

      pred_index_info.pred_stream = (char *) malloc (idx->filter_predicate->pred_stream_size * sizeof (char));
      if (pred_index_info.pred_stream == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  idx->filter_predicate->pred_stream_size * sizeof (char));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}

      memcpy (pred_index_info.pred_stream, idx->filter_predicate->pred_stream, idx->filter_predicate->pred_stream_size);

      pred_index_info.pred_stream_size = idx->filter_predicate->pred_stream_size;

      if (idx->filter_predicate->num_attrs == 0)
	{
	  pred_index_info.att_ids = NULL;
	}
      else
	{
	  pred_index_info.att_ids = (int *) calloc (idx->filter_predicate->num_attrs, sizeof (int));
	  if (pred_index_info.att_ids == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      idx->filter_predicate->num_attrs * sizeof (int));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error_exit;
	    }
	  for (i = 0; i < idx->filter_predicate->num_attrs; i++)
	    {
	      pred_index_info.att_ids[i] = idx->filter_predicate->att_ids[i];
	    }
	}
      pred_index_info.num_attrs = idx->filter_predicate->num_attrs;
      p_pred_index_info = &pred_index_info;
    }

  if (idx->func_index_info)
    {
      func_index_info = (SM_FUNCTION_INFO *) db_ws_alloc (sizeof (SM_FUNCTION_INFO));
      if (func_index_info == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}
      func_index_info->fi_domain = tp_domain_copy (idx->func_index_info->fi_domain, true);
      func_index_info->expr_str = strdup (idx->func_index_info->expr_str);
      if (func_index_info->expr_str == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		  (strlen (idx->func_index_info->expr_str) + 1) * sizeof (char));
	  goto error_exit;
	}
      func_index_info->expr_stream = (char *) calloc (idx->func_index_info->expr_stream_size, sizeof (char));
      if (func_index_info->expr_stream == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, idx->func_index_info->expr_stream_size * sizeof (char));
	  goto error_exit;
	}
      memcpy (func_index_info->expr_stream, idx->func_index_info->expr_stream, idx->func_index_info->expr_stream_size);
      func_index_info->expr_stream_size = idx->func_index_info->expr_stream_size;
      func_index_info->col_id = idx->func_index_info->col_id;
      func_index_info->attr_index_start = idx->func_index_info->attr_index_start;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }
  do_rollback = true;

  error = sm_drop_constraint (obj, original_ctype, index_name, (const char **) attnames, false, false);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error =
    sm_add_constraint (obj, original_ctype, index_name, (const char **) attnames, asc_desc, attrs_prefix_length, false,
		       p_pred_index_info, func_index_info, comment_str, saved_index_status);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

end:
  if (func_index_info)
    {
      sm_free_function_index_info (func_index_info);
      db_ws_free (func_index_info);
      func_index_info = NULL;
    }

  if (pred_index_info.pred_stream != NULL)
    {
      free_and_init (pred_index_info.pred_stream);
    }

  if (free_pred_string)
    {
      free_and_init (pred_index_info.pred_string);
      /* free allocated attribute ids */
      if (pred_index_info.att_ids != NULL)
	{
	  free_and_init (pred_index_info.att_ids);
	}
    }

  if (free_packing_buff)
    {
      /* mark the end of another level of xasl packing */
      pt_exit_packing_buf ();
    }

  if (attnames)
    {
      for (i = 0; attnames[i]; i++)
	{
	  free_and_init (attnames[i]);
	}
      free_and_init (attnames);
    }

  if (asc_desc)
    {
      free_and_init (asc_desc);
    }

  if (attrs_prefix_length)
    {
      free_and_init (attrs_prefix_length);
    }

  return error;

error_exit:

  if (do_rollback == true)
    {
      if (do_rollback && error != ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
	}
    }
  error = (error == NO_ERROR && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;

  goto end;
}

#if defined (ENABLE_RENAME_CONSTRAINT)
/*
 * do_alter_index_rename() - renames an index on a class.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a alter index statement
 */
static int
do_alter_index_rename (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *obj;
  PT_NODE *cls = NULL;
  SM_TEMPLATE *ctemplate = NULL;
  const char *class_name = NULL;
  const char *index_name = NULL;
  const char *new_index_name = NULL;
  const char *comment = NULL;
  bool do_rollback = false;

  index_name = statement->info.index.index_name ? statement->info.index.index_name->info.name.original : NULL;

  new_index_name = statement->info.index.new_name ? statement->info.index.new_name->info.name.original : NULL;

  if (index_name == NULL || new_index_name == NULL)
    {
      goto error_exit;
    }

  if (statement->info.index.comment != NULL)
    {
      assert (statement->info.index.comment->node_type == PT_VALUE);
      comment = (char *) PT_VALUE_GET_BYTES (statement->info.index.comment);
    }

  cls = statement->info.index.indexed_class ? statement->info.index.indexed_class->info.spec.flat_entity_list : NULL;

  if (cls == NULL)
    {
      goto error_exit;
    }

  class_name = cls->info.name.resolved;
  obj = db_find_class (class_name);

  if (obj == NULL)
    {
      error = er_errid ();
      assert (error != NO_ERROR);
      goto error_exit;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  do_rollback = true;


  ctemplate = smt_edit_class_mop (obj, AU_INDEX);
  if (ctemplate == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      assert (error != NO_ERROR);
      goto error_exit;
    }

  error = smt_rename_constraint (ctemplate, index_name, new_index_name, SM_INDEX_NAME);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (comment != NULL)
    {
      error = smt_change_constraint_comment (ctemplate, new_index_name, comment);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  /* classobj_free_template() is included in sm_update_class() */
  error = sm_update_class (ctemplate, NULL);
  if (error != NO_ERROR)
    {
      /* Even though sm_update() did not return NO_ERROR, ctemplate is already freed */
      ctemplate = NULL;
      goto error_exit;
    }

end:

  return error;

error_exit:
  if (ctemplate != NULL)
    {
      /* smt_quit() always returns NO_ERROR */
      smt_quit (ctemplate);
    }

  if (do_rollback == true)
    {
      if (do_rollback && error != ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
	}
    }
  error = (error == NO_ERROR && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;

  goto end;
}
#endif

/*
 * do_alter_index_comment() - alter an index comment on a class.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a alter index statement
 */
static int
do_alter_index_comment (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *obj;
  PT_NODE *cls = NULL;
  SM_TEMPLATE *ctemplate = NULL;
  const char *class_name = NULL;
  const char *index_name = NULL;
  const char *comment = NULL;
  bool do_rollback = false;

  index_name = statement->info.index.index_name ? statement->info.index.index_name->info.name.original : NULL;

  if (index_name == NULL)
    {
      goto error_exit;
    }

  if (statement->info.index.comment != NULL)
    {
      assert (statement->info.index.comment->node_type == PT_VALUE);
      comment = (char *) PT_VALUE_GET_BYTES (statement->info.index.comment);
    }

  cls = statement->info.index.indexed_class ? statement->info.index.indexed_class->info.spec.flat_entity_list : NULL;

  if (cls == NULL)
    {
      goto error_exit;
    }

  class_name = cls->info.name.resolved;
  obj = db_find_class (class_name);

  if (obj == NULL)
    {
      error = er_errid ();
      assert (error != NO_ERROR);
      goto error_exit;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  do_rollback = true;

  ctemplate = smt_edit_class_mop (obj, AU_INDEX);
  if (ctemplate == NULL)
    {
      error = er_errid ();
      assert (error != NO_ERROR);
      goto error_exit;
    }

  error = smt_change_constraint_comment (ctemplate, index_name, comment);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* classobj_free_template() is included in sm_update_class() */
  error = sm_update_class (ctemplate, NULL);
  if (error != NO_ERROR)
    {
      /* Even though sm_update() did not return NO_ERROR, ctemplate is already freed */
      ctemplate = NULL;
      goto error_exit;
    }

end:

  return error;

error_exit:
  if (ctemplate != NULL)
    {
      /* smt_quit() always returns NO_ERROR */
      smt_quit (ctemplate);
    }

  if (do_rollback == true)
    {
      if (do_rollback && error != ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
	}
    }
  error = (error == NO_ERROR && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;

  goto end;
}

/*
 * do_alter_index() - Alters an index on a class.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a alter index statement
 */
int
do_alter_index (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;

  CHECK_MODIFICATION_ERROR ();

  if (statement->info.index.code == PT_REBUILD_INDEX)
    {
      error = do_alter_index_rebuild (parser, statement);
    }
#if defined (ENABLE_RENAME_CONSTRAINT)
  else if (statement->info.index.code == PT_RENAME_INDEX)
    {
      error = do_alter_index_rename (parser, statement);
    }
#endif
  else if (statement->info.index.code == PT_CHANGE_INDEX_COMMENT)
    {
      error = do_alter_index_comment (parser, statement);
    }
  else if (statement->info.index.code == PT_CHANGE_INDEX_STATUS)
    {
      error = do_alter_index_status (parser, statement);
    }
  else
    {
      return ER_FAILED;
    }

  return error;
}

/*
 * do_create_partition() -  Creates partitions
 *   return: Error code if partitions are not created
 *   parser(in): Parser context
 *   alter(in): The parse tree of a create class
 *   pinfo(in): partition alter context
 *
 * Note:
 */
static int
do_create_partition (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  int error;
  PT_NODE *alter_info, *hash_parts, *newparts, *hashtail;
  PT_NODE *parts, *parts_save, *fmin;
  PT_NODE *parttemp, *entity_name = NULL;
  PART_CLASS_INFO pci = { NULL, NULL, NULL, NULL };
  PART_CLASS_INFO *newpci, *wpci;
  char class_name[DB_MAX_IDENTIFIER_LENGTH];
  DB_VALUE *minval, *parts_val, *fmin_val, partsize;
  int part_cnt = 0, part_add = -1;
  size_t buf_size;
  SM_CLASS *smclass;
  bool reuse_oid = false;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;

  CHECK_MODIFICATION_ERROR ();

  alter_info = hash_parts = newparts = hashtail = NULL;
  parts = parts_save = fmin = NULL;

  if (alter->node_type == PT_ALTER)
    {
      alter_info = alter->info.alter.alter_clause.partition.info;
      if (alter->info.alter.code == PT_ADD_PARTITION || alter->info.alter.code == PT_REORG_PARTITION)
	{
	  parts = alter->info.alter.alter_clause.partition.parts;
	  part_add = parts->info.parts.type;
	}
      else if (alter->info.alter.code == PT_ADD_HASHPARTITION)
	{
	  part_add = PT_PARTITION_HASH;
	}
      entity_name = alter->info.alter.entity_name;
      intl_identifier_lower ((char *) entity_name->info.name.original, class_name);
    }
  else if (alter->node_type == PT_CREATE_ENTITY)
    {
      entity_name = alter->info.create_entity.entity_name;
      alter_info = alter->info.create_entity.partition_info;
      intl_identifier_lower ((char *) entity_name->info.name.original, class_name);
    }
  else
    {
      return NO_ERROR;
    }

  if (part_add == -1)
    {				/* create or apply partition */
      if (!alter_info)
	{
	  return NO_ERROR;
	}

      parts = alter_info->info.partition.parts;
    }

  parts_save = parts;
  parttemp = parser_new_node (parser, PT_CREATE_ENTITY);
  if (parttemp == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end_create;
    }

  error = au_fetch_class (pinfo->root_op, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end_create;
    }

  /* If the current class is part of a hierarchy and the class is
   * not partitioned, end this as we do not allow partitions on hierarchies.
   */
  if (smclass->partition == NULL && (smclass->users != NULL || smclass->inheritance != NULL))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_NO_PARTITION_ON_HIERARCHIES, 0);
      error = ER_SM_NO_PARTITION_ON_HIERARCHIES;
      goto end_create;
    }

  reuse_oid = (smclass->flags & SM_CLASSFLAG_REUSE_OID) ? true : false;
  tde_algo = (TDE_ALGORITHM) smclass->tde_algorithm;

  parttemp->info.create_entity.entity_type = PT_CLASS;
  parttemp->info.create_entity.entity_name = parser_new_node (parser, PT_NAME);
  parttemp->info.create_entity.supclass_list = parser_new_node (parser, PT_NAME);
  if (parttemp->info.create_entity.entity_name == NULL || parttemp->info.create_entity.supclass_list == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end_create;
    }
  parttemp->info.create_entity.supclass_list->info.name.db_object = pinfo->root_op;

  error = NO_ERROR;
  if (part_add == PT_PARTITION_HASH
      || (alter_info && alter_info->node_type != PT_VALUE && alter_info->info.partition.type == PT_PARTITION_HASH))
    {
      int pi, org_hashsize, new_hashsize;

      hash_parts = parser_new_node (parser, PT_PARTS);
      if (hash_parts == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto end_create;
	}
      hash_parts->info.parts.name = parser_new_node (parser, PT_NAME);
      if (hash_parts->info.parts.name == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto end_create;
	}

      hash_parts->info.parts.type = PT_PARTITION_HASH;
      if (part_add == PT_PARTITION_HASH)
	{
	  org_hashsize = do_get_partition_size (pinfo->root_op);
	  if (org_hashsize < 0)
	    {
	      error = org_hashsize;
	      goto end_create;
	    }
	  new_hashsize = alter->info.alter.alter_clause.partition.size->info.value.data_value.i;
	}
      else
	{
	  org_hashsize = 0;
	  new_hashsize = alter_info->info.partition.hashsize->info.value.data_value.i;
	}

      for (pi = 0; pi < new_hashsize; pi++)
	{
	  newpci = (PART_CLASS_INFO *) malloc (sizeof (PART_CLASS_INFO));
	  if (newpci == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PART_CLASS_INFO));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto end_create;
	    }

	  memset (newpci, 0x0, sizeof (PART_CLASS_INFO));

	  newpci->next = pci.next;
	  pci.next = newpci;

	  buf_size = strlen (class_name) + 5 + 13;
	  newpci->pname = (char *) malloc (buf_size);
	  if (newpci->pname == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto end_create;
	    }

	  sprintf (newpci->pname, "%s" PARTITIONED_SUB_CLASS_TAG "p%d", class_name, pi + org_hashsize);
	  if (strlen (newpci->pname) >= PARTITION_VARCHAR_LEN)
	    {
	      error = ER_INVALID_PARTITION_REQUEST;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto end_create;
	    }
	  newpci->temp = dbt_create_class (newpci->pname);
	  if (newpci->temp == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto end_create;
	    }

	  parttemp->info.create_entity.entity_name->info.name.original = newpci->pname;
	  parttemp->info.create_entity.supclass_list->info.name.original = class_name;

	  error = do_create_local (parser, newpci->temp, parttemp, NULL);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (newpci->temp);
	      goto end_create;
	    }

	  newpci->temp->partition_parent_atts = smclass->attributes;

	  hash_parts->info.parts.name->info.name.original =
	    strstr (newpci->pname, PARTITIONED_SUB_CLASS_TAG) + strlen (PARTITIONED_SUB_CLASS_TAG);
	  hash_parts->info.parts.values = NULL;

	  newpci->temp->partition =
	    pt_node_to_partition_info (parser, hash_parts, NULL, class_name, newpci->pname, NULL);
	  if (newpci->temp->partition == NULL)
	    {
	      error = er_errid ();
	      dbt_abort_class (newpci->temp);
	      goto end_create;
	    }

	  newpci->obj = dbt_finish_class (newpci->temp);
	  if (newpci->obj == NULL)
	    {
	      dbt_abort_class (newpci->temp);
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto end_create;
	    }
	  else if (er_errid () == ER_LC_UNKNOWN_CLASSNAME)
	    {
	      /* The class doesn't exist. We are creating the class. */
	      er_clear ();
	    }

	  if (reuse_oid)
	    {
	      error = sm_set_class_flag (newpci->obj, SM_CLASSFLAG_REUSE_OID, 1);
	      if (error != NO_ERROR)
		{
		  goto end_create;
		}
	    }
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      error = sm_set_class_tde_algorithm (newpci->obj, tde_algo);
	      if (error != NO_ERROR)
		{
		  goto end_create;
		}
	    }

	  if (locator_create_heap_if_needed (newpci->obj, reuse_oid) == NULL
	      || locator_flush_class (newpci->obj) != NO_ERROR)
	    {
	      error = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	      goto end_create;
	    }

	  if (part_add == PT_PARTITION_HASH)
	    {
	      hash_parts->next = NULL;
	      hash_parts->info.parts.name->info.name.db_object = newpci->obj;
	      newparts = parser_copy_tree (parser, hash_parts);
	      if (alter->info.alter.alter_clause.partition.parts == NULL)
		{
		  alter->info.alter.alter_clause.partition.parts = newparts;
		}
	      else
		{
		  if (hashtail != NULL)
		    {
		      hashtail->next = newparts;
		    }
		}

	      hashtail = newparts;
	    }
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      error = file_apply_tde_to_class_files (&newpci->obj->oid_info.oid);
	      if (error != NO_ERROR)
		{
		  goto end_create;
		}
	    }
	  error = NO_ERROR;
	}
    }
  else
    {				/* RANGE or LIST */
      char *part_name;

      for (; parts; parts = parts->next, part_cnt++)
	{
	  newpci = (PART_CLASS_INFO *) malloc (sizeof (PART_CLASS_INFO));
	  if (newpci == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (PART_CLASS_INFO));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto end_create;
	    }

	  memset (newpci, 0x0, sizeof (PART_CLASS_INFO));

	  newpci->next = pci.next;
	  pci.next = newpci;

	  part_name = (char *) parts->info.parts.name->info.name.original;
	  buf_size = strlen (class_name) + 5 + 1 + strlen (part_name);

	  newpci->pname = (char *) malloc (buf_size);
	  if (newpci->pname == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto end_create;
	    }
	  sprintf (newpci->pname, "%s" PARTITIONED_SUB_CLASS_TAG "%s", class_name, part_name);

	  if (strlen (newpci->pname) >= PARTITION_VARCHAR_LEN)
	    {
	      error = ER_INVALID_PARTITION_REQUEST;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto end_create;
	    }

	  if (alter->info.alter.code == PT_REORG_PARTITION && parts->flag.partition_pruned)
	    {			/* reused partition */
	      newpci->obj = ws_find_class (newpci->pname);
	      if (newpci->obj == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end_create;
		}

	      newpci->temp = dbt_edit_class (newpci->obj);
	      if (newpci->temp == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end_create;
		}

	      newpci->temp->partition =
		pt_node_to_partition_info (parser, parts, NULL, class_name, newpci->pname, NULL);
	      if (newpci->temp->partition == NULL)
		{
		  error = er_errid ();
		  dbt_abort_class (newpci->temp);
		  goto end_create;
		}

	      newpci->obj = dbt_finish_class (newpci->temp);
	      if (newpci->obj == NULL)
		{
		  dbt_abort_class (newpci->temp);
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end_create;
		}

	      continue;
	    }

	  newpci->temp = dbt_create_class (newpci->pname);
	  if (newpci->temp == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto end_create;
	    }

	  parttemp->info.create_entity.entity_name->info.name.original = newpci->pname;
	  parttemp->info.create_entity.supclass_list->info.name.original = class_name;

	  error = do_create_local (parser, newpci->temp, parttemp, NULL);
	  if (error != NO_ERROR)
	    {
	      dbt_abort_class (newpci->temp);
	      goto end_create;
	    }

	  newpci->temp->partition_parent_atts = smclass->attributes;

	  /* RANGE-MIN VALUE search */
	  minval = NULL;
	  if ((alter_info && alter_info->node_type != PT_VALUE && alter_info->info.partition.type == PT_PARTITION_RANGE)
	      || part_add == PT_PARTITION_RANGE)
	    {
	      parts_val = pt_value_to_db (parser, parts->info.parts.values);
	      for (fmin = parts_save; fmin; fmin = fmin->next)
		{
		  if (fmin == parts)
		    {
		      continue;
		    }
		  if (fmin->info.parts.values == NULL)
		    {
		      continue;	/* RANGE-MAXVALUE */
		    }
		  fmin_val = pt_value_to_db (parser, fmin->info.parts.values);
		  if (fmin_val == NULL)
		    {
		      continue;
		    }
		  if (parts->info.parts.values == NULL || db_value_compare (parts_val, fmin_val) == DB_GT)
		    {
		      if (minval == NULL)
			{
			  minval = fmin_val;
			}
		      else
			{
			  if (db_value_compare (minval, fmin_val) == DB_LT)
			    {
			      minval = fmin_val;
			    }
			}
		    }
		}
	    }
	  if (part_add == PT_PARTITION_RANGE && minval == NULL && alter_info && alter_info->node_type == PT_VALUE)
	    {
	      /* set in pt_check_alter_partition */
	      minval = pt_value_to_db (parser, alter_info);
	    }

	  newpci->temp->partition = pt_node_to_partition_info (parser, parts, NULL, class_name, newpci->pname, minval);
	  if (newpci->temp->partition == NULL)
	    {
	      error = er_errid ();
	      dbt_abort_class (newpci->temp);
	      goto end_create;
	    }

	  newpci->obj = dbt_finish_class (newpci->temp);

	  if (newpci->obj == NULL)
	    {
	      dbt_abort_class (newpci->temp);
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto end_create;
	    }
	  else if (er_errid () == ER_LC_UNKNOWN_CLASSNAME)
	    {
	      /* The class doesn't exist. We are creating the class. */
	      er_clear ();
	    }

	  parts->info.parts.name->info.name.db_object = newpci->obj;

	  sm_set_class_collation (newpci->obj, smclass->collation_id);

	  if (reuse_oid)
	    {
	      error = sm_set_class_flag (newpci->obj, SM_CLASSFLAG_REUSE_OID, 1);
	      if (error != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end_create;
		}
	    }
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      error = sm_set_class_tde_algorithm (newpci->obj, tde_algo);
	      if (error != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end_create;
		}
	    }
	  if (locator_create_heap_if_needed (newpci->obj, reuse_oid) == NULL
	      || locator_flush_class (newpci->obj) != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto end_create;
	    }
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      error = file_apply_tde_to_class_files (&newpci->obj->oid_info.oid);
	      if (error != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  goto end_create;
		}
	    }

	  error = NO_ERROR;
	}
    }

  if (part_add != -1)
    {
      /* partition size update */
      error = adjust_partition_size (pinfo->root_op, pinfo->root_tmpl);
      if (error != NO_ERROR)
	{
	  goto end_create;
	}

      if (alter->info.alter.code == PT_REORG_PARTITION && part_add == PT_PARTITION_RANGE)
	{
	  error = au_fetch_class (pinfo->root_op, &smclass, AU_FETCH_READ, AU_SELECT);
	  if (error != NO_ERROR)
	    {
	      goto end_create;
	    }
	  error = adjust_partition_range (smclass->users);
	  if (error != NO_ERROR)
	    {
	      goto end_create;
	    }
	}
    }
  else
    {
      /* set parent's partition info */
      DB_CTMPL *root_tmpl = NULL;
      bool abort_template = false;

      db_make_int (&partsize, part_cnt);

      if (pinfo->root_tmpl != NULL)
	{
	  root_tmpl = pinfo->root_tmpl;
	}
      else
	{
	  root_tmpl = dbt_edit_class (pinfo->root_op);
	  if (root_tmpl == NULL)
	    {
	      error = er_errid ();
	      goto end_create;
	    }
	  abort_template = true;
	}

      root_tmpl->partition =
	pt_node_to_partition_info (parser, alter_info, entity_name, class_name, class_name, &partsize);
      if (root_tmpl->partition == NULL)
	{
	  error = er_errid ();
	  if (abort_template == true)
	    {
	      dbt_abort_class (root_tmpl);
	    }
	  goto end_create;
	}

      if (abort_template == true)
	{
	  if (dbt_finish_class (root_tmpl) == NULL)
	    {
	      dbt_abort_class (root_tmpl);
	      error = er_errid ();
	      goto end_create;
	    }
	}
    }

end_create:
  for (wpci = pci.next; wpci;)
    {
      if (wpci->pname)
	{
	  free_and_init (wpci->pname);
	}
      newpci = wpci;
      wpci = wpci->next;
      free_and_init (newpci);
    }
  if (parttemp != NULL)
    {
      parser_free_tree (parser, parttemp);
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  assert (er_errid_if_has_error () == NO_ERROR);

  return NO_ERROR;
}

/*
 * compile_partition_expression () - compile the partition expression and
 *				     serialize it to a stream
 *
 * return : serialized expression or NULL
 * parser (in)	    : parser context
 * entity_name (in) : the name of the partitioned table
 * pinfo (in)	    : partition information node
 *
 */
static SM_FUNCTION_INFO *
compile_partition_expression (PARSER_CONTEXT * parser, PT_NODE * entity_name, PT_NODE * pinfo)
{
  PT_NODE *spec = NULL, *expr = NULL;
  SM_FUNCTION_INFO *part_expr = NULL;

  if (pinfo->node_type != PT_PARTITION)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  spec = pt_entity (parser, entity_name, NULL, NULL);
  if (spec == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, sizeof (PT_NODE));
      return NULL;
    }

  /* perform semantic check on the expression */
  expr = pinfo->info.partition.expr;
  mq_clear_ids (parser, expr, NULL);
  if (pt_semantic_quick_check_node (parser, &spec, &expr) == NULL)
    {
      PT_ERRORm (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_PARTITION_DEFINITION);
      return NULL;
    }

  /* pack the partition expression */
  pt_enter_packing_buf ();
  part_expr = pt_node_to_function_index (parser, spec, expr, DO_INDEX_CREATE);
  pt_exit_packing_buf ();

  parser_free_node (parser, spec);

  return part_expr;
}

/*
 * do_check_partitioned_class() - Checks partitioned class
 *   return: Error code if check_map or keyattr is checked
 *   classop(in): MOP of class
 *   class_map(in/out): Checking method(CHECK_PARTITION_NONE, _PARTITION_PARENT,
 *			 _PARTITION_SUBS)
 *   keyattr(in): Partition key attribute to check
 *
 * Note:
 */
int
do_check_partitioned_class (DB_OBJECT * classop, int check_map, char *keyattr)
{
  int error = NO_ERROR;
  int is_partition = 0;
  char attr_name[DB_MAX_IDENTIFIER_LENGTH + 1];

  if (classop == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);

      return ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
    }

  error = sm_partitioned_class_type (classop, &is_partition, (keyattr) ? attr_name : NULL, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (is_partition > 0)
    {
      if (((check_map & CHECK_PARTITION_PARENT) && is_partition == 1)
	  || ((check_map & CHECK_PARTITION_SUBS) && is_partition == 2))
	{
	  error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	}
      else if (keyattr)
	{
	  if (intl_identifier_casecmp (keyattr, attr_name) == 0)
	    {
	      error = ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	    }
	}

      if (error != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
	}
    }

  return error;
}

/*
 * do_get_partition_parent () -
 *   return: NO_ERROR or error code
 *   classop(in): MOP of class
 *   parentop(out): MOP of the parent of the sub-partition or NULL if not a
 *                  sub-partition
 */
int
do_get_partition_parent (DB_OBJECT * const classop, MOP * const parentop)
{
  int error = NO_ERROR;
  int au_save = 0;
  SM_CLASS *smclass = NULL;

  if (classop == NULL)
    {
      assert_release (classop != NULL);
      return ER_FAILED;
    }

  if (parentop == NULL || *parentop != NULL)
    {
      assert_release (parentop == NULL || *parentop != NULL);
      return ER_FAILED;
    }
  *parentop = NULL;

  AU_DISABLE (au_save);

  error = au_fetch_class (classop, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (smclass->partition == NULL)
    {
      /* not a partitioned class */
      goto end;
    }
  if (smclass->inheritance == NULL || smclass->users != NULL)
    {
      /* this is the partitioned table, not a partition */
      goto end;
    }

  if (smclass->inheritance->next != NULL)
    {
      assert (false);
      goto error_exit;
    }

  *parentop = smclass->inheritance->op;

end:
  AU_ENABLE (au_save);
  smclass = NULL;

  return error;

error_exit:
  AU_ENABLE (au_save);
  smclass = NULL;
  *parentop = NULL;

  return error;
}

/*
 * do_is_partitioned_subclass() -
 *   return: 1 if success, else error code
 *   is_partitioned(in/out):
 *   classname(in):
 *   keyattr(in):
 *
 * Note:
 */
int
do_is_partitioned_subclass (int *is_partitioned, const char *classname, char *keyattr)
{
  MOP classop;
  SM_CLASS *smclass;
  DB_VALUE attrname;
  int ret = 0;

  if (!classname)
    {
      return 0;
    }
  if (is_partitioned)
    {
      *is_partitioned = 0;
    }

  classop = db_find_class (classname);
  if (classop == NULL)
    {
      return 0;
    }

  if (au_fetch_class (classop, &smclass, AU_FETCH_READ, AU_SELECT) != NO_ERROR || !smclass->partition)
    {
      return 0;
    }

  if (smclass->partition->pname != NULL)
    {
      ret = 1;			/* partitioned sub-class */
    }
  else
    {
      if (is_partitioned)
	{
	  *is_partitioned = 1;
	}

      if (keyattr)
	{
	  const char *p = NULL;

	  keyattr[0] = 0;

	  if (set_get_element_nocopy (smclass->partition->values, 0, &attrname) == NO_ERROR && !DB_IS_NULL (&attrname)
	      && (p = db_get_string (&attrname)))
	    {
	      strncpy (keyattr, p, DB_MAX_IDENTIFIER_LENGTH);
	      if (strlen (p) < DB_MAX_IDENTIFIER_LENGTH)
		{
		  keyattr[strlen (p)] = 0;
		}
	      else
		{
		  keyattr[DB_MAX_IDENTIFIER_LENGTH] = 0;
		}
	    }
	}
    }

  return ret;
}

/*
 * do_drop_partition() -
 *   return: Error code
 *   class(in):
 *   drop_sub_flag(in):
 *   is_cascade_constraints(in):
 *
 * Note:
 */
int
do_drop_partitioned_class (MOP class_, int drop_sub_flag, bool is_cascade_constraints)
{
  DB_OBJLIST *objs;
  SM_CLASS *smclass, *subclass;
  MOP delobj;
  int error = NO_ERROR;

  CHECK_MODIFICATION_ERROR ();

  if (!class_)
    {
      return ER_FAILED;
    }

  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto fail_return;
    }
  if (!smclass->partition)
    {
      goto fail_return;
    }

  if (smclass->users == NULL)
    {
      /* this is a partition, not the partitioned table */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
      error = ER_FAILED;
      goto fail_return;
    }

  for (objs = smclass->users; objs;)
    {
      error = au_fetch_class (objs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto fail_return;
	}
      if (subclass->partition)
	{
	  delobj = objs->op;
	  objs = objs->next;
	  if (drop_sub_flag)
	    {
	      error = sm_delete_class_mop (delobj, is_cascade_constraints);
	      if (error != NO_ERROR)
		{
		  goto fail_return;
		}
	    }
	}
      else
	{
	  objs = objs->next;
	}
    }

  error = NO_ERROR;

fail_return:
  return error;
}

/*
 * partition_rename() -
 *   return: Error code
 *   old_class(in):
 *   newname(in):
 *
 * Note:
 */
int
do_rename_partition (MOP old_class, const char *newname)
{
  DB_OBJLIST *objs;
  SM_CLASS *smclass, *subclass;
  int newlen;
  int error;
  char new_subname[PARTITION_VARCHAR_LEN + 1], *ptr;

  if (!old_class || !newname)
    {
      return ER_FAILED;
    }

  newlen = strlen (newname);

  error = au_fetch_class (old_class, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto end_rename;
    }

  for (objs = smclass->users; objs; objs = objs->next)
    {
      error = au_fetch_class (objs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto end_rename;
	}
      if (subclass->partition)
	{
	  ptr = strstr ((char *) sm_ch_name ((MOBJ) subclass), PARTITIONED_SUB_CLASS_TAG);
	  if (ptr == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
	      error = ER_PARTITION_WORK_FAILED;
	      goto end_rename;
	    }

	  if ((newlen + strlen (ptr)) >= PARTITION_VARCHAR_LEN)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
	      error = ER_PARTITION_WORK_FAILED;
	      goto end_rename;
	    }
	  sprintf (new_subname, "%s%s", newname, ptr);

	  error = sm_rename_class (objs->op, new_subname);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
    }

end_rename:

  return error;
}

/*
 * do_redistribute_partitions_data() -
 *   return: error code or NO_ERROR
 *   classname(in):
 *   keyname(in):
 *   promoted(in):
 *   promoted_count(in):
 *   alter_op(in):
 *   should_update(in):
 *   should_insert(in):
 * Note:
 */
static int
do_redistribute_partitions_data (const char *classname, const char *keyname, char **promoted, int promoted_count,
				 PT_ALTER_CODE alter_op, bool should_update, bool should_insert)
{
  int error = NO_ERROR;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  char *query_buf;
  size_t query_size;
  int i = 0;
  MOP subclass_mop, class_mop;
  OID *partitions = NULL;
  SM_CONSTRAINT_INFO *index_save_info = NULL;

  if (should_update)
    {
      query_size = 0;
      query_size += 7;		/* 'UPDATE ' */
      query_size += strlen (classname) + 2;
      query_size += 5;		/* ' SET ' */
      query_size += strlen (keyname) * 2 + 6;	/* [keyname]=[keyname]; */
      query_buf = (char *) malloc (query_size + 1);
      if (query_buf == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_size + 1);
	  return ER_FAILED;
	}
      sprintf (query_buf, "UPDATE [%s] SET [%s]=[%s];", classname, keyname, keyname);

      error = db_compile_and_execute_local (query_buf, &query_result, &query_error);
      if (error >= 0)
	{
	  error = NO_ERROR;
	  db_query_end (query_result);
	}
      free_and_init (query_buf);
      if (error < 0)
	{
	  return error;
	}
    }

  if (should_insert)
    {
      class_mop = sm_find_class (classname);
      if (class_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto exit;
	}

      if (alter_op != PT_REORG_PARTITION)
	{
	  error = do_save_all_indexes (class_mop, &index_save_info);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  error = do_drop_saved_indexes (class_mop, index_save_info);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	}

      partitions = (OID *) malloc (promoted_count * sizeof (OID));
      if (partitions == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, promoted_count * sizeof (OID));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      for (i = 0; i < promoted_count; i++)
	{
	  subclass_mop = sm_find_class (promoted[i]);
	  if (subclass_mop == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto exit;
	    }
	  COPY_OID (&partitions[i], &subclass_mop->oid_info.oid);
	}

      error = locator_redistribute_partition_data (&class_mop->oid_info.oid, promoted_count, partitions);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      if (alter_op != PT_REORG_PARTITION)
	{
	  error = do_recreate_saved_indexes (class_mop, index_save_info);
	}
    }

exit:
  if (partitions != NULL)
    {
      free_and_init (partitions);
    }
  if (index_save_info != NULL)
    {
      sm_free_constraint_info (&index_save_info);
    }
  return error;
}


/*
 * do_find_auto_increment_serial() -
 *   return: Error code
 *   auto_increment_obj(out):
 *   class_name(in):
 *   atrr_name(in):
 *
 * Note:
 */
static int
do_find_auto_increment_serial (MOP * auto_increment_obj, const char *class_name, const char *attr_name)
{
  MOP serial_class = NULL;
  char *serial_name = NULL;
  size_t serial_name_size;
  DB_IDENTIFIER serial_obj_id;
  int error = NO_ERROR;

  assert (class_name != NULL && attr_name != NULL);

  *auto_increment_obj = NULL;

  serial_class = sm_find_class (CT_SERIAL_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  serial_name_size = strlen (class_name) + strlen (attr_name) + AUTO_INCREMENT_SERIAL_NAME_EXTRA_LENGTH + 1;

  serial_name = (char *) malloc (serial_name_size);
  if (serial_name == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, serial_name_size);
      goto end;
    }

  SET_AUTO_INCREMENT_SERIAL_NAME (serial_name, class_name, attr_name);

  *auto_increment_obj = do_get_serial_obj_id (&serial_obj_id, serial_class, serial_name);
  if (*auto_increment_obj == NULL)
    {
      error = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, serial_name);
      goto end;
    }

end:
  if (serial_name != NULL)
    {
      free_and_init (serial_name);
    }

  return error;
}

/*
 * adjust_partition_range() -
 *   return: Error code
 *   objs(in):
 *
 * Note:
 */
static int
adjust_partition_range (DB_OBJLIST * objs)
{
  DB_OBJLIST *subs;
  SM_CLASS *subclass;
  DB_VALUE minval, maxval, seqval, *wrtval;
  int error = NO_ERROR;
  char check_flag = 1;
  DB_VALUE_SLIST *ranges = NULL, *rfind, *new_range, *prev_range;
  DB_COLLECTION *dbc = NULL;
  SM_TEMPLATE *tmpl = NULL;

  db_make_null (&minval);
  db_make_null (&maxval);

  for (subs = objs; subs; subs = subs->next)
    {
      error = au_fetch_class (subs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  break;
	}
      if (!subclass->partition)
	{
	  continue;
	}

      if (check_flag)
	{			/* RANGE check */
	  if (subclass->partition->partition_type != PT_PARTITION_RANGE)
	    {
	      break;
	    }
	  check_flag = 0;
	}

      if (subclass->partition->expr != NULL)
	{
	  continue;		/* reorg deleted partition */
	}
      error = set_get_element_nocopy (subclass->partition->values, 0, &minval);
      if (error != NO_ERROR)
	{
	  break;
	}
      error = set_get_element_nocopy (subclass->partition->values, 1, &maxval);
      if (error != NO_ERROR)
	{
	  break;
	}
      if ((new_range = (DB_VALUE_SLIST *) malloc (sizeof (DB_VALUE_SLIST))) == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (DB_VALUE_SLIST));
	  break;
	}
      new_range->partition = subclass->partition;
      new_range->class_obj = subs->op;
      new_range->min = db_value_copy (&minval);
      new_range->max = db_value_copy (&maxval);
      new_range->next = NULL;

      if (ranges == NULL)
	{
	  ranges = new_range;
	}
      else
	{			/* sort ranges */
	  for (rfind = ranges, prev_range = NULL; rfind; rfind = rfind->next)
	    {
	      if (DB_IS_NULL (rfind->max) || db_value_compare (rfind->max, new_range->max) == DB_GT)
		{
		  if (prev_range == NULL)
		    {
		      new_range->next = ranges;
		      ranges = new_range;
		    }
		  else
		    {
		      new_range->next = prev_range->next;
		      prev_range->next = new_range;
		    }
		  break;
		}
	      prev_range = rfind;
	    }

	  if (rfind == NULL)
	    {
	      prev_range->next = new_range;
	    }
	}
    }

  for (rfind = ranges, prev_range = NULL; rfind; rfind = rfind->next)
    {
      wrtval = NULL;
      if (prev_range == NULL)
	{			/* Min value of first range is low infinite */
	  if (!DB_IS_NULL (rfind->min))
	    {
	      db_make_null (&minval);
	      wrtval = &minval;
	    }
	}
      else
	{
	  if (db_value_compare (prev_range->max, rfind->min) != DB_EQ)
	    {
	      wrtval = prev_range->max;
	    }
	}
      if (wrtval != NULL)
	{			/* adjust min value of range */
	  dbc = set_create_sequence (0);
	  if (dbc != NULL)
	    {
	      set_add_element (dbc, wrtval);
	      set_add_element (dbc, rfind->max);
	      db_make_sequence (&seqval, dbc);

	      tmpl = dbt_edit_class (rfind->class_obj);
	      if (tmpl == NULL)
		{
		  set_free (dbc);
		  error = ER_FAILED;
		  break;
		}

	      if (tmpl->partition->values != NULL)
		{
		  /* free previous set */
		  set_free (tmpl->partition->values);
		  tmpl->partition->values = NULL;
		}

	      tmpl->partition->values = db_seq_copy (dbc);
	      if (tmpl->partition->values == NULL)
		{
		  set_free (dbc);
		  dbt_abort_class (tmpl);
		  error = ER_FAILED;
		  break;
		}

	      if (dbt_finish_class (tmpl) == NULL)
		{
		  set_free (dbc);
		  dbt_abort_class (tmpl);
		  error = ER_FAILED;
		  break;
		}
	      set_free (dbc);
	    }

	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
      prev_range = rfind;
    }

  for (rfind = ranges; rfind;)
    {
      db_value_free (rfind->min);
      db_value_free (rfind->max);
      prev_range = rfind->next;
      free_and_init (rfind);
      rfind = prev_range;
    }

  return error;
}

/*
 * adjust_partition_size() -
 *   return: Error code
 *   class(in):
 *   tmpl (in): partitioned class template
 *
 * Note:
 */
static int
adjust_partition_size (MOP class_, DB_CTMPL * tmpl)
{
  int error = NO_ERROR;
  SM_CLASS *smclass, *subclass;
  DB_VALUE psize;
  DB_OBJLIST *subs;
  int partcnt;

  if (class_ == NULL)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (smclass->partition == NULL || tmpl == NULL)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_null (&psize);

  for (subs = smclass->users, partcnt = 0; subs; subs = subs->next)
    {
      error = au_fetch_class (subs->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (!subclass->partition)
	{
	  continue;
	}
      partcnt++;
    }

  error = set_get_element_nocopy (tmpl->partition->values, 1, &psize);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (psize.data.i != partcnt)
    {
      psize.data.i = partcnt;
      error = set_put_element (tmpl->partition->values, 1, &psize);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * do_get_partition_size() -
 *   return: Size if success, else error code
 *   class(in):
 *
 * Note:
 */
int
do_get_partition_size (MOP class_)
{
  int error = NO_ERROR;
  SM_CLASS *smclass;
  DB_VALUE psize;

  if (class_ == NULL)
    {
      return ER_FAILED;
    }
  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_null (&psize);

  error = set_get_element_nocopy (smclass->partition->values, 1, &psize);
  if (error != NO_ERROR)
    {
      return error;
    }
  error = psize.data.i;
  if (error == 0)
    {
      error = ER_FAILED;
    }

  return error;
}

/*
 * do_get_partition_keycol() -
 *   return: Error code
 *   keycol(out):
 *   class(in):
 *
 * Note:
 */
int
do_get_partition_keycol (char *keycol, MOP class_)
{
  int error = NO_ERROR;
  SM_CLASS *smclass;
  DB_VALUE keyname;
  const char *keyname_str;

  if (class_ == NULL || keycol == NULL)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  *keycol = '\0';

  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  db_make_null (&keyname);

  error = set_get_element_nocopy (smclass->partition->values, 0, &keyname);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (DB_IS_NULL (&keyname))
    {
      return error;
    }

  keyname_str = db_get_string (&keyname);
  strncpy (keycol, keyname_str, DB_MAX_IDENTIFIER_LENGTH);
  error = NO_ERROR;

  return error;
}

/*
 * do_drop_partition_list() -
 *   return: Error code
 *   class(in):
 *   name_list(in):
 *   tmpl (in): partitioned class template
 *
 * Note:
 */
int
do_drop_partition_list (MOP class_, PT_NODE * name_list, DB_CTMPL * tmpl)
{
  PT_NODE *names;
  int error = NO_ERROR;
  char subclass_name[DB_MAX_IDENTIFIER_LENGTH];
  SM_CLASS *smclass, *subclass;
  MOP classcata;
  OID *partitions = NULL;
  int no_partitions = 0;
  int i;

  if (class_ == NULL || name_list == NULL)
    {
      return ER_FAILED;
    }

  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (smclass->partition == NULL)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }

  for (names = name_list; names; names = names->next)
    {
      no_partitions++;
    }

  partitions = (OID *) malloc (no_partitions * sizeof (OID));
  if (partitions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, no_partitions * sizeof (OID));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  for (names = name_list, i = 0; names; names = names->next, i++)
    {
      sprintf (subclass_name, "%s" PARTITIONED_SUB_CLASS_TAG "%s", sm_ch_name ((MOBJ) smclass),
	       names->info.name.original);
      classcata = sm_find_class (subclass_name);
      if (classcata == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto exit;
	}

      COPY_OID (&partitions[i], &classcata->oid_info.oid);
    }

  for (names = name_list, i = 0; names; names = names->next, i++)
    {
      sprintf (subclass_name, "%s" PARTITIONED_SUB_CLASS_TAG "%s", smclass->header.ch_name, names->info.name.original);
      classcata = sm_find_class (subclass_name);
      if (classcata == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto exit;
	}

      COPY_OID (&partitions[i], &classcata->oid_info.oid);

      error = au_fetch_class (classcata, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      if (subclass->partition)
	{
	  error = sm_delete_class_mop (classcata, false);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	}
      else
	{
	  error = ER_PARTITION_NOT_EXIST;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto exit;
	}
    }

  error = adjust_partition_range (smclass->users);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = adjust_partition_size (class_, tmpl);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = NO_ERROR;

exit:
  if (partitions != NULL)
    {
      free_and_init (partitions);
    }
  return error;
}

/*
 * do_create_partition_constraints () - copy indexes from the root table to
 *					partitions
 * return : error code or NO_ERROR
 * parser (in)	: parser context
 * alter (in)	: alter node
 * pinfo (in)	: partition alter context
 *
 * Note: At the moment the following constraints are added to partitions:
 *    - SM_CONSTRAINT_INDEX
 *    - SM_CONSTRAINT_REVERSE_INDEX
 */
static int
do_create_partition_constraints (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  SM_CLASS *smclass = NULL;
  SM_CLASS_CONSTRAINT *cons = NULL;
  int error = NO_ERROR;

  /* sanity check */
  assert (parser != NULL && alter != NULL && pinfo != NULL);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  smclass = sm_get_class_with_statistics (pinfo->root_op);
  if (smclass == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }


  for (cons = smclass->constraints; cons != NULL; cons = cons->next)
    {
      if (cons->type != SM_CONSTRAINT_INDEX && cons->type != SM_CONSTRAINT_REVERSE_INDEX)
	{
	  continue;
	}
      error = do_create_partition_constraint (alter, smclass, cons, pinfo);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return error;
}

/*
 * do_create_partition_constraint () - copy a constraint from the root class
 *				       to the new partitions
 * return : error code or NO_ERROR
 * alter (in)	   : alter node
 * root_class (in) : root class
 * constraint (in) : root constraint
 */
static int
do_create_partition_constraint (PT_NODE * alter, SM_CLASS * root_class, SM_CLASS_CONSTRAINT * constraint,
				SM_PARTITION_ALTER_INFO * pinfo)
{
  int error = NO_ERROR, i = 0;
  char **namep = NULL, **attrnames = NULL;
  int *asc_desc = NULL;
  SM_CLASS *subclass = NULL;
  SM_ATTRIBUTE **attp = NULL;
  TP_DOMAIN *key_type = NULL;
  DB_OBJLIST *objs = NULL;
  PT_ALTER_CODE alter_op;
  PT_NODE *parts;
  SM_FUNCTION_INFO *new_func_index_info = NULL;

  alter_op = alter->info.alter.code;
  attp = constraint->attributes;
  i = 0;
  /* count attributes in constraint */
  while (*attp)
    {
      attp++;
      i++;
    }
  if (i == 0)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      error = ER_FAILED;
      goto cleanup;
    }

  namep = (char **) malloc ((i + 1) * sizeof (char *));
  if (namep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (i + 1) * sizeof (char *));
      error = ER_FAILED;
      goto cleanup;
    }
  asc_desc = (int *) malloc (i * sizeof (int));
  if (asc_desc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, i * sizeof (int *));
      error = ER_FAILED;
      goto cleanup;
    }

  attp = constraint->attributes;
  attrnames = namep;
  key_type = classobj_find_cons_index2_col_type_list (constraint, &pinfo->root_op->oid_info.oid);
  if (key_type == NULL)
    {
      if ((error = er_errid ()) == NO_ERROR)
	{
	  /* set an error if none was set yet */
	  error = ER_PARTITION_WORK_FAILED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
	}
      goto cleanup;
    }

  i = 0;
  while (*attp && key_type)
    {
      *attrnames = (char *) (*attp)->header.name;
      attrnames++;
      asc_desc[i] = 0;		/* guess as Asc */
      if (DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (constraint->type) || key_type->is_desc)
	{
	  asc_desc[i] = 1;	/* Desc */
	}
      i++;
      attp++;
      key_type = key_type->next;
    }

  *attrnames = NULL;

  if (alter_op == PT_ADD_PARTITION || alter_op == PT_REORG_PARTITION || alter_op == PT_ADD_HASHPARTITION)
    {
      /* only create constraint for new partitions */
      parts = alter->info.alter.alter_clause.partition.parts;
      for (; parts; parts = parts->next)
	{
	  MOP subclass_op = parts->info.parts.name->info.name.db_object;

	  if (alter_op == PT_REORG_PARTITION && parts->flag.partition_pruned)
	    {
	      continue;		/* reused partition */
	    }
	  error = au_fetch_class (subclass_op, &subclass, AU_FETCH_READ, AU_SELECT);
	  if (error != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto cleanup;
	    }

	  if (subclass->partition == NULL)
	    {
	      assert (false);
	      error = ER_FAILED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
	      goto cleanup;
	    }

	  if (constraint->func_index_info)
	    {
	      error = sm_save_function_index_info (&new_func_index_info, constraint->func_index_info);
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
	      error =
		do_recreate_func_index_constr (NULL, NULL, new_func_index_info, NULL, sm_ch_name ((MOBJ) root_class),
					       sm_ch_name ((MOBJ) subclass));
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
	    }
	  else
	    {
	      new_func_index_info = NULL;
	    }

	  error =
	    sm_add_constraint (subclass_op, db_constraint_type (constraint), constraint->name, (const char **) namep,
			       asc_desc, constraint->attrs_prefix_length, false, constraint->filter_predicate,
			       new_func_index_info, constraint->comment, constraint->index_status);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }

	  if (new_func_index_info)
	    {
	      sm_free_function_index_info (new_func_index_info);
	      free_and_init (new_func_index_info);
	    }
	}
    }
  else
    {
      for (objs = root_class->users; objs; objs = objs->next)
	{
	  error = au_fetch_class (objs->op, &subclass, AU_FETCH_READ, AU_SELECT);
	  if (error != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto cleanup;
	    }

	  if (subclass->partition == NULL)
	    {
	      /* not partitioned */
	      continue;
	    }

	  if (constraint->func_index_info)
	    {
	      error = sm_save_function_index_info (&new_func_index_info, constraint->func_index_info);
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
	      error =
		do_recreate_func_index_constr (NULL, NULL, new_func_index_info, NULL, sm_ch_name ((MOBJ) root_class),
					       sm_ch_name ((MOBJ) subclass));
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
	    }
	  else
	    {
	      new_func_index_info = NULL;
	    }

	  error =
	    sm_add_constraint (objs->op, db_constraint_type (constraint), constraint->name, (const char **) namep,
			       asc_desc, constraint->attrs_prefix_length, false, constraint->filter_predicate,
			       new_func_index_info, constraint->comment, constraint->index_status);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }

	  if (new_func_index_info)
	    {
	      sm_free_function_index_info (new_func_index_info);
	      free_and_init (new_func_index_info);
	    }
	}
    }

cleanup:
  if (namep != NULL)
    {
      free_and_init (namep);
    }
  if (asc_desc != NULL)
    {
      free_and_init (asc_desc);
    }
  if (new_func_index_info)
    {
      sm_free_function_index_info (new_func_index_info);
      free_and_init (new_func_index_info);
    }
  return error;
}

/*
 * do_alter_partitioning_pre () - perform partition manipulation
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : partition alter context
 *
 * Note: Altering a partitioned class is an action that that involves two
 *  steps:
 *    1. modifying the schema of the partitioned class
 *    2. redistributing the data to the new schema
 *  The "pre" action is the action responsible for schema modification.
 *  We must ensure that the result of this action is a valid schema because
 *  the redistributing action can only be performed on a valid schema.
 *  The pre action performs the following actions:
 *  PT_APPLY_PARTITION
 *  PT_ADD_PARTITION
 *  PT_ADD_HASHPARTITION
 *    - create [new] partitions and update the root class with this info
 *  PT_REMOVE_PARTITION
 *    - promote all partitions from the schema to normal classes
 *  PT_COALESCE_PARTITION
 *    - promote partitions which will be dropped
 *  PT_REORG_PARTITION
 *    - promote partitions which will be reorganized
 *  PT_ANALYZE_PARTITION
 *    - N/A
 *  PT_DROP_PARTITION
 *    - drop partitions
 *  PT_PROMOTE_PARTITION:
 *    - promote partitions
 */
static int
do_alter_partitioning_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  PT_ALTER_CODE alter_op;
  int error = NO_ERROR;
  const char *entity_name = NULL;

  /* sanity check */
  assert (parser != NULL && alter != NULL && pinfo != NULL);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);
  assert (pinfo->root_op != NULL && pinfo->root_tmpl != NULL);

  entity_name = alter->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      ERROR1 (error, ER_UNEXPECTED, "Expecting a class or virtual class name.");
      return ER_FAILED;
    }

  alter_op = alter->info.alter.code;
  if (alter_op != PT_ANALYZE_PARTITION && alter_op != PT_ADD_PARTITION)
    {
      /* Check to see if the root class is referenced by foreign keys. If the class is referenced by a foreign key, we
       * only allow PT_ADD_PARTITION and PT_ANALYZE_PARTITION alter operations. All other alter operations will
       * probably move data through different classes which is hard to track and can cause the foreign key constraint
       * to be violated. */
      SM_CLASS *root_class = NULL;
      SM_CLASS_CONSTRAINT *pk;
      error = au_fetch_class (pinfo->root_op, &root_class, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}

      pk = classobj_find_cons_primary_key (root_class->constraints);
      if (pk != NULL && pk->fk_info != NULL && classobj_is_pk_referred (pinfo->root_op, pk->fk_info, true, NULL))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ALTER_PARTITIONS_FK_NOT_ALLOWED, 0);
	  return ER_FAILED;
	}
    }

  if (alter_op != PT_APPLY_PARTITION)
    {
      /* get partition key, we're going to need it later on */
      error = do_get_partition_keycol (pinfo->keycol, pinfo->root_op);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  switch (alter_op)
    {
    case PT_APPLY_PARTITION:
      {
	PT_NODE *pt_key_col = NULL;
	const char *key_col_name = NULL;
	SM_ATTRIBUTE *attr = NULL;
	error = do_check_fk_constraints_internal (pinfo->root_tmpl, alter->info.alter.constraint_list, true);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	/* Set SM_ATTFLAG_PARTITION_KEY on the partitioning key. Notice that at this point, we have access to the root
	 * class template which is being edited. The calling function will call finish on this template and we will
	 * create the actual partitions on the "post" action of the alter statement. */
	pt_key_col = alter->info.alter.alter_clause.partition.info->info.partition.keycol;
	key_col_name = pt_key_col->info.name.original;
	attr = pinfo->root_tmpl->attributes;

	while (attr != NULL)
	  {
	    if (SM_COMPARE_NAMES (key_col_name, attr->header.name) == 0)
	      {
		attr->flags |= SM_ATTFLAG_PARTITION_KEY;
		break;
	      }
	    attr = (SM_ATTRIBUTE *) attr->header.next;
	  }
	break;
      }
    case PT_ADD_PARTITION:
    case PT_ADD_HASHPARTITION:
      /* create new partitions */
      error = do_create_partition (parser, alter, pinfo);
      if (error != NO_ERROR)
	{
	  return error;
	}
      error = do_check_fk_constraints (pinfo->root_tmpl, alter->info.alter.constraint_list);
      break;
    case PT_REMOVE_PARTITION:
      error = do_remove_partition_pre (parser, alter, pinfo);
      break;
    case PT_COALESCE_PARTITION:
      error = do_coalesce_partition_pre (parser, alter, pinfo);
      break;
    case PT_REORG_PARTITION:
      error = do_reorganize_partition_pre (parser, alter, pinfo);
      break;
    case PT_ANALYZE_PARTITION:
      break;
    case PT_DROP_PARTITION:
      error =
	do_drop_partition_list (pinfo->root_op, alter->info.alter.alter_clause.partition.name_list, pinfo->root_tmpl);
      break;
    case PT_PROMOTE_PARTITION:
      error = do_promote_partition_list (parser, alter, pinfo);
      break;
    default:
      error = ER_FAILED;
      break;
    }

  return error;
}

/*
 * do_alter_partitioning_post () - redistribute data for partition
 *				   manipulation
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : partition alter context
 *
 * Note: Altering a partitioned class is an action that that involves two
 *  steps:
 *    1. modifying the schema of the partitioned class
 *    2. redistributing the data to the new schema
 *  The "post" action is the action responsible for redistributing data.
 *  The post action performs the following actions:
 *  PT_APPLY_PARTITION
 *  PT_ADD_HASHPARTITION
 *    - redistribute data from the root table [and the old partitions] to the
 *	new partitioning schema by performing "UPDATE t SET * = *"
 *  PT_REMOVE_PARTITION
 *  PT_COALESCE_PARTITION
 *  PT_REORG_PARTITION
 *    - redistribute data from the partitions promoted in the pre action to
 *	the new schema (using INSERT SELECT)and drop the promoted partitions
 *  PT_ANALYZE_PARTITION
 *    - update statistics on partitions
 *  PT_ADD_PARTITION
 *  PT_DROP_PARTITION
 *  PT_PROMOTE_PARTITION:
 *    - N/A
 */
static int
do_alter_partitioning_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  PT_ALTER_CODE alter_op;
  int error = NO_ERROR;
  const char *entity_name = NULL;

  assert (parser != NULL && alter != NULL && pinfo != NULL);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  assert (alter != NULL);
  assert (alter->node_type == PT_ALTER);

  alter_op = alter->info.alter.code;

  entity_name = alter->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      ERROR1 (error, ER_UNEXPECTED, "Expecting a class or virtual class name.");
      return ER_FAILED;
    }

  switch (alter_op)
    {
    case PT_APPLY_PARTITION:
      error = do_create_partition (parser, alter, pinfo);
      if (error != NO_ERROR)
	{
	  return error;
	}

      error = do_get_partition_keycol (pinfo->keycol, pinfo->root_op);
      if (error != NO_ERROR)
	{
	  return error;
	}
      /* fall through */
    case PT_ADD_HASHPARTITION:
      error = do_redistribute_partitions_data (entity_name, pinfo->keycol, NULL, 0, PT_ADD_HASHPARTITION, true, false);
      break;
    case PT_COALESCE_PARTITION:
      error = do_coalesce_partition_post (parser, alter, pinfo);
      break;
    case PT_REORG_PARTITION:
      error = do_reorganize_partition_post (parser, alter, pinfo);
      break;
    case PT_REMOVE_PARTITION:
      error = do_remove_partition_post (parser, alter, pinfo);
      break;
    case PT_ANALYZE_PARTITION:
    case PT_ADD_PARTITION:
    case PT_DROP_PARTITION:
    case PT_PROMOTE_PARTITION:
      /* nothing to do */
      break;
    default:
      error = NO_ERROR;
      break;
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  /* if we created new partitions, we need to propagate indexes here */
  switch (alter_op)
    {
    case PT_APPLY_PARTITION:
    case PT_ADD_PARTITION:
    case PT_REORG_PARTITION:
    case PT_ADD_HASHPARTITION:
      error = do_create_partition_constraints (parser, alter, pinfo);
      break;
    default:
      break;
    }
  return error;
}


/*
 * do_remove_partition_pre () - perform schema actions for partition removing
 *
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : the partition context for the alter operation
 *
 * Note: The "pre" action for removing partitions from a class is to promote
 *	 all partitions to stand alone tables.
 *	 See notes for do_alter_partitioning_pre, do_alter_partitioning_post
 *	 to understand the nature of post/pre actions
 */
static int
do_remove_partition_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  SM_CLASS *class_ = NULL, *subclass = NULL;
  DB_OBJLIST *obj = NULL, *obj_next = NULL;
  int error;
  char **names = NULL;
  int names_count = 0, allocated = 0, i = 0;

  /* sanity checks */
  assert (parser && alter && pinfo);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);
  assert (alter->node_type == PT_ALTER);

  error = au_fetch_class (pinfo->root_op, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (class_->partition == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return ER_FAILED;
    }

  /* preallocate 10 elements for the names array */
  names = (char **) malloc (10 * sizeof (char *));
  if (names == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, 10 * sizeof (char *));
      return ER_FAILED;
    }
  allocated = 10;
  names_count = 0;

  obj = class_->users;
  while (obj != NULL)
    {
      /* save next because obj will be modified by the promote call below */
      obj_next = obj->next;
      error = au_fetch_class (obj->op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}
      if (subclass->partition == NULL)
	{
	  /* not a partition */
	  obj = obj_next;
	  continue;
	}

      if (names_count >= allocated - 1)
	{
	  /* If the names array is to small to accept a new element, reallocate it */
	  char **buf = (char **) realloc (names, (allocated + 10) * sizeof (char *));
	  if (buf == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      ((allocated + 10) * sizeof (char *)));
	      error = ER_FAILED;
	      goto error_return;
	    }
	  names = buf;
	  allocated += 10;
	}
      names[names_count] = strdup (sm_ch_name ((MOBJ) subclass));
      if (names[names_count] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  ((strlen (sm_ch_name ((MOBJ) subclass)) + 1) * sizeof (char)));
	  error = ER_FAILED;
	  goto error_return;
	}
      names_count++;
      error = do_promote_partition (subclass);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}
      obj = obj_next;
    }

  /* keep the promoted names in the partition alter context */
  pinfo->promoted_names = names;
  pinfo->promoted_count = names_count;
  classobj_free_partition_info (pinfo->root_tmpl->partition);
  pinfo->root_tmpl->partition = NULL;

  return NO_ERROR;

error_return:
  if (names != NULL)
    {
      for (i = 0; i < names_count; i++)
	{
	  free_and_init (names[i]);
	}
      free_and_init (names);
    }
  return error;
}

/*
 * do_remove_partition_post () - redistribute data from removed partitions
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : the partition context for the alter operation
 *
 * Note: The "post" action for removing partitions from a class is to
 *	 redistribute data from promoted partitions in pre action to the root
 *	 table and drop the promoted partitions
 *	 See notes for do_alter_partitioning_pre, do_alter_partitioning_post
 *	 to understand the nature of post/pre actions
 */
static int
do_remove_partition_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  int error = NO_ERROR, i = 0;
  const char *root_name = NULL;
  MOP subclass_mop = NULL;

  /* sanity checks */
  assert (parser && alter && pinfo);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  /* At this point, the root class of the partitioned table has been modified not to be partitioned anymore and the all
   * the promoted partition names are stored in pinfo->promoted_names. step 1: do an INSERT ... SELECT to move all data
   * from promoted classes into the root class step 2: drop promoted classes; */
  root_name = alter->info.alter.entity_name->info.name.original;

  error =
    do_redistribute_partitions_data (root_name, pinfo->keycol, pinfo->promoted_names, pinfo->promoted_count,
				     alter->info.alter.code, false, true);
  if (error != NO_ERROR)
    {
      return error;
    }
  for (i = 0; i < pinfo->promoted_count; i++)
    {
      subclass_mop = sm_find_class (pinfo->promoted_names[i]);
      if (subclass_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  return error;
	}
      error = sm_delete_class_mop (subclass_mop, false);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return error;
}

/*
 * do_coalesce_partition_pre () - perform schema actions for partition
 *				  coalescing
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : the partition context for the alter operation
 *
 *  Note: The "pre" action for coalescing partitions from a class is to
 *	promote the number of partitions requested by the user to stand alone
 *	tables. This request is only valid for HASH partitions. For this type
 *	of partitioning, the name of the partition classes is given
 *	automatically by CUBRID and we will promote the last created ones.
 *	See notes for do_alter_partitioning_pre, do_alter_partitioning_post
 *	to understand the nature of post/pre actions
 */
static int
do_coalesce_partition_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  SM_CLASS *class_ = NULL, *subclass = NULL;
  MOP subclass_op = NULL;
  int error;
  char **names = NULL;
  int names_count = 0, i = 0;
  int coalesce_count = 0, partitions_count = 0;
  OID *partitions = NULL;

  /* sanity checks */
  assert (parser && alter && pinfo);
  assert (parser && alter && pinfo);
  assert (alter->node_type == PT_ALTER);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  partitions_count = do_get_partition_size (pinfo->root_op);
  if (partitions_count < 0)
    {
      return ER_FAILED;
    }
  else if (partitions_count == 0)
    {
      /* cannot coalesce partitions of a class which is not partitioned */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return ER_FAILED;
    }

  coalesce_count = alter->info.alter.alter_clause.partition.size->info.value.data_value.i;
  if (coalesce_count >= partitions_count)
    {
      /* cannot coalesce partitions of a class which is not partitioned */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return ER_FAILED;
    }

  error = au_fetch_class (pinfo->root_op, &class_, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (class_->partition == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_WORK_FAILED, 0);
      return ER_FAILED;
    }

  /* Promote the last (partition_count - coalesce_count) partitions */
  names = (char **) malloc (coalesce_count * sizeof (char *));
  if (names == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, coalesce_count * sizeof (char *));
      return ER_FAILED;
    }

  partitions = (OID *) malloc (partitions_count * sizeof (OID));
  if (partitions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, partitions_count * sizeof (OID));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_return;
    }
  for (i = partitions_count - 1, names_count = 0; i >= partitions_count - coalesce_count; i--)
    {
      names[names_count] = (char *) malloc (DB_MAX_IDENTIFIER_LENGTH + 1);
      if (names[names_count] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) (DB_MAX_IDENTIFIER_LENGTH + 1));
	  error = ER_FAILED;
	  goto error_return;
	}
      sprintf (names[names_count], "%s" PARTITIONED_SUB_CLASS_TAG "p%d", sm_ch_name ((MOBJ) class_), i);
      subclass_op = sm_find_class (names[names_count]);
      if (subclass_op == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error_return;
	}
      error = au_fetch_class (subclass_op, &subclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}

      COPY_OID (&partitions[names_count], &subclass_op->oid_info.oid);
      names_count++;

      error = do_promote_partition (subclass);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}
    }

  error = adjust_partition_size (pinfo->root_op, pinfo->root_tmpl);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  /* keep the promoted names in the partition alter context */
  pinfo->promoted_names = names;
  pinfo->promoted_count = names_count;

  if (partitions != NULL)
    {
      free_and_init (partitions);
    }

  return NO_ERROR;

error_return:
  if (names != NULL)
    {
      for (i = 0; i < names_count; i++)
	{
	  free_and_init (names[i]);
	}
      free_and_init (names);
    }
  if (partitions != NULL)
    {
      free_and_init (partitions);
    }
  return error;
}

/*
 * do_coalesce_partition_post () - redistribute data from removed partitions
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : the partition context for the alter operation
 *
 * Note: The "post" action for coalescing partitions from a class is to
 *	 redistribute data from partitions promoted in pre action to the root
 *	 table and drop the promoted partitions
 *	 See notes for do_alter_partitioning_pre, do_alter_partitioning_post
 *	 to understand the nature of post/pre actions
 */
static int
do_coalesce_partition_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  int error = NO_ERROR, i = 0;
  const char *root_name = NULL;
  MOP subclass_mop = NULL;
  OID *partitions = NULL;
  SM_CLASS *smclass = NULL;
  MOP class_ = NULL;

  /* sanity checks */
  assert (parser && alter && pinfo);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  /* At this point, the root class of the partitioned table has been modified and contains only the final partitions.
   * The promoted partition names are stored in pinfo->promoted_names. step 1: redistribute data step 2: drop promoted
   * classes; */
  root_name = alter->info.alter.entity_name->info.name.original;

  class_ = sm_find_class (root_name);
  if (class_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error =
    do_redistribute_partitions_data (root_name, pinfo->keycol, pinfo->promoted_names, pinfo->promoted_count,
				     alter->info.alter.code, true, true);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (pinfo->promoted_count < 1)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto exit;
    }

  partitions = (OID *) malloc (pinfo->promoted_count * sizeof (OID));
  if (partitions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, pinfo->promoted_count * sizeof (OID));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  for (i = 0; i < pinfo->promoted_count; i++)
    {
      subclass_mop = sm_find_class (pinfo->promoted_names[i]);
      if (subclass_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto exit;
	}
      COPY_OID (&partitions[i], &subclass_mop->oid_info.oid);
    }

  for (i = 0; i < pinfo->promoted_count; i++)
    {
      subclass_mop = sm_find_class (pinfo->promoted_names[i]);
      if (subclass_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto exit;
	}
      error = sm_delete_class_mop (subclass_mop, false);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

exit:
  if (partitions != NULL)
    {
      free_and_init (partitions);
    }
  return error;
}

/*
 * do_reorganize_partition_pre () - perform schema actions for partition
 *				    reorganizing
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : the partition context for the alter operation
 *
 *  Note: The "pre" action for reorganizing partitions from a class is to
 *	promote the partitions that will be reorganized to alone tables and
 *	to create the new partitions.
 *	See notes for do_alter_partitioning_pre, do_alter_partitioning_post
 *	to understand the nature of post/pre actions
 */
static int
do_reorganize_partition_pre (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  PT_NODE *old_part = NULL, *new_part = NULL;
  int error = NO_ERROR;
  char **names = NULL;
  int names_count = 0, allocated = 0, i;
  const char *old_name = NULL, *new_name = NULL, *class_name = NULL;
  bool found = false;

  /* sanity checks */
  assert (parser && alter && pinfo);
  assert (alter->node_type == PT_ALTER);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  class_name = alter->info.alter.entity_name->info.name.original;
  old_part = alter->info.alter.alter_clause.partition.name_list;
  new_part = alter->info.alter.alter_clause.partition.parts;

  /* Reorganize partitions might mean that we're dropping some of the curent partitions and changing some others to
   * contain a new range/list We only want to promote partitions which will be deleted, not the ones which will be
   * changed. */
  while (old_part != NULL)
    {
      /* search old_part through new_part */
      found = false;
      old_name = old_part->info.name.original;
      while (new_part != NULL)
	{
	  new_name = new_part->info.parts.name->info.name.original;
	  if (intl_identifier_casecmp (old_name, new_name) == 0)
	    {
	      found = true;
	      break;
	    }
	  new_part = new_part->next;
	}
      new_part = alter->info.alter.alter_clause.partition.parts;
      if (!found)
	{
	  /* we will drop this partition so we have to promote it first */
	  if (names == NULL)
	    {
	      /* allocate */
	      names = (char **) malloc (10 * sizeof (char *));
	      if (names == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, 10 * sizeof (char *));
		  error = ER_FAILED;
		  goto error_return;
		}
	      allocated = 10;
	    }
	  else if (names_count >= allocated - 1)
	    {
	      /* need to reallocate */
	      char **new_buf = (char **) realloc (names, 10 * sizeof (char *));
	      if (new_buf == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, 10 * sizeof (char *));
		  error = ER_FAILED;
		  goto error_return;
		}
	      names = new_buf;
	      allocated += 10;
	    }
	  error = do_promote_partition_by_name (class_name, old_name, &names[names_count]);
	  if (error != NO_ERROR)
	    {
	      goto error_return;
	    }
	  names_count++;
	}

      old_part = old_part->next;
    }

  /* create new partitions */
  error = do_create_partition (parser, alter, pinfo);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  pinfo->promoted_names = names;
  pinfo->promoted_count = names_count;
  return NO_ERROR;

error_return:
  if (names != NULL)
    {
      for (i = 0; i < names_count; i++)
	{
	  free_and_init (names[i]);
	}
      free_and_init (names);
    }
  return error;
}

/*
 * do_reorganize_partition_post () - redistribute data from removed partitions
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * alter (in)	  : the alter node
 * pinfo (in/out) : the partition context for the alter operation
 *
 * Note: The "post" action for reorganizing partitions from a class is to
 *	 redistribute data from promoted partitions in pre action to the new
 *	 partitions and drop the promoted ones.
 *	 See notes for do_alter_partitioning_pre, do_alter_partitioning_post
 *	 to understand the nature of post/pre actions
 */
static int
do_reorganize_partition_post (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  int error = NO_ERROR, i = 0;
  const char *root_name = NULL;
  MOP subclass_mop = NULL;
  PT_NODE *name = NULL;
  bool insert = false, update = false;

  /* sanity checks */
  assert (parser && alter && pinfo);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  /* At this point, the root class of the partitioned table has been modified to the new partitioning schema. We might
   * have promoted some partitions and also changed other partitions to a new range or list. We have to run an INSERT
   * ... SELECT for the promoted partitions and an UPDATE for the ones that we only changed the schema (in order to
   * redistribute the data) */
  root_name = alter->info.alter.entity_name->info.name.original;

  if (pinfo->promoted_names != NULL)
    {
      /* we have at least one promoted partition */
      insert = true;
    }

  name = alter->info.alter.alter_clause.partition.parts;
  while (name)
    {
      if (name->flag.partition_pruned != 0)
	{
	  /* at least one partitions has been changed */
	  update = true;
	  break;
	}
      name = name->next;
    }

  error =
    do_redistribute_partitions_data (root_name, pinfo->keycol, pinfo->promoted_names, pinfo->promoted_count,
				     alter->info.alter.code, update, insert);
  if (error != NO_ERROR)
    {
      return error;
    }

  for (i = 0; i < pinfo->promoted_count; i++)
    {
      subclass_mop = sm_find_class (pinfo->promoted_names[i]);
      if (subclass_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  return error;
	}
      error = sm_delete_class_mop (subclass_mop, false);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * do_analyze_partition () - update statistics on partitions
 * return : error code or NO_ERROR
 * parser (in) : parser context
 * alter (in)  : alter node
 */
static int
do_analyze_partition (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  PT_NODE *name = NULL;
  int error = NO_ERROR;

  /* sanity check */
  assert (parser != NULL && alter != NULL);
  CHECK_2ARGS_ERROR (parser, alter);

  name = alter->info.alter.alter_clause.partition.name_list;
  if (name != NULL)
    {
      /* update statistics for given names */
      while (name)
	{
	  assert (name->info.name.db_object != NULL);
	  error = sm_update_statistics (name->info.name.db_object, STATS_WITH_SAMPLING);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  name = name->next;
	}
    }
  else
    {
      /* update statistics on all partitions */
      SM_CLASS *class_ = NULL, *subclass = NULL;
      DB_OBJLIST *obj = NULL;
      error = au_fetch_class (pinfo->root_op, &class_, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}
      error = sm_update_statistics (pinfo->root_op, STATS_WITH_SAMPLING);
      if (error != NO_ERROR)
	{
	  return error;
	}

      for (obj = class_->users; obj != NULL; obj = obj->next)
	{
	  error = au_fetch_class (obj->op, &subclass, AU_FETCH_READ, AU_SELECT);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (subclass->partition == NULL)
	    {
	      /* not partitioned */
	      continue;
	    }

	  error = sm_update_statistics (obj->op, STATS_WITH_SAMPLING);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return error;
}
#endif

/*
 * do_promote_partition_list () -
 *   return: error code or NO_ERROR
 *   class_(in)	  : root class mop
 *   name_list(in): partition names
 *
 * Note: Currently, only the "local" indexes are promoted. After this
 *  operation, global indexes (like unique indexes and primary keys)
 *  will not be automatically created on the promoted table
 */
static int
do_promote_partition_list (PARSER_CONTEXT * parser, PT_NODE * alter, SM_PARTITION_ALTER_INFO * pinfo)
{
  int error = NO_ERROR;
  char subclass_name[DB_MAX_IDENTIFIER_LENGTH];
  SM_CLASS *smclass = NULL, *smsubclass = NULL;
  MOP subclass = NULL;
  PT_NODE *name = NULL;
  PT_NODE *name_list = NULL;
  DB_OBJLIST *obj = NULL;
  int promoted_count = 0, partitions_count = 0;
  OID *partitions = NULL;

  /* sanity check */
  assert (parser != NULL && alter != NULL && pinfo != NULL);
  CHECK_3ARGS_ERROR (parser, alter, pinfo);

  name_list = alter->info.alter.alter_clause.partition.name_list;
  if (name_list == NULL)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  if (pinfo->root_op == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  error = au_fetch_class (pinfo->root_op, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (!smclass->partition)
    {
      error = ER_INVALID_PARTITION_REQUEST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  /* count the total number of partitions */
  partitions_count = 0;
  for (obj = smclass->users; obj != NULL; obj = obj->next)
    {
      error = au_fetch_class (obj->op, &smsubclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (!smsubclass->partition)
	{
	  continue;
	}
      partitions_count++;
    }

  partitions = (OID *) malloc (partitions_count * sizeof (OID));
  if (partitions == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, partitions_count * sizeof (OID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  promoted_count = 0;
  for (name = name_list; name != NULL; name = name->next)
    {
      sprintf (subclass_name, "%s" PARTITIONED_SUB_CLASS_TAG "%s", sm_ch_name ((MOBJ) smclass),
	       name->info.name.original);

      /* Before promoting, make sure to recreate filter and function indexes because the expression used in these
       * indexes depends on the partitioned class name, not on the partition name */
      error = do_recreate_renamed_class_indexes (parser, sm_ch_name ((MOBJ) smclass), subclass_name);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      subclass = sm_find_class (subclass_name);
      if (subclass == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto exit;
	}
      error = au_fetch_class (subclass, &smsubclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      error = do_promote_partition (smsubclass);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      COPY_OID (&partitions[promoted_count], &subclass->oid_info.oid);
      promoted_count++;
    }
  assert (partitions_count >= promoted_count);

  if (partitions_count - promoted_count == 0)
    {
      /* All partitions were promoted so mark this class as not being partitioned. Through the pinfo object, we have
       * access to the root class template on which an edit operation was started. We can use it to perform the update. */
      assert (pinfo->root_tmpl != NULL);
      classobj_free_partition_info (pinfo->root_tmpl->partition);
      pinfo->root_tmpl->partition = NULL;
    }
  else
    {
      /* smclass might not be valid here, we need to re fetch it */
      error = au_fetch_class (pinfo->root_op, &smclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      error = adjust_partition_range (smclass->users);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      error = adjust_partition_size (pinfo->root_op, pinfo->root_tmpl);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

exit:
  if (partitions != NULL)
    {
      free_and_init (partitions);
    }

  return error;
}

/*
 * do_promote_partition_by_name () - promote a partition
 * return : error code or NO_ERROR
 * class_name (in) :  root class name
 * part_num (in)   :  partition number/indicator
 * partition_name (in/out) : full partition name
 */
static int
do_promote_partition_by_name (const char *class_name, const char *part_num, char **partition_name)
{
  int error = NO_ERROR;
  MOP subclass = NULL;
  SM_CLASS *smsmclass = NULL;
  char name[DB_MAX_IDENTIFIER_LENGTH];

  assert (class_name != NULL && part_num != NULL);
  CHECK_2ARGS_ERROR (class_name, part_num);
  sprintf (name, "%s" PARTITIONED_SUB_CLASS_TAG "%s", class_name, part_num);
  subclass = sm_find_class (name);
  if (subclass == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
  error = au_fetch_class (subclass, &smsmclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }
  error = do_promote_partition (smsmclass);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (partition_name != NULL)
    {
      *partition_name = strdup (name);
      if (*partition_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (strlen (name) + 1) * sizeof (char));
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * do_promote_partition () - promote a partition
 * return : error code or NO_ERROR
 * class_ (in) : class to promote
 */
static int
do_promote_partition (SM_CLASS * class_)
{
  MOP subclass_mop = NULL;
  int error = NO_ERROR;
  SM_CLASS *current = NULL;
  DB_CTMPL *ctemplate = NULL;
  SM_ATTRIBUTE *smattr = NULL;
  bool has_pk = false;

  CHECK_1ARG_ERROR (class_);

  if (class_->partition == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_NOT_EXIST, 0);
      return ER_PARTITION_NOT_EXIST;
    }

  subclass_mop = sm_find_class (sm_ch_name ((MOBJ) class_));
  if (subclass_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  ctemplate = dbt_edit_class (subclass_mop);
  if (ctemplate == NULL)
    {
      return ER_FAILED;
    }

  current = ctemplate->current;
  assert (current != NULL);

  /* Copy attributes in the order in which they were defined in order to preserve the order from the root class */
  error = classobj_copy_attlist (current->ordered_attributes, NULL, 1, &ctemplate->attributes);
  if (error != NO_ERROR)
    {
      dbt_abort_class (ctemplate);
      return error;
    }
  for (smattr = ctemplate->attributes; smattr != NULL; smattr = (SM_ATTRIBUTE *) smattr->header.next)
    {
      /* make attributes point to the subclass, not the parent */
      smattr->class_mop = subclass_mop;
    }

  error = classobj_copy_attlist (current->class_attributes, NULL, 0, &ctemplate->class_attributes);
  if (error != NO_ERROR)
    {
      dbt_abort_class (ctemplate);
      return error;
    }
  for (smattr = ctemplate->class_attributes; smattr != NULL; smattr = (SM_ATTRIBUTE *) smattr->header.next)
    {
      /* make attributes point to the subclass, not the parent */
      smattr->class_mop = subclass_mop;
    }

  /* Make sure we do not copy anything that actually belongs to the root class (the class to which this partition
   * belongs to). This includes: auto_increment flags, unique indexes, primary keys, and foreign keys */
  for (smattr = ctemplate->attributes; smattr != NULL; smattr = (SM_ATTRIBUTE *) smattr->header.next)
    {
      /* reset flags that belong to the root partitioned table */
      smattr->auto_increment = NULL;
      smattr->flags &= ~(SM_ATTFLAG_AUTO_INCREMENT);
      if ((smattr->flags & SM_ATTFLAG_PRIMARY_KEY) != 0)
	{
	  smattr->flags &= ~(SM_ATTFLAG_PRIMARY_KEY);
	  smattr->flags &= ~(SM_ATTFLAG_NON_NULL);
	  has_pk = true;
	}
      smattr->flags &= ~(SM_ATTFLAG_UNIQUE);
      smattr->flags &= ~(SM_ATTFLAG_REVERSE_UNIQUE);
      smattr->flags &= ~(SM_ATTFLAG_FOREIGN_KEY);
      smattr->flags &= ~(SM_ATTFLAG_PARTITION_KEY);
    }

  ctemplate->inheritance = NULL;
  ctemplate->methods = NULL;
  ctemplate->resolutions = NULL;
  ctemplate->class_methods = NULL;
  ctemplate->class_resolutions = NULL;
  ctemplate->method_files = NULL;
  ctemplate->loader_commands = NULL;
  ctemplate->query_spec = NULL;
  ctemplate->instance_attributes = NULL;
  ctemplate->shared_attributes = NULL;
  ctemplate->partition_parent_atts = NULL;
  ctemplate->triggers = NULL;
  classobj_free_partition_info (ctemplate->partition);
  ctemplate->partition = NULL;

  if (ctemplate->properties != NULL)
    {
      if (has_pk)
	{
	  classobj_drop_prop (ctemplate->properties, SM_PROPERTY_PRIMARY_KEY);
	  classobj_drop_prop (ctemplate->properties, SM_PROPERTY_NOT_NULL);
	}
      classobj_drop_prop (ctemplate->properties, SM_PROPERTY_FOREIGN_KEY);
      classobj_drop_prop (ctemplate->properties, SM_PROPERTY_REVERSE_UNIQUE);
      classobj_drop_prop (ctemplate->properties, SM_PROPERTY_UNIQUE);
    }

  if (dbt_finish_class (ctemplate) == NULL)
    {
      dbt_abort_class (ctemplate);
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
      return error;
    }

  return error;
}

/*
 * validate_attribute_domain() - This checks an attribute to make sure
 *                                  that it makes sense
 *   return: Error code
 *   parser(in): Parser context
 *   attribute(in): Parse tree of an attribute or method
 *   check_zero_precision(in): Do not permit zero precision if true
 *
 * Note: Error reporting system
 */
static int
validate_attribute_domain (PARSER_CONTEXT * parser, PT_NODE * attribute, const bool check_zero_precision)
{
  int error = NO_ERROR;

  if (attribute == NULL)
    {
      pt_record_error (parser, parser->statement_number, __LINE__, 0, "system error - NULL attribute node", NULL);
    }
  else
    {
      if (attribute->type_enum == PT_TYPE_NONE)
	{
	  pt_record_error (parser, parser->statement_number, attribute->line_number, attribute->column_number,
			   "system error - attribute type not set", NULL);
	}
      else
	{
	  PT_NODE *dtyp = attribute->data_type;

	  if (dtyp)
	    {
	      int p = attribute->data_type->info.data_type.precision;

	      switch (attribute->type_enum)
		{
		case PT_TYPE_FLOAT:
		case PT_TYPE_DOUBLE:
		  if (p != DB_DEFAULT_PRECISION && (p < 0 || p > DB_MAX_NUMERIC_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_NUMERIC_PRECISION);
		    }
		  break;

		case PT_TYPE_NUMERIC:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_NUMERIC_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_NUMERIC_PRECISION);
		    }
		  break;

		case PT_TYPE_BIT:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_BIT_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_BIT_PRECISION);
		    }
		  break;

		case PT_TYPE_VARBIT:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_VARBIT_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_VARBIT_PRECISION);
		    }
		  break;

		case PT_TYPE_CHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_CHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_CHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_NCHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_NCHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_NCHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_VARCHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_VARCHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_VARCHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_VARNCHAR:
		  if (p != DB_DEFAULT_PRECISION
		      && (p < 0 || (p == 0 && check_zero_precision) || p > DB_MAX_VARNCHAR_PRECISION))
		    {
		      PT_ERRORmf3 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INV_PREC, p, 0,
				   DB_MAX_VARNCHAR_PRECISION);
		    }
		  break;

		case PT_TYPE_SET:
		case PT_TYPE_MULTISET:
		case PT_TYPE_SEQUENCE:
		  {
		    PT_NODE *elem;
		    for (elem = dtyp; elem != NULL; elem = elem->next)
		      {
			if (PT_IS_LOB_TYPE (elem->type_enum))
			  {
			    PT_ERRORmf2 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC,
					 MSGCAT_SEMANTIC_INVALID_SET_ELEMENT, pt_show_type_enum (attribute->type_enum),
					 pt_show_type_enum (elem->type_enum));
			    break;
			  }
		      }
		  }
		  break;

		case PT_TYPE_ENUMERATION:
		  (void) pt_check_enum_data_type (parser, dtyp);
		  break;

		default:
		  break;
		}
	    }
	}
    }

  if (error == NO_ERROR)
    {
      if (pt_has_error (parser))
	{
	  error = ER_PT_SEMANTIC;
	}
    }

  return error;

}

static const char *
get_attr_name (PT_NODE * attribute)
{
  /* First try the derived name and then the original name. For example: create view a_view as select a av1, a av2, b
   * bv from a_tbl; */
  return (attribute->info.attr_def.attr_name->alias_print
	  ? attribute->info.attr_def.attr_name->alias_print : attribute->info.attr_def.attr_name->info.name.original);
}

/*
 * do_add_attribute() - Adds an attribute to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   attribute(in/out): Attribute to add
 *   constraints(in/out): the constraints of the class
 *   error_on_not_normal(in): whether to flag an error on class and shared attributes or not
 *
 * Note : The class object is modified
 */
static int
do_add_attribute (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attribute, PT_NODE * constraints,
		  bool error_on_not_normal)
{
  const char *attr_name = NULL;
  int meta, shared;
  DB_VALUE stack_value;
  DB_VALUE *default_value = &stack_value;
  DB_DEFAULT_EXPR_TYPE on_update_expr = DB_DEFAULT_NONE;
  int error = NO_ERROR;
  TP_DOMAIN *attr_db_domain;
  MOP auto_increment_obj = NULL;
  SM_ATTRIBUTE *att = NULL;
  SM_NAME_SPACE name_space = ID_NULL;
  bool add_first = false;
  const char *add_after_attr = NULL;
  PT_NODE *cnstr, *pk_attr, *comment;
  DB_DEFAULT_EXPR default_expr;
  PARSER_VARCHAR *comment_str = NULL;

  db_make_null (&stack_value);
  attr_name = get_attr_name (attribute);

  meta = (attribute->info.attr_def.attr_type == PT_META_ATTR);
  shared = (attribute->info.attr_def.attr_type == PT_SHARED);

  if (error_on_not_normal && attribute->info.attr_def.attr_type != PT_NORMAL)
    {
      ERROR1 (error, ER_SM_ONLY_NORMAL_ATTRIBUTES, attr_name);
      goto error_exit;
    }

  /* set default codeset and collation for attribute if none is specified */
  if (ctemplate->current != NULL && attribute->node_type == PT_ATTR_DEF && PT_HAS_COLLATION (attribute->type_enum))
    {
      pt_attr_check_default_cs_coll (parser, attribute, -1, ctemplate->current->collation_id);
    }

  if (validate_attribute_domain (parser, attribute, smt_get_class_type (ctemplate) == SM_CLASS_CT ? true : false))
    {
      /* validate_attribute_domain() is assumed to issue whatever messages are pertinent. */
      error = ER_GENERIC_ERROR;
      goto error_exit;
    }

  assert (default_value == &stack_value);
  error = get_att_default_from_def (parser, attribute, &default_value, ctemplate->name);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (default_value && DB_IS_NULL (default_value))
    {
      /* don't allow a default value of NULL for NOT NULL constrained columns */
      if (attribute->info.attr_def.constrain_not_null)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CANNOT_HAVE_NOTNULL_DEFAULT_NULL, 1, attr_name);
	  error = ER_CANNOT_HAVE_NOTNULL_DEFAULT_NULL;
	  goto error_exit;
	}

      /* don't allow a default value of NULL in new PK constraint */
      for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
	{
	  if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
	    {
	      break;
	    }
	}
      if (cnstr != NULL)
	{
	  for (pk_attr = cnstr->info.constraint.un.primary_key.attrs; pk_attr != NULL; pk_attr = pk_attr->next)
	    {
	      if (intl_identifier_casecmp (pk_attr->info.name.original, attr_name) == 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CANNOT_HAVE_PK_DEFAULT_NULL, 1, attr_name);
		  error = ER_CANNOT_HAVE_PK_DEFAULT_NULL;
		  goto error_exit;
		}
	    }
	}
    }

  attr_db_domain = pt_node_to_db_domain (parser, attribute, ctemplate->name);
  if (attr_db_domain == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }

  error = check_default_on_update_clause (parser, attribute);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  error = get_att_order_from_def (attribute, &add_first, &add_after_attr);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (meta)
    {
      name_space = ID_CLASS_ATTRIBUTE;
    }
  else if (shared)
    {
      name_space = ID_SHARED_ATTRIBUTE;
    }
  else
    {
      name_space = ID_ATTRIBUTE;
    }

  on_update_expr = attribute->info.attr_def.on_update;
  pt_get_default_expression_from_data_default_node (parser, attribute->info.attr_def.data_default, &default_expr);
  default_value = &stack_value;

  error = smt_add_attribute_w_dflt_w_order (ctemplate, attr_name, NULL, attr_db_domain, default_value, name_space,
					    add_first, add_after_attr, &default_expr, &on_update_expr, NULL);

  db_value_clear (&stack_value);

  /* Does the attribute belong to a NON_NULL constraint? */
  if (error == NO_ERROR)
    {
      if (attribute->info.attr_def.constrain_not_null)
	{
	  error = dbt_constrain_non_null (ctemplate, attr_name, meta, 1);
	}
    }

  /* create & set auto_increment attribute's serial object */
  if (error == NO_ERROR && !meta && !shared)
    {
      if (attribute->info.attr_def.auto_increment)
	{
	  error = do_create_auto_increment_serial (parser, &auto_increment_obj, ctemplate->name, attribute);

	  if (error == NO_ERROR)
	    {
	      if (smt_find_attribute (ctemplate, attr_name, 0, &att) == NO_ERROR)
		{
		  att->auto_increment = auto_increment_obj;
		  att->flags |= SM_ATTFLAG_AUTO_INCREMENT;
		}
	    }
	}
    }

  comment = attribute->info.attr_def.comment;
  if (error == NO_ERROR && comment != NULL)
    {
      /* skip finding attribute if att is already available */
      if (att == NULL)
	{
	  error = smt_find_attribute (ctemplate, attr_name, 0, &att);
	}

      if (error == NO_ERROR)
	{
	  assert (comment->node_type == PT_VALUE);
	  comment_str = comment->info.value.data_value.str;
	  att->comment = ws_copy_string ((char *) pt_get_varchar_bytes (comment_str));
	  if (att->comment == NULL)
	    {
	      error = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	      goto error_exit;
	    }
	}
    }

  return error;

error_exit:
  db_value_clear (&stack_value);
  return error;
}

/*
 * do_add_attribute_from_select_column() - Adds an attribute to a class object
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   column(in): Attribute to add, as specified by a SELECT column in a
 *               CREATE ... AS SELECT statement. The original SELECT column's
 *               NOT NULL and default value need to be copied.
 *
 * Note : The class object is modified
 */
static int
do_add_attribute_from_select_column (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, DB_QUERY_TYPE * column)
{
  DB_VALUE default_value;
  int error = NO_ERROR;
  const char *attr_name;
  MOP class_obj = NULL;
  DB_DEFAULT_EXPR *default_expr = NULL;
  DB_DEFAULT_EXPR_TYPE *on_update_default_expr = NULL;

  db_make_null (&default_value);

  if (column == NULL || column->domain == NULL)
    {
      error = ER_FAILED;
      goto error_exit;
    }

  if (column->domain->type->id == DB_TYPE_NULL)
    {
      error = ER_CREATE_AS_SELECT_NULL_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto error_exit;
    }
  else if (TP_IS_SET_TYPE (column->domain->type->id))
    {
      TP_DOMAIN *elem;
      for (elem = column->domain->setdomain; elem != NULL; elem = elem->next)
	{
	  if (TP_DOMAIN_TYPE (elem) == DB_TYPE_BLOB || TP_DOMAIN_TYPE (elem) == DB_TYPE_CLOB)
	    {
	      PT_TYPE_ENUM elem_type, set_type;
	      elem_type = pt_db_to_type_enum (TP_DOMAIN_TYPE (elem));
	      set_type = pt_db_to_type_enum (column->domain->type->id);
	      PT_ERRORmf2 (parser, NULL, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_SET_ELEMENT,
			   pt_show_type_enum (set_type), pt_show_type_enum (elem_type));
	      error = ER_PT_SEMANTIC;
	      goto error_exit;
	    }
	}
    }

  attr_name = db_query_format_name (column);

  if (column->spec_name != NULL)
    {
      class_obj = sm_find_class (column->spec_name);
      if (class_obj == NULL)
	{
	  goto error_exit;
	}

      error = sm_att_default_value (class_obj, column->attr_name, &default_value, &default_expr,
				    &on_update_default_expr);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  error = smt_add_attribute_w_dflt (ctemplate, attr_name, NULL, column->domain, &default_value, ID_ATTRIBUTE,
				    default_expr, on_update_default_expr, NULL);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (class_obj != NULL)
    {
      if (sm_att_constrained (class_obj, column->attr_name, SM_ATTFLAG_NON_NULL))
	{
	  error = dbt_constrain_non_null (ctemplate, attr_name, 0, 1);
	}
    }

  return error;

error_exit:
  db_value_clear (&default_value);
  return error;
}

static DB_QUERY_TYPE *
query_get_column_with_name (DB_QUERY_TYPE * query_columns, const char *name)
{
  DB_QUERY_TYPE *column;
  char real_name[SM_MAX_IDENTIFIER_LENGTH];
  char column_name[SM_MAX_IDENTIFIER_LENGTH];

  if (query_columns == NULL)
    {
      return NULL;
    }

  sm_downcase_name (name, real_name, SM_MAX_IDENTIFIER_LENGTH);
  for (column = query_columns; column != NULL; column = db_query_format_next (column))
    {
      sm_downcase_name (db_query_format_name (column), column_name, SM_MAX_IDENTIFIER_LENGTH);
      if (intl_identifier_casecmp (real_name, column_name) == 0)
	{
	  return column;
	}
    }
  return NULL;
}

static PT_NODE *
get_attribute_with_name (PT_NODE * atts, const char *name)
{
  PT_NODE *crt_attr;
  char real_name[SM_MAX_IDENTIFIER_LENGTH];
  char attribute_name[SM_MAX_IDENTIFIER_LENGTH];

  if (atts == NULL)
    {
      return NULL;
    }

  sm_downcase_name (name, real_name, SM_MAX_IDENTIFIER_LENGTH);
  for (crt_attr = atts; crt_attr != NULL; crt_attr = crt_attr->next)
    {
      sm_downcase_name (get_attr_name (crt_attr), attribute_name, SM_MAX_IDENTIFIER_LENGTH);
      if (intl_identifier_casecmp (real_name, attribute_name) == 0)
	{
	  return crt_attr;
	}
    }
  return NULL;
}

/*
 * do_add_attributes() - Adds attributes to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   atts(in/out): Attributes to add
 *   constraints(in/out): the constraints of the class
 *   create_select_columns(in): the column list of a select for
 *                              CREATE ... AS SELECT statements
 *
 * Note : The class object is modified
 */
int
do_add_attributes (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * atts, PT_NODE * constraints,
		   DB_QUERY_TYPE * create_select_columns)
{
  PT_NODE *crt_attr;
  DB_QUERY_TYPE *column;
  int error = NO_ERROR;

  crt_attr = atts;
  while (crt_attr)
    {
      const char *const attr_name = get_attr_name (crt_attr);
      if (query_get_column_with_name (create_select_columns, attr_name) == NULL)
	{
	  error = do_add_attribute (parser, ctemplate, crt_attr, constraints, false);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      crt_attr = crt_attr->next;
    }

  for (column = create_select_columns; column != NULL; column = db_query_format_next (column))
    {
      const char *const col_name = db_query_format_name (column);
      crt_attr = get_attribute_with_name (atts, col_name);
      if (crt_attr != NULL)
	{
	  error = do_add_attribute (parser, ctemplate, crt_attr, constraints, true);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      else
	{
	  error = do_add_attribute_from_select_column (parser, ctemplate, column);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return error;
}


/*
 * map_pt_to_sm_action() -
 *   return: SM_FOREIGN_KEY_ACTION
 *   action(in): Action to map
 *
 * Note:
 */
static SM_FOREIGN_KEY_ACTION
map_pt_to_sm_action (const PT_MISC_TYPE action)
{
  switch (action)
    {
    case PT_RULE_CASCADE:
      return SM_FOREIGN_KEY_CASCADE;

    case PT_RULE_RESTRICT:
      return SM_FOREIGN_KEY_RESTRICT;

    case PT_RULE_NO_ACTION:
      return SM_FOREIGN_KEY_NO_ACTION;

    case PT_RULE_SET_NULL:
      return SM_FOREIGN_KEY_SET_NULL;

    default:
      break;
    }

  return SM_FOREIGN_KEY_NO_ACTION;
}

/*
 * add_foreign_key() -
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   cnstr(in): Constraint name
 *   att_names(in): Key attribute names
 *
 * Note:
 */
static int
add_foreign_key (DB_CTMPL * ctemplate, const PT_NODE * cnstr, const char **att_names)
{
  PT_FOREIGN_KEY_INFO *fk_info;
  const char *constraint_name = NULL;
  char **ref_attrs = NULL;
  int i, n_atts, n_ref_atts;
  PT_NODE *p;
  int error = NO_ERROR;
  const char *comment = NULL;
  size_t buf_size;

  fk_info = (PT_FOREIGN_KEY_INFO *) (&cnstr->info.constraint.un.foreign_key);

  n_atts = pt_length_of_list (fk_info->attrs);
  i = 0;
  for (p = fk_info->attrs; p; p = p->next)
    {
      att_names[i++] = p->info.name.original;
    }
  att_names[i] = NULL;

  if (fk_info->referenced_attrs != NULL)
    {
      n_ref_atts = pt_length_of_list (fk_info->referenced_attrs);

      buf_size = (n_ref_atts + 1) * sizeof (char *);
      ref_attrs = (char **) malloc (buf_size);
      if (ref_attrs == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return error;
	}

      i = 0;
      for (p = fk_info->referenced_attrs; p; p = p->next)
	{
	  ref_attrs[i++] = (char *) p->info.name.original;
	}
      ref_attrs[i] = NULL;
    }

  /* Get the constraint name (if supplied) */
  if (cnstr->info.constraint.name)
    {
      constraint_name = cnstr->info.constraint.name->info.name.original;
    }

  if (cnstr->info.constraint.comment != NULL)
    {
      assert (cnstr->info.constraint.comment->node_type == PT_VALUE);
      comment = (char *) PT_VALUE_GET_BYTES (cnstr->info.constraint.comment);
    }

  error =
    dbt_add_foreign_key (ctemplate, constraint_name, att_names, fk_info->referenced_class->info.name.original,
			 (const char **) ref_attrs, map_pt_to_sm_action (fk_info->delete_action),
			 map_pt_to_sm_action (fk_info->update_action), comment);
  free_and_init (ref_attrs);
  return error;
}


/*
 * do_add_constraints() - This extern routine adds constraints
 *			  to a class object.
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   constraints(in): Constraints to add
 *
 * Note : Class object is modified
 */
int
do_add_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints)
{
  int error = NO_ERROR;
  PT_NODE *cnstr;
  int max_attrs = 0;
  char **att_names = NULL;
  size_t buf_size;

  /* Find the size of the largest UNIQUE constraint list and allocate a character array large enough to contain it. */
  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
    {
      if (cnstr->info.constraint.type == PT_CONSTRAIN_UNIQUE)
	{
	  max_attrs = MAX (max_attrs, pt_length_of_list (cnstr->info.constraint.un.unique.attrs));
	}
      if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
	{
	  max_attrs = MAX (max_attrs, pt_length_of_list (cnstr->info.constraint.un.primary_key.attrs));
	}
      if (cnstr->info.constraint.type == PT_CONSTRAIN_FOREIGN_KEY)
	{
	  max_attrs = MAX (max_attrs, pt_length_of_list (cnstr->info.constraint.un.foreign_key.attrs));
	}
    }

  if (max_attrs > 0)
    {
      buf_size = (max_attrs + 1) * sizeof (char *);
      att_names = (char **) malloc (buf_size);

      if (att_names == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
	    {
	      if (cnstr->info.constraint.type == PT_CONSTRAIN_UNIQUE)
		{
		  PT_NODE *p;
		  int i, n_atts;
		  int class_attributes = 0;
		  char *constraint_name = NULL;
		  DB_CONSTRAINT_TYPE constraint_type = DB_CONSTRAINT_UNIQUE;
		  int *asc_desc = NULL;
		  char *comment = NULL;

		  n_atts = pt_length_of_list (cnstr->info.constraint.un.unique.attrs);

		  asc_desc = (int *) malloc (n_atts * sizeof (int));
		  if (asc_desc == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (n_atts * sizeof (int)));
		      error = ER_OUT_OF_VIRTUAL_MEMORY;
		      goto constraint_error;
		    }

		  if (PT_NAME_INFO_IS_FLAGED (cnstr->info.constraint.un.unique.attrs, PT_NAME_INFO_DESC))
		    {
		      constraint_type = DB_CONSTRAINT_REVERSE_UNIQUE;
		    }

		  i = 0;
		  for (p = cnstr->info.constraint.un.unique.attrs; p; p = p->next)
		    {
		      asc_desc[i] = PT_NAME_INFO_IS_FLAGED (p, PT_NAME_INFO_DESC) ? 1 : 0;
		      att_names[i++] = (char *) p->info.name.original;

		      /* Determine if the unique constraint is being applied to class or normal attributes.  The way
		       * the parser currently works, all multi-column constraints will be on normal attributes and it's
		       * therefore impossible for a constraint to contain both class and normal attributes. */
		      if (p->info.name.meta_class == PT_META_ATTR)
			{
			  class_attributes = 1;
			}

		      /* We keep DB_CONSTRAINT_REVERSE_UNIQUE only if all columns are marked as DESC. */
		      if (!PT_NAME_INFO_IS_FLAGED (p, PT_NAME_INFO_DESC))
			{
			  constraint_type = DB_CONSTRAINT_UNIQUE;
			}
		    }
		  att_names[i] = NULL;

		  /* Get the constraint name (if supplied) */
		  if (cnstr->info.constraint.name)
		    {
		      constraint_name = (char *) cnstr->info.constraint.name->info.name.original;
		    }

		  constraint_name =
		    sm_produce_constraint_name_tmpl (ctemplate, constraint_type, (const char **) att_names, asc_desc,
						     constraint_name);
		  if (constraint_name == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      free_and_init (asc_desc);
		      goto constraint_error;
		    }

		  if (cnstr->info.constraint.comment != NULL)
		    {
		      assert (cnstr->info.constraint.comment->node_type == PT_VALUE);
		      comment = (char *) PT_VALUE_GET_BYTES (cnstr->info.constraint.comment);
		    }

		  error = smt_add_constraint (ctemplate, constraint_type, constraint_name, (const char **) att_names,
					      asc_desc, NULL, class_attributes, NULL, NULL, NULL, comment,
					      SM_NORMAL_INDEX);

		  free_and_init (constraint_name);
		  free_and_init (asc_desc);
		  if (error != NO_ERROR)
		    {
		      goto constraint_error;
		    }
		}
	      else if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
		{
		  PT_NODE *p;
		  int i, n_atts;
		  int class_attributes = 0;
		  char *constraint_name = NULL;
		  int *asc_desc = NULL;
		  char *comment = NULL;

		  n_atts = pt_length_of_list (cnstr->info.constraint.un.primary_key.attrs);

		  asc_desc = (int *) malloc (n_atts * sizeof (int));
		  if (asc_desc == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (n_atts * sizeof (int)));
		      error = ER_OUT_OF_VIRTUAL_MEMORY;
		      goto constraint_error;
		    }

		  i = 0;
		  for (p = cnstr->info.constraint.un.primary_key.attrs; p; p = p->next)
		    {
		      asc_desc[i] = PT_NAME_INFO_IS_FLAGED (p, PT_NAME_INFO_DESC) ? 1 : 0;
		      att_names[i++] = (char *) p->info.name.original;

		      /* Determine if the unique constraint is being applied to class or normal attributes.  The way
		       * the parser currently works, all multi-column constraints will be on normal attributes and it's
		       * therefore impossible for a constraint to contain both class and normal attributes. */
		      if (p->info.name.meta_class == PT_META_ATTR)
			{
			  class_attributes = 1;
			}
		    }
		  att_names[i] = NULL;

		  /* Get the constraint name (if supplied) */
		  if (cnstr->info.constraint.name)
		    {
		      constraint_name = (char *) cnstr->info.constraint.name->info.name.original;
		    }

		  constraint_name =
		    sm_produce_constraint_name_tmpl (ctemplate, DB_CONSTRAINT_PRIMARY_KEY, (const char **) att_names,
						     asc_desc, constraint_name);
		  if (constraint_name == NULL)
		    {
		      free_and_init (asc_desc);
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      goto constraint_error;
		    }

		  if (cnstr->info.constraint.comment != NULL)
		    {
		      assert (cnstr->info.constraint.comment->node_type == PT_VALUE);
		      comment = (char *) PT_VALUE_GET_BYTES (cnstr->info.constraint.comment);
		    }

		  error = smt_add_constraint (ctemplate, DB_CONSTRAINT_PRIMARY_KEY, constraint_name,
					      (const char **) att_names, asc_desc, NULL, class_attributes, NULL, NULL,
					      NULL, comment, SM_NORMAL_INDEX);

		  free_and_init (constraint_name);
		  free_and_init (asc_desc);

		  if (error != NO_ERROR)
		    {
		      goto constraint_error;
		    }
		}
	      else if (cnstr->info.constraint.type == PT_CONSTRAIN_FOREIGN_KEY)
		{
		  error = add_foreign_key (ctemplate, cnstr, (const char **) att_names);
		  if (error != NO_ERROR)
		    {
		      goto constraint_error;
		    }
		}
	    }

	  free_and_init (att_names);
	}
    }

  return (error);

/* error handler */
constraint_error:
  if (att_names)
    {
      free_and_init (att_names);
    }
  return (error);
}

/*
 * do_check_fk_constraints_internal () - Checks that foreign key constraints
 *					 are consistent with the schema.
 * The routine only works when a new class is created or when it is altered
 * with a single change; it might not work in the future if a class will be
 * altered with multiple changes in a single call.
 *
 * Currently the following checks are performed:
 *   SET NULL referential actions must not contradict the attributes' domains
 *   (the attributes cannot have a NOT NULL constraint as they cannot be
 *   NULL).
 *
 *   SET NULL actions are not yet supported on partitioned tables.
 *
 * In the future the function should also check for foreign keys that have
 * cascading referential actions and either represent cycles in the schema or
 * represent "race" updates (the same attribute can be affected on two
 * separate cascading action paths; the results are undefined).
 *
 *   return: Error code
 *   ctemplate(in/out): Class template
 *   constraints(in): List of all the class constraints that have been added.
 *                    Currently the function does not support checking for
 *                    consistency when NOT NULL constraints are added.
 *
 * Note : The class object is not modified
 */
static int
do_check_fk_constraints_internal (DB_CTMPL * ctemplate, PT_NODE * constraints, bool is_partitioned)
{
  int error = NO_ERROR;
  PT_NODE *cnstr;

  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
    {
      PT_NODE *attr;
      PT_FOREIGN_KEY_INFO *fk_info;

      if (cnstr->info.constraint.type != PT_CONSTRAIN_FOREIGN_KEY)
	{
	  continue;
	}

      fk_info = (PT_FOREIGN_KEY_INFO *) (&cnstr->info.constraint.un.foreign_key);
      if (is_partitioned && (fk_info->delete_action == PT_RULE_SET_NULL || fk_info->update_action == PT_RULE_SET_NULL))
	{
	  PT_NODE *const constr_name = cnstr->info.constraint.name;
	  ERROR2 (error, ER_FK_CANT_ON_PARTITION, constr_name ? constr_name->info.name.original : "", ctemplate->name);
	  goto error_exit;
	}

      for (attr = fk_info->attrs; attr; attr = attr->next)
	{
	  const char *const att_name = attr->info.name.original;
	  SM_ATTRIBUTE *attp = NULL;

	  error = smt_find_attribute (ctemplate, att_name, 0, &attp);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  /* FK cannot be defined on shared attribute. */
	  if (db_attribute_is_shared (attp))
	    {
	      PT_NODE *const constr_name = cnstr->info.constraint.name;
	      ERROR2 (error, ER_FK_CANT_ON_SHARED_ATTRIBUTE, constr_name ? constr_name->info.name.original : "",
		      att_name);
	      goto error_exit;
	    }
	  if ((fk_info->delete_action == PT_RULE_SET_NULL || fk_info->update_action == PT_RULE_SET_NULL)
	      && db_attribute_is_non_null (attp))
	    {
	      PT_NODE *const constr_name = cnstr->info.constraint.name;
	      ERROR2 (error, ER_FK_MUST_NOT_BE_NOT_NULL, constr_name ? constr_name->info.name.original : "", att_name);
	      goto error_exit;
	    }
	}
    }

  if (ctemplate->current != NULL)
    {
      SM_CLASS_CONSTRAINT *c;

      for (c = ctemplate->current->constraints; c != NULL; c = c->next)
	{
	  SM_ATTRIBUTE **attribute_p = NULL;

	  if (c->type != SM_CONSTRAINT_FOREIGN_KEY)
	    {
	      continue;
	    }
	  if (is_partitioned
	      && (c->fk_info->delete_action == SM_FOREIGN_KEY_SET_NULL
		  || c->fk_info->update_action == SM_FOREIGN_KEY_SET_NULL))
	    {
	      ERROR2 (error, ER_FK_CANT_ON_PARTITION, c->name ? c->name : "", ctemplate->name);
	      goto error_exit;
	    }
	  if (c->fk_info->delete_action != SM_FOREIGN_KEY_SET_NULL
	      && c->fk_info->update_action != SM_FOREIGN_KEY_SET_NULL)
	    {
	      continue;
	    }
	  for (attribute_p = c->attributes; *attribute_p; ++attribute_p)
	    {
	      const char *const att_name = (*attribute_p)->header.name;
	      SM_ATTRIBUTE *attp = NULL;

	      smt_find_attribute (ctemplate, att_name, 0, &attp);
	      if (db_attribute_is_non_null (attp))
		{
		  ERROR2 (error, ER_FK_MUST_NOT_BE_NOT_NULL, c->name ? c->name : "", att_name);
		  goto error_exit;
		}
	    }
	}
    }
  return error;

error_exit:
  return error;
}

/*
* do_check_fk_constraints() - Checks that foreign key constraints are
*                             consistent with the schema.
*   return: Error code
*   ctemplate(in/out): Class template
*   constraints(in): List of all the class constraints that have been added.
*                    Currently the function does not support checking for
*                    consistency when NOT NULL constraints are added.
* Note : The class object is not modified
*/
int
do_check_fk_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints)
{
  bool is_partitioned = false;
  if (ctemplate == NULL)
    {
      assert_release (ctemplate != NULL);
      return ER_FAILED;
    }

  if (ctemplate->partition != NULL)
    {
      is_partitioned = true;
    }

  return do_check_fk_constraints_internal (ctemplate, constraints, is_partitioned);
}


/*
 * do_add_methods() - Adds methods to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   methods(in): Methods to add
 *
 * Note : Class object is modified
 */
int
do_add_methods (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * methods)
{
  const char *method_name, *method_impl;
  PT_NODE *args_list, *type, *type_list;
  PT_NODE *data_type;
  int arg_num;
  int is_meta;
  int error = NO_ERROR;
  TP_DOMAIN *arg_db_domain;

  /* add each method listed in the class definition */

  while (methods && (error == NO_ERROR))
    {
      method_name = methods->info.method_def.method_name->info.name.original;

      if (methods->info.method_def.function_name != NULL)
	{
	  method_impl = methods->info.method_def.function_name->info.name.original;
	}
      else
	{
	  method_impl = NULL;
	}

      if (methods->info.method_def.mthd_type == PT_META_ATTR)
	{
	  error = dbt_add_class_method (ctemplate, method_name, method_impl);
	}
      else
	{
	  error = dbt_add_method (ctemplate, method_name, method_impl);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* if the result of the method is declared, then add it! */

      arg_num = 0;
      is_meta = methods->info.method_def.mthd_type == PT_META_ATTR;

      if (methods->type_enum != PT_TYPE_NONE)
	{
	  if (PT_IS_COLLECTION_TYPE (methods->type_enum))
	    {
	      arg_db_domain = pt_node_to_db_domain (parser, methods, ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name, is_meta, NULL, arg_num, NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      type_list = methods->data_type;
	      for (type = type_list; type != NULL; type = type->next)
		{
		  arg_db_domain = pt_data_type_to_db_domain (parser, type, ctemplate->name);
		  if (arg_db_domain == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      return (er_errid ());
		    }

		  error =
		    smt_add_set_argument_domain (ctemplate, method_name, is_meta, NULL, arg_num, NULL, arg_db_domain);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}
	    }
	  else
	    {
	      if (validate_attribute_domain (parser, methods, false))
		{
		  return ER_GENERIC_ERROR;
		}
	      arg_db_domain = pt_node_to_db_domain (parser, methods, ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name, is_meta, NULL, arg_num, NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}

      /* add each argument of the method that is declared. */

      args_list = methods->info.method_def.method_args_list;
      for (data_type = args_list; data_type != NULL; data_type = data_type->next)
	{
	  arg_num++;

	  if (PT_IS_COLLECTION_TYPE (data_type->type_enum))
	    {
	      arg_db_domain = pt_data_type_to_db_domain (parser, data_type, ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name, is_meta, NULL, arg_num, NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}

	      type_list = data_type->data_type;
	      for (type = type_list; type != NULL; type = type->next)
		{
		  arg_db_domain = pt_data_type_to_db_domain (parser, type, ctemplate->name);
		  if (arg_db_domain == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      return (er_errid ());
		    }

		  error =
		    smt_add_set_argument_domain (ctemplate, method_name, is_meta, NULL, arg_num, NULL, arg_db_domain);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}
	    }
	  else
	    {
	      if (validate_attribute_domain (parser, data_type, false))
		{
		  return ER_GENERIC_ERROR;
		}
	      arg_db_domain = pt_node_to_db_domain (parser, data_type, ctemplate->name);
	      if (arg_db_domain == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  return (er_errid ());
		}

	      error = smt_assign_argument_domain (ctemplate, method_name, is_meta, NULL, arg_num, NULL, arg_db_domain);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}

      methods = methods->next;
    }
  return (error);
}

/*
 * do_add_method_files() - Adds method files to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   method_files(in): Method files to add
 *
 * Note : Class object is modified
 */
int
do_add_method_files (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * method_files)
{
  const char *method_file_name;
  int error = NO_ERROR;
  PT_NODE *path, *mf;

  /* add each method_file listed in the class definition */

  for (mf = method_files; mf && error == NO_ERROR; mf = mf->next)
    {
      if (mf->node_type == PT_FILE_PATH && (path = mf->info.file_path.string) != NULL && path->node_type == PT_VALUE
	  && (path->type_enum == PT_TYPE_VARCHAR || path->type_enum == PT_TYPE_CHAR || path->type_enum == PT_TYPE_NCHAR
	      || path->type_enum == PT_TYPE_VARNCHAR))
	{
	  method_file_name = (char *) path->info.value.data_value.str->bytes;
	  error = dbt_add_method_file (ctemplate, method_file_name);
	}
      else
	{
	  break;
	}
    }

  return (error);
}

/*
 * do_add_supers() - Adds super-classes to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Superclasses to add
 *
 * Note : Class object is modified
 */
int
do_add_supers (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * supers)
{
  MOP super_class;
  int error = NO_ERROR;


  /* Add each superclass listed in the class definition. Each superclass must already exist inthe database before it
   * can be added. */

  while (supers && (error == NO_ERROR))
    {
      super_class = db_find_class (supers->info.name.original);
      if (super_class == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      else
	{
	  error = dbt_add_super (ctemplate, super_class);
	}

      supers = supers->next;
    }

  return (error);
}

/*
 * do_add_resolutions() - Adds resolutions to a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Resolution to add
 *
 * Note : Class object is modified
 */
int
do_add_resolutions (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * resolution)
{
  int error = NO_ERROR;
  DB_OBJECT *resolution_super_mop;
  const char *resolution_attr_mthd_name, *resolution_as_attr_mthd_name;

  /* add each conflict resolution listed in the class definition */

  while (resolution && (error == NO_ERROR))
    {
      resolution_super_mop = db_find_class (resolution->info.resolution.of_sup_class_name->info.name.original);

      if (resolution_super_mop == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  break;
	}

      resolution_attr_mthd_name = resolution->info.resolution.attr_mthd_name->info.name.original;
      if (resolution->info.resolution.as_attr_mthd_name == NULL)
	{
	  resolution_as_attr_mthd_name = NULL;
	}
      else
	{
	  resolution_as_attr_mthd_name = resolution->info.resolution.as_attr_mthd_name->info.name.original;
	}

      if (resolution->info.resolution.attr_type == PT_META_ATTR)
	{
	  error =
	    dbt_add_class_resolution (ctemplate, resolution_super_mop, resolution_attr_mthd_name,
				      resolution_as_attr_mthd_name);
	}
      else
	{
	  error =
	    dbt_add_resolution (ctemplate, resolution_super_mop, resolution_attr_mthd_name,
				resolution_as_attr_mthd_name);
	}

      resolution = resolution->next;
    }

  return (error);
}

/*
 * add_query_to_virtual_class() - Adds a query to a virtual class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Queries to add
 *
 * Note : Class object is modified
 */
static int
add_query_to_virtual_class (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * queries)
{
  const char *query;
  int error = NO_ERROR;
  unsigned int save_custom;

  save_custom = parser->custom_print;
  parser->custom_print |= PT_CHARSET_COLLATE_FULL;

  query = parser_print_tree_with_quotes (parser, queries);
  parser->custom_print = save_custom;
  error = dbt_add_query_spec (ctemplate, query);

  return (error);
}

/*
 * add_union_query() - Adds a query to a virtual class object.
 *			  If the query is a union all query
 *			  without limit and order_by, it is
 * 			  divided into its component queries
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   supers(in): Union queries to add
 *
 * Note : class object modified
 */
static int
add_union_query (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * query)
{
  int error = NO_ERROR;

  /* Add each query listed in the virtual class definition. */

  if (query->node_type == PT_UNION && query->info.query.all_distinct == PT_ALL && query->info.query.limit == NULL
      && query->info.query.order_by == NULL)
    {
      error = add_union_query (parser, ctemplate, query->info.query.q.union_.arg1);

      if (error == NO_ERROR)
	{
	  error = add_union_query (parser, ctemplate, query->info.query.q.union_.arg2);
	}
    }
  else
    {
      error = add_query_to_virtual_class (parser, ctemplate, query);
    }

  return (error);
}

/*
 * do_add_queries() - Adds a list of queries to a virtual class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   queries(in): Queries to add
 *
 * Note : Class object is modified
 */
int
do_add_queries (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, const PT_NODE * queries)
{
  int error = NO_ERROR;

  while (queries && (error == NO_ERROR))
    {
      error = add_union_query (parser, ctemplate, queries);

      queries = queries->next;
    }

  return (error);
}

/*
 * do_set_object_id() - Sets the object_id for a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   queries(in): Object ids to set
 *
 * Note : Class object is modified
 */
int
do_set_object_id (const PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * object_id_list)
{
  int error = NO_ERROR;
  PT_NODE *object_id;
  int total_ids = 0;
  DB_NAMELIST *id_list = NULL;
  const char *att_name;

  object_id = object_id_list;
  while (object_id)
    {
      att_name = object_id->info.name.original;
      if (att_name)
	{
	  (void) db_namelist_append (&id_list, att_name);
	}
      ++total_ids;
      object_id = object_id->next;
    }
  if (total_ids == 0)
    {
      if (id_list)
	{
	  db_namelist_free (id_list);
	}
      return (error);
    }

  error = dbt_set_object_id (ctemplate, id_list);
  db_namelist_free (id_list);

  return (error);
}

/*
 * do_create_local() - Creates a new class or vclass
 *   return: Error code if the class/vclass is not created
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   pt_node(in): Parse tree of a create class/vclass
 */
int
do_create_local (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * pt_node,
		 DB_QUERY_TYPE * create_select_columns)
{
  int error = NO_ERROR;

  /* create a MOP for the ctemplate, extracting its name from the parse tree. */

  error =
    do_add_attributes (parser, ctemplate, pt_node->info.create_entity.attr_def_list,
		       pt_node->info.create_entity.constraint_list, create_select_columns);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_attributes (parser, ctemplate, pt_node->info.create_entity.class_attr_def_list, NULL, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_constraints (ctemplate, pt_node->info.create_entity.constraint_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_check_fk_constraints (ctemplate, pt_node->info.create_entity.constraint_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_methods (parser, ctemplate, pt_node->info.create_entity.method_def_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_method_files (parser, ctemplate, pt_node->info.create_entity.method_file_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_resolutions (parser, ctemplate, pt_node->info.create_entity.resolution_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_supers (parser, ctemplate, pt_node->info.create_entity.supclass_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_add_queries (parser, ctemplate, pt_node->info.create_entity.as_query_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = do_set_object_id (parser, ctemplate, pt_node->info.create_entity.object_id_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (pt_node->info.create_entity.partition_info)
    {
      /* set partitioning key flag on the attribute */
      PT_NODE *pt_key_col = pt_node->info.create_entity.partition_info->info.partition.keycol;
      const char *key_col_name = pt_key_col->info.name.original;
      SM_ATTRIBUTE *attr = ctemplate->attributes;

      while (attr != NULL)
	{
	  if (SM_COMPARE_NAMES (key_col_name, attr->header.name) == 0)
	    {
	      attr->flags |= SM_ATTFLAG_PARTITION_KEY;
	      break;
	    }
	  attr = (SM_ATTRIBUTE *) attr->header.next;
	}
    }
  return (error);
}

/*
 * create_select_to_insert_into() - An "INSERT INTO ... SELECT" statement is
 *                                  built from a simple SELECT statement to be
 *                                  used for "CREATE ... AS SELECT" execution
 *   return: The INSERT statement or NULL on error
 *   parser(in): Parser context
 *   class_name(in): the name of the table created by the CREATE statement
 *   create_select(in): the select statement parse tree
 *   query_columns(in): the columns of the select statement
 */
static PT_NODE *
create_select_to_insert_into (PARSER_CONTEXT * parser, const char *class_name, PT_NODE * create_select,
			      PT_CREATE_SELECT_ACTION create_select_action, DB_QUERY_TYPE * query_columns)
{
  PT_NODE *ins = NULL;
  PT_NODE *ocs = NULL;
  PT_NODE *nls = NULL;
  DB_QUERY_TYPE *column = NULL;
  char real_name[SM_MAX_IDENTIFIER_LENGTH] = { 0 };
  PT_NODE *name = NULL;

  /* TODO The generated nodes have incorrect line and column information. */
  ins = parser_new_node (parser, PT_INSERT);
  if (ins == NULL)
    {
      goto error_exit;
    }

  if (create_select_action == PT_CREATE_SELECT_REPLACE)
    {
      ins->info.insert.do_replace = true;
    }
  else
    {
      /* PT_CREATE_SELECT_IGNORE is not yet implemented */
      assert (create_select_action == PT_CREATE_SELECT_NO_ACTION);
    }

  ins->info.insert.spec = ocs = parser_new_node (parser, PT_SPEC);
  if (ocs == NULL)
    {
      goto error_exit;
    }

  ocs->info.spec.only_all = PT_ONLY;
  ocs->info.spec.meta_class = PT_CLASS;
  ocs->info.spec.entity_name = pt_name (parser, class_name);
  if (ocs->info.spec.entity_name == NULL)
    {
      goto error_exit;
    }

  for (column = query_columns; column != NULL; column = db_query_format_next (column))
    {
      sm_downcase_name (db_query_format_name (column), real_name, SM_MAX_IDENTIFIER_LENGTH);

      name = pt_name (parser, real_name);
      if (name == NULL)
	{
	  goto error_exit;
	}
      ins->info.insert.attr_list = parser_append_node (name, ins->info.insert.attr_list);
    }

  ins->info.insert.value_clauses = nls = pt_node_list (parser, PT_IS_SUBQUERY, create_select);
  if (nls == NULL)
    {
      goto error_exit;
    }

  return ins;

error_exit:
  parser_free_tree (parser, ins);
  return NULL;
}

/*
 * execute_create_select_query() - Executes an "INSERT INTO ... SELECT"
 *                                 statement built from a SELECT statement to
 *                                 be used for "CREATE ... AS SELECT"
 *                                 execution
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): Parser context
 *   class_name(in): the name of the table created by the CREATE statement
 *   create_select(in): the select statement parse tree
 *   query_columns(in): the columns of the select statement
 *   flagged_statement(in): a node to copy the special statement flags from;
 *                          flags such as recompile will be used for the
 *                          INSERT statement
 */
static int
execute_create_select_query (PARSER_CONTEXT * parser, const char *const class_name, PT_NODE * create_select,
			     PT_CREATE_SELECT_ACTION create_select_action, DB_QUERY_TYPE * query_columns,
			     PT_NODE * flagged_statement)
{
  PT_NODE *insert_into = NULL;
  PT_NODE *create_select_copy = parser_copy_tree (parser, create_select);
  int error = NO_ERROR;

  if (create_select_copy == NULL)
    {
      error = ER_FAILED;
      goto error_exit;
    }
  insert_into =
    create_select_to_insert_into (parser, class_name, create_select_copy, create_select_action, query_columns);
  if (insert_into == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }
  pt_copy_statement_flags (flagged_statement, insert_into);
  create_select_copy = NULL;

  insert_into = pt_compile (parser, insert_into);
  if (!insert_into || pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, insert_into);
      error = er_errid ();
      goto error_exit;
    }

  insert_into = mq_translate (parser, insert_into);
  if (!insert_into || pt_has_error (parser))
    {
      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, insert_into);
      error = er_errid ();
      goto error_exit;
    }

  error = do_statement (parser, insert_into);
  pt_free_statement_xasl_id (insert_into);
  if (error < 0)
    {
      goto error_exit;
    }
  else
    {
      error = 0;
    }

  parser_free_tree (parser, insert_into);
  insert_into = NULL;

  return error;

error_exit:
  if (create_select_copy != NULL)
    {
      parser_free_tree (parser, create_select_copy);
      create_select_copy = NULL;
    }
  if (insert_into != NULL)
    {
      parser_free_tree (parser, insert_into);
      insert_into = NULL;
    }
  return error;
}

/*
 * do_create_entity() - Creates a new class/vclass
 *   return: Error code if the class/vclass is not created
 *   parser(in): Parser context
 *   node(in/out): Parse tree of a create class/vclass
 */
int
do_create_entity (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int error = NO_ERROR;
  DB_CTMPL *ctemplate = NULL;
  DB_OBJECT *class_obj = NULL;
  const char *class_name = NULL;
  const char *create_like = NULL;
  SM_CLASS *source_class = NULL;
  PT_NODE *create_select = NULL;
  PT_NODE *create_index = NULL;
  DB_QUERY_TYPE *query_columns = NULL;
  PT_NODE *tbl_opt = NULL;
  bool found_reuse_oid_option = false, reuse_oid = false;
  bool do_rollback_on_error = false;
  bool do_abort_class_on_error = false;
  bool do_flush_class_mop = false;
  int charset = LANG_SYS_CODESET;
  int collation_id = LANG_SYS_COLLATION;
  PARSER_VARCHAR *comment = NULL;
  PT_NODE *tbl_opt_charset, *tbl_opt_coll, *cs_node, *coll_node;
  PT_NODE *tbl_opt_comment, *comment_node, *super_node;
  PT_NODE *tbl_opt_encrypt, *encrypt_node;
  const char *comment_str = NULL;
  MOP super_class = NULL;
  int tde_algo_opt = -1;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;

  CHECK_MODIFICATION_ERROR ();

  tbl_opt_charset = tbl_opt_coll = cs_node = coll_node = NULL;
  tbl_opt_comment = comment_node = NULL;
  tbl_opt_encrypt = encrypt_node = NULL;

  class_name = node->info.create_entity.entity_name->info.name.original;

  if (node->info.create_entity.create_like != NULL)
    {
      create_like = node->info.create_entity.create_like->info.name.original;
    }

  create_select = node->info.create_entity.create_select;
  if (create_select != NULL)
    {
      DB_QUERY_TYPE *column;

      error = pt_get_select_query_columns (parser, create_select, &query_columns);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      /* check for mis-creating string type with -1 precision */
      for (column = query_columns; column != NULL; column = db_query_format_next (column))
        {
          switch (column->domain->type->id)
            {
              case DB_TYPE_VARCHAR:
                if (column->domain->precision == DB_DEFAULT_PRECISION)
                  {
                    column->domain->precision = DB_MAX_VARCHAR_PRECISION;
                  }
                break;
              case DB_TYPE_VARNCHAR:
                if (column->domain->precision == DB_DEFAULT_PRECISION)
                  {
                    column->domain->precision = DB_MAX_VARNCHAR_PRECISION;
                  }
                else if (column->domain->precision > DB_MAX_VARNCHAR_PRECISION)
                  {
                    column->domain->precision = DB_MAX_VARNCHAR_PRECISION;
                  }
                default:
                break;
            }
        }
    }
  assert (!(create_like != NULL && create_select != NULL));

  switch (node->info.create_entity.entity_type)
    {
    case PT_CLASS:

      if (node->info.create_entity.if_not_exists == 1 && db_find_class (class_name))
	{
	  goto error_exit;
	}

      /* here we need to check if the super classes are partitioned, as it is not allowed to inherit from partition
       * tables. */
      super_node = node->info.create_entity.supclass_list;

      while (super_node)
	{
	  super_class = db_find_class (super_node->info.name.original);

	  if (super_class == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto error_exit;
	    }
	  else
	    {
	      error = sm_is_partitioned_class (super_class);
	      if (error < 0)
		{
		  goto error_exit;
		}
	      if (error > 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INHERIT_FROM_PARTITION_TABLE, 0);
		  error = er_errid ();
		  goto error_exit;
		}
	    }

	  super_node = super_node->next;
	}

      for (tbl_opt = node->info.create_entity.table_option_list; tbl_opt != NULL; tbl_opt = tbl_opt->next)
	{
	  assert (tbl_opt->node_type == PT_TABLE_OPTION);
	  switch (tbl_opt->info.table_option.option)
	    {
	    case PT_TABLE_OPTION_REUSE_OID:
	      found_reuse_oid_option = true;
	      reuse_oid = true;
	      break;
	    case PT_TABLE_OPTION_ENCRYPT:
	      tbl_opt_encrypt = tbl_opt;
	      break;
	    case PT_TABLE_OPTION_DONT_REUSE_OID:
	      found_reuse_oid_option = true;
	      reuse_oid = false;
	      break;
	    case PT_TABLE_OPTION_CHARSET:
	      tbl_opt_charset = tbl_opt;
	      break;
	    case PT_TABLE_OPTION_COLLATION:
	      tbl_opt_coll = tbl_opt;
	      break;
	    case PT_TABLE_OPTION_COMMENT:
	      tbl_opt_comment = tbl_opt;
	      break;
	    default:
	      break;
	    }
	}

      /* get default value of reuse_oid from system parameter, if don't use table option related reuse_oid */
      if (!found_reuse_oid_option)
	{
	  reuse_oid = prm_get_bool_value (PRM_ID_TB_DEFAULT_REUSE_OID);
	}

      /* validate charset and collation options, if any */
      cs_node = (tbl_opt_charset) ? tbl_opt_charset->info.table_option.val : NULL;
      coll_node = (tbl_opt_coll) ? tbl_opt_coll->info.table_option.val : NULL;
      charset = LANG_SYS_CODESET;
      collation_id = LANG_SYS_COLLATION;
      if (cs_node != NULL || coll_node != NULL)
	{
	  error = pt_check_grammar_charset_collation (parser, cs_node, coll_node, &charset, &collation_id);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      error = tran_system_savepoint (UNIQUE_SAVEPOINT_CREATE_ENTITY);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      do_rollback_on_error = true;

      if (create_like)
	{
	  ctemplate = dbt_copy_class (class_name, create_like, &source_class);
	}
      else
	{
	  ctemplate = dbt_create_class (class_name);
	}
      break;

    case PT_VCLASS:
      error = tran_system_savepoint (UNIQUE_SAVEPOINT_CREATE_ENTITY);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
      do_rollback_on_error = true;

      if (node->info.create_entity.or_replace && db_find_class (class_name))
	{
	  /* drop existing view */
	  if (do_is_partitioned_subclass (NULL, class_name, NULL))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_PARTITION_REQUEST, 0);
	      error = er_errid ();
	      goto error_exit;
	    }

	  error = drop_class_name (class_name, false);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  /* Quick fix: create a savepoint to mark deleted transient state. */
	  /* NOTE: Without this savepoint, creating view will completely replace deleted classname. */
	  error = tran_system_savepoint (UNIQUE_SAVEPOINT_REPLACE_VIEW);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      ctemplate = dbt_create_vclass (class_name);
      break;

    default:
      error = ER_GENERIC_ERROR;	/* a system error */
      break;
    }

  if (ctemplate == NULL)
    {
      if (error == NO_ERROR)
	{
	  error = er_errid ();
	}
      assert (error != NO_ERROR);
      goto error_exit;
    }
  do_abort_class_on_error = true;

  if (create_like != NULL)
    {
      /* Nothing left to do, but get the collation from the source class. */
      collation_id = source_class->collation_id;
    }
  else
    {
      error = do_create_local (parser, ctemplate, node, query_columns);
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  class_obj = dbt_finish_class (ctemplate);

  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error_exit;
    }

  if (er_errid () == ER_LC_UNKNOWN_CLASSNAME && er_get_severity () == ER_WARNING_SEVERITY)
    {
      /* Because the class is still inexistent, normally, here we will have to encounter some errors/warnings like
       * ER_LC_UNKNOWN_CLASSNAME which is unuseful for current context indeed and may disturb other subsequent
       * routines. Thus, we can/should clear the errors safely. */
      er_clear ();
    }

  do_abort_class_on_error = false;
  ctemplate = NULL;

  switch (node->info.create_entity.entity_type)
    {
    case PT_VCLASS:
      if (node->info.create_entity.with_check_option == PT_CASCADED)
	{
	  error = sm_set_class_flag (class_obj, SM_CLASSFLAG_WITHCHECKOPTION, 1);
	}
      else if (node->info.create_entity.with_check_option == PT_LOCAL)
	{
	  error = sm_set_class_flag (class_obj, SM_CLASSFLAG_LOCALCHECKOPTION, 1);
	}

      comment_node = node->info.create_entity.vclass_comment;
      if (comment_node != NULL)
	{
	  assert (comment_node->node_type == PT_VALUE);
	  comment_str = (char *) comment_node->info.value.data_value.str->bytes;
	  error = sm_set_class_comment (class_obj, comment_str);
	  if (error == NO_ERROR)
	    {
	      do_flush_class_mop = true;
	    }
	}
      break;

    case PT_CLASS:
      if (create_like)
	{
	  assert (source_class != NULL);

	  if (!reuse_oid && (source_class->flags & SM_CLASSFLAG_REUSE_OID))
	    {
	      reuse_oid = true;
	    }

	  tde_algo = (TDE_ALGORITHM) source_class->tde_algorithm;
	  if (tde_algo != TDE_ALGORITHM_NONE)
	    {
	      error = sm_set_class_tde_algorithm (class_obj, tde_algo);
	      if (error != NO_ERROR)
		{
		  break;
		}
	      do_flush_class_mop = true;
	    }

	  if (source_class->comment)
	    {
	      error = sm_set_class_comment (class_obj, source_class->comment);
	      if (error != NO_ERROR)
		{
		  break;
		}
	    }
	}
      if (locator_create_heap_if_needed (class_obj, reuse_oid) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  break;
	}
      if (reuse_oid)
	{
	  error = sm_set_class_flag (class_obj, SM_CLASSFLAG_REUSE_OID, 1);
	  if (error == NO_ERROR)
	    {
	      /* Need to flush class mop in order to reflect reuse_oid flag into catalog table. Without flushing it,
	       * catalog information is incorrect under non-autocommit mode. */
	      do_flush_class_mop = true;
	    }
	}
      if (tbl_opt_encrypt)
	{
	  encrypt_node = tbl_opt_encrypt->info.table_option.val;
	  assert (encrypt_node != NULL && encrypt_node->node_type == PT_VALUE
		  && encrypt_node->type_enum == PT_TYPE_INTEGER);
	  tde_algo_opt = encrypt_node->info.value.data_value.i;
	  /*
	   *  -1 means using deafult encryption algorithm.
	   *  Other values but -1, TDE_ALGORITHM_AES, TDE_ALGORITHM_ARIA has been denied by parser.
	   */
	  if (tde_algo_opt == -1)
	    {
	      tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
	    }
	  else
	    {
	      tde_algo = (TDE_ALGORITHM) tde_algo_opt;
	    }
	  error = sm_set_class_tde_algorithm (class_obj, tde_algo);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	  do_flush_class_mop = true;
	}
      error = sm_set_class_collation (class_obj, collation_id);
      if (error != NO_ERROR)
	{
	  break;
	}
      if (tbl_opt_comment)
	{
	  comment_node = tbl_opt_comment->info.table_option.val;
	  assert (comment_node != NULL && comment_node->node_type == PT_VALUE);
	  comment = comment_node->info.value.data_value.str;
	  error = sm_set_class_comment (class_obj, (char *) pt_get_varchar_bytes (comment));
	  if (error == NO_ERROR)
	    {
	      do_flush_class_mop = true;
	    }
	}
      break;

    default:
      break;
    }

  if (do_flush_class_mop == true)
    {
      assert (error == NO_ERROR);
      assert (class_obj != NULL);

      error = locator_flush_class (class_obj);
    }

  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  if (node->info.create_entity.partition_info != NULL)
    {
      SM_PARTITION_ALTER_INFO info;

      info.keycol[0] = 0;
      info.promoted_count = 0;
      info.promoted_names = NULL;
      info.root_tmpl = NULL;
      info.root_op = class_obj;

      error = do_create_partition (parser, node, &info);
      if (error != NO_ERROR)
	{
	  if (error == ER_LK_UNILATERALLY_ABORTED)
	    {
	      do_rollback_on_error = false;
	    }
	  goto error_exit;
	}
    }
  if (create_like)
    {
      error = do_copy_indexes (parser, class_obj, source_class);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  if (create_select)
    {
      error =
	execute_create_select_query (parser, class_name, create_select, node->info.create_entity.create_select_action,
				     query_columns, node);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      db_free_query_format (query_columns);
      query_columns = NULL;
    }
  assert (query_columns == NULL);

  for (create_index = node->info.create_entity.create_index; create_index != NULL; create_index = create_index->next)
    {
      PT_NODE *save_next = NULL;

      create_index->info.index.indexed_class = pt_entity (parser, node->info.create_entity.entity_name, NULL, NULL);

      if (create_index->info.index.indexed_class == NULL)
	{
	  error = ER_FAILED;
	  goto error_exit;
	}

      save_next = create_index->next;
      create_index->next = NULL;
      pt_semantic_check (parser, create_index);
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	  goto error_exit;
	}
      create_index->next = save_next;

      error = do_create_index (parser, create_index);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  if (tde_algo != TDE_ALGORITHM_NONE)
    {
      error = file_apply_tde_to_class_files (&class_obj->oid_info.oid);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  return error;

error_exit:
  if (query_columns != NULL)
    {
      db_free_query_format (query_columns);
      query_columns = NULL;
    }
  if (do_abort_class_on_error)
    {
      (void) dbt_abort_class (ctemplate);
    }
  if (do_rollback_on_error && error != ER_LK_UNILATERALLY_ABORTED)
    {
      tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CREATE_ENTITY);
    }

  return error;
}

/*
 * do_recreate_renamed_class_indexes() - Recreate indexes of previously
 *					  renamed class
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): Parser context
 *   old_class_name(in): the old name of the renamed class
 *   class_name(in): the new name of the renamed class
 *
 *  Note : This function must be called after class renaming.
 *	   Currently, only filter and function index are recreated since
 *	   their expression must be modified and recompiled.
 */
static int
do_recreate_renamed_class_indexes (const PARSER_CONTEXT * parser, const char *const old_class_name,
				   const char *const class_name)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *c = NULL;
  SM_CONSTRAINT_INFO *index_save_info = NULL, *saved = NULL;
  SM_CLASS *class_ = NULL;
  DB_OBJECT *classmop = NULL;

  assert (parser != NULL && old_class_name != NULL && class_name != NULL);

  classmop = db_find_class (class_name);
  if (classmop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (au_fetch_class (classmop, &class_, AU_FETCH_READ, DB_AUTH_SELECT) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  if (class_->constraints == NULL)
    {
      /* no constraints, nothing to do */
      return NO_ERROR;
    }

  if (class_->class_type != SM_CLASS_CT)
    {
      return ER_OBJ_NOT_A_CLASS;
    }

  for (c = class_->constraints; c; c = c->next)
    {
      if (c->type != SM_CONSTRAINT_INDEX && c->type != SM_CONSTRAINT_REVERSE_INDEX && c->type != SM_CONSTRAINT_UNIQUE
	  && c->type != SM_CONSTRAINT_REVERSE_UNIQUE)
	{
	  continue;
	}

      if (c->func_index_info || c->filter_predicate)
	{
	  /* save constraints */
	  error = sm_save_constraint_info (&index_save_info, c);
	  if (error == NO_ERROR)
	    {
	      assert (index_save_info != NULL);
	      saved = index_save_info;
	      while (saved->next)
		{
		  saved = saved->next;
		}
	      if (c->func_index_info)
		{
		  /* recompile function index expression */
		  error =
		    do_recreate_func_index_constr ((PARSER_CONTEXT *) parser, saved, NULL, NULL, old_class_name,
						   sm_ch_name ((MOBJ) class_));
		  if (error != NO_ERROR)
		    {
		      goto error_exit;
		    }
		}
	      else
		{
		  /* recompile filter index expression */
		  error =
		    do_recreate_filter_index_constr ((PARSER_CONTEXT *) parser, saved->filter_predicate, NULL,
						     old_class_name, sm_ch_name ((MOBJ) class_));
		  if (error != NO_ERROR)
		    {
		      goto error_exit;
		    }
		}
	    }
	}
    }

  /* drop indexes */
  for (saved = index_save_info; saved != NULL; saved = saved->next)
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY ((SM_CONSTRAINT_TYPE) saved->constraint_type))
	{
	  error =
	    sm_drop_constraint (classmop, saved->constraint_type, saved->name, (const char **) saved->att_names, false,
				false);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
      else
	{
	  error = sm_drop_index (classmop, saved->name);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
    }

  /* add indexes */
  for (saved = index_save_info; saved != NULL; saved = saved->next)
    {
      error = sm_add_constraint (classmop, saved->constraint_type, saved->name, (const char **) saved->att_names,
				 saved->asc_desc, saved->prefix_length, false, saved->filter_predicate,
				 saved->func_index_info, saved->comment, saved->index_status);

      if (error != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  if (index_save_info != NULL)
    {
      sm_free_constraint_info (&index_save_info);
    }

  return NO_ERROR;

error_exit:

  if (index_save_info != NULL)
    {
      sm_free_constraint_info (&index_save_info);
    }

  return error;
}

/*
 * do_copy_indexes() - Copies all the indexes of a given class to another
 *                     class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in): Parser context
 *   classmop(in): the class to copy the indexes to
 *   class_(in): the class to copy the indexes from
 */
static int
do_copy_indexes (PARSER_CONTEXT * parser, MOP classmop, SM_CLASS * src_class)
{
  int error = NO_ERROR;
  const char **att_names = NULL;
  SM_CLASS_CONSTRAINT *c;
  char *new_cons_name = NULL;
  SM_CONSTRAINT_INFO *index_save_info = NULL;
  DB_CONSTRAINT_TYPE constraint_type;
  int free_constraint = 0;
  const char *class_name = NULL;

  assert (src_class != NULL);

  if (src_class->constraints == NULL)
    {
      return NO_ERROR;
    }

  for (c = src_class->constraints; c; c = c->next)
    {
      if (c->type != SM_CONSTRAINT_INDEX && c->type != SM_CONSTRAINT_REVERSE_INDEX)
	{
	  /* These should have been copied already. */
	  continue;
	}

      att_names = classobj_point_at_att_names (c, NULL);
      if (att_names == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      constraint_type = db_constraint_type (c);
      new_cons_name = (char *) c->name;

      if (c->func_index_info || c->filter_predicate)
	{
	  /* we need to recompile the expression need for function index */
	  error = sm_save_constraint_info (&index_save_info, c);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  free_constraint = 1;
	  class_name = sm_get_ch_name (classmop);
	  if (class_name == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto exit_on_error;
	    }

	  if (c->func_index_info)
	    {
	      error =
		do_recreate_func_index_constr (parser, index_save_info, NULL, NULL, sm_ch_name ((MOBJ) src_class),
					       class_name);
	    }
	  else
	    {
	      /* filter index predicate available */
	      error =
		do_recreate_filter_index_constr (parser, index_save_info->filter_predicate, NULL,
						 sm_ch_name ((MOBJ) src_class), class_name);
	    }

	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      if (c->func_index_info || c->filter_predicate)
	{
	  error = sm_add_constraint (classmop, constraint_type, new_cons_name, att_names, index_save_info->asc_desc,
				     index_save_info->prefix_length, false, index_save_info->filter_predicate,
				     index_save_info->func_index_info, index_save_info->comment,
				     index_save_info->index_status);
	}
      else
	{
	  error =
	    sm_add_constraint (classmop, constraint_type, new_cons_name, att_names, c->asc_desc, c->attrs_prefix_length,
			       false, c->filter_predicate, c->func_index_info, c->comment, c->index_status);
	}
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}

      free_and_init (att_names);

      if (new_cons_name != NULL && new_cons_name != c->name)
	{
	  free_and_init (new_cons_name);
	}

      if (free_constraint)
	{
	  sm_free_constraint_info (&index_save_info);
	}
      free_constraint = 0;
    }

  return error;

exit_on_error:

  free_and_init (att_names);

  if (new_cons_name != NULL && new_cons_name != c->name)
    {
      free_and_init (new_cons_name);
    }

  if (free_constraint)
    {
      sm_free_constraint_info (&index_save_info);
    }

  return error;
}





/*
 * Function Group :
 * Code for truncating Classes by Parse Tree descriptions.
 *
 */

static int truncate_class_name (const char *name);

/*
 * truncate_class_name() - This static routine truncates a class by name.
 *   return: Error code
 *   name(in): Class name to truncate
 */
static int
truncate_class_name (const char *name)
{
  DB_OBJECT *class_mop;

  class_mop = db_find_class (name);

  if (class_mop)
    {
      return db_truncate_class (class_mop);
    }
  else
    {
      /* if class is null, return the global error. */
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }
}

/*
 * do_truncate() - Truncates a class
 *   return: Error code if truncation fails.
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of the statement
 */
int
do_truncate (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *entity_spec = NULL;
  PT_NODE *entity = NULL;
  PT_NODE *entity_list = NULL;

  CHECK_MODIFICATION_ERROR ();

  entity_spec = statement->info.truncate.spec;
  if (entity_spec == NULL)
    {
      return NO_ERROR;
    }

  entity_list = entity_spec->info.spec.flat_entity_list;
  for (entity = entity_list; entity != NULL; entity = entity->next)
    {
      /* partitioned sub-class check */
      if (do_is_partitioned_subclass (NULL, entity->info.name.original, NULL))
	{
	  error = ER_INVALID_PARTITION_REQUEST;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  return error;
	}
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_TRUNCATE);
  if (error != NO_ERROR)
    {
      return error;
    }

  for (entity = entity_list; entity != NULL; entity = entity->next)
    {
      error = truncate_class_name (entity->info.name.original);
      if (error != NO_ERROR)
	{
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void) tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_TRUNCATE);
	    }

	  return error;
	}
    }

  return error;
}

/*
 * do_alter_clause_change_attribute() - Executes an ALTER CHANGE or
 *				        ALTER MODIFY clause
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_RENAME_ENTITY clause potentially
 *                  followed by the rest of the clauses in the ALTER
 *                  statement.
 */
static int
do_alter_clause_change_attribute (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  int error = NO_ERROR;
  const PT_ALTER_CODE alter_code = alter->info.alter.code;
  const char *entity_name = NULL;
  DB_OBJECT *class_obj = NULL;
  DB_CTMPL *ctemplate = NULL;
  SM_ATTR_CHG_SOL change_mode = SM_ATTR_CHG_ONLY_SCHEMA;
  SM_ATTR_PROP_CHG attr_chg_prop;
  bool tran_saved = false;
  MOP class_mop = NULL;
  OID *usr_oid_array = NULL;
  int user_count = 0;
  int i;
  bool has_partitions = false;
  bool is_srv_update_needed = false;
  OID class_oid;
  int att_id = -1;

  assert (alter_code == PT_CHANGE_ATTR);
  assert (alter->info.alter.super.resolution_list == NULL);

  OID_SET_NULL (&class_oid);
  reset_att_property_structure (&attr_chg_prop);

  entity_name = alter->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Expecting a class or virtual class name.");
      goto exit;
    }

  class_obj = db_find_class (entity_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  error = locator_flush_class (class_obj);
  if (error != NO_ERROR)
    {
      /* don't overwrite error */
      goto exit;
    }

  /* force exclusive lock on class, even though it should have been already acquired */
  if (locator_fetch_class (class_obj, DB_FETCH_WRITE) == NULL)
    {
      error = ER_FAILED;
      goto exit;
    }

  ctemplate = dbt_edit_class (class_obj);
  if (ctemplate == NULL)
    {
      /* when dbt_edit_class fails (e.g. because the server unilaterally aborts us), we must record the associated
       * error message into the parser.  Otherwise, we may get a confusing error msg of the form: "so_and_so is not a
       * class". */
      pt_record_error (parser, parser->statement_number - 1, alter->line_number, alter->column_number, er_msg (), NULL);
      error = er_errid ();
      goto exit;
    }

  /* this ALTER CHANGE syntax supports only one attribute change per ALTER clause */
  assert (alter->info.alter.alter_clause.attr_mthd.mthd_def_list == NULL);
  assert (alter->info.alter.alter_clause.attr_mthd.attr_def_list->next == NULL);

  error =
    check_change_attribute (parser, ctemplate, alter->info.alter.alter_clause.attr_mthd.attr_def_list,
			    alter->info.alter.alter_clause.attr_mthd.attr_old_name,
			    &(alter->info.alter.constraint_list), &attr_chg_prop, &change_mode);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (change_mode == SM_ATTR_CHG_NOT_NEEDED)
    {
      /* nothing to do */
      goto exit;
    }

  if (ctemplate->current->users != NULL && ctemplate->partition != NULL)
    {
      DB_OBJLIST *user_list = NULL;

      user_count = ws_list_length ((DB_LIST *) ctemplate->current->users);

      usr_oid_array = (OID *) calloc (user_count, sizeof (OID));
      if (usr_oid_array == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, user_count * sizeof (OID));
	  goto exit;
	}

      for (i = 0, user_list = ctemplate->current->users; i < user_count && user_list != NULL;
	   i++, user_list = user_list->next)
	{
	  /* copy partition class OID for later use */
	  COPY_OID (&(usr_oid_array[i]), &(user_list->op->oid_info.oid));

	  /* force exclusive lock on class, even though it should have been already acquired */
	  if (locator_fetch_class (user_list->op, DB_FETCH_WRITE) == NULL)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }
	}
      has_partitions = true;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_ATTR);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  tran_saved = true;

  error =
    do_change_att_schema_only (parser, ctemplate, alter->info.alter.alter_clause.attr_mthd.attr_def_list,
			       alter->info.alter.alter_clause.attr_mthd.attr_old_name,
			       alter->info.alter.constraint_list, &attr_chg_prop, &change_mode);

  if (error != NO_ERROR)
    {
      goto exit;
    }

  /* save class MOP */
  class_mop = ctemplate->op;

  /* check foreign key constraints */
  error = do_check_fk_constraints (ctemplate, alter->info.alter.constraint_list);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  is_srv_update_needed = ((change_mode == SM_ATTR_CHG_WITH_ROW_UPDATE || change_mode == SM_ATTR_CHG_BEST_EFFORT)
			  && attr_chg_prop.name_space == ID_ATTRIBUTE) ? true : false;
  if (is_srv_update_needed)
    {
      COPY_OID (&class_oid, &(ctemplate->op->oid_info.oid));
      att_id = attr_chg_prop.att_id;
    }

  /* force schema update to server */
  class_obj = dbt_finish_class (ctemplate);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }
  /* set NULL, avoid 'abort_class' in case of error */
  ctemplate = NULL;

  if (attr_chg_prop.constr_info != NULL)
    {
      SM_CONSTRAINT_INFO *saved_constr = NULL;

      for (saved_constr = attr_chg_prop.constr_info; saved_constr != NULL; saved_constr = saved_constr->next)
	{
	  if (saved_constr->func_index_info || saved_constr->filter_predicate)
	    {
	      if (saved_constr->func_index_info)
		{
		  error = do_recreate_func_index_constr (parser, saved_constr, NULL, alter, NULL, NULL);
		  if (error != NO_ERROR)
		    {
		      goto exit;
		    }
		}
	      if (saved_constr->filter_predicate)
		{
		  error = do_recreate_filter_index_constr (parser, saved_constr->filter_predicate, alter, NULL, NULL);
		  if (error != NO_ERROR)
		    {
		      goto exit;
		    }
		}

	      if (!is_srv_update_needed)
		{
		  const char *att_names[2];
		  PT_NODE *att_old_name = alter->info.alter.alter_clause.attr_mthd.attr_old_name;

		  if (att_old_name != NULL)
		    {
		      assert (att_old_name->node_type == PT_NAME);
		      att_names[0] = att_old_name->info.name.original;
		      att_names[1] = NULL;

		      assert (alter->info.alter.alter_clause.attr_mthd.attr_old_name->node_type == PT_NAME);
		      error =
			sm_drop_constraint (class_mop, saved_constr->constraint_type, saved_constr->name, att_names,
					    false, false);

		      if (error != NO_ERROR)
			{
			  goto exit;
			}

		      error = sm_add_constraint (class_mop, saved_constr->constraint_type, saved_constr->name,
						 (const char **) saved_constr->att_names, saved_constr->asc_desc,
						 saved_constr->prefix_length, false, saved_constr->filter_predicate,
						 saved_constr->func_index_info, saved_constr->comment,
						 saved_constr->index_status);
		      if (error != NO_ERROR)
			{
			  goto exit;
			}
		    }
		}
	    }
	}
    }

  if (is_srv_update_needed || is_att_prop_set (attr_chg_prop.p[P_TYPE], ATT_CHG_TYPE_PREC_INCR))
    {
      error = do_drop_att_constraints (class_mop, attr_chg_prop.constr_info);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      /* perform UPDATE or each row */
      if (is_srv_update_needed)
	{
	  assert (att_id >= 0);
	  assert (!OID_ISNULL (&class_oid));

	  if (has_partitions)
	    {
	      assert (user_count > 0);
	      assert (usr_oid_array != NULL);

	      for (i = 0; i < user_count; i++)
		{
		  error = do_run_upgrade_instances_domain (parser, &(usr_oid_array[i]), att_id);
		  if (error != NO_ERROR)
		    {
		      goto exit;
		    }
		}
	    }
	  else
	    {
	      error = do_run_upgrade_instances_domain (parser, &class_oid, att_id);
	      if (error != NO_ERROR)
		{
		  goto exit;
		}
	    }
	}

      error = sort_constr_info_list (&(attr_chg_prop.constr_info));
      if (error != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Sorting constraints failed.");
	  goto exit;
	}

      error = do_recreate_att_constraints (class_mop, attr_chg_prop.constr_info);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }
  else
    {
      assert (change_mode == SM_ATTR_CHG_ONLY_SCHEMA);
    }

  /* create any new constraints: */
  if (attr_chg_prop.new_constr_info != NULL)
    {
      SM_CONSTRAINT_INFO *ci = NULL;

      error = sort_constr_info_list (&(attr_chg_prop.new_constr_info));
      if (error != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Sorting constraints failed.");
	  goto exit;
	}

      /* add new constraints */
      for (ci = attr_chg_prop.new_constr_info; ci != NULL; ci = ci->next)
	{
	  if (ci->constraint_type == DB_CONSTRAINT_NOT_NULL)
	    {
	      const char *att_name = *(ci->att_names);

	      if (alter->info.alter.hint & PT_HINT_SKIP_UPDATE_NULL)
		{
		  error = db_add_constraint (class_mop, ci->constraint_type, NULL, (const char **) ci->att_names, 0);
		}
	      else if (!prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT))
		{
		  char query[SM_MAX_IDENTIFIER_LENGTH * 4 + 36] = { 0 };
		  const char *class_name = NULL;
		  const char *hard_default =
		    get_hard_default_for_type (alter->info.alter.alter_clause.attr_mthd.attr_def_list->type_enum);
		  int update_rows_count = 0;

		  class_name = db_get_class_name (class_mop);
		  if (class_name == NULL)
		    {
		      error = ER_UNEXPECTED;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Cannot get class name of mop.");
		      goto exit;
		    }

		  assert (class_name != NULL && att_name != NULL && hard_default != NULL);

		  snprintf (query, SM_MAX_IDENTIFIER_LENGTH * 4 + 30, "UPDATE [%s] SET [%s]=%s WHERE [%s] IS NULL",
			    class_name, att_name, hard_default, att_name);
		  error = do_run_update_query_for_class (query, class_mop, &update_rows_count);
		  if (error != NO_ERROR)
		    {
		      goto exit;
		    }

		  if (update_rows_count > 0)
		    {
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_ALTER_CHANGE_ADD_NOT_NULL_SET_HARD_DEFAULT, 0);
		    }
		  error = db_add_constraint (class_mop, ci->constraint_type, NULL, (const char **) ci->att_names, 0);
		}
	      else
		{
		  error = db_constrain_non_null (class_mop, *(ci->att_names), 0, 1);
		}
	      if (error != NO_ERROR)
		{
		  goto exit;
		}
	    }
	  else
	    {
	      assert (ci->constraint_type == DB_CONSTRAINT_UNIQUE || ci->constraint_type == DB_CONSTRAINT_PRIMARY_KEY);

	      error = db_add_constraint (class_mop, ci->constraint_type, NULL, (const char **) ci->att_names, 0);
	    }

	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	}
    }

exit:

  if (ctemplate != NULL)
    {
      dbt_abort_class (ctemplate);
      ctemplate = NULL;
    }

  if (error != NO_ERROR && tran_saved && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_ATTR);
    }

  if (attr_chg_prop.constr_info != NULL)
    {
      sm_free_constraint_info (&(attr_chg_prop.constr_info));
    }

  if (attr_chg_prop.new_constr_info != NULL)
    {
      sm_free_constraint_info (&(attr_chg_prop.new_constr_info));
    }

  if (usr_oid_array != NULL)
    {
      free_and_init (usr_oid_array);
    }

  return error;
}

/*
 * do_alter_change_owner() - change the owner of a class/vclass
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_CHANGE_OWNER claus
 */
static int
do_alter_change_owner (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  int error = NO_ERROR;
  DB_OBJECT *obj = NULL;
  DB_VALUE returnval, class_val, user_val;
  PT_NODE *class_, *user;

  assert (alter != NULL);

  class_ = alter->info.alter.entity_name;
  assert (class_ != NULL);

  user = alter->info.alter.alter_clause.user.user_name;
  assert (user != NULL);

  db_make_null (&returnval);

  db_make_string (&class_val, class_->info.name.original);
  db_make_string (&user_val, user->info.name.original);

  au_change_owner_method (obj, &returnval, &class_val, &user_val);

  pr_clear_value (&class_val);
  pr_clear_value (&user_val);

  if (DB_VALUE_TYPE (&returnval) == DB_TYPE_ERROR)
    {
      error = db_get_error (&returnval);
    }

  return error;
}

/*
 * do_alter_change_default_cs_coll() - change the default collation of a
 *				       class/vclass
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_CHANGE_OWNER claus
 */
static int
do_alter_change_default_cs_coll (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  int error = NO_ERROR;
  const char *entity_name = NULL;
  DB_OBJECT *class_obj = NULL;
  DB_CTMPL *ctemplate = NULL;
  PT_ALTER_INFO *alter_info;
  bool tran_saved = false;
  MOP class_mop = NULL;
  OID class_oid;
  bool is_chg_needed = false;
  int i, collation_id = -1, is_partition = -1;
  MOP *sub_partitions = NULL;

  alter_info = &(alter->info.alter);
  assert (alter_info->code == PT_CHANGE_COLLATION);

  OID_SET_NULL (&class_oid);

  entity_name = alter_info->entity_name->info.name.original;
  if (entity_name == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Expecting a class or virtual class name.");
      goto exit;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_DEF_COLL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  tran_saved = true;

  class_obj = db_find_class (entity_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  error = locator_flush_class (class_obj);
  if (error != NO_ERROR)
    {
      /* don't overwrite error */
      goto exit;
    }

  /* get exclusive lock on class */
  if (locator_fetch_class (class_obj, DB_FETCH_WRITE) == NULL)
    {
      error = ER_FAILED;
      goto exit;
    }

  ctemplate = dbt_edit_class (class_obj);
  if (ctemplate == NULL)
    {
      /* when dbt_edit_class fails (e.g. because the server unilaterally aborts us), we must record the associated
       * error message into the parser.  Otherwise, we may get a confusing error msg of the form: "so_and_so is not a
       * class". */
      pt_record_error (parser, parser->statement_number - 1, alter->line_number, alter->column_number, er_msg (), NULL);
      error = er_errid ();
      goto exit;
    }

  error = check_change_class_collation (parser, ctemplate, alter_info, &is_chg_needed, &collation_id);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (!is_chg_needed)
    {
      /* nothing to do */
      goto exit;
    }

  class_mop = ctemplate->op;

  error = sm_set_class_collation (class_mop, collation_id);

  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = sm_partitioned_class_type (class_mop, &is_partition, NULL, &sub_partitions);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (is_partition == DB_PARTITIONED_CLASS)
    {
      for (i = 0; sub_partitions[i]; i++)
	{
	  error = sm_set_class_collation (sub_partitions[i], collation_id);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}

      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

  /* force schema update to server */
  class_obj = dbt_finish_class (ctemplate);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }
  /* set NULL, avoid 'abort_class' in case of error */
  ctemplate = NULL;

exit:

  if (ctemplate != NULL)
    {
      dbt_abort_class (ctemplate);
      ctemplate = NULL;
    }

  if (sub_partitions)
    {
      free_and_init (sub_partitions);
    }

  if (error != NO_ERROR && tran_saved && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_ATTR);
    }

  return error;
}

/*
 * do_alter_change_tbl_comment() - change the table comment
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_CHANGE_TABLE_COMMENT clause
 */
static int
do_alter_change_tbl_comment (PARSER_CONTEXT * const parser, PT_NODE * const alter)
{
  int error = NO_ERROR;
  const char *entity_name = NULL;
  DB_OBJECT *class_obj = NULL;
  DB_CTMPL *ctemplate = NULL;
  PT_ALTER_INFO *alter_info;
  bool tran_saved = false;
  MOP class_mop = NULL;
  PT_NODE *comment_node = NULL;
  PARSER_VARCHAR *comment = NULL;

  alter_info = &(alter->info.alter);
  if (alter_info->code == PT_CHANGE_TABLE_COMMENT)
    {
      comment_node = alter_info->alter_clause.comment.tbl_comment;
    }
  else if (alter_info->code == PT_RESET_QUERY || alter_info->code == PT_ADD_QUERY
	   || alter_info->code == PT_MODIFY_QUERY)
    {
      comment_node = alter_info->alter_clause.query.view_comment;
    }
  else
    {
      /*
       * code shall be one of the above 4 types, otherwise it's an error.
       */
      assert (0);
    }

  assert (comment_node != NULL && comment_node->node_type == PT_VALUE);

  entity_name = alter_info->entity_name->info.name.original;
  if (entity_name == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Expecting a class or virtual class name.");
      goto exit;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_TBL_COMMENT);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  tran_saved = true;

  class_obj = db_find_class (entity_name);
  if (class_obj == NULL)
    {
      error = er_errid ();
      goto exit;
    }

  error = locator_flush_class (class_obj);
  if (error != NO_ERROR)
    {
      /* don't overwrite error */
      goto exit;
    }

  /* get exclusive lock on class */
  if (locator_fetch_class (class_obj, DB_FETCH_WRITE) == NULL)
    {
      error = ER_FAILED;
      goto exit;
    }

  ctemplate = dbt_edit_class (class_obj);
  if (ctemplate == NULL)
    {
      /* when dbt_edit_class fails (e.g. because the server unilaterally aborts us), we must record the associated
       * error message into the parser.  Otherwise, we may get a confusing error msg of the form: "so_and_so is not a
       * class". */
      pt_record_error (parser, parser->statement_number - 1, alter->line_number, alter->column_number, er_msg (), NULL);
      error = er_errid ();
      goto exit;
    }

  class_mop = ctemplate->op;
  comment = comment_node->info.value.data_value.str;
  error = sm_set_class_comment (class_mop, (char *) pt_get_varchar_bytes (comment));
  if (error != NO_ERROR)
    {
      goto exit;
    }

  /* force schema update to server */
  class_obj = dbt_finish_class (ctemplate);
  if (class_obj == NULL)
    {
      error = er_errid ();
      goto exit;
    }
  /* set NULL, avoid 'abort_class' in case of error */
  ctemplate = NULL;

exit:
  if (ctemplate != NULL)
    {
      dbt_abort_class (ctemplate);
      ctemplate = NULL;
    }

  if (error != NO_ERROR && tran_saved && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_TBL_COMMENT);
    }
  return error;
}

/*
 * do_alter_change_col_comment() - change the column comment
 *   return: Error code
 *   parser(in): Parser context
 *   alter(in/out): Parse tree of a PT_CHANGE_COLUMN_COMMENT clause
 */
static int
do_alter_change_col_comment (PARSER_CONTEXT * const parser, PT_NODE * const alter_node)
{
  int error = NO_ERROR;
  int meta = 0, shared = 0;
  SM_ATTRIBUTE *found_attr = NULL;
  SM_NAME_SPACE name_space = ID_NULL;
  const PT_ALTER_CODE alter_code = alter_node->info.alter.code;
  const char *entity_name = NULL;
  PT_NODE *attr_node = NULL;
  const char *attr_name = NULL;
  PT_NODE *comment_node = NULL;
  PARSER_VARCHAR *comment_str = NULL;
  DB_OBJECT *class_obj = NULL;
  DB_CTMPL *ctemplate = NULL;
  MOP class_mop = NULL;
  OID class_oid;
  bool tran_saved = false;

  assert (alter_code == PT_CHANGE_COLUMN_COMMENT);

  OID_SET_NULL (&class_oid);

  entity_name = alter_node->info.alter.entity_name->info.name.original;
  if (entity_name == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Expecting a class or virtual class name.");
      goto exit;
    }

  class_obj = db_find_class (entity_name);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  error = locator_flush_class (class_obj);
  if (error != NO_ERROR)
    {
      /* don't overwrite error */
      goto exit;
    }

  /* force exclusive lock on class, even though it should have been already acquired */
  if (locator_fetch_class (class_obj, DB_FETCH_WRITE) == NULL)
    {
      error = ER_FAILED;
      goto exit;
    }

  ctemplate = dbt_edit_class (class_obj);
  if (ctemplate == NULL)
    {
      /* when dbt_edit_class fails (e.g. because the server unilaterally aborts us), we must record the associated
       * error message into the parser. Otherwise, we may get a confusing error msg of the form: "so_and_so is not a
       * class". */
      pt_record_error (parser, parser->statement_number - 1, alter_node->line_number, alter_node->column_number,
		       er_msg (), NULL);
      error = er_errid ();
      goto exit;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_COLUMN_COMMENT);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  tran_saved = true;

  attr_node = alter_node->info.alter.alter_clause.attr_mthd.attr_def_list;
  while (attr_node != NULL)
    {
      attr_name = get_attr_name (attr_node);

      comment_node = attr_node->info.attr_def.comment;

      assert (comment_node != NULL);
      assert (comment_node->node_type == PT_VALUE);

      meta = (attr_node->info.attr_def.attr_type == PT_META_ATTR);
      shared = (attr_node->info.attr_def.attr_type == PT_SHARED);
      name_space = (meta) ? ID_CLASS_ATTRIBUTE : ((shared) ? ID_SHARED_ATTRIBUTE : ID_ATTRIBUTE);

      /* get the attribute structure */
      error = smt_find_attribute (ctemplate, attr_name, (name_space == ID_CLASS_ATTRIBUTE) ? 1 : 0, &found_attr);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      assert (found_attr != NULL);

      /* comment */
      comment_str = comment_node->info.value.data_value.str;

      ws_free_string_and_init (found_attr->comment);
      found_attr->comment = ws_copy_string ((char *) pt_get_varchar_bytes (comment_str));
      if (found_attr->comment == NULL && comment_str != NULL)
	{
	  error = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	  goto exit;
	}

      attr_node = attr_node->next;
    }

  /* save class MOP */
  class_mop = ctemplate->op;

  /* force schema update to server */
  class_obj = dbt_finish_class (ctemplate);
  if (class_obj == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  /* set NULL, avoid 'abort_class' in case of error */
  ctemplate = NULL;

exit:
  if (ctemplate != NULL)
    {
      dbt_abort_class (ctemplate);
    }

  if (error != NO_ERROR && tran_saved && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_CHANGE_COLUMN_COMMENT);
    }

  return error;
}

/*
 * do_change_att_schema_only() - Change an attribute of a class object
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   attribute(in/out): Attribute to add
 */
static int
do_change_att_schema_only (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attribute, PT_NODE * old_name_node,
			   PT_NODE * constraints, SM_ATTR_PROP_CHG * attr_chg_prop, SM_ATTR_CHG_SOL * change_mode)
{
  DB_VALUE stack_value;
  TP_DOMAIN *attr_db_domain = NULL;
  DB_VALUE *new_default = NULL;
  DB_VALUE *default_value = &stack_value;
  SM_ATTRIBUTE *found_att = NULL;
  int error = NO_ERROR;
  bool change_first = false;
  const char *change_after_attr = NULL;
  const char *old_name = NULL;
  const char *new_name = NULL;
  const char *attr_name = NULL;
  PARSER_VARCHAR *comment_str = NULL;
  DB_DEFAULT_EXPR new_default_expr;
  PT_NODE *comment = NULL;

  assert (attr_chg_prop != NULL);
  assert (change_mode != NULL);

  assert (attribute->node_type == PT_ATTR_DEF);

  db_make_null (&stack_value);

  attr_name = get_attr_name (attribute);

  /* get new name */
  if (old_name_node != NULL)
    {
      assert (old_name_node->node_type == PT_NAME);
      old_name = old_name_node->info.name.original;
      assert (old_name != NULL);

      /* attr_name is supplied using the ATTR_DEF node and it means: for MODIFY syntax : current and unchanged name
       * (attr_name) for CHANGE syntax : new name of the attribute (new_name) */
      if (is_att_prop_set (attr_chg_prop->p[P_NAME], ATT_CHG_PROPERTY_DIFF))
	{
	  new_name = attr_name;
	  attr_name = old_name;
	}
      else
	{
	  attr_name = old_name;
	  new_name = NULL;
	}
    }

  if (validate_attribute_domain (parser, attribute, smt_get_class_type (ctemplate) == SM_CLASS_CT ? true : false))
    {
      /* validate_attribute_domain() is assumed to issue whatever messages are pertinent. */
      error = ER_FAILED;
      goto exit;
    }

  if (*change_mode == SM_ATTR_CHG_ONLY_SCHEMA)
    {
      if (attr_chg_prop->name_space == ID_ATTRIBUTE)
	{
	  assert (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_UNCHANGED)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_SET_CLS_COMPAT)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PREC_INCR));
	}
      else
	{
	  assert (attr_chg_prop->name_space == ID_CLASS_ATTRIBUTE || attr_chg_prop->name_space == ID_SHARED_ATTRIBUTE);
	  assert (!is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG)
		  && !is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NOT_SUPPORTED));
	}

    }
  else if (*change_mode == SM_ATTR_CHG_WITH_ROW_UPDATE)
    {
      assert (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_UPGRADE));
    }
  else
    {
      assert (*change_mode == SM_ATTR_CHG_BEST_EFFORT);
      /* this mode is needed when: - a type change other than UPGRADE */
      assert (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NEED_ROW_CHECK)
	      || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PSEUDO_UPGRADE));
    }

  error = check_default_on_update_clause (parser, attribute);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  /* default value: for CLASS and SHARED attributes this changes the value itself of the atribute */
  error = get_att_default_from_def (parser, attribute, &default_value, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  /* default_value is either NULL or pointing to address of stack_value */
  assert (default_value == NULL || default_value == &stack_value);
  new_default = default_value;
  pt_get_default_expression_from_data_default_node (parser, attribute->info.attr_def.data_default, &new_default_expr);

  attr_db_domain = pt_node_to_db_domain (parser, attribute, ctemplate->name);
  if (attr_db_domain == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit;
    }

  error = get_att_order_from_def (attribute, &change_first, &change_after_attr);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = smt_change_attribute_w_dflt_w_order (ctemplate, attr_name, new_name, NULL, attr_db_domain,
					       attr_chg_prop->name_space, new_default, &new_default_expr,
					       attribute->info.attr_def.on_update, change_first, change_after_attr,
					       &found_att);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  if (found_att == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Attribute not found.");
      goto exit;
    }

  if (is_att_prop_set (attr_chg_prop->p[P_NAME], ATT_CHG_PROPERTY_DIFF))
    {
      assert (new_name != NULL);
      attr_name = new_name;
    }

  /* save attribute id */
  attr_chg_prop->att_id = found_att->id;

  if (attr_chg_prop->name_space != ID_ATTRIBUTE)
    {
      assert (error == NO_ERROR);
      goto exit;
    }

  /* processing only for normal attributes */

  /* DEFAULT value */
  if (is_att_prop_set (attr_chg_prop->p[P_DEFAULT_VALUE], ATT_CHG_PROPERTY_LOST))
    {
      pr_clear_value (&(found_att->default_value.value));
      classobj_initialize_default_expr (&found_att->default_value.default_expr);

      if (found_att->properties != NULL)
	{
	  classobj_drop_prop (found_att->properties, "default_expr");
	}
    }

  /* on update expression */
  if (is_att_prop_set (attr_chg_prop->p[P_ON_UPDATE_EXPR], ATT_CHG_PROPERTY_LOST))
    {
      found_att->on_update_default_expr = DB_DEFAULT_NONE;

      if (found_att->properties != NULL)
	{
	  classobj_drop_prop (found_att->properties, "update_default");
	}
    }

  /* add or drop NOT NULL constraint */
  if (is_att_prop_set (attr_chg_prop->p[P_NOT_NULL], ATT_CHG_PROPERTY_GAINED))
    {
      assert (attribute->info.attr_def.constrain_not_null != 0);
      /* constraint is added later when new constraints are created */
    }
  else if (is_att_prop_set (attr_chg_prop->p[P_NOT_NULL], ATT_CHG_PROPERTY_LOST))
    {
      error =
	dbt_constrain_non_null (ctemplate, attr_name, (attr_chg_prop->name_space == ID_CLASS_ATTRIBUTE) ? 1 : 0, 0);
    }


  /* delete or (re-)create auto_increment attribute's serial object */
  if (is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_DIFF)
      || is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_LOST))
    {
      /* delete current serial */
      int save;
      OID *oidp, serial_obj_id;
      const char *name = found_att->header.name;

      if (is_att_prop_set (attr_chg_prop->p[P_NAME], ATT_CHG_PROPERTY_DIFF))
	{
	  name = old_name;
	  assert (name != NULL);
	}

      OID_SET_NULL (&serial_obj_id);

      if (found_att->auto_increment == NULL)
	{
	  char auto_increment_name[AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH];
	  MOP serial_class_mop, serial_mop;

	  serial_class_mop = sm_find_class (CT_SERIAL_NAME);

	  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name, ctemplate->name, name);
	  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, auto_increment_name);
	  found_att->auto_increment = serial_mop;
	}

      assert_release (found_att->auto_increment);

      error = au_check_serial_authorization (found_att->auto_increment);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      if (OID_ISNULL (&serial_obj_id))
	{
	  oidp = ws_identifier (found_att->auto_increment);
	  COPY_OID (&serial_obj_id, oidp);
	}

      AU_DISABLE (save);

      error = obj_delete (found_att->auto_increment);

      AU_ENABLE (save);

      if (error != NO_ERROR)
	{
	  goto exit;
	}

      (void) serial_decache (&serial_obj_id);

      found_att->flags &= ~(SM_ATTFLAG_AUTO_INCREMENT);
      found_att->auto_increment = NULL;
    }

  /* create or re-create serial with new properties */
  if (is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_DIFF)
      || is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_GAINED))
    {
      MOP auto_increment_obj = NULL;

      assert (attribute->info.attr_def.auto_increment != NULL);

      error = do_create_auto_increment_serial (parser, &auto_increment_obj, ctemplate->name, attribute);
      if (error == NO_ERROR)
	{
	  if (found_att != NULL)
	    {
	      found_att->auto_increment = auto_increment_obj;
	      found_att->flags |= SM_ATTFLAG_AUTO_INCREMENT;
	    }
	}
      else
	{
	  goto exit;
	}
    }

  /* the serial property has not changed, we are only dealing with renaming */
  if (is_att_prop_set (attr_chg_prop->p[P_NAME], ATT_CHG_PROPERTY_DIFF)
      && attribute->info.attr_def.auto_increment != NULL
      && !is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_DIFF)
      && !is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_LOST)
      && !is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_GAINED))
    {
      OID serial_obj_id;

      OID_SET_NULL (&serial_obj_id);

      if (found_att->auto_increment == NULL)
	{
	  char auto_increment_name[AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH];
	  MOP serial_class_mop, serial_mop;

	  serial_class_mop = sm_find_class (CT_SERIAL_NAME);

	  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name, ctemplate->name, old_name);
	  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, auto_increment_name);
	  found_att->auto_increment = serial_mop;
	}

      assert_release (found_att->auto_increment);

      error = do_update_auto_increment_serial_on_rename (found_att->auto_increment, ctemplate->name, new_name);

      if (error != NO_ERROR)
	{
	  goto exit;
	}
    }

  /* attribute type changed, and auto_increment is set to use(unchanged), update max_val in db_serial according to new
   * type */
  if (is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_PRESENT_OLD | ATT_CHG_PROPERTY_UNCHANGED)
      && is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
    {
      MOP auto_increment_obj = NULL;

      assert (attribute->info.attr_def.auto_increment != NULL);
      assert_release (found_att->auto_increment != NULL);

      error = do_update_maxvalue_of_auto_increment_serial (parser, &auto_increment_obj, ctemplate->name, attribute);

      if (error == NO_ERROR)
	{
	  assert_release (auto_increment_obj != NULL);
	  if (found_att != NULL)
	    {
	      found_att->auto_increment = auto_increment_obj;
	      found_att->flags |= SM_ATTFLAG_AUTO_INCREMENT;
	    }
	}
      else
	{
	  goto exit;
	}
    }

  if (is_att_prop_set (attr_chg_prop->p[P_COMMENT], ATT_CHG_PROPERTY_DIFF))
    {
      comment = attribute->info.attr_def.comment;
      assert (comment != NULL && comment->node_type == PT_VALUE);
      comment_str = comment->info.value.data_value.str;
      found_att->comment = ws_copy_string ((char *) pt_get_varchar_bytes (comment_str));
      if (found_att->comment == NULL)
	{
	  error = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	  goto exit;
	}
    }
  else if (is_att_prop_set (attr_chg_prop->p[P_COMMENT], ATT_CHG_PROPERTY_LOST))
    {
      if (found_att->comment != NULL)
	{
	  ws_free_string (found_att->comment);
	  found_att->comment = NULL;
	}
    }

  assert (attr_chg_prop->name_space == ID_ATTRIBUTE);

exit:
  db_value_clear (&stack_value);
  return error;
}

/*
 * build_attr_change_map() - This builds a map of changes on the attribute
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in): Class template
 *   attr_def(in): New attribute definition (PT_NODE : PT_ATTR_DEF)
 *   attr_old_name(in): Old name of attribute (PT_NODE : PT_NAME)
 *   constraints(in): New constraints for class template
 *		      (PT_NODE : PT_CONSTRAINT)
 *   attr_chg_properties(out): map of attribute changes to build
 *
 */
static int
build_attr_change_map (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attr_def, PT_NODE * attr_old_name,
		       PT_NODE * constraints, SM_ATTR_PROP_CHG * attr_chg_properties)
{
  TP_DOMAIN *attr_db_domain = NULL;
  SM_ATTRIBUTE *att = NULL;
  SM_CLASS_CONSTRAINT *sm_cls_constr = NULL;
  PT_NODE *cnstr = NULL;
  const char *attr_name = NULL;
  const char *old_name = NULL;
  const char *new_name = NULL;
  int error = NO_ERROR;
  PT_NODE *comment;

  attr_name = get_attr_name (attr_def);

  /* attribute name */
  attr_chg_properties->p[P_NAME] = 0;
  attr_chg_properties->p[P_NAME] |= ATT_CHG_PROPERTY_PRESENT_OLD;
  if (attr_old_name != NULL)
    {
      assert (attr_old_name->node_type == PT_NAME);
      old_name = attr_old_name->info.name.original;
      assert (old_name != NULL);

      /* attr_name is supplied using the ATTR_DEF node and it means: for MODIFY syntax : current and unchanged node
       * (attr_name) for CHANGE syntax : new name of the attribute (new_name) */
      new_name = attr_name;
      attr_name = old_name;

      attr_chg_properties->p[P_NAME] |= ATT_CHG_PROPERTY_PRESENT_NEW;
      if (intl_identifier_casecmp (attr_name, new_name) == 0)
	{
	  attr_chg_properties->p[P_NAME] |= ATT_CHG_PROPERTY_UNCHANGED;
	}
      else
	{
	  attr_chg_properties->p[P_NAME] |= ATT_CHG_PROPERTY_DIFF;
	}
    }
  else
    {
      attr_chg_properties->p[P_NAME] |= ATT_CHG_PROPERTY_UNCHANGED;
    }

  /* at this point, attr_name is the current name of the attribute, new_name is either the desired new name or NULL, if
   * name change is not requested */

  /* get the attribute structure */
  error =
    smt_find_attribute (ctemplate, attr_name, (attr_chg_properties->name_space == ID_CLASS_ATTRIBUTE) ? 1 : 0, &att);
  if (error != NO_ERROR)
    {
      return error;
    }

  assert (att != NULL);

  attr_chg_properties->name_space = att->header.name_space;

  if (attr_def->info.attr_def.attr_type == PT_NORMAL)
    {
      attr_chg_properties->new_name_space = ID_ATTRIBUTE;
    }
  else if (attr_def->info.attr_def.attr_type == PT_SHARED)
    {
      attr_chg_properties->new_name_space = ID_SHARED_ATTRIBUTE;
    }

  if (attr_def->info.attr_def.data_default != NULL)
    {
      if (attr_def->info.attr_def.data_default->info.data_default.shared == PT_SHARED)
	{
	  attr_chg_properties->new_name_space = ID_SHARED_ATTRIBUTE;
	}
    }

  /* DEFAULT value */
  attr_chg_properties->p[P_DEFAULT_VALUE] = 0;
  if (attr_def->info.attr_def.data_default != NULL)
    {
      attr_chg_properties->p[P_DEFAULT_VALUE] |= ATT_CHG_PROPERTY_PRESENT_NEW;
    }
  if (!DB_IS_NULL (&(att->default_value.original_value)) || !DB_IS_NULL (&(att->default_value.value))
      || att->default_value.default_expr.default_expr_type != DB_DEFAULT_NONE)
    {
      attr_chg_properties->p[P_DEFAULT_VALUE] |= ATT_CHG_PROPERTY_PRESENT_OLD;
    }

  /* ON UPDATE expr */
  attr_chg_properties->p[P_ON_UPDATE_EXPR] = 0;
  if (attr_def->info.attr_def.on_update != DB_DEFAULT_NONE)
    {
      attr_chg_properties->p[P_ON_UPDATE_EXPR] |= ATT_CHG_PROPERTY_PRESENT_NEW;
    }
  if (att->on_update_default_expr != DB_DEFAULT_NONE)
    {
      attr_chg_properties->p[P_ON_UPDATE_EXPR] |= ATT_CHG_PROPERTY_PRESENT_OLD;
    }

  /* DEFFERABLE : not supported, just mark as checked */
  attr_chg_properties->p[P_DEFFERABLE] = 0;

  /* ORDERING */
  attr_chg_properties->p[P_ORDER] = 0;
  if (attr_def->info.attr_def.ordering_info != NULL)
    {
      attr_chg_properties->p[P_ORDER] |= ATT_CHG_PROPERTY_PRESENT_NEW;
    }

  /* AUTO INCREMENT */
  attr_chg_properties->p[P_AUTO_INCR] = 0;
  if (attr_def->info.attr_def.auto_increment != NULL)
    {
      attr_chg_properties->p[P_AUTO_INCR] |= ATT_CHG_PROPERTY_PRESENT_NEW;
    }
  if (att->flags & SM_ATTFLAG_AUTO_INCREMENT)
    {
      attr_chg_properties->p[P_AUTO_INCR] |= ATT_CHG_PROPERTY_PRESENT_OLD;
    }

  /* existing FOREIGN KEY (referencing) */
  attr_chg_properties->p[P_CONSTR_FK] = 0;
  if (att->flags & SM_ATTFLAG_FOREIGN_KEY)
    {
      attr_chg_properties->p[P_CONSTR_FK] |= ATT_CHG_PROPERTY_PRESENT_OLD;
    }

  /* existing PRIMARY KEY: mark as checked */
  attr_chg_properties->p[P_S_CONSTR_PK] = 0;
  attr_chg_properties->p[P_M_CONSTR_PK] = 0;

  /* existing non-unique INDEX ? */
  attr_chg_properties->p[P_CONSTR_NON_UNI] = 0;
  if (att->flags & SM_ATTFLAG_INDEX)
    {
      attr_chg_properties->p[P_CONSTR_NON_UNI] |= ATT_CHG_PROPERTY_PRESENT_OLD;
    }

  /* prefix index */
  attr_chg_properties->p[P_PREFIX_INDEX] = 0;

  /* constraint : NOT NULL */
  attr_chg_properties->p[P_NOT_NULL] = 0;
  if (att->flags & SM_ATTFLAG_NON_NULL)
    {
      attr_chg_properties->p[P_NOT_NULL] |= ATT_CHG_PROPERTY_PRESENT_OLD;
    }

  /* constraint CHECK : not supported, just mark as checked */
  attr_chg_properties->p[P_CONSTR_CHECK] = 0;

  /* check for existing constraints: FK referenced, unique, non-unique idx */
  if (ctemplate->current != NULL)
    {
      const char *attr_name_to_check = attr_name;

      attr_chg_properties->p[P_S_CONSTR_UNI] = 0;
      attr_chg_properties->p[P_M_CONSTR_UNI] = 0;

      for (sm_cls_constr = ctemplate->current->constraints; sm_cls_constr != NULL; sm_cls_constr = sm_cls_constr->next)
	{
	  /* check if attribute is contained in this constraint */
	  SM_ATTRIBUTE **sm_constr_attr = sm_cls_constr->attributes;
	  int nb_att_in_constr = 0;
	  int attr_name_found_at = -1;

	  while (*sm_constr_attr != NULL)
	    {
	      if ((*sm_constr_attr)->header.name != NULL
		  && (*sm_constr_attr)->header.name_space == att->header.name_space
		  && !intl_identifier_casecmp ((*sm_constr_attr)->header.name, attr_name_to_check))
		{
		  attr_name_found_at = nb_att_in_constr;
		}
	      sm_constr_attr++;
	      nb_att_in_constr++;
	    }

	  if (attr_name_found_at != -1)
	    {
	      bool save_constr = false;

	      /* referenced FK */
	      if (sm_cls_constr->fk_info != NULL)
		{
		  assert (sm_cls_constr->fk_info->name != NULL);
		  attr_chg_properties->p[P_CONSTR_FK] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		}

	      /* PRIMARY KEY */
	      if (sm_cls_constr->type == SM_CONSTRAINT_PRIMARY_KEY)
		{
		  assert (nb_att_in_constr >= 1);
		  if (nb_att_in_constr >= 2)
		    {
		      attr_chg_properties->p[P_M_CONSTR_PK] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		    }
		  else
		    {
		      attr_chg_properties->p[P_S_CONSTR_PK] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		    }
		  attr_chg_properties->p[P_NOT_NULL] |= ATT_CHG_PROPERTY_PRESENT_NEW;
		  save_constr = true;
		}
	      /* non-unique index */
	      else if (sm_cls_constr->type == SM_CONSTRAINT_INDEX || sm_cls_constr->type == SM_CONSTRAINT_REVERSE_INDEX)
		{
		  assert (nb_att_in_constr >= 1);
		  attr_chg_properties->p[P_CONSTR_NON_UNI] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		  save_constr = true;

		  if (sm_cls_constr->attrs_prefix_length != NULL
		      && sm_cls_constr->attrs_prefix_length[attr_name_found_at] != -1)
		    {
		      attr_chg_properties->p[P_PREFIX_INDEX] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		    }
		}
	      /* UNIQUE */
	      else if (sm_cls_constr->type == SM_CONSTRAINT_UNIQUE
		       || sm_cls_constr->type == SM_CONSTRAINT_REVERSE_UNIQUE)
		{
		  assert (nb_att_in_constr >= 1);
		  if (nb_att_in_constr >= 2)
		    {
		      attr_chg_properties->p[P_M_CONSTR_UNI] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		    }
		  else
		    {
		      attr_chg_properties->p[P_S_CONSTR_UNI] |= ATT_CHG_PROPERTY_PRESENT_OLD;
		    }
		  save_constr = true;
		}

	      if (save_constr)
		{
		  assert (attr_chg_properties->name_space == ID_ATTRIBUTE);

		  error = sm_save_constraint_info (&(attr_chg_properties->constr_info), sm_cls_constr);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}
	    }
	}
    }
  else
    {
      error = ER_OBJ_TEMPLATE_INTERNAL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  /* attribute is saved in constraints info with the old name; replace all occurences with the new name; constraints
   * names are not adjusted to reflect new attribute name, but are kept with the old name, reason: MySQL compatibility */
  if ((new_name != NULL) && (attr_name != NULL) && (attr_chg_properties->constr_info != NULL)
      && (intl_identifier_casecmp (new_name, attr_name) != 0))
    {
      SM_CONSTRAINT_INFO *saved_constr = NULL;

      for (saved_constr = attr_chg_properties->constr_info; saved_constr != NULL; saved_constr = saved_constr->next)
	{
	  char **c_name = NULL;
	  for (c_name = saved_constr->att_names; *c_name != NULL; ++c_name)
	    {
	      if (intl_identifier_casecmp (attr_name, *c_name) == 0)
		{
		  free_and_init (*c_name);
		  *c_name = strdup (new_name);
		  if (*c_name == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      (strlen (new_name) + 1) * sizeof (char));
		      return ER_OUT_OF_VIRTUAL_MEMORY;
		    }
		}
	    }
	}
    }

  /* check for constraints in the new attribute definition */
  for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
    {
      PT_NODE *constr_att = NULL;
      PT_NODE *constr_att_list = NULL;
      bool save_pt_costraint = false;
      int chg_prop_idx = NUM_ATT_CHG_PROP;
      const char *attr_name_to_check = attr_name;

      if (is_att_prop_set (attr_chg_properties->p[P_NAME], ATT_CHG_PROPERTY_DIFF))
	{
	  attr_name_to_check = new_name;
	}

      assert (cnstr->node_type == PT_CONSTRAINT);
      switch (cnstr->info.constraint.type)
	{
	case PT_CONSTRAIN_FOREIGN_KEY:
	  constr_att_list = cnstr->info.constraint.un.foreign_key.attrs;
	  chg_prop_idx = P_CONSTR_FK;
	  break;
	case PT_CONSTRAIN_PRIMARY_KEY:
	  constr_att_list = cnstr->info.constraint.un.primary_key.attrs;
	  chg_prop_idx = P_S_CONSTR_PK;
	  save_pt_costraint = true;
	  break;
	case PT_CONSTRAIN_UNIQUE:
	  constr_att_list = cnstr->info.constraint.un.unique.attrs;
	  chg_prop_idx = P_S_CONSTR_UNI;
	  save_pt_costraint = true;
	  break;
	case PT_CONSTRAIN_NOT_NULL:
	  constr_att_list = cnstr->info.constraint.un.not_null.attr;
	  chg_prop_idx = P_NOT_NULL;
	  save_pt_costraint = true;
	  break;
	case PT_CONSTRAIN_CHECK:
	  /* not supported, just mark as 'PRESENT' */
	  assert (false);
	  attr_chg_properties->p[P_CONSTR_CHECK] |= ATT_CHG_PROPERTY_PRESENT_NEW;
	  continue;
	default:
	  assert (false);
	}

      for (constr_att = constr_att_list; constr_att != NULL; constr_att = constr_att->next)
	{
	  assert (constr_att->node_type == PT_NAME);
	  if (intl_identifier_casecmp (attr_name_to_check, constr_att->info.name.original) == 0)
	    {
	      if (chg_prop_idx >= NUM_ATT_CHG_PROP)
		{
		  continue;
		}

	      assert (chg_prop_idx < NUM_ATT_CHG_PROP);
	      assert (chg_prop_idx >= 0);

	      /* save new constraint only if it is not already present in current template */
	      if (save_pt_costraint
		  && !is_att_prop_set (attr_chg_properties->p[chg_prop_idx], ATT_CHG_PROPERTY_PRESENT_OLD))
		{
		  error = save_constraint_info_from_pt_node (&(attr_chg_properties->new_constr_info), cnstr);
		  if (error != NO_ERROR)
		    {
		      return error;
		    }
		}

	      attr_chg_properties->p[chg_prop_idx] |= ATT_CHG_PROPERTY_PRESENT_NEW;
	      break;
	    }
	}
    }

  /* partitions: */
  attr_chg_properties->p[P_IS_PARTITION_COL] = 0;
  if (ctemplate->partition)
    {
      char keycol[DB_MAX_IDENTIFIER_LENGTH] = { 0 };

      assert (attr_chg_properties->name_space == ID_ATTRIBUTE);

      error = do_get_partition_keycol (keycol, ctemplate->op);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (intl_identifier_casecmp (keycol, attr_name) == 0)
	{
	  attr_chg_properties->p[P_IS_PARTITION_COL] |= ATT_CHG_PROPERTY_PRESENT_OLD;
	}
    }

  /* DOMAIN */
  attr_db_domain = pt_node_to_db_domain (parser, attr_def, ctemplate->name);
  if (attr_db_domain == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return (er_errid ());
    }
  attr_chg_properties->p[P_TYPE] = 0;
  attr_chg_properties->p[P_TYPE] |= ATT_CHG_PROPERTY_PRESENT_NEW;
  attr_chg_properties->p[P_TYPE] |= ATT_CHG_PROPERTY_PRESENT_OLD;

  /* consolidate properties : */
  {
    int i = 0;

    for (i = 0; i < NUM_ATT_CHG_PROP; i++)
      {
	int *const p = &(attr_chg_properties->p[i]);

	if (*p & ATT_CHG_PROPERTY_PRESENT_OLD)
	  {
	    if (*p & ATT_CHG_PROPERTY_PRESENT_NEW)
	      {
		*p |= ATT_CHG_PROPERTY_UNCHANGED;
	      }
	    else
	      {
		*p |= ATT_CHG_PROPERTY_LOST;
	      }
	  }
	else
	  {
	    if (*p & ATT_CHG_PROPERTY_PRESENT_NEW)
	      {
		*p |= ATT_CHG_PROPERTY_GAINED;
	      }
	    else
	      {
		*p |= ATT_CHG_PROPERTY_UNCHANGED;
	      }
	  }

	if (is_att_prop_set (*p, ATT_CHG_PROPERTY_DIFF) && is_att_prop_set (*p, ATT_CHG_PROPERTY_UNCHANGED))
	  {
	    /* remove UNCHANGED flag if DIFF flag was already set */
	    *p &= ~ATT_CHG_PROPERTY_UNCHANGED;
	  }
      }

  }

  /* special case : TYPE */
  if (tp_domain_match (attr_db_domain, att->domain, TP_EXACT_MATCH) != 0)
    {
      attr_chg_properties->p[P_TYPE] |= ATT_CHG_PROPERTY_UNCHANGED;
    }
  else
    {
      assert (attr_db_domain->type != NULL);

      /* remove "UNCHANGED" flag */
      attr_chg_properties->p[P_TYPE] &= ~ATT_CHG_PROPERTY_UNCHANGED;

      if (TP_DOMAIN_TYPE (attr_db_domain) == TP_DOMAIN_TYPE (att->domain)
	  && TP_IS_CHAR_BIT_TYPE (TP_DOMAIN_TYPE (attr_db_domain)))
	{
	  if (tp_domain_match (attr_db_domain, att->domain, TP_STR_MATCH) != 0)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PREC_INCR;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_PROPERTY_DIFF;
	      if (attr_db_domain->precision >= att->domain->precision)
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;

		  (void) build_att_coll_change_map (att->domain, attr_db_domain, attr_chg_properties);
		}
	      else
		{
		  assert (attr_db_domain->precision < att->domain->precision
			  || (TP_DOMAIN_COLLATION (attr_db_domain) != TP_DOMAIN_COLLATION (att->domain)));

		  if (QSTR_IS_FIXED_LENGTH (TP_DOMAIN_TYPE (attr_db_domain))
		      && prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT) == true)
		    {
		      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG;
		    }
		  else
		    {
		      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;

		      (void) build_att_coll_change_map (att->domain, attr_db_domain, attr_chg_properties);
		    }
		}
	    }
	}
      else if (TP_DOMAIN_TYPE (attr_db_domain) == TP_DOMAIN_TYPE (att->domain)
	       && TP_DOMAIN_TYPE (attr_db_domain) == DB_TYPE_NUMERIC)
	{
	  if (attr_db_domain->scale == att->domain->scale && attr_db_domain->precision > att->domain->precision)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PREC_INCR;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	}
      else if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (attr_db_domain)) && TP_IS_SET_TYPE (TP_DOMAIN_TYPE (att->domain)))
	{
	  if (tp_domain_compatible (att->domain, attr_db_domain) != 0)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_SET_CLS_COMPAT;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_PROPERTY_DIFF;
	    }
	}
      else
	{
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_PROPERTY_DIFF;
	  error = build_att_type_change_map (att->domain, attr_db_domain, attr_chg_properties);
	  if (error != NO_ERROR)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	      return error;
	    }
	}
    }

  tp_domain_free (attr_db_domain);

  /* special case : AUTO INCREMENT if start value specified, we create a new serial or keep the old one Be mysql
   * compatible, see CUBRIDSUS-6441 */
  if (is_att_prop_set (attr_chg_properties->p[P_AUTO_INCR], ATT_CHG_PROPERTY_PRESENT_OLD | ATT_CHG_PROPERTY_PRESENT_NEW)
      && attr_def->info.attr_def.auto_increment->info.auto_increment.start_val != NULL)
    {
      attr_chg_properties->p[P_AUTO_INCR] |= ATT_CHG_PROPERTY_DIFF;
      /* remove "UNCHANGED" flag */
      attr_chg_properties->p[P_AUTO_INCR] &= ~ATT_CHG_PROPERTY_UNCHANGED;
    }

  /* special case : DEFAULT */
  if (is_att_prop_set (attr_chg_properties->p[P_DEFAULT_VALUE],
		       (ATT_CHG_PROPERTY_PRESENT_OLD | ATT_CHG_PROPERTY_PRESENT_NEW)))
    {
      attr_chg_properties->p[P_DEFAULT_VALUE] |= ATT_CHG_PROPERTY_DIFF;
      /* remove "UNCHANGED" flag */
      attr_chg_properties->p[P_DEFAULT_VALUE] &= ~ATT_CHG_PROPERTY_UNCHANGED;
    }

  /* special case : UNIQUE on multiple columns */
  if (is_att_prop_set (attr_chg_properties->p[P_M_CONSTR_UNI], ATT_CHG_PROPERTY_PRESENT_OLD))
    {
      if (is_att_prop_set (attr_chg_properties->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
	{
	  attr_chg_properties->p[P_M_CONSTR_UNI] |= ATT_CHG_PROPERTY_DIFF;
	  /* remove "UNCHANGED" flag */
	  attr_chg_properties->p[P_M_CONSTR_UNI] &= ~ATT_CHG_PROPERTY_UNCHANGED;
	}
      else
	{
	  attr_chg_properties->p[P_M_CONSTR_UNI] |= ATT_CHG_PROPERTY_UNCHANGED;
	}
    }

  /* comment */
  attr_chg_properties->p[P_COMMENT] = 0;
  attr_chg_properties->p[P_COMMENT] |= ATT_CHG_PROPERTY_LOST;
  comment = attr_def->info.attr_def.comment;
  if (comment != NULL)
    {
      assert (comment->node_type == PT_VALUE);
      if (comment->info.value.data_value.str != NULL)
	{
	  attr_chg_properties->p[P_COMMENT] |= ATT_CHG_PROPERTY_DIFF;
	  /* remove "LOST" flag */
	  attr_chg_properties->p[P_COMMENT] &= ~ATT_CHG_PROPERTY_LOST;
	}
    }

  return error;
}

/*
 * build_att_type_change_map() - This checks the attribute type change
 *
 *   return: Error code
 *   parser(in): Parser context
 *   curr_domain(in): Current domain of the atribute
 *   req_domain(in): Requested (new) domain of the attribute
 *   attr_chg_properties(out): structure summarizing the changed properties
 *   of attribute
 */
static int
build_att_type_change_map (TP_DOMAIN * curr_domain, TP_DOMAIN * req_domain, SM_ATTR_PROP_CHG * attr_chg_properties)
{
  int error = NO_ERROR;
  const int MIN_DIGITS_FOR_INTEGER = TP_INTEGER_PRECISION;
  const int MIN_DIGITS_FOR_SHORT = TP_SMALLINT_PRECISION;
  const int MIN_DIGITS_FOR_BIGINT = TP_BIGINT_PRECISION;
  const int MIN_CHARS_FOR_TIME = TP_TIME_AS_CHAR_LENGTH;
  const int MIN_CHARS_FOR_DATE = TP_DATE_AS_CHAR_LENGTH;
  const int MIN_CHARS_FOR_DATETIME = TP_DATETIME_AS_CHAR_LENGTH;
  const int MIN_CHARS_FOR_DATETIMETZ = TP_DATETIMETZ_AS_CHAR_LENGTH;
  const int MIN_CHARS_FOR_TIMESTAMP = TP_TIMESTAMP_AS_CHAR_LENGTH;
  const int MIN_CHARS_FOR_TIMESTAMPTZ = TP_TIMESTAMPTZ_AS_CHAR_LENGTH;

  DB_TYPE current_type = TP_DOMAIN_TYPE (curr_domain);
  DB_TYPE new_type = TP_DOMAIN_TYPE (req_domain);
  int req_prec = req_domain->precision;
  int req_scale = req_domain->scale;
  int cur_prec = curr_domain->precision;
  int cur_scale = curr_domain->scale;

  bool is_req_max_prec = false;

  /* check if maximum precision was requested for new domain */
  if (new_type == DB_TYPE_VARCHAR)
    {
      if (req_prec == DB_MAX_VARCHAR_PRECISION)
	{
	  is_req_max_prec = true;
	}
      else if (req_prec == TP_FLOATING_PRECISION_VALUE)
	{
	  req_prec = DB_MAX_VARCHAR_PRECISION;
	  is_req_max_prec = true;
	}
      else
	{
	  assert (req_prec >= 0);
	}
    }
  else if (new_type == DB_TYPE_VARNCHAR)
    {
      if (req_prec == DB_MAX_VARNCHAR_PRECISION)
	{
	  is_req_max_prec = true;
	}
      else if (req_prec == TP_FLOATING_PRECISION_VALUE)
	{
	  req_prec = DB_MAX_VARNCHAR_PRECISION;
	  is_req_max_prec = true;
	}
      else
	{
	  assert (req_prec >= 0);
	}
    }
  else
    {
      assert (is_req_max_prec == false);
    }

  switch (current_type)
    {
    case DB_TYPE_SHORT:
      switch (new_type)
	{
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_NUMERIC:
	  if (req_prec - req_scale >= MIN_DIGITS_FOR_SHORT)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_DIGITS_FOR_SHORT + 1)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_INTEGER:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_FLOAT:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_BIGINT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_NUMERIC:
	  if (req_prec - req_scale >= MIN_DIGITS_FOR_INTEGER)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_DIGITS_FOR_INTEGER + 1)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_BIGINT:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_NUMERIC:
	  if (req_prec - req_scale >= MIN_DIGITS_FOR_BIGINT)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_DIGITS_FOR_BIGINT + 1)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_NUMERIC:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	  if ((cur_prec < MIN_DIGITS_FOR_SHORT) && (cur_scale == 0))
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_INTEGER:
	  if ((cur_prec < MIN_DIGITS_FOR_INTEGER) && (cur_scale == 0))
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_BIGINT:
	  if ((cur_prec < MIN_DIGITS_FOR_BIGINT) && (cur_scale == 0))
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= cur_prec + 2)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_FLOAT:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (is_req_max_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_DOUBLE:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (is_req_max_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_MONETARY:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_MONETARY:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (is_req_max_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	case DB_TYPE_DOUBLE:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_TIME:
      switch (new_type)
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_TIME)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_DATE:
      switch (new_type)
	{
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_DATE)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_DATETIME:
      switch (new_type)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_DATE:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	  break;
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_DATETIME)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_DATETIMETZ:
      switch (new_type)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_DATE:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_TIMESTAMP:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_DATETIMETZ)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_DATETIMELTZ:
      switch (new_type)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_DATE:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	  break;
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_DATETIMETZ)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMP:
      switch (new_type)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_DATE:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_TIMESTAMP)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMPTZ:
      switch (new_type)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_DATE:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMELTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	  break;
	case DB_TYPE_DATETIMETZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_TIMESTAMPTZ)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      switch (new_type)
	{
	case DB_TYPE_TIME:
	case DB_TYPE_DATE:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	  break;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= MIN_CHARS_FOR_TIMESTAMPTZ)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  break;
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_CHAR:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_VARCHAR:
	  if (req_prec >= cur_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      if (prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT) == true)
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG;
		}
	      else
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
		}
	    }
	  break;
	default:
	  assert (new_type != DB_TYPE_CHAR);
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_VARCHAR:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	case DB_TYPE_ENUMERATION:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_CHAR:
	  if (req_prec >= cur_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	default:
	  assert (new_type != DB_TYPE_VARCHAR);
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_NCHAR:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_VARNCHAR:
	  if (req_prec >= cur_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      if (prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT) == true)
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG;
		}
	      else
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
		}
	    }
	  break;
	default:
	  assert (new_type != DB_TYPE_NCHAR);
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_VARNCHAR:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	case DB_TYPE_NCHAR:
	  if (req_prec >= cur_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	default:
	  assert (new_type != DB_TYPE_VARNCHAR);
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_BIT:
      switch (new_type)
	{
	case DB_TYPE_VARBIT:
	  if (req_prec >= cur_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      if (prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT) == true)
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG;
		}
	      else
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
		}
	    }
	  break;
	default:
	  assert (new_type != DB_TYPE_BIT);
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_VARBIT:
      switch (new_type)
	{
	case DB_TYPE_BIT:
	  if (req_prec >= cur_prec)
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_UPGRADE;
	    }
	  else
	    {
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	    }
	  break;
	default:
	  assert (new_type != DB_TYPE_VARBIT);
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    case DB_TYPE_OBJECT:
      if (new_type != DB_TYPE_OBJECT)
	{
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	}
      else
	{
	  assert (db_is_class (curr_domain->class_mop) != 0);
	  assert (db_is_class (req_domain->class_mop) != 0);

	  if (req_domain->class_mop != curr_domain->class_mop)
	    {
	      if (db_is_subclass (curr_domain->class_mop, req_domain->class_mop) <= 0)
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
		}
	      else
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_SET_CLS_COMPAT;
		}
	    }
	  else
	    {
	      /* same OBJECT, should have been checked earlier */
	      assert (false);
	      attr_chg_properties->p[P_TYPE] &= ~ATT_CHG_PROPERTY_DIFF;
	    }
	}
      break;

    case DB_TYPE_ENUMERATION:
      switch (new_type)
	{
	case DB_TYPE_SHORT:
	case DB_TYPE_INTEGER:
	case DB_TYPE_BIGINT:
	case DB_TYPE_NUMERIC:
	case DB_TYPE_FLOAT:
	case DB_TYPE_DOUBLE:
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_MONETARY:
	case DB_TYPE_ENUMERATION:
	case DB_TYPE_DATE:
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMETZ:
	case DB_TYPE_DATETIMELTZ:
	case DB_TYPE_TIME:
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPTZ:
	case DB_TYPE_TIMESTAMPLTZ:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NEED_ROW_CHECK;
	  break;
	default:
	  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	  break;
	}
      break;

    default:
      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
      break;
    }

  (void) build_att_coll_change_map (curr_domain, req_domain, attr_chg_properties);

  return error;
}

/*
 * build_att_coll_change_map() - This checks the attribute collation and
 *				 codeset change
 *
 *   return: Error code
 *   parser(in): Parser context
 *   curr_domain(in): Current domain of the attribute
 *   req_domain(in): Requested (new) domain of the attribute
 *   attr_chg_properties(out): structure summarizing the changed properties
 *   of attribute
 */
static int
build_att_coll_change_map (TP_DOMAIN * curr_domain, TP_DOMAIN * req_domain, SM_ATTR_PROP_CHG * attr_chg_properties)
{
  /* check collation change */
  if (TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (curr_domain)) && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (req_domain)))
    {
      const int curr_coll_id = TP_DOMAIN_COLLATION (curr_domain);
      const int req_coll_id = TP_DOMAIN_COLLATION (req_domain);
      const INTL_CODESET curr_cs = TP_DOMAIN_CODESET (curr_domain);
      const INTL_CODESET req_cs = TP_DOMAIN_CODESET (req_domain);

      if (curr_coll_id != req_coll_id)
	{
	  if (!INTL_CAN_COERCE_CS (curr_cs, req_cs))
	    {
	      /* change of codeset not supported */
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
	    }
	  else
	    {
	      /* change of collation allowed : requires index recreation */
	      attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_PSEUDO_UPGRADE;
	    }

	  if (is_att_prop_set (attr_chg_properties->p[P_PREFIX_INDEX], ATT_CHG_PROPERTY_PRESENT_OLD))
	    {
	      /* check if new collation has expansions */
	      LANG_COLLATION *lc = lang_get_collation (req_coll_id);

	      assert (lc != NULL);

	      if (!(lc->options.allow_prefix_index))
		{
		  attr_chg_properties->p[P_TYPE] |= ATT_CHG_TYPE_NOT_SUPPORTED;
		}
	    }
	}
      else
	{
	  /* collation and codeset unchanged */
	  assert (curr_cs == req_cs);
	}
    }

  return NO_ERROR;
}

/*
 * check_att_chg_allowed() - This checks if the attribute change is possible,
 *                           if not it sets an appropiate error
 *
 *   return: NO_ERROR if changed allowed, an error code if change not allowed
 *   att_name(in): name of attribute (to display in error messages)
 *   t(in): new type (parse tree namespace)
 *   attr_chg_properties(in): structure summarizing the changed properties of
 *                            attribute
 *   chg_how(in): the strategy for which the check is requested
 *   log_error_allowed(in): log the error if any
 *   new_attempt(out): is set to false if a new attempt with different
 *		       'chg_how' argument may produce a positive result
 *
 *  Note : this function may be called several times, each time esacalating
 *	   the 'chg_how' mode parameter; the caller should ensure that only
 *	   the last call allows also to log an error, by setting the
 *	   'log_error_allowed' argument.
 *	   The caller should also check the 'new_attempt' value before trying
 *	   a new 'chg_how' argument.
 *	   All error codes set in this function must correspond to messages
 *	   with one argument, otherwise additional processing must be done
 *	   before tracing the error.
 */
static int
check_att_chg_allowed (const char *att_name, const PT_TYPE_ENUM t, const SM_ATTR_PROP_CHG * attr_chg_prop,
		       SM_ATTR_CHG_SOL chg_how, bool log_error_allowed, bool * new_attempt)
{
  int error = NO_ERROR;

  /* these are error codes issued by ALTER CHANGE which map on other exising ALTER CHANGE error messages; they are kept
   * with different names for better differentiation between error contexts */
  const int ER_ALTER_CHANGE_TYPE_WITH_NON_UNIQUE = ER_ALTER_CHANGE_TYPE_WITH_INDEX;
  const int ER_ALTER_CHANGE_TYPE_WITH_M_UNIQUE = ER_ALTER_CHANGE_TYPE_WITH_INDEX;
  const int ER_ALTER_CHANGE_TYPE_WITH_S_UNIQUE = ER_ALTER_CHANGE_TYPE_WITH_INDEX;
  const int ER_ALTER_CHANGE_TYPE_WITH_PK = ER_ALTER_CHANGE_TYPE_WITH_INDEX;

  /* by default we advise new attempt */
  *new_attempt = true;

  /* partitions not allowed : this check (by value instead of bit) ensures that the column doesn't have partitions in
   * current schema and in new definition */
  if (attr_chg_prop->p[P_IS_PARTITION_COL] != ATT_CHG_PROPERTY_UNCHANGED)
    {
      error = ER_ALTER_CHANGE_PARTITIONS;
      *new_attempt = false;
      goto not_allowed;
    }
  /* foreign key not allowed : this check (by value instead of bit) ensures that the column doesn't have a foreign key
   * in current schema and in new definition */
  if (attr_chg_prop->p[P_CONSTR_FK] != ATT_CHG_PROPERTY_UNCHANGED)
    {
      error = ER_ALTER_CHANGE_FK;
      *new_attempt = false;
      goto not_allowed;
    }

  if ((attr_chg_prop->name_space == ID_SHARED_ATTRIBUTE && attr_chg_prop->new_name_space == ID_ATTRIBUTE)
      || (attr_chg_prop->name_space == ID_ATTRIBUTE && attr_chg_prop->new_name_space == ID_SHARED_ATTRIBUTE))
    {
      error = ER_ALTER_CHANGE_ATTR_TO_FROM_SHARED_NOT_ALLOWED;
      *new_attempt = false;
      goto not_allowed;
    }

  /* unique key : drop is allowed */
  /* unique key : gaining UK is matter of adding a new constraint */

  /* primary key : drop is allowed */
  /* primary key : gaining PK is matter of adding a new constraint */

  /* NOT NULL : gaining is not always allowed */
  if (is_att_prop_set (attr_chg_prop->p[P_NOT_NULL], ATT_CHG_PROPERTY_GAINED))
    {
      if (t == PT_TYPE_BLOB || t == PT_TYPE_CLOB)
	{
	  error = ER_SM_NOT_NULL_NOT_ALLOWED;
	  *new_attempt = false;
	  goto not_allowed;
	}
      if (attr_chg_prop->name_space == ID_CLASS_ATTRIBUTE)
	{
	  error = ER_SM_INVALID_CONSTRAINT;
	  *new_attempt = false;
	  goto not_allowed;
	}
      if (prm_get_bool_value (PRM_ID_ALTER_TABLE_CHANGE_TYPE_STRICT) == false)
	{
	  /* in permissive mode, we may have to convert existent NULL values to hard- defaults, so make sure the hard
	   * default type exists */
	  if (get_hard_default_for_type (t) == NULL)
	    {
	      error = ER_ALTER_CHANGE_HARD_DEFAULT_NOT_EXIST;
	      *new_attempt = false;
	      goto not_allowed;
	    }
	}
      /* gaining NOT NULL is matter of adding a new constraint */
    }

  /* check type changes and ... */
  /* check if AUTO_INCR is gained: */
  if (is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_GAINED))
    {
      if (attr_chg_prop->name_space == ID_CLASS_ATTRIBUTE || attr_chg_prop->name_space == ID_SHARED_ATTRIBUTE)
	{
	  error = ER_SM_INVALID_CONSTRAINT;
	  *new_attempt = false;
	  goto not_allowed;
	}

      if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
	{
	  if (chg_how == SM_ATTR_CHG_ONLY_SCHEMA)
	    {
	      error = ER_ALTER_CHANGE_TYPE_WITH_AUTO_INCR;
	      goto not_allowed;
	    }
	}
    }

  /* check type change */
  if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NOT_SUPPORTED))
    {
      error = ER_ALTER_CHANGE_TYPE_NOT_SUPP;
      *new_attempt = false;
      goto not_allowed;
    }
  else if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NOT_SUPPORTED_WITH_CFG))
    {
      error = ER_ALTER_CHANGE_TYPE_UPGRADE_CFG;
      *new_attempt = false;
      goto not_allowed;
    }
  else if (chg_how == SM_ATTR_CHG_ONLY_SCHEMA)
    {
      if (attr_chg_prop->name_space != ID_ATTRIBUTE)
	{
	  /* allow any type change (except when not supported by config) for class and shared attributes */

	  assert (attr_chg_prop->name_space == ID_CLASS_ATTRIBUTE || attr_chg_prop->name_space == ID_SHARED_ATTRIBUTE);

	  assert (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_UNCHANGED)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NEED_ROW_CHECK)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PSEUDO_UPGRADE)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_UPGRADE)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PREC_INCR)
		  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_SET_CLS_COMPAT));
	}
      else
	{
	  if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NEED_ROW_CHECK)
	      || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PSEUDO_UPGRADE))
	    {
	      error = ER_ALTER_CHANGE_TYPE_NEED_ROW_CHECK;
	      goto not_allowed;
	    }
	  else if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_UPGRADE))
	    {
	      error = ER_ALTER_CHANGE_TYPE_UPGRADE_CFG;
	      goto not_allowed;
	    }
	  else if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF)
		   && !(is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PREC_INCR)
			|| is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_SET_CLS_COMPAT)))
	    {
	      error = ER_ALTER_CHANGE_TYPE_NOT_SUPP;
	      goto not_allowed;
	    }
	}
    }
  else if (chg_how == SM_ATTR_CHG_WITH_ROW_UPDATE)
    {
      assert (attr_chg_prop->name_space == ID_ATTRIBUTE);

      if (is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_NEED_ROW_CHECK)
	  || is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_TYPE_PSEUDO_UPGRADE))
	{
	  error = ER_ALTER_CHANGE_TYPE_NEED_ROW_CHECK;
	  goto not_allowed;
	}
    }
  else
    {
      assert (attr_chg_prop->name_space == ID_ATTRIBUTE);

      /* allow any change that is not "NOT_SUPPORTED" */
      assert (chg_how == SM_ATTR_CHG_BEST_EFFORT);
    }

  /* these constraints are not allowed under a "schema only" change: */
  if (chg_how == SM_ATTR_CHG_ONLY_SCHEMA)
    {
      /* CLASS and SHARED attribute are incompatible with UNIQUE, PK */
      if (attr_chg_prop->name_space == ID_CLASS_ATTRIBUTE || attr_chg_prop->name_space == ID_SHARED_ATTRIBUTE)
	{
	  if (is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_UNI], ATT_CHG_PROPERTY_PRESENT_NEW)
	      || is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_NEW))
	    {

	      error = ER_SM_INVALID_CONSTRAINT;
	      *new_attempt = false;
	      goto not_allowed;
	    }
	}

      /* cannot keep UNIQUE constr if type is changed */
      if (is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_UNI],
			   ATT_CHG_PROPERTY_PRESENT_OLD | ATT_CHG_PROPERTY_PRESENT_NEW)
	  && is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
	{
	  error = ER_ALTER_CHANGE_TYPE_WITH_S_UNIQUE;
	  goto not_allowed;
	}
      if (is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_UNI], ATT_CHG_PROPERTY_PRESENT_OLD)
	  && is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
	{
	  error = ER_ALTER_CHANGE_TYPE_WITH_M_UNIQUE;
	  goto not_allowed;
	}

      /* primary key not allowed to be kept when type changes: */
      if (is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD | ATT_CHG_PROPERTY_PRESENT_NEW)
	  && is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
	{
	  error = ER_ALTER_CHANGE_TYPE_WITH_PK;
	  goto not_allowed;
	}

      /* non-unique index not allowed when type changes: */
      if (is_att_prop_set (attr_chg_prop->p[P_CONSTR_NON_UNI], ATT_CHG_PROPERTY_PRESENT_OLD)
	  && is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_DIFF))
	{
	  error = ER_ALTER_CHANGE_TYPE_WITH_NON_UNIQUE;
	  goto not_allowed;
	}
    }

  /* we should not have multiple primary keys defined */
  assert ((is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD))
	  ? (is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD) ? false : true) : true);

  /* ALTER .. CHANGE <attribute> syntax should not allow to define PK on multiple rows */
  assert (!is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_NEW));

  /* check if multiple primary keys after new definition */
  if ((is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD)
       || is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_NEW))
      && (is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD)
	  || is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_NEW)))
    {
      error = ER_ALTER_CHANGE_MULTIPLE_PK;
      *new_attempt = false;
      goto not_allowed;
    }

  /* check if class has subclasses: */
  if (attr_chg_prop->class_has_subclass
      && !(is_att_prop_set (attr_chg_prop->p[P_NAME], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_ORDER], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_TYPE], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_NOT_NULL], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_CONSTR_CHECK], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_DEFFERABLE], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_AUTO_INCR], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_UNCHANGED)
	   && is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_UNI], ATT_CHG_PROPERTY_UNCHANGED)))
    {
      /* allowed changes for class with sub-classes is for DEFAULT value */
      error = ER_ALTER_CHANGE_CLASS_HIERARCHY;
      *new_attempt = false;
      goto not_allowed;
    }

  return NO_ERROR;

not_allowed:
  if (log_error_allowed || !(*new_attempt))
    {
      if (error == ER_SM_NOT_NULL_NOT_ALLOWED || error == ER_SM_INVALID_CONSTRAINT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, pt_show_type_enum (t));
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, att_name);
	}
    }
  return error;
}

/*
 * is_att_property_structure_checked() - Checks all properties from the
 *				    attribute change properties structure
 *
 *   return : true, if all properties are marked as checked, false otherwise
 *   attr_chg_properties(in): structure summarizing the changed properties of
 *                            attribute
 *
 */
static bool
is_att_property_structure_checked (const SM_ATTR_PROP_CHG * attr_chg_properties)
{
  int i = 0;

  for (i = 0; i < NUM_ATT_CHG_PROP; i++)
    {
      if (attr_chg_properties->p[i] >= ATT_CHG_PROPERTY_NOT_CHECKED)
	{
	  return false;
	}
    }
  return true;
}

/*
 * is_att_change_needed() - Checks all properties from the attribute change
 *			    properties structure and decides if the schema
 *			    update is necessary
 *
 *   return : true, if schema change is needed, false otherwise
 *   attr_chg_properties(in): structure summarizing the changed properties of
 *			      attribute
 *
 */
static bool
is_att_change_needed (const SM_ATTR_PROP_CHG * attr_chg_properties)
{
  int i = 0;

  for (i = 0; i < NUM_ATT_CHG_PROP; i++)
    {
      if (attr_chg_properties->p[i] >= ATT_CHG_PROPERTY_DIFF)
	{
	  return true;
	}

      if (!is_att_prop_set (attr_chg_properties->p[i], ATT_CHG_PROPERTY_UNCHANGED))
	{
	  return true;
	}
    }
  return false;
}

/*
 * is_att_prop_set() - Checks that the properties has the flag set
 *
 *   return : true, if property has the value flag set, false otherwise
 *   prop(in): property
 *   value(in): value
 *
 */
static bool
is_att_prop_set (const int prop, const int value)
{
  return ((prop & value) == value);
}

/*
 * reset_att_property_structure() - Resets the attribute change properties
 *				    structure, so that all properties are
 *				    marked as 'unchecked'
 *
 *   attr_chg_properties(in): structure summarizing the changed properties of
 *                            attribute
 *
 */
static void
reset_att_property_structure (SM_ATTR_PROP_CHG * attr_chg_properties)
{
  int i = 0;

  assert (sizeof (attr_chg_properties->p) / sizeof (int) == NUM_ATT_CHG_PROP);

  for (i = 0; i < NUM_ATT_CHG_PROP; i++)
    {
      attr_chg_properties->p[i] = ATT_CHG_PROPERTY_NOT_CHECKED;
    }

  attr_chg_properties->constr_info = NULL;
  attr_chg_properties->new_constr_info = NULL;
  attr_chg_properties->att_id = -1;
  attr_chg_properties->name_space = ID_NULL;
  attr_chg_properties->new_name_space = ID_NULL;
  attr_chg_properties->class_has_subclass = false;
}

/*
 * get_att_order_from_def() - Retrieves the order properties (first,
 *			   after name) from the attribute definition node
 *
 *  return : NO_ERROR, if success; error code otherwise
 *  attribute(in): attribute definition node (PT_ATTR_DEF)
 *  ord_first(out): true if definition contains 'FIRST' specification, false
 *		    otherwise
 *  ord_after_name(out): name of column 'AFTER <col_name>'
 *
 */
static int
get_att_order_from_def (PT_NODE * attribute, bool * ord_first, const char **ord_after_name)
{
  PT_NODE *ordering_info = NULL;

  assert (attribute->node_type == PT_ATTR_DEF);

  ordering_info = attribute->info.attr_def.ordering_info;
  if (ordering_info != NULL)
    {
      assert (ordering_info->node_type == PT_ATTR_ORDERING);

      *ord_first = ordering_info->info.attr_ordering.first;

      if (ordering_info->info.attr_ordering.after != NULL)
	{
	  PT_NODE *const after_name = ordering_info->info.attr_ordering.after;

	  assert (after_name->node_type == PT_NAME);
	  *ord_after_name = after_name->info.name.original;
	  assert (*ord_first == false);
	}
      else
	{
	  *ord_after_name = NULL;
	  /*
	   * If we have no "AFTER name" then this must have been a "FIRST"
	   * token
	   */
	  assert (*ord_first == true);
	}
    }
  else
    {
      *ord_first = false;
      *ord_after_name = NULL;
    }

  return NO_ERROR;
}

static int
check_default_on_update_clause (PARSER_CONTEXT * parser, PT_NODE * attribute)
{
  int error = NO_ERROR;
  PT_TYPE_ENUM desired_type = attribute->type_enum;
  DB_DEFAULT_EXPR_TYPE on_update_expr_type = attribute->info.attr_def.on_update;
  PT_NODE *temp_ptval = NULL;

  if (on_update_expr_type == DB_DEFAULT_NONE)
    {
      return error;
    }

  PT_OP_TYPE op = pt_op_type_from_default_expr_type (on_update_expr_type);

  PT_NODE *on_update_default_expr = parser_make_expression (parser, op, NULL, NULL, NULL);
  if (on_update_default_expr == NULL)
    {
      PT_ERRORm (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return ER_FAILED;
    }

  on_update_default_expr = pt_semantic_type (parser, on_update_default_expr, NULL);
  if (on_update_default_expr == NULL)
    {
      return ER_FAILED;
    }

  on_update_default_expr = pt_semantic_check (parser, on_update_default_expr);
  if (on_update_default_expr == NULL)
    {
      return ER_FAILED;
    }

  on_update_default_expr->buffer_pos = attribute->buffer_pos;
  on_update_default_expr->line_number = attribute->line_number;
  on_update_default_expr->column_number = attribute->column_number;

  DB_VALUE on_update_val;
  db_make_null (&on_update_val);

  pt_evaluate_tree_having_serial (parser, on_update_default_expr, &on_update_val, 1);
  temp_ptval = pt_dbval_to_value (parser, &on_update_val);
  if (temp_ptval == NULL)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();

      pr_clear_value (&on_update_val);
      if (on_update_default_expr != NULL)
	{
	  parser_free_node (parser, on_update_default_expr);
	}
      return error;
    }

  error = pt_coerce_value_for_default_value (parser, temp_ptval, temp_ptval, desired_type, attribute->data_type,
					     on_update_expr_type);

  if (pt_has_error (parser))
    {
      /* forget previous one to set the better error */
      pt_reset_error (parser);
    }

  if (error != NO_ERROR)
    {
      const char *data_type_print;
      if (attribute->data_type != NULL)
	{
	  data_type_print = pt_short_print (parser, attribute->data_type);
	}
      else
	{
	  data_type_print = pt_show_type_enum ((PT_TYPE_ENUM) desired_type);
	}

      if (error == ER_IT_DATA_OVERFLOW)
	{
	  PT_ERRORmf2 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OVERFLOW_COERCING_TO,
		       pt_short_print (parser, on_update_default_expr), data_type_print);
	}
      else
	{
	  PT_ERRORmf2 (parser, attribute, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
		       pt_short_print (parser, on_update_default_expr), data_type_print);
	}
    }

  pr_clear_value (&on_update_val);
  if (temp_ptval != NULL)
    {
      parser_free_node (parser, temp_ptval);
    }
  if (on_update_default_expr != NULL)
    {
      parser_free_node (parser, on_update_default_expr);
    }

  return error;
}

/*
 * get_att_default_from_def() - Retrieves the default value property from the
 *				attribute definition node
 *
 *  return : NO_ERROR, if success; error code otherwise
 *  parser(in): parser context
 *  attribute(in): attribute definition node (PT_ATTR_DEF)
 *  default_value(in/out): default value; this must be initially passed as
 *			   pointer to an allocated DB_VALUE; it is returned
 *			   as NULL if a DEFAULT is not specified for the
 *			   attribute, otherwise the DEFAULT value is returned
 *			   (the initially passed value is used for storage)
 *  classname(in): If part of create class statement, this argument will be
 *		   the name of the class. We want to avoid fetching it since
 *		   it doesn't exist yet.
 *
 */
static int
get_att_default_from_def (PARSER_CONTEXT * parser, PT_NODE * attribute, DB_VALUE ** default_value,
			  const char *classname)
{
  int error = NO_ERROR;
  PT_NODE *def_val = NULL, *initial_def_val = NULL;
  DB_DEFAULT_EXPR_TYPE def_expr_type;
  PT_TYPE_ENUM desired_type = attribute->type_enum;
  bool has_self_ref = false;
  const char *data_type_print;

  assert (attribute->node_type == PT_ATTR_DEF);

  if (attribute->info.attr_def.data_default == NULL)
    {
      *default_value = NULL;
      return NO_ERROR;
    }

  def_expr_type = attribute->info.attr_def.data_default->info.data_default.default_expr_type;
  def_val = attribute->info.attr_def.data_default->info.data_default.default_value;
  def_val = pt_semantic_check (parser, def_val);
  if (pt_has_error (parser) || def_val == NULL)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
      goto exit;
    }

  if (classname != NULL && attribute->data_type != NULL)
    {
      PT_NODE *dt = NULL;

      for (dt = attribute->data_type; dt != NULL; dt = dt->next)
	{
	  if (dt->info.data_type.entity != NULL && dt->info.data_type.entity->node_type == PT_NAME
	      && intl_identifier_casecmp (dt->info.data_type.entity->info.name.original, classname) == 0)
	    {
	      has_self_ref = true;
	      break;
	    }
	}
    }

  initial_def_val = parser_copy_tree (parser, def_val);
  if (initial_def_val == NULL)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
      goto exit;
    }

  if (has_self_ref)
    {
      /* We are creating a new class, and expected domain of default value has a self reference. Class cannot be
       * resolved yet, since it doesn't exist. It is only reserved and it has a temporary OID. Thus, we need to handle
       * it here and avoid fetching the object (which will hit assert due to temporary OID). We can only accept a NULL
       * default value, or if the expected type is a collection (that contains self references too), we can only accept
       * an empty set. */
      DB_VALUE *value;

      if (desired_type != PT_TYPE_OBJECT && !PT_IS_COLLECTION_TYPE (desired_type))
	{
	  /* Should we even be here? */
	  PT_INTERNAL_ERROR (parser, "Self referencing attribute unexpected type.");
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	  goto exit;
	}
      /* Desired type either PT_TYPE_OBJECT or collection type. */

      /* We allow default value if: 1. Not a default expression. 2. Value is NULL or value is empty set and collection
       * type is expected. */
      value = &def_val->info.value.db_value;
      if (def_expr_type == DB_DEFAULT_NONE
	  && (db_value_is_null (value)
	      || (desired_type != PT_TYPE_OBJECT && TP_IS_SET_TYPE (value->domain.general_info.type)
		  && value->data.set->set->size == 0)))
	{
	  /* We can accept the default value. */
	  pt_evaluate_tree (parser, def_val, *default_value, 1);
	}
      else
	{
	  /* Cannot coerce. */
	  if (desired_type == PT_TYPE_OBJECT)
	    {
	      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
			   pt_short_print (parser, initial_def_val), classname);
	    }
	  else
	    {
	      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
			   pt_short_print (parser, initial_def_val), pt_show_type_enum (desired_type));
	    }
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	  goto exit;
	}
    }
  else
    {
      /* try to coerce the default value into the attribute type */
      if (def_expr_type == DB_DEFAULT_NONE)
	{
	  error = pt_coerce_value_for_default_value (parser, def_val, def_val, desired_type, attribute->data_type,
						     def_expr_type);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_coerce_error;
	    }
	}
      else
	{
	  DB_VALUE src;
	  PT_NODE *temp_val;

	  db_make_null (&src);

	  def_val = pt_semantic_type (parser, def_val, NULL);
	  if (pt_has_error (parser) || def_val == NULL)
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      error = er_errid ();
	      goto exit;
	    }

	  pt_evaluate_tree_having_serial (parser, def_val, &src, 1);
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      error = er_errid ();
	      goto exit;
	    }

	  temp_val = pt_dbval_to_value (parser, &src);
	  if (temp_val == NULL)
	    {
	      db_value_clear (&src);
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	      error = er_errid ();
	      goto exit;
	    }

	  error = pt_coerce_value_for_default_value (parser, temp_val, temp_val, desired_type, attribute->data_type,
						     def_expr_type);
	  db_value_clear (&src);
	  temp_val->info.value.db_value_is_in_workspace = 0;
	  parser_free_node (parser, temp_val);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_coerce_error;
	    }
	}

      if (def_expr_type == DB_DEFAULT_NONE)
	{
	  pt_evaluate_tree (parser, def_val, *default_value, 1);
	}
      else
	{
	  *default_value = NULL;
	}

      if (pt_has_error (parser))
	{
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	  goto exit;
	}
    }

exit:
  if (initial_def_val != NULL)
    {
      parser_free_tree (parser, initial_def_val);
    }
  return error;

exit_on_coerce_error:
  if (pt_has_error (parser))
    {
      /* forget previous one to set the better error */
      pt_reset_error (parser);
    }

  if (attribute->data_type != NULL)
    {
      data_type_print = pt_short_print (parser, attribute->data_type);
    }
  else
    {
      data_type_print = pt_show_type_enum ((PT_TYPE_ENUM) desired_type);
    }

  if (error == ER_IT_DATA_OVERFLOW)
    {
      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OVERFLOW_COERCING_TO,
		   pt_short_print (parser, initial_def_val), data_type_print);
    }
  else
    {
      PT_ERRORmf2 (parser, def_val, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CANT_COERCE_TO,
		   pt_short_print (parser, initial_def_val), data_type_print);
    }

  if (initial_def_val != NULL)
    {
      parser_free_tree (parser, initial_def_val);
    }

  return error;
}


/*
 * get_hard_default_for_type() - Get a hard-coded default value for the given
 *				 type, or NULL if there is no such value.
 *
 *  Note: the default is returned as a string, to be used in building queries.
 *
 *  return : pointer to a static char array or NULL
 *  type(in): the type, as stored in the parse tree
 *
 */
static const char *
get_hard_default_for_type (PT_TYPE_ENUM type)
{
  static const char *zero = "0";
  static const char *empty_str = "''";
  static const char *empty_n_str = "N''";
  static const char *empty_bit = "b'0'";
  static const char *empty_date = "DATE '01/01/0001'";
  static const char *empty_time = "TIME '00:00'";
  static const char *empty_datetime = "DATETIME '01/01/0001 00:00'";
  static const char *empty_dt_tz = "DATETIMETZ '01/01/0001 00:00 +00:00'";
  static const char *empty_dt_ltz = "DATETIMELTZ '01/01/0001 00:00 +00:00'";
  static const char *empty_json = "null";

  /* TODO : use db_value_domain_default instead, but make sure that db_value_domain_default is not using NULL DB_VALUE
   * as default for any type */

  /* Timestamp is interpreted as local and converted internally to UTC, so hard default value of Timestamp set to '1'
   * (Unix epoch time + 1). (0 means zero date) */
  static const char *empty_timestamp = "1";
  static const char *empty_set = "{}";

  switch (type)
    {
    case PT_TYPE_INTEGER:
    case PT_TYPE_SMALLINT:
    case PT_TYPE_MONETARY:
    case PT_TYPE_NUMERIC:
    case PT_TYPE_BIGINT:
    case PT_TYPE_FLOAT:
    case PT_TYPE_DOUBLE:
      return zero;

    case PT_TYPE_TIMESTAMP:
    case PT_TYPE_TIMESTAMPLTZ:
    case PT_TYPE_TIMESTAMPTZ:
      return empty_timestamp;

    case PT_TYPE_DATE:
      return empty_date;

    case PT_TYPE_TIME:
      return empty_time;

    case PT_TYPE_DATETIME:
      return empty_datetime;
    case PT_TYPE_DATETIMELTZ:
      return empty_dt_ltz;
    case PT_TYPE_DATETIMETZ:
      return empty_dt_tz;

    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
      return empty_str;

    case PT_TYPE_VARNCHAR:
    case PT_TYPE_NCHAR:
      return empty_n_str;

    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      return empty_set;

    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
      return empty_bit;
    case PT_TYPE_LOGICAL:
    case PT_TYPE_NONE:
    case PT_TYPE_MAYBE:
    case PT_TYPE_NA:
    case PT_TYPE_NULL:
    case PT_TYPE_STAR:
    case PT_TYPE_OBJECT:
    case PT_TYPE_MIDXKEY:
    case PT_TYPE_COMPOUND:
    case PT_TYPE_RESULTSET:
    case PT_TYPE_BLOB:
    case PT_TYPE_CLOB:
    case PT_TYPE_ELO:
      return NULL;
    case PT_TYPE_JSON:
      return empty_json;

    default:
      return NULL;
    }
}


/*
 * do_run_update_query_for_new_notnull_fields() - worker function for
 *    do_update_new_notnull_cols_without_default().
 *    It creates a complex UPDATE query and runs it.
 */
static int
do_run_update_query_for_new_notnull_fields (PARSER_CONTEXT * parser, PT_NODE * alter, PT_NODE * attr_list,
					    int attr_count, MOP class_mop)
{
  char *query, *q;
  int query_len, remaining, n;

  PT_NODE *attr;
  bool first = true;
  int error = NO_ERROR;
  int row_count = 0;

  assert (parser && alter && attr_list);
  assert (attr_count > 0);

  /* Allocate enough for each attribute's name, its default value, and for the "UPDATE table_name" part of the query.
   * 42 is more than the maximum length of any default value for an attribute, including three spaces, the coma sign
   * and an equal. */

  query_len = remaining = (attr_count + 1) * (DB_MAX_IDENTIFIER_LENGTH + 42);
  if (query_len > QUERY_MAX_SIZE)
    {
      ERROR1 (error, ER_UNEXPECTED, "Too many attributes.");
      return error;
    }

  q = query = (char *) malloc (query_len + 1);
  if (query == NULL)
    {
      ERROR1 (error, ER_OUT_OF_VIRTUAL_MEMORY, (size_t) (query_len + 1));
      return error;
    }

  query[0] = 0;

  /* Using UPDATE ALL to update the current class and all its children. */

  n = snprintf (q, remaining, "UPDATE ALL [%s] SET ", alter->info.alter.entity_name->info.name.original);
  if (n < 0)
    {
      ERROR1 (error, ER_UNEXPECTED, "Building UPDATE statement failed.");
      goto end;
    }
  remaining -= n;
  q += n;

  for (attr = attr_list; attr != NULL; attr = attr->next)
    {
      const char *sep = first ? "" : ", ";
      const char *hard_default = get_hard_default_for_type (attr->type_enum);


      n = snprintf (q, remaining, "%s[%s] = %s", sep, attr->info.attr_def.attr_name->info.name.original, hard_default);
      if (n < 0)
	{
	  ERROR1 (error, ER_UNEXPECTED, "Building UPDATE statement failed.");
	  goto end;
	}
      remaining -= n;
      q += n;

      first = false;
    }

  /* Now just RUN thew query */

  error = do_run_update_query_for_class (query, class_mop, &row_count);


end:
  if (query)
    {
      free_and_init (query);
    }

  return error;
}

/*
 * do_run_update_query_for_new_default_expression_fields() - worker function for
 *    do_update_new_cols_with_default_expression().
 *
 *  parser(in): parser
 *  alter(in): alter node
 *  attr_list(in) attribute nodes list
 *  attr_count(int): count attribute nodes
 *  class_mop(in): class mop
 *
 * Note : It creates a complex UPDATE query and runs it.
 */
static int
do_run_update_query_for_new_default_expression_fields (PARSER_CONTEXT * parser, PT_NODE * alter, PT_NODE * attr_list,
						       int attr_count, MOP class_mop)
{
  char *query, *q;
  int query_len, remaining, n;
  PT_NODE *attr;
  bool first = true;
  int error = NO_ERROR;
  int row_count = 0;

  assert (parser && alter && attr_list);
  assert (attr_count > 0);

  /* Allocate enough for each attribute's name, its default value, and for the "UPDATE table_name" part of the query.
   * 100 is more than the maximum length of any default value for an attribute, including three spaces, the comma sign
   * and an equal. */

  query_len = remaining = (attr_count + 1) * (DB_MAX_IDENTIFIER_LENGTH + 100);
  if (query_len > QUERY_MAX_SIZE)
    {
      ERROR1 (error, ER_UNEXPECTED, "Too many attributes.");
      return error;
    }

  q = query = (char *) malloc (query_len + 1);
  if (query == NULL)
    {
      ERROR1 (error, ER_OUT_OF_VIRTUAL_MEMORY, (size_t) (query_len + 1));
      return error;
    }

  query[0] = 0;

  /* Using UPDATE ALL to update the current class and all its children. */
  n = snprintf (q, remaining, "UPDATE ALL [%s] SET ", alter->info.alter.entity_name->info.name.original);
  if (n < 0)
    {
      ERROR1 (error, ER_UNEXPECTED, "Building UPDATE statement failed.");
      goto end;
    }
  remaining -= n;
  q += n;

  for (attr = attr_list; attr != NULL; attr = attr->next)
    {
      const char *sep = first ? "" : ", ";
      char *data_default;

      data_default = parser_print_tree (parser, attr->info.attr_def.data_default->info.data_default.default_value);
      if (data_default == NULL)
	{
	  continue;
	}

      n = snprintf (q, remaining, "%s[%s] = %s", sep, attr->info.attr_def.attr_name->info.name.original, data_default);
      if (n < 0)
	{
	  ERROR1 (error, ER_UNEXPECTED, "Building UPDATE statement failed.");
	  goto end;
	}
      remaining -= n;
      q += n;

      first = false;
    }

  /* Now just RUN the query */
  error = do_run_update_query_for_class (query, class_mop, &row_count);

end:
  if (query)
    {
      free_and_init (query);
    }

  return error;
}

/*
 * is_attribute_primary_key() - Returns true if the attribute given is part
 *				of the primary key of the table.
 *
 *
 *  return : true or false
 *  class_name(in): the class name
 *  attr_name(in):  the attribute name
 *
 */
static bool
is_attribute_primary_key (const char *class_name, const char *attr_name)
{
  DB_ATTRIBUTE *db_att = NULL;

  if (class_name == NULL || attr_name == NULL)
    {
      return false;
    }

  db_att = db_get_attribute_by_name (class_name, attr_name);

  if (db_att && db_attribute_is_primary_key (db_att))
    {
      return true;
    }
  return false;
}




/*
 * do_update_new_notnull_cols_without_default()
 * Populates the newly added columns with hard-coded defaults.
 *
 * Used only on ALTER TABLE ... ADD COLUMN, and only AFTER the operation has
 * been performed (i.e. the columns have been added to the schema, even
 * though the transaction has not been committed).
 *
 * IF the clause has added columns that:
 *   1. have no default value AND
 *     2a. have the NOT NULL constraint OR
 *     2b. are part of the PRIMARY KEY
 * THEN try to fill them with a hard-coded default (zero, empty string etc.)
 *
 * This is done in MySQL compatibility mode, to ensure consistency: otherwise
 * columns with the NOT NULL constraint would have ended up being filled
 * with NULL as a default.
 *
 * NOTE: there are types (such as OBJECT) that do not have a "zero"-like
 * value, and if we encounter one of these, we block the entire operation.
 *
 *   return: Error code if operation fails or if one of the attributes to add
 *           is of type OBJECT, with NOT NULL and no default value.
 *   parser(in): Parser context
 *   alter(in):  Parse tree of the statement
 */
static int
do_update_new_notnull_cols_without_default (PARSER_CONTEXT * parser, PT_NODE * alter, MOP class_mop)
{
  PT_NODE *relevant_attrs = NULL;
  int error = NO_ERROR;
  int attr_count = 0;

  PT_NODE *attr = NULL;
  PT_NODE *save = NULL;
  PT_NODE *copy = NULL;

  assert (alter->node_type == PT_ALTER);
  assert (alter->info.alter.code == PT_ADD_ATTR_MTHD);

  /* Look for attributes that: have NOT NULL, do not have a DEFAULT and their type has a "hard" default. Also look for
   * attributes that are primary keys Throw an error for types that do not have a hard default (like objects). */
  for (attr = alter->info.alter.alter_clause.attr_mthd.attr_def_list; attr; attr = attr->next)
    {
      const bool is_not_null = (attr->info.attr_def.constrain_not_null != 0);
      const bool has_default = (attr->info.attr_def.data_default != NULL);
      const bool is_pri_key = is_attribute_primary_key (alter->info.alter.entity_name->info.name.original,
							attr->info.attr_def.attr_name->info.name.original);
      if (has_default)
	{
	  continue;
	}

      if (!is_not_null && !is_pri_key)
	{
	  continue;
	}

      if (!db_class_has_instance (class_mop))
	{
	  continue;
	}

      if (!prm_get_bool_value (PRM_ID_ADD_COLUMN_UPDATE_HARD_DEFAULT))
	{
	  ERROR1 (error, ER_SM_ATTR_NOT_NULL, attr->info.attr_def.attr_name->info.name.original);
	  goto end;
	}

      if (get_hard_default_for_type (attr->type_enum) == NULL)
	{
	  ERROR1 (error, ER_NOTNULL_ON_TYPE_WITHOUT_DEFAULT_VALUE, pt_show_type_enum (attr->type_enum));
	  goto end;
	}

      /* now we have an interesting node. Copy it in our list. */
      attr_count++;
      save = attr->next;
      attr->next = NULL;
      copy = parser_copy_tree (parser, attr);
      if (copy == NULL)
	{
	  attr->next = save;
	  ERROR0 (error, ER_OUT_OF_VIRTUAL_MEMORY);
	  parser_free_tree (parser, relevant_attrs);
	  goto end;
	}
      relevant_attrs = parser_append_node (copy, relevant_attrs);
      attr->next = save;
    }

  if (relevant_attrs == NULL)
    {
      /* no interesting attribute found, just leave */
      goto end;
    }

  /* RUN an UPDATE query comprising all the attributes */

  error = do_run_update_query_for_new_notnull_fields (parser, alter, relevant_attrs, attr_count, class_mop);
  if (error != NO_ERROR)
    {
      goto end;
    }


end:
  if (relevant_attrs != NULL)
    {
      parser_free_tree (parser, relevant_attrs);
    }

  return error;
}

/*
 * do_update_new_cols_with_default_expression() : Populates the newly added columns with default expression
 *
 * return: error code
 *
 *   parser(in): Parser context
 *   alter(in):  Parse tree of the statement
 *
 * Note: Used only on ALTER TABLE ... ADD COLUMN, and only AFTER the operation has been performed (i.e. the columns
 * have been added to the schema, even though the transaction has not been committed).
 *
 */
static int
do_update_new_cols_with_default_expression (PARSER_CONTEXT * parser, PT_NODE * alter, MOP class_mop)
{
  PT_NODE *relevant_attrs = NULL;
  int error = NO_ERROR;
  int attr_count = 0;
  PT_NODE *pt_data_default = NULL;
  PT_NODE *attr = NULL;
  PT_NODE *save = NULL;
  PT_NODE *copy = NULL;
  DB_DEFAULT_EXPR default_expr;

  assert (alter->node_type == PT_ALTER);
  assert (alter->info.alter.code == PT_ADD_ATTR_MTHD);

  /* Look for attributes that have a DEFAULT expression. */
  for (attr = alter->info.alter.alter_clause.attr_mthd.attr_def_list; attr; attr = attr->next)
    {
      pt_data_default = attr->info.attr_def.data_default;
      if (pt_data_default == NULL)
	{
	  /* don't have default clause */
	  continue;
	}

      pt_get_default_expression_from_data_default_node (parser, pt_data_default, &default_expr);
      if (default_expr.default_expr_type == DB_DEFAULT_NONE)
	{
	  /* don't have default expression */
	  continue;
	}

      if (!db_is_class (class_mop) || !db_class_has_instance (class_mop))
	{
	  continue;
	}

      /* now we have an interesting node. Copy it in our list. */
      attr_count++;
      save = attr->next;
      attr->next = NULL;
      copy = parser_copy_tree (parser, attr);
      if (copy == NULL)
	{
	  attr->next = save;
	  ERROR0 (error, ER_OUT_OF_VIRTUAL_MEMORY);
	  parser_free_tree (parser, relevant_attrs);
	  goto end;
	}
      relevant_attrs = parser_append_node (copy, relevant_attrs);
      attr->next = save;
    }

  if (relevant_attrs == NULL)
    {
      /* no interesting attribute found, just leave */
      goto end;
    }

  /* RUN an UPDATE query comprising all the attributes */
  error = do_run_update_query_for_new_default_expression_fields (parser, alter, relevant_attrs, attr_count, class_mop);
  if (error != NO_ERROR)
    {
      goto end;
    }

end:
  if (relevant_attrs != NULL)
    {
      parser_free_tree (parser, relevant_attrs);
    }

  return error;
}

/*
 * do_run_upgrade_instances_domain() - proxy function for server function
 *				       'xlocator_upgrade_instances_domain'
 *
 *  parser(in):
 *  p_class_oid(in): class OID
 *  att_id(in): constraint list
 */
static int
do_run_upgrade_instances_domain (PARSER_CONTEXT * parser, OID * p_class_oid, int att_id)
{
  int error = NO_ERROR;

  assert (parser != NULL);
  assert (p_class_oid != NULL);
  assert (att_id >= 0);

  error = locator_upgrade_instances_domain (p_class_oid, att_id);

  return error;

}

/*
 * do_drop_att_constraints() - drops constraints in list associated with a
 *			       class
 *  class_mop(in): class object
 *  constr_info_list(in): constraint list
 *
 * Note: Warning : Only non-unique, unique, and primary constraints are
 *	 handled;  FOREIGN KEY constraints are not supported
 */
static int
do_drop_att_constraints (MOP class_mop, SM_CONSTRAINT_INFO * constr_info_list)
{
  int error = NO_ERROR;

  SM_CONSTRAINT_INFO *constr;

  for (constr = constr_info_list; constr != NULL; constr = constr->next)
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY ((SM_CONSTRAINT_TYPE) constr->constraint_type))
	{
	  error =
	    sm_drop_constraint (class_mop, constr->constraint_type, constr->name, (const char **) constr->att_names, 0,
				false);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
      else if (constr->constraint_type == DB_CONSTRAINT_INDEX || constr->constraint_type == DB_CONSTRAINT_REVERSE_INDEX)
	{
	  error = sm_drop_index (class_mop, constr->name);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
    }
error_exit:
  return error;
}

/*
 * do_recreate_att_constraints() - (re-)creates constraints in list associated
 *				    with a class
 *  class_mop(in): class object
 *  constr_info_list(in): constraint list
 *
 * Note: Warning : Only non-unique, unique, and primary constraints are
 *	 handled;  FOREIGN KEY constraints are not supported
 */
static int
do_recreate_att_constraints (MOP class_mop, SM_CONSTRAINT_INFO * constr_info_list)
{
  int error = NO_ERROR;

  SM_CONSTRAINT_INFO *constr;

  for (constr = constr_info_list; constr != NULL; constr = constr->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY ((SM_CONSTRAINT_TYPE) constr->constraint_type))
	{
	  error =
	    sm_add_constraint (class_mop, constr->constraint_type, constr->name, (const char **) constr->att_names,
			       constr->asc_desc, constr->prefix_length, false, constr->filter_predicate,
			       constr->func_index_info, constr->comment, constr->index_status);

	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
    }

error_exit:
  return error;
}

/*
 * check_change_attribute() - Checks if an attribute change attribute is
 *			      possible, in the context of the requested
 *			      change mode
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   attribute(in/out): Attribute to add
 */
static int
check_change_attribute (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_NODE * attribute, PT_NODE * old_name_node,
			PT_NODE ** pointer_constraints, SM_ATTR_PROP_CHG * attr_chg_prop, SM_ATTR_CHG_SOL * change_mode)
{
  SM_NAME_SPACE name_space = ID_NULL;
  int meta = 0, shared = 0;
  int error = NO_ERROR;
  const char *old_name = NULL;
  const char *attr_name = NULL;
  bool new_attempt = true;
  DB_VALUE def_value;
  DB_VALUE *ptr_def = &def_value;
  PT_NODE *cnstr;
  PT_NODE *not_null_node = NULL, *pk_node = NULL, *previous_node = NULL, *tmp_node = NULL;
  PT_NODE *constraints = *pointer_constraints;

  assert (attr_chg_prop != NULL);
  assert (change_mode != NULL);

  assert (attribute->node_type == PT_ATTR_DEF);

  *change_mode = SM_ATTR_CHG_ONLY_SCHEMA;

  db_make_null (&def_value);

  attr_name = get_attr_name (attribute);

  meta = (attribute->info.attr_def.attr_type == PT_META_ATTR);
  shared = (attribute->info.attr_def.attr_type == PT_SHARED);
  name_space = (meta) ? ID_CLASS_ATTRIBUTE : ((shared) ? ID_SHARED_ATTRIBUTE : ID_ATTRIBUTE);
  attr_chg_prop->name_space = name_space;

  /* check if class has subclasses : 'users' of class may be subclass, but also partitions of class */
  if (ctemplate->current->users != NULL && ctemplate->partition == NULL)
    {
      attr_chg_prop->class_has_subclass = true;
    }

  error = check_default_on_update_clause (parser, attribute);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = get_att_default_from_def (parser, attribute, &ptr_def, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  /* ptr_def is either NULL or pointing to address of def_value */
  assert (ptr_def == NULL || ptr_def == &def_value);

  if (ptr_def && DB_IS_NULL (ptr_def)
      && attribute->info.attr_def.data_default->info.data_default.default_expr_type == DB_DEFAULT_NONE)
    {
      for (cnstr = constraints; cnstr != NULL; cnstr = cnstr->next)
	{
	  if (cnstr->info.constraint.type == PT_CONSTRAIN_NOT_NULL)
	    {
	      /* don't allow a default value of NULL for NOT NULL constrained columns */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CANNOT_HAVE_NOTNULL_DEFAULT_NULL, 1, attr_name);
	      error = ER_CANNOT_HAVE_NOTNULL_DEFAULT_NULL;
	      goto exit;
	    }
	  else if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
	    {
	      /* don't allow a default value of NULL in new PK constraint */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CANNOT_HAVE_PK_DEFAULT_NULL, 1, attr_name);
	      error = ER_CANNOT_HAVE_PK_DEFAULT_NULL;
	      goto exit;
	    }
	}
    }

  /* Try to find out the combined constraints definition of NOT_NULL and PK */
  for (cnstr = constraints; cnstr != NULL; tmp_node = cnstr, cnstr = cnstr->next)
    {
      if (cnstr->info.constraint.type == PT_CONSTRAIN_NOT_NULL)
	{
	  not_null_node = cnstr;
	  previous_node = tmp_node;
	}
      else if (cnstr->info.constraint.type == PT_CONSTRAIN_PRIMARY_KEY)
	{
	  pk_node = cnstr;
	}

      if (not_null_node != NULL && pk_node != NULL)
	{
	  break;
	}
    }

  /* Exclude/remove the duplicated NOT_NULL constraint which would be implicitly defined by PK. */
  if (not_null_node != NULL && pk_node != NULL)
    {
      if (previous_node == NULL)
	{
	  /* At the head of the list */
	  constraints = not_null_node->next;
	  *pointer_constraints = constraints;
	}
      else
	{
	  previous_node->next = not_null_node->next;
	}

      not_null_node->next = NULL;
      parser_free_node (parser, not_null_node);
    }

  error = build_attr_change_map (parser, ctemplate, attribute, old_name_node, constraints, attr_chg_prop);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (!is_att_property_structure_checked (attr_chg_prop))
    {
      assert (false);
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Not all properties are checked.");
      goto exit;
    }

  /* get new name */
  if (old_name_node != NULL)
    {
      assert (old_name_node->node_type == PT_NAME);
      old_name = old_name_node->info.name.original;
      assert (old_name != NULL);

      /* attr_name is supplied using the ATTR_DEF node and it means: for MODIFY syntax : current and unchanged name
       * (attr_name) for CHANGE syntax : new name of the attribute (new_name) */
      if (is_att_prop_set (attr_chg_prop->p[P_NAME], ATT_CHG_PROPERTY_DIFF))
	{
	  attr_name = old_name;
	}
      else
	{
	  attr_name = old_name;
	}
    }

  if (!is_att_change_needed (attr_chg_prop))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_ALTER_CHANGE_WARN_NO_CHANGE, 1, attr_name);
      error = NO_ERROR;
      /* just a warning : nothing to do */
      *change_mode = SM_ATTR_CHG_NOT_NEEDED;
      goto exit;
    }

  /* check if domain type is indexable : for constraints that may be acquired with ALTER.. CHANGE, check both if the
   * constraint is present in either old or new schema; if constraint cannot be acquired with CHANGE, check only if it
   * is present with old schema */
  /* TODO : this should be done at semantic check for all attribute definition nodes (including at table creation) */
  if (is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_NEW)
      || is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD)
      || is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_PK], ATT_CHG_PROPERTY_PRESENT_OLD)
      || is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_UNI], ATT_CHG_PROPERTY_PRESENT_NEW)
      || is_att_prop_set (attr_chg_prop->p[P_S_CONSTR_UNI], ATT_CHG_PROPERTY_PRESENT_OLD)
      || is_att_prop_set (attr_chg_prop->p[P_M_CONSTR_UNI], ATT_CHG_PROPERTY_PRESENT_OLD)
      || is_att_prop_set (attr_chg_prop->p[P_CONSTR_NON_UNI], ATT_CHG_PROPERTY_PRESENT_OLD))
    {
      if (!tp_valid_indextype (pt_type_enum_to_db (attribute->type_enum)))
	{
	  error = ER_SM_INVALID_INDEX_TYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, pt_type_enum_to_db_domain_name (attribute->type_enum));
	  goto exit;
	}
    }

  /* check if attribute change is allowed : */
  error = check_att_chg_allowed (attr_name, attribute->type_enum, attr_chg_prop, *change_mode, false, &new_attempt);
  if (error != NO_ERROR && new_attempt)
    {
      *change_mode = SM_ATTR_CHG_WITH_ROW_UPDATE;
      error = check_att_chg_allowed (attr_name, attribute->type_enum, attr_chg_prop, *change_mode, false, &new_attempt);
      if (error != NO_ERROR && new_attempt)
	{
	  *change_mode = SM_ATTR_CHG_BEST_EFFORT;
	  error =
	    check_att_chg_allowed (attr_name, attribute->type_enum, attr_chg_prop, *change_mode, true, &new_attempt);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	}
    }

exit:
  db_value_clear (&def_value);
  return error;
}

/*
 * check_change_class_collation() - Checks if it is necessary to update the
 *				    default collation of a class
 *   return: Error code
 *   parser(in): Parser context
 *   ctemplate(in/out): Class template
 *   alter(in): Node containing desired collation and codeset
 */
static int
check_change_class_collation (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate, PT_ALTER_INFO * alter, bool * need_update,
			      int *collation_id)
{
  int error = NO_ERROR;
  int cs = -1, coll_id = -1;

  assert (ctemplate->current != NULL);

  *need_update = false;
  cs = alter->alter_clause.collation.charset;
  coll_id = alter->alter_clause.collation.collation_id;

  /* check if codeset, collation or both are set, and check if it is necessary to update the default collation of the
   * class */
  if (coll_id != -1)
    {
      if (ctemplate->current->collation_id != coll_id)
	{
	  *need_update = true;
	}
      else
	{
	  *need_update = false;
	}
      *collation_id = coll_id;
    }
  else
    {
      LANG_COLLATION *lc;
      assert (cs != -1);

      lc = lang_get_collation (ctemplate->current->collation_id);
      assert (lc != NULL);

      if ((lc->codeset != cs) || (LANG_GET_BINARY_COLLATION (cs) != ctemplate->current->collation_id))
	{
	  *need_update = true;
	  *collation_id = LANG_GET_BINARY_COLLATION (cs);
	}
    }

  return error;
}

/*
 * sort_constr_info_list - sorts the list of constraints in the order:
 *			   - non-unique indexes
 *			   - unique indexes
 *			   - primary keys
 *			   - foreign key constraints
 *   return: none
 *   source(in/out): list to sort
 */

static int
sort_constr_info_list (SM_CONSTRAINT_INFO ** orig_list)
{
  int error = NO_ERROR;
  SM_CONSTRAINT_INFO *sorted, *next, *prev, *ins, *found, *constr;
  int constr_order[7] = { 0 };

  assert (orig_list != NULL);

  if (*orig_list == NULL)
    {
      return error;
    }

  /* TODO change this to compile-time asserts when we have such a mechanism. */
  assert (DB_CONSTRAINT_UNIQUE == 0);
  assert (DB_CONSTRAINT_FOREIGN_KEY == 6);

  constr_order[DB_CONSTRAINT_UNIQUE] = 2;
  constr_order[DB_CONSTRAINT_INDEX] = 0;
  constr_order[DB_CONSTRAINT_NOT_NULL] = 6;
  constr_order[DB_CONSTRAINT_REVERSE_UNIQUE] = 2;
  constr_order[DB_CONSTRAINT_REVERSE_INDEX] = 0;
  constr_order[DB_CONSTRAINT_PRIMARY_KEY] = 4;
  constr_order[DB_CONSTRAINT_FOREIGN_KEY] = 5;

  sorted = NULL;
  for (constr = *orig_list, next = NULL; constr != NULL; constr = next)
    {
      next = constr->next;

      for (ins = sorted, prev = NULL, found = NULL; ins != NULL && found == NULL; ins = ins->next)
	{
	  if (constr->constraint_type < 0 || constr->constraint_type > DB_CONSTRAINT_FOREIGN_KEY
	      || ins->constraint_type < 0 || ins->constraint_type > DB_CONSTRAINT_FOREIGN_KEY)
	    {
	      assert (false);
	      return ER_UNEXPECTED;
	    }

	  if (constr_order[constr->constraint_type] < constr_order[ins->constraint_type])
	    {
	      found = ins;
	    }
	  else
	    {
	      prev = ins;
	    }
	}

      constr->next = found;
      if (prev == NULL)
	{
	  sorted = constr;
	}
      else
	{
	  prev->next = constr;
	}
    }
  *orig_list = sorted;

  return error;
}

/*
 * save_constraint_info_from_pt_node() - Saves the information necessary to
 *	 create a constraint from a PT_CONSTRAINT_INFO node
 *
 *   return: NO_ERROR on success, non-zero for ERROR
 *   save_info(in/out): The information saved
 *   pt_constr(in): The constraint node to be saved
 *
 *  Note :this function handles only constraints for single
 *	  attributes : PT_CONSTRAIN_NOT_NULL, PT_CONSTRAIN_UNIQUE,
 *	  PT_CONSTRAIN_PRIMARY_KEY.
 *	  Foreign keys, indexes on multiple columns are not supported and also
 *	  'prefix_length' and ASC/DESC info is not supported.
 *	  It process only one node; the 'next' PT_NODE is ignored.
 */
static int
save_constraint_info_from_pt_node (SM_CONSTRAINT_INFO ** save_info, const PT_NODE * const pt_constr)
{
  int error_code = NO_ERROR;
  SM_CONSTRAINT_INFO *new_constraint = NULL;
  PT_NODE *constr_att_name = NULL;

  assert (pt_constr->node_type == PT_CONSTRAINT);

  new_constraint = (SM_CONSTRAINT_INFO *) calloc (1, sizeof (SM_CONSTRAINT_INFO));
  if (new_constraint == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, sizeof (SM_CONSTRAINT_INFO));
      goto error_exit;
    }

  /* set NULL, expect to generate constraint name */
  new_constraint->name = NULL;

  switch (pt_constr->info.constraint.type)
    {
    case PT_CONSTRAIN_PRIMARY_KEY:
      constr_att_name = pt_constr->info.constraint.un.primary_key.attrs;
      new_constraint->constraint_type = DB_CONSTRAINT_PRIMARY_KEY;
      break;
    case PT_CONSTRAIN_UNIQUE:
      constr_att_name = pt_constr->info.constraint.un.unique.attrs;
      new_constraint->constraint_type = DB_CONSTRAINT_UNIQUE;
      break;
    case PT_CONSTRAIN_NOT_NULL:
      constr_att_name = pt_constr->info.constraint.un.not_null.attr;
      new_constraint->constraint_type = DB_CONSTRAINT_NOT_NULL;
      break;
    default:
      assert (false);
    }

  if (constr_att_name->next != NULL)
    {
      error_code = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, "Constraint name should be the last attribute.");
      goto error_exit;
    }

  new_constraint->att_names = (char **) calloc (1 + 1, sizeof (char *));
  if (new_constraint->att_names == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, (1 + 1) * sizeof (char *));
      goto error_exit;
    }

  assert (constr_att_name->info.name.original != NULL);

  new_constraint->att_names[0] = strdup (constr_att_name->info.name.original);
  if (new_constraint->att_names[0] == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1,
	      (size_t) (strlen (constr_att_name->info.name.original) + 1));
      goto error_exit;
    }

  new_constraint->att_names[1] = NULL;

  assert (new_constraint->next == NULL);
  while ((*save_info) != NULL)
    {
      save_info = &((*save_info)->next);
    }
  *save_info = new_constraint;

  return error_code;

error_exit:
  if (new_constraint != NULL)
    {
      sm_free_constraint_info (&new_constraint);
    }
  return error_code;
}

/*
 * do_check_rows_for_null() - checks if a column has NULL values
 *   return: NO_ERROR or error code
 *
 *   class_mop(in): class to check
 *   att_name(in): name of column to check
 *   has_nulls(out): true if column has rows with NULL
 *
 */
int
do_check_rows_for_null (MOP class_mop, const char *att_name, bool * has_nulls)
{
  int error = NO_ERROR;
  int n = 0;
  int stmt_id = 0;
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result = NULL;
  const char *class_name = NULL;
  char query[2 * SM_MAX_IDENTIFIER_LENGTH + 50] = { 0 };
  DB_VALUE count;

  assert (class_mop != NULL);
  assert (att_name != NULL);
  assert (has_nulls != NULL);

  *has_nulls = false;
  db_make_null (&count);

  class_name = db_get_class_name (class_mop);
  if (class_name == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Cannot get class name of mop.");
      goto end;
    }

  n =
    snprintf (query, sizeof (query) / sizeof (char), "SELECT count(*) FROM [%s] WHERE [%s] IS NULL", class_name,
	      att_name);
  if (n < 0 || (n == sizeof (query) / sizeof (char)))
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Building SELECT statement failed.");
      goto end;
    }

  /* RUN the query */
  session = db_open_buffer (query);
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  if (db_get_errors (session) || db_statement_count (session) != 1)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  stmt_id = db_compile_statement (session);
  if (stmt_id != 1)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  error = db_execute_statement_local (session, stmt_id, &result);
  if (error < 0)
    {
      goto end;
    }

  if (result == NULL)
    {
      error = ER_UNEXPECTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, "Executing internal query failed.");
      goto end;
    }

  error = db_query_first_tuple (result);
  if (error != NO_ERROR)
    {
      goto end;
    }

  assert (result->query_type->db_type == DB_TYPE_INTEGER);

  error = db_query_set_copy_tplvalue (result, 0 /* peek */ );
  if (error != NO_ERROR)
    {
      goto end;
    }

  error = db_query_get_tuple_value (result, 0, &count);
  if (error != NO_ERROR)
    {
      goto end;
    }

  assert (!DB_IS_NULL (&count));
  assert (DB_VALUE_DOMAIN_TYPE (&count) == DB_TYPE_INTEGER);

  if (db_get_int (&count) > 0)
    {
      *has_nulls = true;
    }

end:
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session (session);
    }
  db_value_clear (&count);

  return error;
}

/*
 * do_run_update_query_for_class() - runs an UPDATE query.
 *   return: NO_ERROR or error code
 *
 *   query(in): query statement
 *   class_mop(in): class to check
 *   suppress_replication(in): force suppress replication
 *   row_count(out): count of updated rows
 *
 */
static int
do_run_update_query_for_class (char *query, MOP class_mop, int *row_count)
{
  int error = NO_ERROR;
  DB_SESSION *session = NULL;
  int stmt_id = 0;
  bool save_tr_state = tr_get_execution_state ();
  bool check_tr_state = false;

  assert (query != NULL);
  assert (class_mop != NULL);
  assert (row_count != NULL);

  *row_count = -1;

  lang_set_parser_use_client_charset (false);
  session = db_open_buffer (query);
  if (session == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  if (db_get_errors (session) || db_statement_count (session) != 1)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  stmt_id = db_compile_statement (session);
  if (stmt_id != 1)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  /*
   * We are going to perform an UPDATE on the table. We need to disable
   * the triggers because these are not UPDATES that the user required
   * explicitly.
   */

  check_tr_state = tr_set_execution_state (false);
  assert (check_tr_state == save_tr_state);

  error = db_execute_statement_local (session, stmt_id, NULL);
  if (error < 0)
    {
      goto end;
    }

  error = NO_ERROR;

  /* Invalidate the XASL cache by using the touch function */
  assert (class_mop);
  error = sm_touch_class (class_mop);
  if (error != NO_ERROR)
    {
      goto end;
    }

  *row_count = db_get_parser (session)->execution_values.row_count;

end:
  lang_set_parser_use_client_charset (true);

  if (session != NULL)
    {
      db_free_query (session);
      db_close_session (session);
    }

  tr_set_execution_state (save_tr_state);

  return error;
}

/*
 * pt_node_to_function_index () - extracts function index information
 *                                based on the given expression
 * parser(in): parser context
 * spec(in): class spec
 * expr(in): expression used with function index
 * return: pointer to SM_FUNCTION_INFO structure, containing the
 *	function index information
 */
static SM_FUNCTION_INFO *
pt_node_to_function_index (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * node, DO_INDEX do_index)
{
  SM_FUNCTION_INFO *func_index_info;
  FUNC_PRED *func_pred;
  PT_NODE *expr = NULL;
  char *expr_str = NULL;
  TP_DOMAIN *d = NULL;
  unsigned int save_custom;

  if (node->node_type == PT_SORT_SPEC)
    {
      expr = node->info.sort_spec.expr;
    }
  else
    {
      expr = node;
    }

  func_index_info = (SM_FUNCTION_INFO *) db_ws_alloc (sizeof (SM_FUNCTION_INFO));

  if (func_index_info == NULL)
    {
      return NULL;
    }
  memset (func_index_info, 0, sizeof (SM_FUNCTION_INFO));

  if (expr->data_type)
    {
      d = pt_data_type_to_db_domain (parser, expr->data_type, NULL);
    }
  else
    {
      d = pt_type_enum_to_db_domain (expr->type_enum);
    }

  if (node->node_type == PT_SORT_SPEC)
    {
      d->is_desc = (node->info.sort_spec.asc_or_desc == PT_ASC ? 0 : 1);
    }

  d = tp_domain_cache (d);
  func_index_info->fi_domain = d;

  assert (func_index_info->fi_domain != NULL);

  save_custom = parser->custom_print;
  parser->custom_print |= PT_CHARSET_COLLATE_FULL;
  expr_str = parser_print_tree_with_quotes (parser, expr);
  parser->custom_print = save_custom;
  assert (expr_str != NULL);

  func_index_info->expr_str = strdup (expr_str);
  if (func_index_info->expr_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) strlen (expr_str));
      goto error_exit;
    }

  func_index_info->expr_stream = NULL;
  func_index_info->expr_stream_size = -1;

  if (do_index == DO_INDEX_CREATE)
    {
      func_pred = pt_to_func_pred (parser, spec, expr);
      if (func_pred)
	{
	  xts_map_func_pred_to_stream (func_pred, &func_index_info->expr_stream, &func_index_info->expr_stream_size);
	}
      else
	{
	  goto error_exit;
	}
    }

  return func_index_info;

error_exit:
  if (func_index_info != NULL)
    {
      sm_free_function_index_info (func_index_info);
      db_ws_free (func_index_info);
    }
  return NULL;
}

/*
 * do_recreate_func_index_constr () - rebuilds the function index expression
 *   parser(in): parser context
 *   constr(in): constraint info, must be a function index
 *   func_index_info(in): function index info
 *   alter (in): information regarding changes made by an ALTER statement
 *   src_cls_name (in): current table name holding the constraint
 *   new_cls_name (in): new table name holding the constraint
 *			(when CREATE TABLE ... LIKE statement is used)
 *   return: NO_ERROR or error code
 *
 */
int
do_recreate_func_index_constr (PARSER_CONTEXT * parser, SM_CONSTRAINT_INFO * constr, SM_FUNCTION_INFO * func_index_info,
			       PT_NODE * alter, const char *src_cls_name, const char *new_cls_name)
{
  PT_NODE **stmt;
  PT_NODE *expr;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };
  SM_FUNCTION_INFO *fi_info_ws = NULL;
  int error = NO_ERROR;
  const char *class_name = NULL;
  char *query_str = NULL;
  size_t query_str_len = 0;
  int saved_func_index_pos = -1, saved_attr_index_start = -1;
  bool free_packing_buff = false;
  bool free_parser = false;
  SM_FUNCTION_INFO *fi_info = NULL;

  if (constr)
    {
      fi_info = constr->func_index_info;
    }
  else
    {
      fi_info = func_index_info;
    }

  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  error = ER_FAILED;
	  goto error;
	}
      free_parser = true;
    }
  if (alter && alter->node_type == PT_ALTER)
    {
      /* rebuilding the index due to ALTER CHANGE statement */
      if (alter->info.alter.entity_name)
	{
	  class_name = alter->info.alter.entity_name->info.name.original;
	}
    }
  else
    {
      /* rebuilding the index due to CREATE TABLE ... LIKE statement */
      if (src_cls_name)
	{
	  class_name = src_cls_name;
	}
    }
  if (class_name == NULL)
    {
      error = ER_FAILED;
      goto error;
    }

  query_str_len = strlen (fi_info->expr_str) + strlen (class_name) + 7 /* strlen("SELECT ") */  +
    6 /* strlen(" FROM ") */  +
    2 /* [] */  +
    1 /* terminating null */ ;
  query_str = (char *) malloc (query_str_len);
  if (query_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_str_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  snprintf (query_str, query_str_len, "SELECT %s FROM [%s]", fi_info->expr_str, class_name);
  stmt = parser_parse_string_use_sys_charset (parser, query_str);
  if (stmt == NULL || *stmt == NULL || pt_has_error (parser))
    {
      error = ER_FAILED;
      goto error;
    }
  expr = (*stmt)->info.query.q.select.list;

  if (alter)
    {
      (void) parser_walk_tree (parser, expr, replace_names_alter_chg_attr, alter, NULL, NULL);
    }
  else
    {
      PT_NODE *new_node = pt_name (parser, new_cls_name);
      PT_NODE *old_name = (*stmt)->info.query.q.select.from->info.spec.entity_name;
      if (!old_name)
	{
	  error = ER_FAILED;
	  goto error;
	}

      if (new_node)
	{
	  new_node->next = old_name->next;
	  old_name->next = NULL;
	  parser_free_tree (parser, old_name);
	  (*stmt)->info.query.q.select.from->info.spec.entity_name = new_node;
	}
      (void) parser_walk_tree (parser, expr, pt_replace_names_index_expr, (void *) new_cls_name, NULL, NULL);
    }

  *stmt = pt_resolve_names (parser, *stmt, &sc_info);
  if (*stmt != NULL && !pt_has_error (parser))
    {
      *stmt = pt_semantic_type (parser, *stmt, &sc_info);
    }
  else
    {
      error = ER_FAILED;
      goto error;
    }
  if (*stmt != NULL && !pt_has_error (parser))
    {
      expr = (*stmt)->info.query.q.select.list;
      if (expr && !pt_is_function_index_expr (parser, expr, true))
	{
	  error = ER_FAILED;
	  goto error;
	}
    }
  else
    {
      error = ER_FAILED;
      goto error;
    }

  saved_func_index_pos = fi_info->col_id;
  saved_attr_index_start = fi_info->attr_index_start;
  if (constr)
    {
      /* free previous function index info */
      sm_free_function_index_info (fi_info);
      free_and_init (fi_info);
    }
  else
    {
      sm_free_function_index_info (fi_info);
    }

  pt_enter_packing_buf ();
  free_packing_buff = true;
  fi_info_ws = pt_node_to_function_index (parser, (*stmt)->info.query.q.select.from, expr, DO_INDEX_CREATE);
  if (fi_info_ws == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (SM_FUNCTION_INFO));
      goto error;
    }

  if (constr)
    {
      /* original function index uses WS storage, switch to normal heap storage */
      fi_info = (SM_FUNCTION_INFO *) malloc (sizeof (SM_FUNCTION_INFO));
      if (fi_info == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, sizeof (SM_FUNCTION_INFO));
	  db_ws_free (fi_info_ws);
	  goto error;
	}
    }

  memcpy (fi_info, fi_info_ws, sizeof (SM_FUNCTION_INFO));
  db_ws_free (fi_info_ws);

  fi_info->col_id = saved_func_index_pos;
  fi_info->attr_index_start = saved_attr_index_start;
  if (constr)
    {
      /* restore original values */
      constr->func_index_info = fi_info;
    }

error:
  if (free_packing_buff)
    {
      pt_exit_packing_buf ();
    }

  if (free_parser)
    {
      parser_free_parser (parser);
    }
  if (query_str)
    {
      free_and_init (query_str);
    }
  return error;
}

/*
 * do_recreate_filter_index_constr () - rebuilds the filter index expression
 *   parser(in): parser context
 *   filter_index_info(in): filter index information
 *   alter (in): information regarding changes made by an ALTER statement
 *   src_cls_name (in): current table name holding the constraint
 *   new_cls_name (in): new table name holding the constraint
 *			(when CREATE TABLE ... LIKE statement is used)
 *   return: NO_ERROR or error code
 *
 */
int
do_recreate_filter_index_constr (PARSER_CONTEXT * parser, SM_PREDICATE_INFO * filter_index_info, PT_NODE * alter,
				 const char *src_cls_name, const char *new_cls_name)
{
  PT_NODE **stmt;
  PT_NODE *where_predicate;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };
  PARSER_VARCHAR *filter_expr = NULL;
  PRED_EXPR_WITH_CONTEXT *filter_predicate;
  int error;
  const char *class_name = NULL;
  char *query_str = NULL;
  size_t query_str_len = 0;
  char *pred_str = NULL;
  int pred_str_len = 0;
  bool free_packing_buff = false;
  SM_PREDICATE_INFO new_pred = { NULL, NULL, 0, NULL, 0 };
  bool free_parser = false;
  unsigned int save_custom;

  if (parser == NULL)
    {
      parser = parser_create_parser ();
      if (parser == NULL)
	{
	  error = ER_FAILED;
	  goto error;
	}
      free_parser = true;
    }
  if (alter && alter->node_type == PT_ALTER)
    {
      /* rebuilding the index due to ALTER CHANGE statement */
      if (alter->info.alter.entity_name)
	{
	  class_name = alter->info.alter.entity_name->info.name.original;
	}
    }
  else
    {
      /* rebuilding the index due to CREATE TABLE ... LIKE statement */
      if (src_cls_name)
	{
	  class_name = src_cls_name;
	}
    }
  if (class_name == NULL)
    {
      error = ER_FAILED;
      goto error;
    }

  query_str_len = strlen (filter_index_info->pred_string) + strlen (class_name) + 9 /* strlen("SELECT * ") */  +
    6 /* strlen(" FROM ") */  +
    2 /* [] */  +
    7 /* strlen(" WHERE " */  +
    1 /* terminating null */ ;
  query_str = (char *) malloc (query_str_len);
  if (query_str == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, query_str_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  snprintf (query_str, query_str_len, "SELECT * FROM [%s] WHERE %s", class_name, filter_index_info->pred_string);
  stmt = parser_parse_string_use_sys_charset (parser, query_str);
  if (stmt == NULL || *stmt == NULL || pt_has_error (parser))
    {
      error = ER_FAILED;
      goto error;
    }
  where_predicate = (*stmt)->info.query.q.select.where;

  if (alter)
    {
      (void) parser_walk_tree (parser, where_predicate, replace_names_alter_chg_attr, alter, NULL, NULL);
    }
  else
    {
      PT_NODE *new_node = pt_name (parser, new_cls_name);
      PT_NODE *old_name = (*stmt)->info.query.q.select.from->info.spec.entity_name;
      if (!old_name)
	{
	  error = ER_FAILED;
	  goto error;
	}

      if (new_node)
	{
	  new_node->next = old_name->next;
	  old_name->next = NULL;
	  parser_free_tree (parser, old_name);
	  (*stmt)->info.query.q.select.from->info.spec.entity_name = new_node;
	}
      (void) parser_walk_tree (parser, where_predicate, pt_replace_names_index_expr, (void *) new_cls_name, NULL, NULL);
    }

  *stmt = pt_resolve_names (parser, *stmt, &sc_info);
  if (*stmt != NULL && !pt_has_error (parser))
    {
      *stmt = pt_semantic_type (parser, *stmt, &sc_info);
    }
  else
    {
      error = ER_FAILED;
      goto error;
    }

  if (*stmt == NULL || pt_has_error (parser))
    {
      error = ER_FAILED;
      goto error;
    }

  /* make sure paren_type is 0 so parenthesis are not printed */
  where_predicate->info.expr.paren_type = 0;
  save_custom = parser->custom_print;
  parser->custom_print |= PT_CHARSET_COLLATE_FULL;
  filter_expr = pt_print_bytes (parser, where_predicate);
  parser->custom_print = save_custom;
  if (filter_expr)
    {
      pred_str = (char *) filter_expr->bytes;
      pred_str_len = strlen (pred_str);
      new_pred.pred_string = (char *) calloc (pred_str_len + 1, sizeof (char));
      if (new_pred.pred_string == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (pred_str_len + 1) * sizeof (char));
	  goto error;
	}
      memcpy (new_pred.pred_string, pred_str, pred_str_len);

      if (strlen (new_pred.pred_string) > MAX_FILTER_PREDICATE_STRING_LENGTH)
	{
	  PT_ERRORmf (parser, where_predicate, MSGCAT_SET_ERROR, -(ER_SM_INVALID_FILTER_PREDICATE_LENGTH),
		      MAX_FILTER_PREDICATE_STRING_LENGTH);
	  error = ER_FAILED;
	  goto error;
	}
    }

  pt_enter_packing_buf ();
  free_packing_buff = true;
  filter_predicate = pt_to_pred_with_context (parser, where_predicate, (*stmt)->info.query.q.select.from);
  if (filter_predicate)
    {
      error = xts_map_filter_pred_to_stream (filter_predicate, &new_pred.pred_stream, &new_pred.pred_stream_size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, where_predicate, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	  error = ER_FAILED;
	  goto error;
	}
    }
  else
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  if (filter_predicate->attrids_pred)
    {
      int i;
      assert (filter_predicate->num_attrs_pred > 0);
      new_pred.att_ids = (int *) calloc (filter_predicate->num_attrs_pred, sizeof (int));
      if (new_pred.att_ids == NULL)
	{
	  PT_ERRORm (parser, where_predicate, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	  error = ER_FAILED;
	  goto error;
	}
      for (i = 0; i < filter_predicate->num_attrs_pred; i++)
	{
	  new_pred.att_ids[i] = filter_predicate->attrids_pred[i];
	}
    }
  else
    {
      new_pred.att_ids = NULL;
    }
  new_pred.num_attrs = filter_predicate->num_attrs_pred;

  if (filter_index_info->pred_string)
    {
      free_and_init (filter_index_info->pred_string);
    }
  if (filter_index_info->pred_stream)
    {
      free_and_init (filter_index_info->pred_stream);
    }
  if (filter_index_info->att_ids)
    {
      free_and_init (filter_index_info->att_ids);
    }

  filter_index_info->pred_string = new_pred.pred_string;
  filter_index_info->pred_stream = new_pred.pred_stream;
  filter_index_info->pred_stream_size = new_pred.pred_stream_size;
  filter_index_info->att_ids = new_pred.att_ids;
  filter_index_info->num_attrs = new_pred.num_attrs;

  if (free_parser)
    {
      parser_free_parser (parser);
    }
  if (query_str)
    {
      free_and_init (query_str);
    }

  pt_exit_packing_buf ();
  return NO_ERROR;

error:
  if (free_parser)
    {
      parser_free_parser (parser);
    }
  if (query_str)
    {
      free_and_init (query_str);
    }
  if (new_pred.pred_string)
    {
      free_and_init (new_pred.pred_string);
    }
  if (new_pred.pred_stream)
    {
      free_and_init (new_pred.pred_stream);
    }
  if (new_pred.att_ids)
    {
      free_and_init (new_pred.att_ids);
    }
  if (free_packing_buff)
    {
      pt_exit_packing_buf ();
    }
  return error;
}

/*
 * replace_names_alter_chg_attr() - Replaces the attribute name in a given
 *				    expression, based on the changes imposed
 *				    the ALTER CHANGE statement
 *   return: PT_NODE pointer
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 *
 * Note:
 */
static PT_NODE *
replace_names_alter_chg_attr (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg, int *continue_walk)
{
  PT_NODE *alter = (PT_NODE *) void_arg;
  PT_NODE *old_name = NULL;
  const char *new_name = NULL;

  assert (alter->node_type == PT_ALTER);

  if (alter->info.alter.alter_clause.attr_mthd.attr_def_list)
    {
      new_name = get_attr_name (alter->info.alter.alter_clause.attr_mthd.attr_def_list);
    }
  old_name = alter->info.alter.alter_clause.attr_mthd.attr_old_name;
  if (old_name == NULL || new_name == NULL)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }
  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_DOT_)
    {
      if (PT_IS_NAME_NODE (node->info.dot.arg2))
	{
	  PT_NODE *new_node = NULL;
	  if (intl_identifier_casecmp (node->info.dot.arg2->info.name.original, old_name->info.name.original) == 0)
	    {
	      new_node = pt_name (parser, new_name);
	    }
	  else
	    {
	      new_node = pt_name (parser, node->info.dot.arg2->info.name.original);
	    }
	  if (new_node)
	    {
	      new_node->next = node->next;
	      node->next = NULL;
	      parser_free_tree (parser, node);
	      node = new_node;
	    }
	}
    }

  return node;
}

/*
 * pt_replace_names_index_expr () - Replaces the table name in a given
 *				  expression, based on the name required
 *				  when copying a function/filter index on
 *				  CREATE TABLE ... LIKE or when adding an
 *			function/filter index to partitions of a class.
 *
 *   return: PT_NODE pointer
 *   parser(in): Parser context
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in):
 *
 * Note:
 */
static PT_NODE *
pt_replace_names_index_expr (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg, int *continue_walk)
{
  const char *new_name = (char *) void_arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (node->node_type == PT_DOT_)
    {
      if (PT_IS_NAME_NODE (node->info.dot.arg1))
	{
	  PT_NODE *new_node = pt_name (parser, new_name);
	  PT_NODE *dot_arg = node->info.dot.arg1;
	  if (new_node)
	    {
	      new_node->next = dot_arg->next;
	      dot_arg->next = NULL;
	      parser_free_tree (parser, dot_arg);
	      node->info.dot.arg1 = new_node;
	    }
	}
    }

  return node;
}

/*
 * get_index_type_qualifiers() - get qualifiers of the index type that
 *                               matches the index name.
 * return: NO_ERROR or error code
 * obj(in): Memory Object Pointer
 * is_reverse(out): TRUE if the index type has the reverse feature.
 * is_unique(out): TRUE if the index type has the unique feature.
 * index_name(in): the name of index
 *
 * note:
 *    Only index types that satisfy the SM_IS_INDEX_FAMILY
 *    condition will be searched for.
 */

static int
get_index_type_qualifiers (MOP obj, bool * is_reverse, bool * is_unique, const char *index_name)
{
  int error_code = NO_ERROR;
  SM_CLASS_CONSTRAINT *sm_all_constraints = NULL;
  SM_CLASS_CONSTRAINT *sm_constraint = NULL;

  if (obj == NULL)
    {
      error_code = ER_FAILED;
      return error_code;
    }

  sm_all_constraints = sm_class_constraints (obj);
  sm_constraint = classobj_find_constraint_by_name (sm_all_constraints, index_name);
  if (sm_all_constraints == NULL || sm_constraint == NULL)
    {
      error_code = ER_SM_NO_INDEX;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, index_name);
      return error_code;
    }

  if (!SM_IS_INDEX_FAMILY (sm_constraint->type))
    {
      error_code = ER_SM_CONSTRAINT_HAS_DIFFERENT_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, index_name);
      return error_code;
    }

  switch (sm_constraint->type)
    {
    case SM_CONSTRAINT_INDEX:
      *is_reverse = false;
      *is_unique = false;
      break;
    case SM_CONSTRAINT_UNIQUE:
      *is_reverse = false;
      *is_unique = true;
      break;
    case SM_CONSTRAINT_REVERSE_INDEX:
      *is_reverse = true;
      *is_unique = false;
      break;
    case SM_CONSTRAINT_REVERSE_UNIQUE:
      *is_reverse = true;
      *is_unique = true;
      break;
    default:
      break;
    }

  return error_code;
}

/*
 * pt_node_to_partition_info() -
 *   return: Error code
 *   parser(in): Parser context
 *   node(in): Parser tree of an partition class
 *   entity_name(in):
 *   class_name(in): class name
 *   partition_name(in): partition name
 *   minval(in):
 *
 * Note:
 */
static SM_PARTITION *
pt_node_to_partition_info (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * entity_name, char *class_name,
			   char *partition_name, DB_VALUE * minval)
{
  DB_VALUE val, *ptval, *hashsize;
  PT_NODE *parts;
  char *query, *query_str = NULL, *p;
  DB_COLLECTION *dbc = NULL;
  size_t buf_size;
  SM_PARTITION *partition = NULL;

  partition = classobj_make_partition_info ();
  if (partition == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (SM_PARTITION));
      return NULL;
    }

  if (node->node_type == PT_PARTITION)
    {
      partition->pname = NULL;
    }
  else
    {
      partition->pname = ws_copy_string ((char *) node->info.parts.name->info.name.original);
      if (partition->pname == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  strlen ((char *) node->info.parts.name->info.name.original));
	  return NULL;
	}
    }

  if (node->node_type == PT_PARTITION)
    {
      partition->partition_type = node->info.partition.type;
    }
  else
    {
      partition->partition_type = node->info.parts.type;
    }

  if (node->node_type == PT_PARTITION)
    {
      unsigned int save_custom;

      save_custom = parser->custom_print;
      parser->custom_print |= PT_CHARSET_COLLATE_FULL;
      query = parser_print_tree_with_quotes (parser, node->info.partition.expr);
      parser->custom_print = save_custom;
      if (query == NULL)
	{
	  goto fail_return;
	}

      buf_size = strlen (query) + strlen (class_name) + 7 /* strlen("SELECT ") */  +
	6 /* strlen(" FROM ") */  +
	2 /* [] */  +
	1 /* terminating null */ ;
      if (buf_size > DB_MAX_PARTITION_EXPR_LENGTH)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PARTITION_EXPRESSION_TOO_LONG, DB_MAX_PARTITION_EXPR_LENGTH);
	  goto fail_return;
	}

      query_str = (char *) malloc (buf_size);
      if (query_str == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  goto fail_return;
	}
      sprintf (query_str, "SELECT %s FROM [%s]", query, class_name);
      partition->expr = ws_copy_string (query_str);
      if (partition->expr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (query_str));
	  goto fail_return;
	}
    }
  else
    {
      partition->expr = NULL;
    }
  if (query_str)
    {
      free_and_init (query_str);
    }

  dbc = set_create_sequence (0);
  if (dbc == NULL)
    {
      goto fail_return;
    }
  if (node->node_type == PT_PARTITION)
    {
      DB_VALUE expr;
      SM_FUNCTION_INFO *part_expr = NULL;

      p = (char *) node->info.partition.keycol->info.name.original;
      db_make_varchar (&val, PARTITION_VARCHAR_LEN, p, strlen (p), LANG_SYS_CODESET, LANG_SYS_COLLATION);
      set_add_element (dbc, &val);
      if (node->info.partition.type == PT_PARTITION_HASH)
	{
	  hashsize = pt_value_to_db (parser, node->info.partition.hashsize);
	  set_add_element (dbc, hashsize);
	}
      else
	{
	  set_add_element (dbc, minval);
	}

      /* Now compile the partition expression and add it to the end of the collection */
      part_expr = compile_partition_expression (parser, entity_name, node);
      if (part_expr == NULL)
	{
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, node);
	    }

	  goto fail_return;
	}
      db_make_char (&expr, part_expr->expr_stream_size, part_expr->expr_stream, part_expr->expr_stream_size,
		    LANG_SYS_CODESET, LANG_SYS_COLLATION);
      set_add_element (dbc, &expr);

      /* Notice that we're not calling pr_clear_value on expr here because memory allocated for expr_stream is
       * deallocated by 'sm_free_function_index_info' */
      sm_free_function_index_info (part_expr);
      db_ws_free (part_expr);
      part_expr = NULL;
    }
  else
    {
      if (node->info.parts.type == PT_PARTITION_RANGE)
	{
	  if (minval == NULL)
	    {
	      db_make_null (&val);
	      set_add_element (dbc, &val);
	    }
	  else
	    {
	      set_add_element (dbc, minval);
	    }
	}
      if (node->info.parts.values == NULL)
	{			/* RANGE-MAXVALUE */
	  db_make_null (&val);
	  set_add_element (dbc, &val);
	}
      else
	{
	  for (parts = node->info.parts.values; parts; parts = parts->next)
	    {
	      ptval = pt_value_to_db (parser, parts);
	      if (ptval == NULL)
		{
		  goto fail_return;
		}
	      set_add_element (dbc, ptval);
	    }
	}
    }

  partition->values = db_seq_copy (dbc);

  if (node->node_type != PT_PARTITION)
    {
      if (node->info.parts.comment != NULL)
	{
	  assert (node->info.parts.comment->node_type == PT_VALUE);
	  p = (char *) node->info.parts.comment->info.value.data_value.str->bytes;
	  partition->comment = ws_copy_string (p);
	  if (partition->comment == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (p));
	      goto fail_return;
	    }
	}
    }

  set_free (dbc);
  return partition;

fail_return:
  if (dbc != NULL)
    {
      set_free (dbc);
    }
  classobj_free_partition_info (partition);
  if (query_str != NULL)
    {
      free_and_init (query_str);
    }

  assert (er_errid () != NO_ERROR);
  return NULL;
}

/*
 * do_save_all_indexes () - Save all constraint info for a given class
 *   return: error code
 *   classmop(in):
 *   saved_index_info_listpp(out):
 *
 */
static int
do_save_all_indexes (MOP classmop, SM_CONSTRAINT_INFO ** saved_index_info_listpp)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *c = NULL;
  SM_CONSTRAINT_INFO *index_save_info = NULL, *saved = NULL;
  SM_CLASS *class_ = NULL;

  assert (classmop != NULL);
  assert (saved_index_info_listpp != NULL);

  *saved_index_info_listpp = NULL;

  error = au_fetch_class (classmop, &class_, AU_FETCH_READ, DB_AUTH_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (class_->constraints == NULL)
    {
      /* no constraints, nothing to do */
      return NO_ERROR;
    }

  if (class_->class_type != SM_CLASS_CT)
    {
      return NO_ERROR;
    }

  for (c = class_->constraints; c; c = c->next)
    {
      if ((DB_CONSTRAINT_TYPE) c->type == DB_CONSTRAINT_NOT_NULL)
	{
	  continue;
	}

      /* save constraints */
      error = sm_save_constraint_info (&index_save_info, c);
      if (error == NO_ERROR)
	{
	  assert (index_save_info != NULL);
	  saved = index_save_info;
	  while (saved->next)
	    {
	      saved = saved->next;
	    }
	}
      else
	{
	  return error;
	}
    }

  *saved_index_info_listpp = index_save_info;
  return NO_ERROR;
}

/*
 * do_drop_saved_indexes () - drop all indexes from a constraint info list
 *   return: error
 *   classmop (in)   :
 *   index_save_info :
 */
static int
do_drop_saved_indexes (MOP classmop, SM_CONSTRAINT_INFO * index_save_info)
{
  int error = NO_ERROR;
  SM_CONSTRAINT_INFO *saved = NULL;

  for (saved = index_save_info; saved != NULL; saved = saved->next)
    {
      if (SM_IS_CONSTRAINT_EXCEPT_INDEX_FAMILY ((SM_CONSTRAINT_TYPE) saved->constraint_type))
	{
	  error =
	    sm_drop_constraint (classmop, saved->constraint_type, saved->name, (const char **) saved->att_names, false,
				false);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      else
	{
	  error = sm_drop_index (classmop, saved->name);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * do_recreate_saved_indexes () - recreate all indexes from a constraint
 *				  info list
 *   return: error
 *   classmop (in)   :
 *   index_save_info :
 */
static int
do_recreate_saved_indexes (MOP classmop, SM_CONSTRAINT_INFO * index_save_info)
{
  int error = NO_ERROR;
  SM_CONSTRAINT_INFO *saved = NULL;

  for (saved = index_save_info; saved != NULL; saved = saved->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY ((SM_CONSTRAINT_TYPE) saved->constraint_type))
	{
	  error = sm_add_constraint (classmop, saved->constraint_type, saved->name, (const char **) saved->att_names,
				     saved->asc_desc, saved->prefix_length, false, saved->filter_predicate,
				     saved->func_index_info, saved->comment, saved->index_status);

	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return NO_ERROR;
}

int
do_alter_index_status (PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OBJECT *obj;
  PT_NODE *cls = NULL;
  SM_TEMPLATE *ctemplate = NULL;
  const char *class_name = NULL;
  const char *index_name = NULL;
  SM_INDEX_STATUS index_status;
  bool do_rollback = false;

  index_name = statement->info.index.index_name ? statement->info.index.index_name->info.name.original : NULL;

  if (index_name == NULL)
    {
      goto error_exit;
    }

  index_status = (SM_INDEX_STATUS) statement->info.index.index_status;

  cls = statement->info.index.indexed_class ? statement->info.index.indexed_class->info.spec.flat_entity_list : NULL;
  if (cls == NULL)
    {
      goto error_exit;
    }

  class_name = cls->info.name.resolved;
  obj = db_find_class (class_name);
  if (obj == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto error_exit;
    }

  error = tran_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  do_rollback = true;

  ctemplate = smt_edit_class_mop (obj, AU_INDEX);
  if (ctemplate == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto error_exit;
    }

  error = smt_change_constraint_status (ctemplate, index_name, index_status);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* classobj_free_template() is included in sm_update_class() */
  error = sm_update_class (ctemplate, NULL);
  if (error != NO_ERROR)
    {
      /* Even though sm_update() did not return NO_ERROR, ctemplate is already freed */
      ctemplate = NULL;
      goto error_exit;
    }

end:

  return error;

error_exit:
  if (ctemplate != NULL)
    {
      /* smt_quit() always returns NO_ERROR */
      smt_quit (ctemplate);
    }

  if (do_rollback == true)
    {
      if (do_rollback && error != ER_LK_UNILATERALLY_ABORTED)
	{
	  tran_abort_upto_system_savepoint (UNIQUE_SAVEPOINT_ALTER_INDEX);
	}
    }
  error = (error == NO_ERROR && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;

  goto end;
}

int
ib_get_thread_count ()
{
  return ib_thread_count;
}
