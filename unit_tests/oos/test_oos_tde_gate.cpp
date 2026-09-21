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
 * test_oos_tde_gate.cpp - regression guard for the TDE encryption gate over the
 * heap recovery indexes that carry a full record body (CBRD-26668).
 *
 * Background (the bug this guards against):
 *   The MVCC remove-old-forward path in heap_update_relocation
 *   (src/storage/heap_file.c) tags an OOS-bearing forward delete with the
 *   recovery index RVHF_DELETE_NEWHOME_NOTIFY_VACUUM instead of plain
 *   RVHF_DELETE, so vacuum's forward-walk can reclaim the old OOS records from
 *   the undo image. That delete still logs the FULL old REC_NEWHOME body (user
 *   column values + OOS OIDs) via heap_log_delete_physical ->
 *   log_append_undoredo_recdes.
 *
 *   The TDE encryption gate in log_append_undoredo_crumbs
 *   (src/transaction/log_manager.c) only marks a log record for encryption when
 *   LOG_MAY_CONTAIN_USER_DATA(rcvindex) is true. RVHF_DELETE is in that macro,
 *   but RVHF_DELETE_NEWHOME_NOTIFY_VACUUM was originally missed -- so on a
 *   TDE-encrypted heap the old row image was written to the WAL in plaintext, a
 *   confidentiality regression versus the pre-OOS-vacuum RVHF_DELETE behavior.
 *
 * A full end-to-end "scan the WAL for the plaintext marker" test would need a
 * live TDE keystore and a TDE-flagged page, which the OOS unit-test harness
 * does not set up. The bug, however, is exactly a missing allowlist entry, so
 * the precise and deterministic regression guard is to assert the invariant
 * directly: LOG_MAY_CONTAIN_USER_DATA is a compile-time predicate over the
 * LOG_RCVINDEX enum, needing no server, page buffer, or DB fixture.
 */

#include "gtest/gtest.h"

#include "recovery.h"		/* LOG_RCVINDEX, RVHF_* */
#include "tde.h"			/* LOG_MAY_CONTAIN_USER_DATA */

TEST (TdeUserDataGate, OosForwardDeleteIsEncryptedRcvindex)
{
  /* The headline invariant / the exact fix for CBRD-26668. If anyone removes
   * RVHF_DELETE_NEWHOME_NOTIFY_VACUUM from LOG_MAY_CONTAIN_USER_DATA in
   * src/storage/tde.h, this fails -- which is precisely the plaintext-leak
   * regression. */
  EXPECT_TRUE (LOG_MAY_CONTAIN_USER_DATA (RVHF_DELETE_NEWHOME_NOTIFY_VACUUM));
}

TEST (TdeUserDataGate, SiblingAndBaselineDeletePathsAreGated)
{
  /* Pre-PR baseline: the plain RVHF_DELETE path (non-OOS forward deletes) was
   * always gated; the OOS path above must match it. */
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
