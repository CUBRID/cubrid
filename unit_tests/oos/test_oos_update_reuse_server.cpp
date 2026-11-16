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
 * test_oos_update_reuse_server.cpp - SERVER_MODE tests of an UPDATE that reuses the OOS chains of the columns it does
 * not assign (CBRD-27230)
 *
 * Rows are written through locator_attribute_info_force, the way qexec_execute_update writes them: the attribute
 * information starts uninitialized for every column, and only the assigned ones are set. Each case owns a table that
 * scripts/setup_update_reuse_tables.sh creates as (id INT PRIMARY KEY, k INT, c1 BIT VARYING, c2 BIT VARYING), so
 * vacuum reclaiming one case's garbage never changes another case's OOS file.
 */

#include "test_oos_server_common.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "connection_defs.h"
#include "dbtype.h"
#include "heap_oos.hpp"
#include "lock_manager.h"
#include "locator_sr.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "replication.h"
#include "scope_exit.hpp"
#include "server_support.h"
#include "vacuum.h"
#include "xserver_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  /* Large enough that both values leave the record, small enough that each chain is one chunk. */
  const std::string C1_VALUE (6000, '\xA1');
  const std::string C2_VALUE (6000, '\xB2');
  const std::string NEW_C1_VALUE (7000, '\xC3');
  const std::string NEW_C2_VALUE (7000, '\xD4');

  /* An OOS column of one record version: its 24-byte stub and the chain it names. */
  struct oos_column
  {
    std::string stub;
    oos_chain_ref chain;
    DB_BIGINT length = 0;
  };

  /* What a reader got for a row: how its read ended, the error it left, and the values of the version it found. */
  struct snapshot_read
  {
    SCAN_CODE scan = S_ERROR;
    int error = NO_ERROR;
    std::string k;
    std::string c1;
    std::string c2;
  };

  bool
  same_chain (const oos_column &a, const oos_column &b)
  {
    return OID_EQ (&a.chain.head_oid, &b.chain.head_oid);
  }

  void
  commit_current_tran ()
  {
    ASSERT_EQ (xtran_server_commit (thread_p, false), TRAN_UNACTIVE_COMMITTED);
  }

  VACUUM_LOG_BLOCKID
  current_log_blockid ()
  {
    return vacuum_get_log_blockid (log_Gl.prior_info.prior_lsa.load ().pageid);
  }

  /* Poll pred, waking the vacuum master each round. */
  bool
  wait_for_vacuum (const std::function<bool ()> &pred, int timeout_sec)
  {
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (timeout_sec);
    while (std::chrono::steady_clock::now () < deadline)
      {
	if (pred ())
	  {
	    return true;
	  }
	(void) vacuum_wakeup_master_daemon ();
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
      }
    return pred ();
  }

  class replication_tracking_guard
  {
    public:
      replication_tracking_guard ()
	: m_old_ha_mode (prm_get_integer_value (PRM_ID_HA_MODE))
	, m_old_ha_state (css_ha_server_state ())
      {
	prm_set_integer_value (PRM_ID_HA_MODE, HA_MODE_FAIL_BACK);
	m_error = css_change_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE, true, 0, true);
	er_clear ();
      }

      ~replication_tracking_guard ()
      {
	(void) css_change_ha_server_state (thread_p, m_old_ha_state, true, 0, true);
	prm_set_integer_value (PRM_ID_HA_SERVER_STATE, m_old_ha_state);
	prm_set_integer_value (PRM_ID_HA_MODE, m_old_ha_mode);
	er_clear ();
      }

      int error () const
      {
	return m_error;
      }

    private:
      int m_old_ha_mode;
      HA_SERVER_STATE m_old_ha_state;
      int m_error = NO_ERROR;
  };
}

class OosUpdateReuse : public ::testing::Test
{
  protected:
    OID class_oid = OID_INITIALIZER;
    HFID hfid = HFID_INITIALIZER;
    HEAP_CACHE_ATTRINFO attr_info;
    bool attr_info_started = false;
    HFID filler_hfid = HFID_INITIALIZER;
    ATTR_ID attr_row_id = -1;
    ATTR_ID attr_k = -1;
    ATTR_ID attr_c1 = -1;
    ATTR_ID attr_c2 = -1;
    ATTR_ID attr_extra = -1;	/* t_reuse_repr only, added after its row */

    void TearDown () override
    {
      if (attr_info_started)
	{
	  heap_attrinfo_end (thread_p, &attr_info);
	}
      if (!HFID_IS_NULL (&filler_hfid))
	{
	  (void) xheap_destroy (thread_p, &filler_hfid, &class_oid);
	}
      (void) xtran_server_commit (thread_p, false);
    }

