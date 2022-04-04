/*
 *
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

#include "schema_class_truncator.hpp"

#include "authenticate.h"
#include "dbi.h"
#include "db.h"
#include "dbtype_function.h"
#include "execute_statement.h"
#include "network_interface_cl.h"

#include <algorithm>

using namespace cubschema;

namespace cubschema
{
  /*
   * This class is used internally by class_truncator while truncating, which could end up truncating several classes.
   * This context is in charge of truncating each of truncating classes and saving the context of each.
   */
  class class_truncate_context final
  {
    public:
      using cons_predicate = std::function<bool (const SM_CLASS_CONSTRAINT &)>;
      using saved_cons_predicate = std::function<bool (const SM_CONSTRAINT_INFO &)>;

      int save_constraints_or_clear (cons_predicate pred = nullptr);
      int drop_saved_constraints (saved_cons_predicate pred = nullptr);
      int truncate_heap ();
      int restore_constraints (saved_cons_predicate pred = nullptr);
      int reset_serials ();

      class_truncate_context (const OID &class_oid);
      class_truncate_context (class_truncate_context &&other);
      ~class_truncate_context ();

      class_truncate_context (const class_truncate_context &other) = delete;
      class_truncate_context &operator= (const class_truncate_context &other) = delete;
      class_truncate_context &operator= (const class_truncate_context &&other) = delete;

    private:
      MOP m_mop = NULL;
      SM_CLASS *m_class = NULL;
      SM_CONSTRAINT_INFO *m_unique_info = NULL;
      SM_CONSTRAINT_INFO *m_fk_info = NULL;
      SM_CONSTRAINT_INFO *m_index_info = NULL;
  };

  /*
   * collect_trun_classes () - Collects OIDs of truncatable classes regarding the CASCADE option
   *   return: NO_ERROR on success, non-zero for ERROR
   *   class_mop(in):
   *   is_cascade(in): whether to cascade TRUNCATE to FK-referring classes
   */
  int
  class_truncator::collect_trun_classes (MOP class_mop, bool is_cascade)
  {
    int error = NO_ERROR;
    SM_CLASS *class_ = NULL;
    SM_CLASS_CONSTRAINT *pk_constraint = NULL;
    SM_FOREIGN_KEY_INFO *fk_ref;
    OID *fk_cls_oid;

    error = au_fetch_class (class_mop, &class_, AU_FETCH_READ, DB_AUTH_ALTER);
    if (error != NO_ERROR || class_ == NULL)
      {
	assert (er_errid () != NO_ERROR);
	return er_errid ();
      }

    m_trun_classes.emplace (*ws_oid (class_mop));

    pk_constraint = classobj_find_cons_primary_key (class_->constraints);
    if (pk_constraint == NULL || classobj_is_pk_referred (class_mop, pk_constraint->fk_info, false, NULL) == false)
      {
	/* if no PK or FK-referred, it can be truncated */
	return NO_ERROR;
      }

    /* Now, there is a PK, and are some FKs-referring to the PK */

    if (!is_cascade)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TRUNCATE_PK_REFERRED, 1, pk_constraint->fk_info->name);
	return ER_TRUNCATE_PK_REFERRED;
      }

    /* Find FK-child classes to cascade. */
    for (fk_ref = pk_constraint->fk_info; fk_ref; fk_ref = fk_ref->next)
      {
	if (fk_ref->delete_action == SM_FOREIGN_KEY_CASCADE)
	  {
	    if (m_trun_classes.find (fk_ref->self_oid) != m_trun_classes.end ())
	      {
		continue;		/* already checked */
	      }

	    MOP fk_child_mop = ws_mop (&fk_ref->self_oid, NULL);
	    if (fk_child_mop == NULL)
	      {
		assert (er_errid () != NO_ERROR);
		return er_errid ();
	      }

	    int partition_type = DB_NOT_PARTITIONED_CLASS;
	    error = sm_partitioned_class_type (fk_child_mop, &partition_type, NULL, NULL);
	    if (error != NO_ERROR)
	      {
		return error;
	      }

	    if (partition_type == DB_PARTITION_CLASS)
	      {
		/*
		 * FKs of all partition classes refers to a class that the parittioned class of them referes to.
		 * But, partition class will be processed when the partitioned class is done.
		 */
		continue;
	      }

	    error = collect_trun_classes (fk_child_mop, is_cascade);
	    if (error != NO_ERROR)
	      {
		return error;
	      }
	  }
	else
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TRUNCATE_CANT_CASCADE, 1, fk_ref->name);
	    return ER_TRUNCATE_CANT_CASCADE;
	  }
      }

    return NO_ERROR;
  }

  /*
   * truncate () - truncates classes colleted through collect_trun_classes()
   *   return: NO_ERROR on success, non-zero for ERROR
   *
   *   NOTE: this function truncates several classes which are bond in a partitioning or some foreign keys.
   *   truncating a class consists of a few steps, each of which is done one by one across all the classess.
   *   this horizontal processing is necessary that all FKs has to be processed before PKs.
   */
  int
  class_truncator::truncate (const bool is_cascade)
  {
    int error = NO_ERROR;
    std::vector<class_truncate_context> contexts;

    assert (m_class_mop != NULL);

    error = collect_trun_classes (m_class_mop, is_cascade);
    if (error != NO_ERROR)
      {
	return error;
      }

    try
      {
	std::for_each (m_trun_classes.begin(), m_trun_classes.end(), [&contexts] (const OID& oid)
	{
	  contexts.emplace_back (oid);
	});
      }
    catch (int &error)
      {
	// exception from sm_context constructor
	return error;
      }

    /* Save constraints. Or, remove instances from the constraint if impossible. FK first, then non-FK */
    for (auto &context : contexts)
      {
	auto cons_predicate = [] (const SM_CLASS_CONSTRAINT& cons) -> bool
	{
	  return cons.type == SM_CONSTRAINT_FOREIGN_KEY;
	};

	error = context.save_constraints_or_clear (cons_predicate);
	if (error != NO_ERROR)
	  {
	    return error;
	  }

	/* FK must be dropped earlier than PK, because of referencing */
	error = context.drop_saved_constraints ();
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    for (auto &context : contexts)
      {
	auto cons_predicate = [] (const SM_CLASS_CONSTRAINT& cons) -> bool
	{
	  return cons.type != SM_CONSTRAINT_FOREIGN_KEY;
	};

	error = context.save_constraints_or_clear (cons_predicate);
	if (error != NO_ERROR)
	  {
	    return error;
	  }

	auto saved_cons_predicate = [] (const SM_CONSTRAINT_INFO& cons_info) -> bool
	{
	  return cons_info.constraint_type != DB_CONSTRAINT_FOREIGN_KEY;
	};

	error = context.drop_saved_constraints (saved_cons_predicate);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* Destroy heap file. Or, delete instances from the heap if impossible */
    for (auto &context : contexts)
      {
	error = context.truncate_heap();
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* Restore constraints. non-FK first, and then FK */
    for (auto &context : contexts)
      {
	auto saved_cons_predicate = [] (const SM_CONSTRAINT_INFO& cons_info) -> bool
	{
	  return cons_info.constraint_type != DB_CONSTRAINT_FOREIGN_KEY;
	};

	error = context.restore_constraints (saved_cons_predicate);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    for (auto &context : contexts)
      {
	auto saved_cons_predicate = [] (const SM_CONSTRAINT_INFO& cons_info) -> bool
	{
	  return cons_info.constraint_type == DB_CONSTRAINT_FOREIGN_KEY;
	};
	error = context.restore_constraints (saved_cons_predicate);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* reset auto_increment starting value */
    for (auto &context : contexts)
      {
	error = context.reset_serials ();
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    return error;
  }

  class_truncate_context::class_truncate_context (const OID &class_oid)
  {
    int error = NO_ERROR;
    m_mop = ws_mop (&class_oid, NULL);
    if (m_mop == NULL)
      {
	assert (er_errid () != NO_ERROR);
	throw er_errid ();
      }

    /* We need to flush everything so that the server logs the inserts that happened before the truncate. We need this in
     * order to make sure that a rollback takes us into a consistent state. If we can prove that simply discarding the
     * objects would work correctly we would be able to remove this call. However, it's better to be safe than sorry. */
    error = sm_flush_and_decache_objects (m_mop, true);
    if (error != NO_ERROR)
      {
	throw error;
      }

    error = au_fetch_class (m_mop, &m_class, AU_FETCH_WRITE, DB_AUTH_ALTER);
    if (error != NO_ERROR || m_class == NULL)
      {
	assert (er_errid () != NO_ERROR);
	throw er_errid ();
      }

    m_class->load_index_from_heap = 0;
  }

  class_truncate_context::class_truncate_context (class_truncate_context &&other) :
    m_mop (other.m_mop),
    m_class (other.m_class),
    m_unique_info (other.m_unique_info),
    m_fk_info (other.m_fk_info),
    m_index_info (other.m_index_info)
  {
    other.m_mop = NULL;
    other.m_class = NULL;
    other.m_unique_info = NULL;
    other.m_fk_info = NULL;
    other.m_index_info = NULL;
  }

  class_truncate_context::~class_truncate_context ()
  {
    if (m_class != NULL)
      {
	m_class->load_index_from_heap = 1;
      }

    if (m_unique_info != NULL)
      {
	sm_free_constraint_info (&m_unique_info);
      }
    if (m_fk_info != NULL)
      {
	sm_free_constraint_info (&m_fk_info);
      }
    if (m_index_info != NULL)
      {
	sm_free_constraint_info (&m_index_info);
      }
  }

  /*
   * save_constraints_or_clear () - save constraints of the the class,
   *                                or remove instance manually if it is impossible.
   *
   *   return: error code or NO_ERROR
   *   pred(in): only constraints which meet this condition are processed
   */
  int
  class_truncate_context::save_constraints_or_clear (cons_predicate pred)
  {
    int error = NO_ERROR;
    SM_CLASS_CONSTRAINT *c = NULL;

    for (c = m_class->constraints; c; c = c->next)
      {
	if (pred && !pred (*c))
	  {
	    continue;
	  }

	if (!SM_IS_CONSTRAINT_INDEX_FAMILY (c->type))
	  {
	    assert (c->type == SM_CONSTRAINT_NOT_NULL);
	    continue;
	  }

	if ((c->type == SM_CONSTRAINT_PRIMARY_KEY && classobj_is_pk_referred (m_mop, c->fk_info, false, NULL))
	    || !sm_is_possible_to_recreate_constraint (m_mop, m_class, c))
	  {
	    /*
	     * In these cases, We cannot drop and recreate the index as this might be
	     * too costly, so we just remove the instances of the current class.
	     */
	    error = locator_remove_class_from_index (ws_oid (m_mop), &c->index_btid, &m_class->header.ch_heap);
	    if (error != NO_ERROR)
	      {
		return error;
	      }
	  }
	else
	  {
	    /* All the OIDs in the index should belong to the current class, so it is safe to drop and create the
	     * constraint again. We save the information required to recreate the constraint. */

	    if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (c->type))
	      {
		if (sm_save_constraint_info (&m_unique_info, c) != NO_ERROR)
		  {
		    return error;
		  }
	      }
	    else if (c->type == SM_CONSTRAINT_FOREIGN_KEY)
	      {
		if (sm_save_constraint_info (&m_fk_info, c) != NO_ERROR)
		  {
		    return error;
		  }
	      }
	    else
	      {
		if (sm_save_constraint_info (&m_index_info, c) != NO_ERROR)
		  {
		    return error;
		  }
	      }
	  }
      }

    return error;
  }

  /*
   * drop_saved_constraints () - drop constraints saved in save_constraints_or_clear().
   *
   *   return: error code or NO_ERROR
   *   pred(in): only constraints which meet this condition are processed
   */
  int
  class_truncate_context::drop_saved_constraints (saved_cons_predicate pred)
  {
    DB_CTMPL *ctmpl = NULL;
    SM_CONSTRAINT_INFO *saved = NULL;
    int error = NO_ERROR;

    /* Even for a class, FK must be dropped earlier than PK, because of the self-referencing case */
    if (m_fk_info != NULL)
      {
	ctmpl = dbt_edit_class (m_mop);
	if (ctmpl == NULL)
	  {
	    assert (er_errid () != NO_ERROR);
	    error = er_errid ();
	    return error;
	  }

	for (saved = m_fk_info; saved != NULL; saved = saved->next)
	  {
	    if (pred && !pred (*saved))
	      {
		continue;
	      }

	    error =
		    dbt_drop_constraint (ctmpl, saved->constraint_type, saved->name, (const char **) saved->att_names, 0);
	    if (error != NO_ERROR)
	      {
		dbt_abort_class (ctmpl);
		return error;
	      }
	  }

	if (dbt_finish_class (ctmpl) == NULL)
	  {
	    dbt_abort_class (ctmpl);
	    assert (er_errid () != NO_ERROR);
	    error = er_errid ();
	    return error;
	  }
      }

    for (saved = m_unique_info; saved != NULL; saved = saved->next)
      {
	if (pred && !pred (*saved))
	  {
	    continue;
	  }

	error = sm_drop_constraint (m_mop, saved->constraint_type,
				    saved->name, (const char **) saved->att_names, 0, false);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    for (saved = m_index_info; saved != NULL; saved = saved->next)
      {
	if (pred && !pred (*saved))
	  {
	    continue;
	  }

	error = sm_drop_index (m_mop, saved->name);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    return error;
  }

  /*
   * truncate_heap () - truncate the heap of the class.
   *
   *   return: error code or NO_ERROR
   */
  int
  class_truncate_context::truncate_heap ()
  {
    /*
     * case 1: REUSE_OID
     * case 2: DONT_REUSE_OID with no domain referring to this class
     * case 3: DONT_REUSE_OID with some domains referring to this class
     *
     * case 1 and 2: destory heap, case 3: use DELETE query to delete all records.
     */
    if (!sm_is_reuse_oid_class (m_mop))
      {
	/*
	 * This is the case 3.
	 * Check if (1) there is a doamin referring to this class or (2) there is a general object domain in any user table (non system class).
	 * We check it using SELECT to _db_domain.
	 */
	int error = NO_ERROR;
	DB_SESSION *session = NULL;
	DB_QUERY_RESULT *result = NULL;
	STATEMENT_ID stmt_id;
	DB_VALUE value;
	char select_query[DB_MAX_IDENTIFIER_LENGTH + 256] = { 0 };
	constexpr int CNT_CATCLS_OBJECTS = 5;
	DB_BIGINT cnt_refers = CNT_CATCLS_OBJECTS + 1;
	int au_save;

	const char *class_name = db_get_class_name (m_mop);
	if (class_name == NULL)
	  {
	    return ER_FAILED;
	  }

	/*
	 * !!CAUTION!!
	 * If [data_type] is DB_TYPE_OBJECT and [class_of] is NULL, it is a general object domain, but we have to check only user classes.
	 * To do this, we use an walkaround in which we count the number of general object domains in existing system catalogs
	 * and if the SELECT result is over this, we asuume that there are some general object domain in some user class.
	 *
	 * The number is now 5 and hard-coded, so we MUST consider it when add or remove a general object domain in a system class.
	 * If it is changed, we MUST also change the value of CNT_CATCLS_OBJECTS.
	 *
	 * We add a QA test case to confirm there are only 5 general object domains in system classes, which will help notice this constraint
	 * and this test case also has to be changed along if CNT_CATCLS_OBJECTS is changed.
	 *
	 * See CBRD-23983 for the details.
	 */

	AU_DISABLE (au_save);

	(void) snprintf (select_query, sizeof (select_query),
			 "SELECT COUNT(*) FROM [_db_domain] WHERE [data_type]=%d AND ([class_of].[class_name]='%s' OR [class_of] IS NULL) AND ROWNUM <= %d",
			 DB_TYPE_OBJECT, class_name, CNT_CATCLS_OBJECTS + 1);

	session = db_open_buffer (select_query);
	if (session == NULL)
	  {
	    assert (er_errid () != NO_ERROR);
	    error = er_errid ();
	    AU_ENABLE (au_save);
	    return error;
	  }

	stmt_id = db_compile_statement (session);
	if (stmt_id != 1)
	  {
	    assert (er_errid () != NO_ERROR);
	    error = er_errid ();
	    db_close_session (session);
	    AU_ENABLE (au_save);
	    return error;
	  }

	error = db_execute_statement_local (session, stmt_id, &result);
	if (error < 0)
	  {
	    db_close_session (session);
	    AU_ENABLE (au_save);
	    return error;
	  }

	error = db_query_first_tuple (result);
	if (error < 0)
	  {
	    db_query_end (result);
	    db_close_session (session);
	    AU_ENABLE (au_save);
	    return error;
	  }

	error = db_query_get_tuple_value (result, 0, &value);
	if (error != NO_ERROR)
	  {
	    db_query_end (result);
	    db_close_session (session);
	    AU_ENABLE (au_save);
	    return error;
	  }

	cnt_refers = db_get_bigint (&value);

	db_query_end (result);
	db_close_session (session);

	AU_ENABLE (au_save);

	if (cnt_refers > CNT_CATCLS_OBJECTS)
	  {
	    return sm_truncate_using_delete (m_mop);
	  }
      }

    /* Now, the heap is REUSE_OID, or DONT_REUSE_OID with no domain referring to this class. (case 1, 2) */
    return sm_truncate_using_destroy_heap (m_mop);
  }

  /*
   * restore_constraints () - restore constraints saved in save_constraints_or_clear().
   *
   *   return: error code or NO_ERROR
   *   pred(in): only constraints which meet this condition are processed
   */
  int
  class_truncate_context::restore_constraints (saved_cons_predicate pred)
  {
    DB_CTMPL *ctmpl = NULL;
    SM_CONSTRAINT_INFO *saved = NULL;
    int error = NO_ERROR;

    /* Normal index must be created earlier than unique constraint or FK, because of shared btree case. */
    for (saved = m_index_info; saved != NULL; saved = saved->next)
      {
	if (pred && !pred (*saved))
	  {
	    continue;
	  }

	error = sm_add_constraint (m_mop, saved->constraint_type, saved->name, (const char **) saved->att_names,
				   saved->asc_desc, saved->prefix_length, false, saved->filter_predicate,
				   saved->func_index_info, saved->comment, saved->index_status);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* Even for a class, PK must be created earlier than FK, because of the self-referencing case */
    for (saved = m_unique_info; saved != NULL; saved = saved->next)
      {
	if (pred && !pred (*saved))
	  {
	    continue;
	  }

	error = sm_add_constraint (m_mop, saved->constraint_type, saved->name, (const char **) saved->att_names,
				   saved->asc_desc, saved->prefix_length, false, saved->filter_predicate,
				   saved->func_index_info, saved->comment, saved->index_status);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* To drop all xasl cache related class, we need to touch class. */
    ctmpl = dbt_edit_class (m_mop);
    if (ctmpl == NULL)
      {
	assert (er_errid () != NO_ERROR);
	error = er_errid ();
	return error;
      }

    for (saved = m_fk_info; saved != NULL; saved = saved->next)
      {
	if (pred && !pred (*saved))
	  {
	    continue;
	  }

	error = dbt_add_foreign_key (ctmpl, saved->name, (const char **) saved->att_names, saved->ref_cls_name,
				     (const char **) saved->ref_attrs, saved->fk_delete_action,
				     saved->fk_update_action, saved->comment);

	if (error != NO_ERROR)
	  {
	    dbt_abort_class (ctmpl);
	    return error;
	  }
      }

    if (dbt_finish_class (ctmpl) == NULL)
      {
	dbt_abort_class (ctmpl);
	assert (er_errid () != NO_ERROR);
	error = er_errid ();
	return error;
      }

    return error;
  }

  /*
   * reset_serials () - reset serials used by the class.
   *
   *   return: error code or NO_ERROR
   */
  int
  class_truncate_context::reset_serials ()
  {
    int au_save = 0;
    SM_ATTRIBUTE *att = NULL;
    int error = NO_ERROR;

    for (att = db_get_attributes (m_mop); att != NULL; att = db_attribute_next (att))
      {
	if (att->auto_increment != NULL)
	  {
	    AU_DISABLE (au_save);
	    error = do_reset_auto_increment_serial (att->auto_increment);
	    AU_ENABLE (au_save);

	    if (error != NO_ERROR)
	      {
		return error;
	      }
	  }
      }

    return error;
  }
}
