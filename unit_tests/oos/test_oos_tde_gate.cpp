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

/*
 * test_oos_tde_gate.cpp - regression guard for the TDE encryption gate over the heap and OOS recovery
 * indexes (CBRD-26668, ADR-0008).
 *
 * The TDE encryption gate in the log append functions (src/transaction/log_manager.c) marks a log record
 * for encryption only when LOG_MAY_CONTAIN_USER_DATA(rcvindex) is true. A record that carries a full
 * heap record body must be gated, or a TDE-encrypted heap writes its rows to the WAL in plaintext. The
 * CBRD-26668 leak was a forward delete logged under an index missing from the gate; since ADR-0008 that
 * delete is a plain RVHF_DELETE again.
 *
 * The OOS reclaim records of ADR-0008 carry no user data and are logged without a page, so they must stay
 * out of the gate. The invariant is a compile-time predicate over the LOG_RCVINDEX enum, needing no
 * server, page buffer, or DB fixture.
 */

#include "gtest/gtest.h"

#include "recovery.h"		/* LOG_RCVINDEX, RVHF_* */
#include "tde.h"			/* LOG_MAY_CONTAIN_USER_DATA */

TEST (TdeUserDataGate, OosReclaimRecordsCarryNoUserData)
{
  /* ADR-0008: the reclaim postpones and the UPDATE notification carry only OOS OIDs, identity stamps,
   * file ids and an MVCCID, and are logged without a page. log_append_postpone reads the page's TDE
   * algorithm for a gated index, so gating one of them would also dereference a NULL page. The relocated
   * forward delete of an MVCC UPDATE is a plain RVHF_DELETE again, gated below. */
  EXPECT_FALSE (LOG_MAY_CONTAIN_USER_DATA (RVOOS_NOTIFY_VACUUM));
  EXPECT_FALSE (LOG_MAY_CONTAIN_USER_DATA (RVOOS_RECLAIM_POSTPONE));
  EXPECT_FALSE (LOG_MAY_CONTAIN_USER_DATA (RVOOS_UPDATE_DROP_POSTPONE));
}

TEST (TdeUserDataGate, SiblingAndBaselineDeletePathsAreGated)
{
  /* The relocated forward delete of an UPDATE, OOS-bearing or not, is a plain RVHF_DELETE. */
  EXPECT_TRUE (LOG_MAY_CONTAIN_USER_DATA (RVHF_DELETE));

  /* Sibling on the UPDATE side: RVHF_UPDATE_NOTIFY_VACUUM carries the same kind
   * of full record body and was already gated -- it is the precedent that made
   * the DELETE-side omission an oversight rather than a deliberate exclusion. */
  EXPECT_TRUE (LOG_MAY_CONTAIN_USER_DATA (RVHF_UPDATE_NOTIFY_VACUUM));
}

TEST (TdeUserDataGate, MetadataOnlyHeapOpIsNotGated)
{
  /* Negative control: proves the macro actually discriminates rather than being
   * vacuously true for any RVHF_* index. RVHF_MARK_REUSABLE_SLOT logs only a
   * slot id (no user payload), so it is intentionally NOT in the user-data
   * set. */
  EXPECT_FALSE (LOG_MAY_CONTAIN_USER_DATA (RVHF_MARK_REUSABLE_SLOT));
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