    void open_table (const char *name)
    {
      ASSERT_EQ (xlocator_find_class_oid (thread_p, name, &class_oid, NULL_LOCK), LC_CLASSNAME_EXIST) << name;
      ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &hfid, NULL), NO_ERROR);
      ASSERT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info), NO_ERROR);
      attr_info_started = true;

      const std::pair<std::string, ATTR_ID *> columns[] =
      {
	{ "id", &attr_row_id }, { "k", &attr_k }, { "c1", &attr_c1 }, { "c2", &attr_c2 }, { "extra", &attr_extra }
      };
      HEAP_SCANCACHE scan_cache;
      (void) heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);
      RECDES class_record = RECDES_INITIALIZER;
      ASSERT_EQ (heap_get_class_record (thread_p, &class_oid, &class_record, &scan_cache, PEEK), S_SUCCESS);
      for (int i = 0; i < attr_info.last_classrepr->n_attributes; i++)
	{
	  const ATTR_ID id = attr_info.last_classrepr->attributes[i].id;
	  char *attr_name = NULL;
	  int alloced = 0;
	  ASSERT_EQ (or_get_attrname (&class_record, id, &attr_name, &alloced), NO_ERROR);
	  for (const auto &column : columns)
	    {
	      if (attr_name != NULL && column.first == attr_name)
		{
		  *column.second = id;
		}
	    }
	  if (attr_name != NULL && alloced == 1)
	    {
	      db_private_free_and_init (thread_p, attr_name);
	    }
	}
      (void) heap_scancache_end (thread_p, &scan_cache);
      ASSERT_TRUE (attr_row_id >= 0 && attr_k >= 0 && attr_c1 >= 0 && attr_c2 >= 0) << name;
    }

    int location_of (ATTR_ID attr)
    {
      for (int i = 0; i < attr_info.last_classrepr->n_attributes; i++)
	{
	  if (attr_info.last_classrepr->attributes[i].id == attr)
	    {
	      return attr_info.last_classrepr->attributes[i].location;
	    }
	}
      return -1;
    }

    void set_value (const OID &oid, ATTR_ID attr, DB_VALUE &value)
    {
      ASSERT_EQ (heap_attrinfo_set (&oid, attr, &value, &attr_info), NO_ERROR);
      pr_clear_value (&value);
    }

    void set_int (const OID &oid, ATTR_ID attr, int v)
    {
      DB_VALUE value;
      db_make_int (&value, v);
      set_value (oid, attr, value);
    }

    void set_bits (const OID &oid, ATTR_ID attr, const std::string &bytes)
    {
      DB_VALUE value;
      db_make_varbit (&value, DB_MAX_VARBIT_PRECISION, bytes.data (), (int) bytes.size () * 8);
      set_value (oid, attr, value);
    }

    /* The row's class takes the intention lock a statement would hold. */
    void lock_class ()
    {
      ASSERT_EQ (lock_object (thread_p, &class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK), LK_GRANTED);
    }

    int force (OID &oid, LC_COPYAREA_OPERATION operation, int op_type, ATTR_ID *att_ids, int n_att_ids,
	       UPDATE_INPLACE_STYLE style = UPDATE_INPLACE_NONE, int pruning_type = DB_NOT_PARTITIONED_CLASS)
    {
      HEAP_SCANCACHE scan_cache;
      int error = locator_start_force_scan_cache (thread_p, &scan_cache, &hfid, &class_oid, op_type);
      if (error != NO_ERROR)
	{
	  return error;
	}
      int force_count = 0;
      error = locator_attribute_info_force (thread_p, &hfid, &oid, &attr_info, att_ids, n_att_ids, operation, op_type,
					    &scan_cache, &force_count, false, REPL_INFO_TYPE_RBR_NORMAL, pruning_type,
					    NULL, NULL, NULL, style, NULL, LOCATOR_LOCK_AT_FORCE);
      locator_end_force_scan_cache (thread_p, &scan_cache);
      return error;
    }

    OID insert_row (int id)
    {
      OID oid = OID_INITIALIZER;
      lock_class ();
      EXPECT_EQ (heap_attrinfo_clear_dbvalues (&attr_info), NO_ERROR);
      set_int (oid, attr_row_id, id);
      set_int (oid, attr_k, 0);
      set_bits (oid, attr_c1, C1_VALUE);
      set_bits (oid, attr_c2, C2_VALUE);
      EXPECT_EQ (force (oid, LC_FLUSH_INSERT, SINGLE_ROW_INSERT, NULL, 0), NO_ERROR);
      EXPECT_FALSE (OID_ISNULL (&oid));
      return oid;
    }

    /* An UPDATE of one row that assigns only the columns given; the others stay uninitialized. */
    int update_row (OID oid, const int *k, const std::string *c1, const std::string *c2,
		    UPDATE_INPLACE_STYLE style = UPDATE_INPLACE_NONE, int pruning_type = DB_NOT_PARTITIONED_CLASS)
    {
      std::vector<ATTR_ID> assigned;
      lock_class ();
      EXPECT_EQ (heap_attrinfo_clear_dbvalues (&attr_info), NO_ERROR);
      if (k != nullptr)
	{
	  set_int (oid, attr_k, *k);
	  assigned.push_back (attr_k);
	}
      if (c1 != nullptr)
	{
	  set_bits (oid, attr_c1, *c1);
	  assigned.push_back (attr_c1);
	}
      if (c2 != nullptr)
	{
	  set_bits (oid, attr_c2, *c2);
	  assigned.push_back (attr_c2);
	}
      return force (oid, LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, assigned.data (), (int) assigned.size (), style,
		    pruning_type);
    }

    /* The row's latest version, as stored. */
    std::string latest_record (const OID &oid)
    {
      HEAP_SCANCACHE scan_cache;
      EXPECT_EQ (heap_scancache_start (thread_p, &scan_cache, &hfid, &class_oid, false, NULL), NO_ERROR);
      RECDES recdes = RECDES_INITIALIZER;
      HEAP_GET_CONTEXT context;
      OID oid_copy = oid;
      heap_init_get_context (thread_p, &context, &oid_copy, &class_oid, &recdes, &scan_cache, COPY, NULL_CHN,
			     HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      SCAN_CODE scan = heap_get_last_version (thread_p, &context);
      heap_clean_get_context (thread_p, &context);
      std::string record = scan == S_SUCCESS ? std::string (recdes.data, recdes.length) : std::string ();
      (void) heap_scancache_end (thread_p, &scan_cache);
      EXPECT_EQ (scan, S_SUCCESS);
      return record;
    }

    /* The version of the row the current transaction's snapshot sees, if any. */
    SCAN_CODE read_visible (const OID &oid, std::string &record)
    {
      HEAP_SCANCACHE scan_cache;
      EXPECT_EQ (heap_scancache_start (thread_p, &scan_cache, &hfid, &class_oid, false,
				       logtb_get_mvcc_snapshot (thread_p)), NO_ERROR);
      RECDES recdes = RECDES_INITIALIZER;
      OID oid_copy = oid;
      SCAN_CODE scan = heap_get_visible_version (thread_p, &oid_copy, &class_oid, &recdes, &scan_cache, COPY,
		       NULL_CHN, HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      record = scan == S_SUCCESS ? std::string (recdes.data, recdes.length) : std::string ();
      (void) heap_scancache_end (thread_p, &scan_cache);
      return scan;
    }

    std::string visible_record (const OID &oid)
    {
      std::string record;
      EXPECT_EQ (read_visible (oid, record), S_SUCCESS);
      return record;
    }

    /* The column's stub in record, located through the record's own representation. */
    oos_column oos_column_of (const std::string &record, ATTR_ID attr)
    {
      oos_column column;
      RECDES view = { (int) record.size (), (int) record.size (), REC_HOME, const_cast<char *> (record.data ()) };
      int cache_index = -1;
      OR_CLASSREP *repr = heap_classrepr_get (thread_p, &class_oid, NULL, or_rep_id (&view), &cache_index);
      EXPECT_NE (repr, nullptr);
      if (repr == nullptr)
	{
	  return column;
	}
      for (int i = 0; i < repr->n_attributes; i++)
	{
	  const OR_ATTRIBUTE &attribute = repr->attributes[i];
	  char *stub = NULL;
	  if (attribute.id == attr
	      && heap_recdes_get_oos_inline_stub (&view, attribute.location, &stub) == NO_ERROR
	      && heap_oos_parse_inline_ref (&view, attribute.location, &column.chain, &column.length) == NO_ERROR)
	    {
	      column.stub.assign (stub, OR_OOS_INLINE_SIZE);
	    }
	}
      heap_classrepr_free_and_init (repr, &cache_index);
      EXPECT_EQ (column.stub.size (), (std::size_t) OR_OOS_INLINE_SIZE) << "attribute " << attr << " is not OOS";
      return column;
    }

    /* The column's value in record, read through the attribute layer. */
    std::string value_of (const OID &oid, const std::string &record, ATTR_ID attr)
    {
      HEAP_CACHE_ATTRINFO read_info;
      std::string text;
      RECDES view = { (int) record.size (), (int) record.size (), REC_HOME, const_cast<char *> (record.data ()) };
      EXPECT_EQ (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &read_info), NO_ERROR);
      EXPECT_EQ (heap_attrinfo_read_dbvalues (thread_p, &oid, &view, &read_info), NO_ERROR);
      DB_VALUE *value = heap_attrinfo_access (attr, &read_info);
      EXPECT_NE (value, nullptr);
      if (value != nullptr && DB_VALUE_TYPE (value) == DB_TYPE_INTEGER)
	{
	  text = std::to_string (db_get_int (value));
	}
      else if (value != nullptr && DB_VALUE_TYPE (value) == DB_TYPE_VARBIT)
	{
	  int bits = 0;
	  const char *bytes = db_get_bit (value, &bits);
	  text.assign (bytes, bits / 8);
	}
      heap_attrinfo_end (thread_p, &read_info);
      return text;
    }

    /* The chain still holds the value its stub names: its head carries the stub's identity stamp. */
    bool chain_live (const oos_column &column)
    {
      std::string buffer ((std::size_t) column.length, '\0');
      int error = oos_read (thread_p, column.chain, oos_buffer (&buffer[0], buffer.size ()));
      er_clear ();
      return error == NO_ERROR;
    }

    OOS_STATS_INFO oos_stats ()
    {
      OOS_STATS_INFO info {};
      EXPECT_EQ (xoos_get_stats_by_class_oid (thread_p, &class_oid, &info), NO_ERROR);
      return info;
    }

    /* Vacuum takes a log block only once the log is two blocks past it, so plain rows in a scratch heap of the
     * same class push it there. */
    void close_log_block (VACUUM_LOG_BLOCKID blockid)
    {
      if (HFID_IS_NULL (&filler_hfid))
	{
	  ASSERT_EQ (xheap_create (thread_p, &filler_hfid, &class_oid, false), NO_ERROR);
	}
      HEAP_SCANCACHE scan_cache;
      heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &filler_hfid);
      COPY_OID (&scan_cache.node.class_oid, &class_oid);
      scan_cache.file_type = FILE_HEAP;
      scan_cache.mvcc_disabled_class = false;
      scan_cache.page_latch = PGBUF_LATCH_WRITE;
      scan_cache.cache_last_fix_page = false;

      const int filler_size = 4096;
      std::vector<char> filler (OR_MVCC_REP_SIZE + OR_CHN_SIZE + filler_size + 2 * OR_MVCCID_SIZE, 0);
      for (int guard = 0; current_log_blockid () <= blockid + 1; guard++)
	{
	  ASSERT_LT (guard, 20000) << "log block did not close";
	  std::fill (filler.begin (), filler.end (), 0);
	  OR_PUT_INT (filler.data () + OR_REP_OFFSET, OR_OFFSET_SIZE_4BYTE);
	  RECDES recdes = { (int) filler.size (), OR_MVCC_REP_SIZE + OR_CHN_SIZE + filler_size, REC_HOME,
			    filler.data ()
			  };
	  HEAP_OPERATION_CONTEXT ctx;
	  heap_create_insert_context (&ctx, &filler_hfid, &class_oid, &recdes, &scan_cache);
	  ASSERT_EQ (heap_insert_logical (thread_p, &ctx, NULL), NO_ERROR);
	}
      (void) heap_scancache_end (thread_p, &scan_cache);
      commit_current_tran ();
    }

    /* A reader takes its snapshot, holding no lock on the class, before an UPDATE of k commits; later_write then
     * commits, and the reader reads the row under the IS_LOCK a statement takes. The UPDATE reuses the row's OOS
     * chains or, with reuse false, assigns c1 and c2 their own values and so writes new chains, as every UPDATE did
     * before CBRD-27230. */
    void read_across_later_write (int id, bool reuse, const std::function<void (OID &)> &later_write,
				  snapshot_read &seen)
    {
      OID oid = insert_row (id);
      commit_current_tran ();
      const oos_column v0_c1 = oos_column_of (latest_record (oid), attr_c1);

      const int worker_idx = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      TRAN_STATE reader_state;
      const int reader_idx = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &reader_state,
			     TRAN_LOCK_INFINITE_WAIT, TRAN_REPEATABLE_READ);
      ASSERT_NE (reader_idx, NULL_TRAN_INDEX);
      ASSERT_NE (logtb_get_mvcc_snapshot (thread_p), nullptr);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);
      scope_exit end_reader ([&] () noexcept
      {
	LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_idx);
	(void) log_abort (thread_p, reader_idx);
	logtb_release_tran_index (thread_p, reader_idx);
	LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);
      });

      const int k = 1;
      ASSERT_EQ (reuse ? update_row (oid, &k, nullptr, nullptr) : update_row (oid, &k, &C1_VALUE, &C2_VALUE),
		 NO_ERROR);
      commit_current_tran ();
      ASSERT_EQ (same_chain (oos_column_of (latest_record (oid), attr_c1), v0_c1), reuse);

      ASSERT_NO_FATAL_FAILURE (later_write (oid));
      commit_current_tran ();

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_idx);
      ASSERT_EQ (lock_object (thread_p, &class_oid, oid_Root_class_oid, IS_LOCK, LK_UNCOND_LOCK), LK_GRANTED);
      er_clear ();
      std::string record;
      seen.scan = read_visible (oid, record);
      if (seen.scan == S_SUCCESS)
	{
	  seen.k = value_of (oid, record, attr_k);
	  seen.c1 = value_of (oid, record, attr_c1);
	  seen.c2 = value_of (oid, record, attr_c2);
	}
      seen.error = er_errid ();
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);
    }
};

