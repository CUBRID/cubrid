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
 * log_applier_wset_label.cpp - implementation of the pending writeset label
 */

#include "config.h"

#include "log_applier_wset_label.hpp"

#include <assert.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static bool la_Wset_label_stored = false;
static int la_Wset_label_tranid = 0;
static LOG_LSA la_Wset_label_dependency_lsa = LSA_INITIALIZER;
static UINT64 la_Wset_label_unit_key_hash = 0;

/*
 * la_wset_label_store - retain a writeset label until its transaction boundary is read
 *
 * return: none
 *
 *   tranid(in): transaction identified by the label
 *   dependency_lsa(in): dependency carried by the label
 *   unit_key_hash(in): hash of the single key of an independent replication unit, or 0
 */
void
la_wset_label_store (int tranid, const LOG_LSA *dependency_lsa, UINT64 unit_key_hash)
{
  assert (dependency_lsa != NULL);

  la_Wset_label_stored = true;
  la_Wset_label_tranid = tranid;
  LSA_COPY (&la_Wset_label_dependency_lsa, dependency_lsa);
  la_Wset_label_unit_key_hash = unit_key_hash;
}

/*
 * la_wset_label_matches - check whether the pending label belongs to a transaction
 *
 * return: true if the pending label belongs to tranid
 */
bool
la_wset_label_matches (int tranid)
{
  return la_Wset_label_stored && la_Wset_label_tranid == tranid;
}

/*
 * la_wset_label_take - consume the pending label of a transaction
 *
 * return: true if a matching label was consumed
 *
 *   tranid(in): transaction boundary consuming the label
 *   dependency_lsa(out): dependency carried by the label; may be NULL when only consumption is needed
 *   unit_key_hash(out): single key hash carried by the label, 0 if none; may be NULL
 *
 * Note: a transaction mismatch leaves the label pending for its own boundary.
 */
bool
la_wset_label_take (int tranid, LOG_LSA *dependency_lsa, UINT64 *unit_key_hash)
{
  if (dependency_lsa != NULL)
    {
      LSA_SET_NULL (dependency_lsa);
    }

  if (unit_key_hash != NULL)
    {
      *unit_key_hash = 0;
    }

  if (!la_wset_label_matches (tranid))
    {
      return false;
    }

  if (dependency_lsa != NULL)
    {
      LSA_COPY (dependency_lsa, &la_Wset_label_dependency_lsa);
    }

  if (unit_key_hash != NULL)
    {
      *unit_key_hash = la_Wset_label_unit_key_hash;
    }

  la_Wset_label_stored = false;
  LSA_SET_NULL (&la_Wset_label_dependency_lsa);
  la_Wset_label_unit_key_hash = 0;

  return true;
}
