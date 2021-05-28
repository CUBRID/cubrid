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
#include "execute_statement.h"
#include "network_interface_cl.h"

#include <algorithm>

using namespace cubschema;

/*
 * sm_truncate_class_internal () - truncates classes
 *   return: NO_ERROR on success, non-zero for ERROR
 *   trun_claases (in): class mops to truncate collected by sm_collect_truncatable_classes()
 *
 *   NOTE: this function truncates several classes which are bond in a partitioning or some foreign keys.
 *   truncating a class consists of a few steps, each of which is done across all the classess one by one.
 *   this horizontal processing is necessary that all FKs has to be processed before PKs.
 */
int
sm_truncate_class_internal (std::unordered_set<OID>&& trun_classes)
{
  int error = NO_ERROR;
  std::vector<class_truncator> truncators;

  try
    {
      std::for_each (trun_classes.begin(), trun_classes.end(),
          [&truncators](const OID& oid) { truncators.emplace_back (oid); });
    }
  catch (int& error)
    {
      // exception from sm_truncator constructor
      return error;
    }

  /* Save constraints. Or, remove instances from the constraint if impossible. FK first, then non-FK */
  for (auto& truncator : truncators)
    {
      auto cons_predicate = [](const SM_CLASS_CONSTRAINT& cons) -> bool
        {
          return cons.type == SM_CONSTRAINT_FOREIGN_KEY;
        };

      error = truncator.save_constraints_or_clear (cons_predicate);
      if (error != NO_ERROR)
        {
          return error;
        }

      /* FK must be dropped earlier than PK, because of referencing */
      auto saved_cons_predicate = [](const SM_CONSTRAINT_INFO& cons_info) -> bool
        {
          return cons_info.constraint_type == DB_CONSTRAINT_FOREIGN_KEY;
        };

      error = truncator.drop_saved_constraints (saved_cons_predicate);
      if (error != NO_ERROR)
        {
          return error;
        }
    }

  for (auto& truncator : truncators)
    {
      auto cons_predicate = [](const SM_CLASS_CONSTRAINT& cons) -> bool
        {
          if (!SM_IS_CONSTRAINT_INDEX_FAMILY (cons.type))
            {
              assert (cons.type == SM_CONSTRAINT_NOT_NULL);
              return false;
            }
          else if (cons.type == SM_CONSTRAINT_FOREIGN_KEY)
            {
              return false;
            }
          else
            {
              return true;
            }
        };

      error = truncator.save_constraints_or_clear (cons_predicate);
      if (error != NO_ERROR)
      {
        return error;
      }

      auto saved_cons_predicate = [](const SM_CONSTRAINT_INFO& cons_info) -> bool
        {
          return cons_info.constraint_type != DB_CONSTRAINT_FOREIGN_KEY;
        };

      error = truncator.drop_saved_constraints (saved_cons_predicate);
      if (error != NO_ERROR)
        {
          return error;
        }
    }

  /* Destroy heap file. Or, delete instances from the heap if impossible */
  for (auto& truncator : truncators)
    {
      error = truncator.truncate_heap();
      if (error != NO_ERROR)
        {
          return error;
        }
    }

  /* Restore constraints. non-FK first, and then FK */
  for (auto& truncator : truncators)
    {
      auto saved_cons_predicate = [](const SM_CONSTRAINT_INFO& cons_info) -> bool
        {
          return cons_info.constraint_type != DB_CONSTRAINT_FOREIGN_KEY;
        };

      error = truncator.restore_constraints (saved_cons_predicate);
      if (error != NO_ERROR)
        {
          return error;
        }
    }

  for (auto& truncator : truncators)
    {
      auto saved_cons_predicate = [](const SM_CONSTRAINT_INFO& cons_info) -> bool
        {
          return cons_info.constraint_type == DB_CONSTRAINT_FOREIGN_KEY;
        };
      error = truncator.restore_constraints (saved_cons_predicate);
      if (error != NO_ERROR)
        {
          return error;
        }
    }

  /* reset auto_increment starting value */
  for (auto& truncator : truncators)
    {
      error = truncator.reset_serials ();
      if (error != NO_ERROR)
        {
          return error;
        }
    }

  return error;
}

namespace cubschema 
{
  class_truncator::class_truncator (const OID& class_oid)
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
  }

  class_truncator::class_truncator (class_truncator&& other) :
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

  class_truncator::~class_truncator ()
  {
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
  class_truncator::save_constraints_or_clear (cons_predicate pred)
  {
    int error = NO_ERROR;
    SM_CLASS_CONSTRAINT *c = NULL;

    for (c = m_class->constraints; c; c = c->next)
      {
	if (!pred (*c))
	  {
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
  class_truncator::drop_saved_constraints (saved_cons_predicate pred)
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
	    if (!pred (*saved))
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
	if (!pred (*saved))
	  {
	    continue;
	  }

	error =
	  sm_drop_constraint (m_mop, saved->constraint_type, saved->name, (const char **) saved->att_names, 0,
			      false);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    for (saved = m_index_info; saved != NULL; saved = saved->next)
      {
	if (!pred (*saved))
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
  class_truncator::truncate_heap ()
  {
    int error = NO_ERROR;
    if (sm_is_reuse_oid_class (m_mop))
      {
	error = sm_truncate_using_destroy_heap (m_mop);
      }
    else
      {
	error = sm_truncate_using_delete (m_mop);
      }

    return error;
  }

  /*
   * restore_constraints () - restore constraints saved in save_constraints_or_clear().
   *
   *   return: error code or NO_ERROR
   *   pred(in): only constraints which meet this condition are processed
   */
  int
  class_truncator::restore_constraints (saved_cons_predicate pred)
  {
    DB_CTMPL *ctmpl = NULL;
    SM_CONSTRAINT_INFO *saved = NULL;
    int error = NO_ERROR;

    /* Normal index must be created earlier than unique constraint or FK, because of shared btree case. */
    for (saved = m_index_info; saved != NULL && pred (*saved); saved = saved->next)
      {
	error = sm_add_constraint (m_mop, saved->constraint_type, saved->name, (const char **) saved->att_names,
				   saved->asc_desc, saved->prefix_length, false, saved->filter_predicate,
				   saved->func_index_info, saved->comment, saved->index_status);
	if (error != NO_ERROR)
	  {
	    return error;
	  }
      }

    /* Even for a class, PK must be created earlier than FK, because of the self-referencing case */
    for (saved = m_unique_info; saved != NULL && pred (*saved); saved = saved->next)
      {
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

    for (saved = m_fk_info; saved != NULL && pred (*saved); saved = saved->next)
      {
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
  class_truncator::reset_serials ()
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