/* An UPDATE that assigns only an inline column keeps every OOS stub byte for byte, inserts no chain, and the
 * values read back. */
TEST_F (OosUpdateReuse, UnassignedChainsAreReusedNotRewritten)
{
  open_table ("dba.t_reuse_inline");
  OID oid = insert_row (1);
  commit_current_tran ();
  const std::string before = latest_record (oid);
  const OOS_STATS_INFO stats_before = oos_stats ();
  ASSERT_GT (stats_before.num_recs, 0);

  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  const OOS_STATS_INFO stats_after = oos_stats ();
  EXPECT_EQ (stats_after.num_recs, stats_before.num_recs) << "an UPDATE of an inline column inserted an OOS chain";
  EXPECT_EQ (stats_after.num_user_pages, stats_before.num_user_pages);
  commit_current_tran ();

  const std::string after = latest_record (oid);
  EXPECT_EQ (oos_column_of (after, attr_c1).stub, oos_column_of (before, attr_c1).stub);
  EXPECT_EQ (oos_column_of (after, attr_c2).stub, oos_column_of (before, attr_c2).stub);
  EXPECT_EQ (value_of (oid, after, attr_k), "1");
  EXPECT_EQ (value_of (oid, after, attr_c1), C1_VALUE);
  EXPECT_EQ (value_of (oid, after, attr_c2), C2_VALUE);
}

