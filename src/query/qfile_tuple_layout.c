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
 * qfile_tuple_layout.c - temporary list file tuple slot & accessor API (CBRD-27365, ADR 0016)
 */

#ident "$Id$"

#include "config.h"

#include "qfile_tuple_layout.h"
#include "memory_alloc.h"

/*
 * qfile_slot_clear () - release the slot-owned scratch area and unbind the descriptor.
 *   Called by the slot owner when the scan/cursor is closed (D-182-10). Does not touch rec->tpl / rec->size:
 *   the owned tuple buffer is still freed by the record owner as before.
 */
void
qfile_slot_clear (QFILE_TUPLE_RECORD * rec)
{
  if (rec->scratch != NULL)
    {
      db_private_free (NULL, rec->scratch);
      rec->scratch = NULL;
    }
  rec->scratch_size = 0;
  rec->tl = NULL;
  rec->nvalid = 0;
  rec->fast_limit = 0;
  rec->off = 0;
}
