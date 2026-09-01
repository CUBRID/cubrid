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

//
// Transient row lock bookkeeping for the delete/update force phase
//

#include "query_transient_row_lock.hpp"

#include "heap_file.h"
#include "lock_manager.h"
#include "memory_alloc.h"
#include "object_representation_sr.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define TRANSIENT_ROW_LOCKS_INITIAL_CAPACITY 64

/*
 * transient_row_locks_add () - remember a published row whose lock this statement will release
 *   return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY when the row cannot be remembered
 *   thread_p(in): thread entry
 *   locks(in/out): the statement's set
 *   oid(in): the row
 *   class_oid(in): the class it was locked under
 *
 * Note: a row that cannot be remembered keeps its lock to commit.  That costs one lock; releasing a
 *	 row the set does not know about would leave it unlocked for good.
 */
int
transient_row_locks_add (THREAD_ENTRY *thread_p, TRANSIENT_ROW_LOCKS *locks, const OID *oid,
			 const OID *class_oid)
{
  assert (locks != NULL && oid != NULL && class_oid != NULL);

  if (locks->count == locks->capacity)
    {
      int new_capacity = (locks->capacity == 0 ? TRANSIENT_ROW_LOCKS_INITIAL_CAPACITY : locks->capacity * 2);
      OID *new_oids = (OID *) db_private_realloc (thread_p, locks->oids, new_capacity * sizeof (OID));
      OID *new_class_oids;

      if (new_oids == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      locks->oids = new_oids;

      new_class_oids = (OID *) db_private_realloc (thread_p, locks->class_oids, new_capacity * sizeof (OID));
      if (new_class_oids == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      locks->class_oids = new_class_oids;
      locks->capacity = new_capacity;
    }

  COPY_OID (&locks->oids[locks->count], oid);
  COPY_OID (&locks->class_oids[locks->count], class_oid);
  locks->count++;

  return NO_ERROR;
}

/*
 * transient_row_locks_release () - release the row locks this statement was holding
 *   return: void
 *   thread_p(in): thread entry
 *   locks(in/out): the statement's set, emptied here
 */
void
transient_row_locks_release (THREAD_ENTRY *thread_p, TRANSIENT_ROW_LOCKS *locks)
{
  int i;

  assert (locks != NULL);

  for (i = 0; i < locks->count; i++)
    {
      lock_unlock_object_donot_move_to_non2pl (thread_p, &locks->oids[i], &locks->class_oids[i], X_LOCK);
    }
  locks->count = 0;
}

/*
 * transient_row_locks_clear () - free the set
 *   return: void
 *   thread_p(in): thread entry
 *   locks(in/out): the statement's set
 */
void
transient_row_locks_clear (THREAD_ENTRY *thread_p, TRANSIENT_ROW_LOCKS *locks)
{
  assert (locks != NULL);

  if (locks->oids != NULL)
    {
      db_private_free_and_init (thread_p, locks->oids);
    }
  if (locks->class_oids != NULL)
    {
      db_private_free_and_init (thread_p, locks->class_oids);
    }
  locks->count = 0;
  locks->capacity = 0;
}

/*
 * transient_row_locks_class_has_online_index () - is an index of this class being built online?
 *   return: true when at least one index is in OR_ONLINE_INDEX_BUILDING_IN_PROGRESS
 *   thread_p(in): thread entry
 *   class_oid(in): the class
 *
 * Note: an online build keeps its own state on each index entry -- INSERT_FLAG, DELETE_FLAG, or
 *	 neither -- and that state carries no MVCCID: it names neither the transaction that left it nor
 *	 whether that transaction ended.  The row lock is what keeps two writers from reading the same
 *	 entry under different assumptions, so a class under an online build keeps it to commit.
 *
 *	 One answer per class holds for the whole statement, and not because the build holds a strong
 *	 lock throughout -- it demotes to IX for the load precisely so DML is not blocked.  It holds
 *	 because the status is published by a schema change under SCH_M, and a DML statement holds IX on
 *	 the class to commit: no statement spans that publication.  A caller that cannot read the class
 *	 representation is told to keep the lock.
 */
bool
transient_row_locks_class_has_online_index (THREAD_ENTRY *thread_p, const OID *class_oid)
{
  OR_CLASSREP *classrep = NULL;
  int idx_in_cache = -1;
  bool found = false;
  int i;

  assert (class_oid != NULL && !OID_ISNULL (class_oid));

  classrep = heap_classrepr_get (thread_p, (OID *) class_oid, NULL, NULL_REPRID, &idx_in_cache);
  if (classrep == NULL)
    {
      return true;
    }

  for (i = 0; i < classrep->n_indexes; i++)
    {
      if (classrep->indexes[i].index_status == OR_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  found = true;
	  break;
	}
    }

  heap_classrepr_free_and_init (classrep, &idx_in_cache);

  return found;
}