/* A mixed UPDATE writes a new chain for the column it assigns and reuses the other; its notification names only
 * the assigned column's old chain, which vacuum reclaims while the reused one stays. */
TEST_F (OosUpdateReuse, MixedUpdateDropsOnlyTheAssignedChain)
{
  open_table ("dba.t_reuse_mixed");
  OID oid = insert_row (1);
  commit_current_tran ();
  const std::string before = latest_record (oid);
  const oos_column old_c1 = oos_column_of (before, attr_c1);
  const oos_column old_c2 = oos_column_of (before, attr_c2);

  ASSERT_EQ (update_row (oid, nullptr, &NEW_C1_VALUE, nullptr), NO_ERROR);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();
  const std::string after = latest_record (oid);
  const oos_column new_c1 = oos_column_of (after, attr_c1);
  EXPECT_FALSE (same_chain (new_c1, old_c1)) << "an assigned column must get a new chain";
  EXPECT_EQ (oos_column_of (after, attr_c2).stub, old_c2.stub);

  close_log_block (update_block);
  ASSERT_TRUE (wait_for_vacuum ([&] { return !chain_live (old_c1); }, 50))
      << "vacuum did not reclaim the chain the UPDATE dropped";
  EXPECT_TRUE (chain_live (old_c2)) << "vacuum reclaimed the reused chain";
  EXPECT_TRUE (chain_live (new_c1));
  EXPECT_EQ (value_of (oid, after, attr_c1), NEW_C1_VALUE);
  EXPECT_EQ (value_of (oid, after, attr_c2), C2_VALUE);
}

/* A chain that a reusing UPDATE kept is reclaimed once a later committed UPDATE assigns its column, and the chain
 * that version still reuses stays. */
TEST_F (OosUpdateReuse, VacuumReclaimsAReusedChainOnceAVersionDropsIt)
{
  open_table ("dba.t_reuse_vacuum");
  OID oid = insert_row (1);
  commit_current_tran ();
  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  commit_current_tran ();
  const std::string shared = latest_record (oid);
  const oos_column shared_c1 = oos_column_of (shared, attr_c1);
  const oos_column shared_c2 = oos_column_of (shared, attr_c2);

  ASSERT_EQ (update_row (oid, nullptr, &NEW_C1_VALUE, nullptr), NO_ERROR);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();
  const std::string latest = latest_record (oid);
  const oos_column new_c1 = oos_column_of (latest, attr_c1);

  close_log_block (update_block);
  ASSERT_TRUE (wait_for_vacuum ([&] { return !chain_live (shared_c1); }, 50))
      << "vacuum did not reclaim the chain the third version dropped";
  EXPECT_TRUE (chain_live (shared_c2)) << "vacuum reclaimed a chain the latest version reuses";
  EXPECT_TRUE (chain_live (new_c1));
  EXPECT_EQ (value_of (oid, latest, attr_c1), NEW_C1_VALUE);
  EXPECT_EQ (value_of (oid, latest, attr_c2), C2_VALUE);
}

/* A snapshot that sees the reusing version keeps the chain it shares with the version before it, after a later
 * UPDATE dropped that chain, until the snapshot ends. */
TEST_F (OosUpdateReuse, SnapshotOfTheReusingVersionHoldsTheReclaim)
{
  open_table ("dba.t_reuse_snapshot");
  OID oid = insert_row (1);
  commit_current_tran ();
  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  commit_current_tran ();
  const oos_column reused_c2 = oos_column_of (latest_record (oid), attr_c2);

  const int worker_idx = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  TRAN_STATE reader_state;
  const int reader_idx = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, &reader_state,
			 TRAN_LOCK_INFINITE_WAIT, TRAN_REPEATABLE_READ);
  ASSERT_NE (reader_idx, NULL_TRAN_INDEX);
  ASSERT_NE (logtb_get_mvcc_snapshot (thread_p), nullptr);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);
  bool reader_active = true;
  scope_exit end_reader ([&] () noexcept
  {
    if (reader_active)
      {
	LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_idx);
	(void) log_abort (thread_p, reader_idx);
	logtb_release_tran_index (thread_p, reader_idx);
	LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);
      }
  });

  ASSERT_EQ (update_row (oid, nullptr, nullptr, &NEW_C2_VALUE), NO_ERROR);
  const VACUUM_LOG_BLOCKID update_block = current_log_blockid ();
  commit_current_tran ();
  const oos_column new_c2 = oos_column_of (latest_record (oid), attr_c2);
  close_log_block (update_block);

  EXPECT_FALSE (wait_for_vacuum ([&] { return !chain_live (reused_c2); }, 3))
      << "vacuum reclaimed a chain a live snapshot still reads";
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, reader_idx);
  const std::string seen = visible_record (oid);
  EXPECT_EQ (oos_column_of (seen, attr_c2).stub, reused_c2.stub);
  EXPECT_EQ (value_of (oid, seen, attr_c2), C2_VALUE);
  (void) log_abort (thread_p, reader_idx);
  logtb_release_tran_index (thread_p, reader_idx);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, worker_idx);
  reader_active = false;

  ASSERT_TRUE (wait_for_vacuum ([&] { return !chain_live (reused_c2); }, 50))
      << "vacuum did not reclaim the dropped chain after the snapshot ended";
  EXPECT_TRUE (chain_live (oos_column_of (latest_record (oid), attr_c1)));
  EXPECT_TRUE (chain_live (new_c2));
}

/* A rollback to a savepoint, which is also how a failed statement rolls back, cancels the notification of an
 * UPDATE after the savepoint and keeps the one appended before it. */
TEST_F (OosUpdateReuse, PartialRollbackCancelsOnlyTheLaterDrop)
{
  open_table ("dba.t_reuse_savepoint");
  OID oid = insert_row (1);
  commit_current_tran ();
  const std::string original = latest_record (oid);
  const oos_column old_c1 = oos_column_of (original, attr_c1);
  const oos_column old_c2 = oos_column_of (original, attr_c2);

  ASSERT_EQ (update_row (oid, nullptr, &NEW_C1_VALUE, nullptr), NO_ERROR);
  const std::string before_savepoint = latest_record (oid);
  LOG_LSA savepoint_lsa;
  ASSERT_EQ (xtran_server_savepoint (thread_p, "reuse_sp", &savepoint_lsa), NO_ERROR);
  const int k = 5;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  ASSERT_EQ (update_row (oid, nullptr, nullptr, &NEW_C2_VALUE), NO_ERROR);
  const oos_column rolled_back_c2 = oos_column_of (latest_record (oid), attr_c2);
  ASSERT_EQ (xtran_server_partial_abort (thread_p, "reuse_sp", &savepoint_lsa), TRAN_UNACTIVE_ABORTED);
  const VACUUM_LOG_BLOCKID commit_block = current_log_blockid ();
  commit_current_tran ();

  const std::string latest = latest_record (oid);
  EXPECT_EQ (latest, before_savepoint) << "the rollback did not restore the version before the savepoint";
  EXPECT_FALSE (chain_live (rolled_back_c2)) << "the rolled-back insert left its chain";

  close_log_block (commit_block);
  ASSERT_TRUE (wait_for_vacuum ([&] { return !chain_live (old_c1); }, 50))
      << "the drop before the savepoint was not reclaimed";
  EXPECT_TRUE (chain_live (old_c2)) << "the drop after the savepoint survived its rollback";
  EXPECT_EQ (value_of (oid, latest, attr_k), "0");
  EXPECT_EQ (value_of (oid, latest, attr_c1), NEW_C1_VALUE);
  EXPECT_EQ (value_of (oid, latest, attr_c2), C2_VALUE);
}

/* A transaction rollback restores the row with the chains its versions shared and publishes nothing. */
TEST_F (OosUpdateReuse, TransactionRollbackKeepsTheChains)
{
  open_table ("dba.t_reuse_abort");
  OID oid = insert_row (1);
  commit_current_tran ();
  const std::string original = latest_record (oid);
  const oos_column old_c1 = oos_column_of (original, attr_c1);
  const oos_column old_c2 = oos_column_of (original, attr_c2);

  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  ASSERT_EQ (update_row (oid, nullptr, &NEW_C1_VALUE, nullptr), NO_ERROR);
  const oos_column rolled_back_c1 = oos_column_of (latest_record (oid), attr_c1);
  const VACUUM_LOG_BLOCKID abort_block = current_log_blockid ();
  ASSERT_EQ (xtran_server_abort (thread_p), TRAN_UNACTIVE_ABORTED);

  EXPECT_EQ (latest_record (oid), original);
  EXPECT_FALSE (chain_live (rolled_back_c1));
  close_log_block (abort_block);
  EXPECT_FALSE (wait_for_vacuum ([&] { return !chain_live (old_c1) || !chain_live (old_c2); }, 3))
      << "a rolled-back UPDATE reclaimed a chain";
  EXPECT_EQ (value_of (oid, original, attr_c1), C1_VALUE);
  EXPECT_EQ (value_of (oid, original, attr_c2), C2_VALUE);
}

/* The master replicates a reused column as a marker in its place among the row's OOS items, which follow the order
 * of the record's stubs. */
TEST_F (OosUpdateReuse, ReplicatesAReusedColumnAsAMarkerInAttributeOrder)
{
  open_table ("dba.t_reuse_repl");
  OID oid = insert_row (1);
  commit_current_tran ();

  replication_tracking_guard tracking;
  ASSERT_EQ (tracking.error (), NO_ERROR);
  ASSERT_TRUE (log_does_allow_replication ());
  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
  ASSERT_NE (tdes, nullptr);
  scope_exit rollback ([&] () noexcept
  {
    (void) xtran_server_abort (thread_p);
  });

  auto items_since = [tdes] (int first)
  {
    std::vector<LOG_RCVINDEX> items;
    for (int i = first; i < tdes->cur_repl_record; i++)
      {
	items.push_back (tdes->repl_records[i].rcvindex);
      }
    return items;
  };

  const int c1_position = location_of (attr_c1) < location_of (attr_c2) ? 0 : 1;
  const int c2_position = 1 - c1_position;
  std::vector<LOG_RCVINDEX> expected (2, RVREPL_OOS_INSERT);
  expected[c2_position] = RVREPL_OOS_REUSE;
  expected.push_back (RVREPL_DATA_UPDATE);

  int first = tdes->cur_repl_record;
  ASSERT_EQ (update_row (oid, nullptr, &NEW_C1_VALUE, nullptr), NO_ERROR);
  EXPECT_EQ (items_since (first), expected);
  ASSERT_EQ (thread_p->oos_oids.size (), 2U);
  EXPECT_FALSE (thread_p->oos_oids[c1_position].reused);
  const oos_column new_c1 = oos_column_of (latest_record (oid), attr_c1);
  EXPECT_TRUE (OID_EQ (&thread_p->oos_oids[c1_position].oid, &new_c1.chain.head_oid));
  EXPECT_TRUE (thread_p->oos_oids[c2_position].reused);
  EXPECT_TRUE (LSA_ISNULL (&tdes->repl_records[first + c2_position].lsa)) << "a marker points at no OOS insert";

  first = tdes->cur_repl_record;
  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  EXPECT_EQ (items_since (first),
	     (std::vector<LOG_RCVINDEX> { RVREPL_OOS_REUSE, RVREPL_OOS_REUSE, RVREPL_DATA_UPDATE }));

  /* a new chain longer than a page is a boundary item and the chain, and the marker keeps its own place */
  first = tdes->cur_repl_record;
  const std::string long_c1 (3 * DB_PAGESIZE, '\xE5');
  ASSERT_EQ (update_row (oid, nullptr, &long_c1, nullptr), NO_ERROR);
  std::vector<LOG_RCVINDEX> expected_long = { RVREPL_DUMMY_OOS_RECORD, RVREPL_OOS_INSERT };
  expected_long.insert (expected_long.begin () + (c2_position == 0 ? 0 : 2), RVREPL_OOS_REUSE);
  expected_long.push_back (RVREPL_DATA_UPDATE);
  EXPECT_EQ (items_since (first), expected_long);
}

/* An in-place UPDATE, the style ALTER ... CHANGE rewrites rows with, reclaims what it drops at commit without
 * waiting for snapshots, so it writes new chains instead of sharing the old ones. */
TEST_F (OosUpdateReuse, InPlaceUpdateWritesNewChains)
{
  open_table ("dba.t_reuse_inplace");
  OID oid = insert_row (1);
  commit_current_tran ();
  const std::string before = latest_record (oid);

  lock_class ();
  ASSERT_EQ (lock_object (thread_p, &oid, &class_oid, X_LOCK, LK_UNCOND_LOCK), LK_GRANTED);
  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr, UPDATE_INPLACE_OLD_MVCCID), NO_ERROR);
  commit_current_tran ();

  const std::string after = latest_record (oid);
  EXPECT_FALSE (same_chain (oos_column_of (after, attr_c1), oos_column_of (before, attr_c1)));
  EXPECT_FALSE (same_chain (oos_column_of (after, attr_c2), oos_column_of (before, attr_c2)));
  EXPECT_EQ (value_of (oid, after, attr_k), "1");
  EXPECT_EQ (value_of (oid, after, attr_c1), C1_VALUE);
  EXPECT_EQ (value_of (oid, after, attr_c2), C2_VALUE);
}

/* ALTER ... CHANGE rewrites each row in place and reclaims the chains it drops at commit without waiting for
 * snapshots, a chain the row's last UPDATE reused from the version before it included. The rewrite also drops the
 * row's link to its older versions, so a snapshot older than that UPDATE cannot reach the version whose chain was
 * reclaimed: it reads the row as it would had the UPDATE written new chains. Keeping the link would hand that
 * snapshot the reclaimed chain. */
TEST_F (OosUpdateReuse, OldSnapshotAcrossInPlaceRewriteOfReusedChainMatchesNewChains)
{
  open_table ("dba.t_reuse_inplace_snapshot");
  auto alter_change = [&] (OID &oid)
  {
    /* as xlocator_upgrade_instances_domain does: SCH_M on the class, vacuum below the oldest snapshot, then the row */
    ASSERT_EQ (lock_object (thread_p, &class_oid, oid_Root_class_oid, SCH_M_LOCK, LK_UNCOND_LOCK), LK_GRANTED);
    HEAP_SCANCACHE scan_cache;
    ASSERT_EQ (heap_scancache_start_modify (thread_p, &scan_cache, &hfid, &class_oid, SINGLE_ROW_UPDATE, NULL),
	       NO_ERROR);
    LOG_FIND_CURRENT_TDES (thread_p)->lock_global_oldest_visible_mvccid ();
    const int error = heap_vacuum_all_objects (thread_p, &scan_cache, log_Gl.mvcc_table.get_global_oldest_visible ());
    heap_scancache_end_modify (thread_p, &scan_cache);
    ASSERT_EQ (error, NO_ERROR);
    ASSERT_EQ (lock_object (thread_p, &oid, &class_oid, X_LOCK, LK_UNCOND_LOCK), LK_GRANTED);
    const int k = 2;
    ASSERT_EQ (update_row (oid, &k, nullptr, nullptr, UPDATE_INPLACE_OLD_MVCCID), NO_ERROR);
  };

  snapshot_read reused, rewritten;
  ASSERT_NO_FATAL_FAILURE (read_across_later_write (1, true, alter_change, reused));
  ASSERT_NO_FATAL_FAILURE (read_across_later_write (2, false, alter_change, rewritten));

  EXPECT_EQ (reused.error, NO_ERROR) << "the snapshot read a chain the rewrite reclaimed";
  EXPECT_EQ (rewritten.error, NO_ERROR);
  EXPECT_EQ (reused.scan, rewritten.scan);
  EXPECT_EQ (reused.k, rewritten.k);
  EXPECT_EQ (reused.c1, rewritten.c1);
  EXPECT_EQ (reused.c2, rewritten.c2);
}

/* An UPDATE that moves a row to another partition, as ALTER ... PARTITION BY does to every row, deletes it MVCC
 * style, so vacuum reclaims the chains of the deleted version only once no snapshot can read it or the version
 * before it, which shares them. */
TEST_F (OosUpdateReuse, OldSnapshotReadsTheReusedChainAfterTheRowIsDeleted)
{
  open_table ("dba.t_reuse_delete_snapshot");
  auto delete_row = [&] (OID &oid)
  {
    lock_class ();
    ASSERT_EQ (force (oid, LC_FLUSH_DELETE, SINGLE_ROW_DELETE, NULL, 0), NO_ERROR);
  };

  snapshot_read seen;
  ASSERT_NO_FATAL_FAILURE (read_across_later_write (1, true, delete_row, seen));
  EXPECT_EQ (seen.error, NO_ERROR);
  EXPECT_EQ (seen.scan, S_SUCCESS);
  EXPECT_EQ (seen.k, "0");
  EXPECT_EQ (seen.c1, C1_VALUE);
  EXPECT_EQ (seen.c2, C2_VALUE);
}

/* An increment recomputes only its own column; the OOS columns the statement does not assign keep their chains. */
TEST_F (OosUpdateReuse, IncrementReusesTheOtherColumnsChains)
{
  open_table ("dba.t_reuse_incr");
  OID oid = insert_row (1);
  commit_current_tran ();
  const std::string before = latest_record (oid);

  lock_class ();
  ASSERT_EQ (heap_attrinfo_clear_dbvalues (&attr_info), NO_ERROR);
  HEAP_ATTRVALUE *k_value = heap_attrvalue_locate (attr_k, &attr_info);
  ASSERT_NE (k_value, nullptr);
  k_value->do_increment = 1;
  scope_exit reset_increment ([&] () noexcept
  {
    k_value->do_increment = 0;
  });
  ATTR_ID incremented = attr_k;
  ASSERT_EQ (force (oid, LC_FLUSH_UPDATE, SINGLE_ROW_UPDATE, &incremented, 1), NO_ERROR);
  commit_current_tran ();

  const std::string after = latest_record (oid);
  EXPECT_EQ (value_of (oid, after, attr_k), "1");
  EXPECT_EQ (oos_column_of (after, attr_c1).stub, oos_column_of (before, attr_c1).stub);
  EXPECT_EQ (oos_column_of (after, attr_c2).stub, oos_column_of (before, attr_c2).stub);
}

class OosUpdateReuseSetupRow : public OosUpdateReuse
{
  protected:
    /* The one row the setup script inserted into class (a partition, for a partitioned table). */
    bool find_setup_row (const char *name, OID &oid)
    {
      OID found_class = OID_INITIALIZER;
      HFID found_hfid = HFID_INITIALIZER;
      if (xlocator_find_class_oid (thread_p, name, &found_class, NULL_LOCK) != LC_CLASSNAME_EXIST
	  || heap_get_class_hfid (thread_p, &found_class, &found_hfid, NULL) != NO_ERROR)
	{
	  er_clear ();
	  return false;
	}
      HEAP_SCANCACHE scan_cache;
      EXPECT_EQ (heap_scancache_start (thread_p, &scan_cache, &found_hfid, &found_class, false,
				       logtb_get_mvcc_snapshot (thread_p)), NO_ERROR);
      RECDES recdes = RECDES_INITIALIZER;
      OID_SET_NULL (&oid);
      SCAN_CODE scan = heap_next (thread_p, &found_hfid, &found_class, &oid, &recdes, &scan_cache, PEEK,
				  HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      (void) heap_scancache_end (thread_p, &scan_cache);
      if (scan != S_SUCCESS)
	{
	  er_clear ();
	  return false;
	}
      open_table (name);
      return true;
    }
};

/* A row written before its class gained a column is in an older representation, where a column can lie
 * elsewhere, so an UPDATE writes new chains. */
TEST_F (OosUpdateReuseSetupRow, OlderRepresentationWritesNewChains)
{
  OID oid;
  ASSERT_TRUE (find_setup_row ("dba.t_reuse_repr", oid));
  const std::string before = latest_record (oid);
  RECDES before_view = { (int) before.size (), (int) before.size (), REC_HOME, const_cast<char *> (before.data ()) };
  ASSERT_NE (or_rep_id (&before_view), attr_info.last_classrepr->id) << "the setup row must predate the column";

  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr), NO_ERROR);
  commit_current_tran ();

  const std::string after = latest_record (oid);
  EXPECT_FALSE (same_chain (oos_column_of (after, attr_c1), oos_column_of (before, attr_c1)));
  EXPECT_FALSE (same_chain (oos_column_of (after, attr_c2), oos_column_of (before, attr_c2)));
  EXPECT_EQ (value_of (oid, after, attr_k), "1");
  EXPECT_EQ (value_of (oid, after, attr_extra), "7");
  EXPECT_EQ (value_of (oid, after, attr_c1), C1_VALUE);
  EXPECT_EQ (value_of (oid, after, attr_c2), C2_VALUE);
}

/* An UPDATE of a partitioned class may move the row to another partition's heap and OOS file, so it writes new
 * chains. */
TEST_F (OosUpdateReuseSetupRow, PartitionedClassWritesNewChains)
{
  OID oid;
  ASSERT_TRUE (find_setup_row ("dba.t_reuse_part__p__p0", oid) || find_setup_row ("dba.t_reuse_part__p__p1", oid));
  const std::string before = latest_record (oid);

  const int k = 1;
  ASSERT_EQ (update_row (oid, &k, nullptr, nullptr, UPDATE_INPLACE_NONE, DB_PARTITIONED_CLASS), NO_ERROR);
  commit_current_tran ();

  const std::string after = latest_record (oid);
  EXPECT_FALSE (same_chain (oos_column_of (after, attr_c1), oos_column_of (before, attr_c1)));
  EXPECT_FALSE (same_chain (oos_column_of (after, attr_c2), oos_column_of (before, attr_c2)));
  EXPECT_EQ (value_of (oid, after, attr_k), "1");
  EXPECT_EQ (value_of (oid, after, attr_c1), C1_VALUE);
  EXPECT_EQ (value_of (oid, after, attr_c2), C2_VALUE);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  return RUN_ALL_TESTS ();
}
