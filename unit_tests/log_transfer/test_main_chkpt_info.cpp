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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "client_credentials.hpp"
#define private public
#include "log_checkpoint_info.hpp"
#undef private
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_record.hpp"
#include "mem_block.hpp"
#include "thread_entry.hpp"
#include "system_parameter.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <random>
#include <vector>

using namespace cublog;

class test_env_chkpt
{
  public:
    test_env_chkpt ();
    test_env_chkpt (int size_trans, int size_sysops);
    ~test_env_chkpt ();

    LOG_LSA generate_log_lsa();
    LOG_TDES *generate_tdes (int index);
    LOG_2PC_GTRINFO generate_2pc_gtrinfo();
    LOG_2PC_COORDINATOR *generate_2pc_coordinator();
    CLIENTIDS generate_client (int index);
    LOG_INFO_CHKPT_TRANS generate_log_info_chkpt_trans();
    LOG_INFO_CHKPT_SYSOP generate_log_info_chkpt_sysop();
    std::vector<LOG_LSA> used_logs;
    void generate_tran_table();

    static constexpr int MAX_RAND = 32700;
    static void require_equal (checkpoint_info before, checkpoint_info after);

    checkpoint_info before;
    checkpoint_info after;

};

TEST_CASE ("Test pack/unpack checkpoint_info class 1", "")
{
  test_env_chkpt env {100, 100};

  cubpacking::packer pac;
  size_t size = env.before.get_packed_size (pac, 0);
  char buffer[size];
  pac.set_buffer (buffer, size);
  env.before.pack (pac);

  cubpacking::unpacker unpac;
  unpac.set_buffer (buffer, size);
  env.after.unpack (unpac);

  env.require_equal (env.before, env.after);
}

TEST_CASE ("Test pack/unpack checkpoint_info class 2", "")
{
  test_env_chkpt env {0, 100};

  cubpacking::packer pac;
  size_t size = env.before.get_packed_size (pac, 0);
  char buffer[size];
  pac.set_buffer (buffer, size);
  env.before.pack (pac);

  cubpacking::unpacker unpac;
  unpac.set_buffer (buffer, size);
  env.after.unpack (unpac);

  env.require_equal (env.before, env.after);
}

TEST_CASE ("Test pack/unpack checkpoint_info class 3", "")
{
  test_env_chkpt env {220, 80};

  cubpacking::packer pac;
  size_t size = env.before.get_packed_size (pac, 0);
  size += env.before.get_packed_size (pac, size);
  char buffer[size];
  pac.set_buffer (buffer, size);
  env.before.pack (pac);
  env.before.pack (pac);

  cubpacking::unpacker unpac;
  unpac.set_buffer (buffer, size);
  env.after.unpack (unpac);
  env.require_equal (env.before, env.after);

  checkpoint_info after_2;

  after_2.unpack (unpac);

  env.require_equal (env.after, after_2);

}

TEST_CASE ("Test load and recovery on empty tran table", "")
{
  test_env_chkpt env;
  LOG_LSA smallest_lsa;
  LOG_LSA star_lsa;
  THREAD_ENTRY thd;

  env.after.load_trantable_snapshot (&thd, smallest_lsa);
  env.after.recovery_analysis (&thd, star_lsa);
  env.after.recovery_2pc_analysis (&thd);

}

std::map<TRANID, log_tdes *> tran_map;
int afterCount;

int
search_for_id (int id)
{
  for (int i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      if (log_Gl.trantable.all_tdes[i]->trid == id)
	{
	  return 1;
	}
    }
  return 0;
}

void
fullCompareTDES (log_tdes *td1, log_tdes *td2)
{
  REQUIRE (td1->head_lsa == td2->head_lsa);
  REQUIRE (td1->tail_lsa == td2->tail_lsa);
  REQUIRE (td1->undo_nxlsa == td2->undo_nxlsa);
  REQUIRE (td1->posp_nxlsa == td2->posp_nxlsa);
  REQUIRE (td1->savept_lsa == td2->savept_lsa);
  REQUIRE (td1->tail_topresult_lsa == td2->tail_topresult_lsa);
}

void
check_recovery (checkpoint_info obj)
{
  std::map<TRANID, log_tdes *>::iterator itr;
  for (itr = tran_map.begin(); itr != tran_map.end(); ++itr)
    {
      REQUIRE (itr->first != NULL_TRANID);
    }

  for (int i = 1; i < log_Gl.trantable.num_total_indices; i++)
    {
      LOG_TDES *tdes = log_Gl.trantable.all_tdes[i];
      std::map<TRANID, log_tdes *>::const_iterator tdes_after = tran_map.find (tdes->trid);

      if (tdes->trid != NULL && tdes->tail_lsa == NULL_LSA)
	{
	  REQUIRE (tdes_after == tran_map.end());
	  continue;
	}
      REQUIRE (tdes_after != tran_map.end());
      if (tdes->state == TRAN_ACTIVE || tdes->state == TRAN_UNACTIVE_ABORTED)
	{
	  REQUIRE (tdes_after->second->state == TRAN_UNACTIVE_UNILATERALLY_ABORTED);
	  continue;
	}
      REQUIRE (tdes_after->second->state == tdes->state);
      fullCompareTDES (tdes, tdes_after->second);
    }
}
int
numberOf2pc()
{
  int count = 0;
  for (int i = 1; i < log_Gl.trantable.num_total_indices; i++)
    {
      if (LOG_ISTRAN_2PC (log_Gl.trantable.all_tdes[i]))
	{
	  count ++;
	}
    }
  return count;
}

TEST_CASE ("Test load and recovery on 100 tran table entries", "")
{
  test_env_chkpt env;
  LOG_LSA smallest_lsa;
  LOG_LSA start_lsa;
  THREAD_ENTRY thd;

  env.generate_tran_table();
  int count = numberOf2pc();
  env.after.load_trantable_snapshot (&thd, smallest_lsa);
  env.after.recovery_analysis (&thd, start_lsa);
  env.after.recovery_2pc_analysis (&thd);
  check_recovery (env.after);
  REQUIRE (count == afterCount);

}

test_env_chkpt::test_env_chkpt ()
{
}

test_env_chkpt::test_env_chkpt (int size_trans, int size_sysops)
{
  std::srand (time (0));
  before.m_start_redo_lsa = this->generate_log_lsa();
  before.m_snapshot_lsa = this->generate_log_lsa();

  LOG_INFO_CHKPT_TRANS chkpt_trans_to_add;
  LOG_INFO_CHKPT_SYSOP chkpt_sysop_to_add;

  for (int i = 0; i < size_trans; i++)
    {
      chkpt_trans_to_add = generate_log_info_chkpt_trans();
      before.m_trans.push_back (chkpt_trans_to_add);
    }

  for (int i = 0; i < size_sysops; i++)
    {
      chkpt_sysop_to_add = generate_log_info_chkpt_sysop();
      before.m_sysops.push_back (chkpt_sysop_to_add);
    }

  before.m_has_2pc = std::rand() % 2;
}

test_env_chkpt::~test_env_chkpt ()
{
}

LOG_INFO_CHKPT_TRANS
test_env_chkpt::generate_log_info_chkpt_trans()
{
  LOG_INFO_CHKPT_TRANS chkpt_trans;

  chkpt_trans.isloose_end = std::rand() % 2; // can be true or false
  chkpt_trans.trid        = std::rand() % MAX_RAND;
  chkpt_trans.state       = static_cast<TRAN_STATE> (std::rand() % TRAN_UNACTIVE_UNKNOWN);

  chkpt_trans.head_lsa    = generate_log_lsa();
  chkpt_trans.tail_lsa    = generate_log_lsa();
  chkpt_trans.undo_nxlsa  = generate_log_lsa();

  chkpt_trans.posp_nxlsa  = generate_log_lsa();
  chkpt_trans.savept_lsa  = generate_log_lsa();
  chkpt_trans.tail_topresult_lsa  = generate_log_lsa();
  chkpt_trans.start_postpone_lsa  = generate_log_lsa();

  int length = std::rand() % LOG_USERNAME_MAX;

  length = std::min (5, length);

  for (int i = 0; i < length; i++)
    {
      chkpt_trans.user_name[i] = 'A' + std::rand() % 20;
    }

  return chkpt_trans;
}

LOG_INFO_CHKPT_SYSOP
test_env_chkpt::generate_log_info_chkpt_sysop()
{
  LOG_INFO_CHKPT_SYSOP chkpt_sysop;

  chkpt_sysop.trid                      = std::rand() % MAX_RAND;
  chkpt_sysop.sysop_start_postpone_lsa  = generate_log_lsa();
  chkpt_sysop.atomic_sysop_start_lsa    = generate_log_lsa();

  return chkpt_sysop;
}

LOG_LSA
test_env_chkpt::generate_log_lsa()
{
  return log_lsa (std::rand() % MAX_RAND, std::rand() % MAX_RAND);
}

void
test_env_chkpt::require_equal (checkpoint_info before, checkpoint_info after)
{
  REQUIRE (before.m_start_redo_lsa == after.m_start_redo_lsa);
  REQUIRE (before.m_snapshot_lsa == after.m_snapshot_lsa);

  REQUIRE (before.m_trans == after.m_trans);
  REQUIRE (before.m_sysops == after.m_sysops);

  REQUIRE (before.m_has_2pc == after.m_has_2pc);
}

LOG_2PC_GTRINFO
test_env_chkpt::generate_2pc_gtrinfo()
{
  log_2pc_gtrinfo pc;
  pc.info_length = rand() % MAX_RAND;
  pc.info_data = malloc (pc.info_length);

  return pc;
}

LOG_2PC_COORDINATOR *
test_env_chkpt::generate_2pc_coordinator()
{
  log_2pc_coordinator *pc = new log_2pc_coordinator();
  pc->num_particps = rand() % MAX_RAND;
  pc->particp_id_length = rand() % MAX_RAND;

  return pc;
}

CLIENTIDS
test_env_chkpt::generate_client (int index)
{
  clientids clnt;
  char number[25];

  clnt.client_type = static_cast<db_client_type> (1);
  sprintf (number, "client_%d", index);
  clnt.client_info = (number);
  sprintf (number, "db_user_%d", index);
  clnt.db_user = (number);
  sprintf (number, "database_%d", index);
  clnt.program_name = (number);
  sprintf (number, "login_%d", index);
  clnt.login_name = (number);
  sprintf (number, "host_%d", index);
  clnt.host_name = (number);
  clnt.process_id = rand() % MAX_RAND;

  return clnt;
}

LOG_TDES *
test_env_chkpt::generate_tdes (int index)
{
  LOG_TDES *tdes = new log_tdes();
  tdes->trid = index + 1;
  tdes->tran_index = index + 1;

  tdes->tail_lsa = generate_log_lsa();
  tdes->isloose_end = std::rand() % 2;
  tdes->head_lsa = generate_log_lsa();
  tdes->state    = static_cast<TRAN_STATE> (std::rand() % TRAN_UNACTIVE_UNKNOWN);

  tdes->undo_nxlsa = generate_log_lsa();
  tdes->posp_nxlsa = generate_log_lsa();
  tdes->savept_lsa = generate_log_lsa();
  tdes->tail_topresult_lsa = generate_log_lsa();
  tdes->rcv.tran_start_postpone_lsa = generate_log_lsa();
  tdes->wait_msecs = rand() % MAX_RAND;
  tdes->client_id  = rand() % MAX_RAND;
  tdes->gtrid      = rand() % MAX_RAND;

  tdes->num_transient_classnames = rand() % MAX_RAND;
  tdes->num_repl_records         = rand() % MAX_RAND;
  tdes->cur_repl_record          = rand() % MAX_RAND;
  tdes->append_repl_recidx       = rand() % MAX_RAND;
  tdes->fl_mark_repl_recidx      = rand() % MAX_RAND;
  tdes->repl_insert_lsa          = generate_log_lsa();
  tdes->repl_update_lsa          = generate_log_lsa();

  tdes->client  = generate_client (index);
  tdes->gtrinfo = generate_2pc_gtrinfo();
  tdes->coord   = generate_2pc_coordinator();

  return tdes;
}


void
test_env_chkpt::generate_tran_table()
{
  log_Gl.trantable.num_total_indices = 100;

  int size = log_Gl.trantable.num_total_indices * sizeof (*log_Gl.trantable.all_tdes);
  log_Gl.trantable.all_tdes = (LOG_TDES **) malloc (size);

  for (int i = 1; i < log_Gl.trantable.num_total_indices; i++)
    {
      log_Gl.trantable.all_tdes[i] = generate_tdes (i);
    }
}

//
// Definitions of CUBRID stuff that is used and cannot be included
//

#include "log_impl.h"
#include "dbtype_def.h"
#include "packer.hpp"
#include "memory_alloc.h"
#include "object_representation.h"
#include "object_representation_constants.h"
#define MAX_SMALL_STRING_SIZE 255
#define LARGE_STRING_CODE 0xff

std::mutex systb_Mutex;
std::map<TRANID, log_tdes *> systb_System_tdes;

//unused stuff needed by the linker
log_global log_Gl;

int
or_packed_value_size (const DB_VALUE *value, int collapse_null, int include_domain, int include_domain_classoids)
{
  return 0;
}

char *
or_pack_value (char *buf, DB_VALUE *value)
{
  return nullptr;
}

char *
or_unpack_value (const char *buf, DB_VALUE *value)
{
  return nullptr;
}

bool
prm_get_bool_value (PARAM_ID prmid)
{
  return false;
}

LOG_PRIOR_NODE *
prior_lsa_alloc_and_copy_data (THREAD_ENTRY *thread_p, LOG_RECTYPE rec_type, LOG_RCVINDEX rcvindex,
			       LOG_DATA_ADDR *addr, int ulength, const char *udata, int rlength, const char *rdata)
{
  assert (false);
}

LOG_LSA
prior_lsa_next_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, log_tdes *tdes)
{
  assert (false);
}

void
_er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
  assert (false);
}

int
logtb_reflect_global_unique_stats_to_btree (THREAD_ENTRY *thread_p)
{
  assert (false);
}

int
pgbuf_flush_checkpoint (THREAD_ENTRY *thread_p, const LOG_LSA *flush_upto_lsa, const LOG_LSA *prev_chkpt_redo_lsa,
			LOG_LSA *smallest_lsa, int *flushed_page_cnt)
{
  assert (false);
}

int
fileio_synchronize_all (THREAD_ENTRY *thread_p, bool include_log)
{
  assert (false);
}

int
csect_exit (THREAD_ENTRY *thread_p, int cs_index)
{
  assert (true);
}

void
logpb_flush_pages_direct (THREAD_ENTRY *thread_p)
{
  assert (false);
}

LOG_TDES *
logtb_get_system_tdes (THREAD_ENTRY *thread_p)
{
  assert (false);
}

log_global::log_global ()
  : m_prior_recver (prior_info)
{
}

log_global::~log_global ()
{
}

namespace cublog
{
  prior_recver::prior_recver (log_prior_lsa_info &prior_lsa_info)
    : m_prior_lsa_info (prior_lsa_info)
  {
  }
  prior_recver::~prior_recver () = default;
}

mvcc_active_tran::mvcc_active_tran () = default;
mvcc_active_tran::~mvcc_active_tran () = default;
mvcc_trans_status::mvcc_trans_status () = default;
mvcc_trans_status::~mvcc_trans_status () = default;
mvcctable::mvcctable () = default;
mvcctable::~mvcctable () = default;

log_append_info::log_append_info () = default;
log_prior_lsa_info::log_prior_lsa_info () = default;

int
csect_enter (THREAD_ENTRY *thread_p, int cs_index, int wait_secs)
{
  assert (true);
}

void
log_system_tdes::map_all_tdes (const map_func &func)
{
  std::lock_guard<std::mutex> lg (systb_Mutex);
  for (auto &el : systb_System_tdes)
    {
      log_tdes *tdes = el.second;
      assert (tdes != NULL);
      func (*tdes);
    }
}
#define NUM_TOTAL_TRAN_INDICES log_Gl.trantable.num_total_indices

void
logtb_clear_tdes (THREAD_ENTRY *thread_p, LOG_TDES *tdes)
{
  int i, j;
  DB_VALUE *dbval;
  HL_HEAPID save_heap_id;

  tdes->isloose_end = false;
  tdes->state = TRAN_ACTIVE;
  LSA_SET_NULL (&tdes->head_lsa);
  LSA_SET_NULL (&tdes->tail_lsa);
  LSA_SET_NULL (&tdes->undo_nxlsa);
  LSA_SET_NULL (&tdes->posp_nxlsa);
  LSA_SET_NULL (&tdes->savept_lsa);
  LSA_SET_NULL (&tdes->topop_lsa);
  LSA_SET_NULL (&tdes->tail_topresult_lsa);
  tdes->topops.last = -1;
  tdes->gtrid = LOG_2PC_NULL_GTRID;
  tdes->gtrinfo.info_length = 0;
  tdes->cur_repl_record = 0;
  tdes->append_repl_recidx = -1;
  tdes->fl_mark_repl_recidx = -1;
  LSA_SET_NULL (&tdes->repl_insert_lsa);
  LSA_SET_NULL (&tdes->repl_update_lsa);
  tdes->first_save_entry = NULL;
  tdes->query_timeout = 0;
  tdes->query_start_time = 0;
  tdes->tran_start_time = 0;
  tdes->waiting_for_res = NULL;
  tdes->tran_abort_reason = TRAN_NORMAL;
  tdes->num_exec_queries = 0;
  tdes->suppress_replication = 0;
  tdes->has_deadlock_priority = false;

  tdes->num_log_records_written = 0;

  LSA_SET_NULL (&tdes->rcv.tran_start_postpone_lsa);
  LSA_SET_NULL (&tdes->rcv.sysop_start_postpone_lsa);
  LSA_SET_NULL (&tdes->rcv.atomic_sysop_start_lsa);
  LSA_SET_NULL (&tdes->rcv.analysis_last_aborted_sysop_lsa);
  LSA_SET_NULL (&tdes->rcv.analysis_last_aborted_sysop_start_lsa);
}

log_tdes *
systdes_create_tdes ()
{
  log_tdes *tdes = new log_tdes ();
  return tdes;
}

log_tdes *
log_system_tdes::rv_get_tdes (TRANID trid)
{
  auto it = systb_System_tdes.find (trid);
  if (it != systb_System_tdes.end ())
    {
      return it->second;
    }
  else
    {
      return NULL;
    }
}

LOG_TDES *
logtb_rv_find_allocate_tran_index (THREAD_ENTRY *thread_p, TRANID trid, const LOG_LSA *log_lsa)
{
  LOG_TDES *tdes = NULL;		/* Transaction descriptor */

  assert (trid != NULL_TRANID);

  for (int i = 1; i < NUM_TOTAL_TRAN_INDICES; i++)
    {
      tdes = log_Gl.trantable.all_tdes[i];
      if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->trid == trid)
	{
	  LSA_COPY (&tdes->head_lsa, log_lsa);
	  tran_map.insert (std::pair<TRANID, LOG_TDES *> (trid, tdes));
	  return tdes;
	}
    }
  assert (false);
  return tdes;
}

void
logpb_fatal_error (THREAD_ENTRY *thread_p, bool log_exit, const char *file_name, const int lineno, const char *fmt,
		   ...)
{
  assert (false);
}

static const int LOG_TOPOPS_STACK_INCREMENT = 3;	/* No more than 3 nested top system operations */
void *
logtb_realloc_topops_stack (LOG_TDES *tdes, int num_elms)
{
  size_t size;
  void *newptr;

  if (num_elms < LOG_TOPOPS_STACK_INCREMENT)
    {
      num_elms = LOG_TOPOPS_STACK_INCREMENT;
    }

  size = tdes->topops.max + num_elms;
  size = size * sizeof (*tdes->topops.stack);

  newptr = (LOG_TOPOPS_ADDRESSES *) realloc (tdes->topops.stack, size);
  if (newptr != NULL)
    {
      tdes->topops.stack = (LOG_TOPOPS_ADDRESSES *) newptr;
      if (tdes->topops.max == 0)
	{
	  tdes->topops.last = -1;
	}
      tdes->topops.max += num_elms;
    }
  else
    {
      return NULL;
    }
  return tdes->topops.stack;
}

PGLENGTH
db_log_page_size (void)
{
  return 16384;
}

int
logtb_get_current_tran_index (void)
{
  return 1;
}

int
log_read_sysop_start_postpone (THREAD_ENTRY *thread_p, LOG_LSA *log_lsa, LOG_PAGE *log_page, bool with_undo_data,
			       LOG_REC_SYSOP_START_POSTPONE *sysop_start_postpone, int *undo_buffer_size,
			       char **undo_buffer, int *undo_size, char **undo_data)
{
  assert (true);
  return NO_ERROR;
}

int
logtb_find_tran_index (THREAD_ENTRY *thread_p, TRANID trid)
{
  int i;
  int tran_index = NULL_TRAN_INDEX;	/* The transaction index */
  LOG_TDES *tdes;		/* Transaction descriptor */

  assert (trid != NULL_TRANID);

  /* Avoid searching as much as possible */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL || tdes->trid != trid)
    {
      tran_index = NULL_TRAN_INDEX;
      /* Search the transaction table for such transaction */
      for (i = 0; i < NUM_TOTAL_TRAN_INDICES; i++)
	{
	  tdes = log_Gl.trantable.all_tdes[i];
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->trid == trid)
	    {
	      tran_index = i;
	      break;
	    }
	}
    }

  return tran_index;
}

void
log_2pc_recovery_analysis_info (THREAD_ENTRY *thread_p, log_tdes *tdes, LOG_LSA *upto_chain_lsa)
{
  afterCount++;
  assert (true);
}

namespace cubmem
{

  void
  standard_alloc (block &b, size_t size)
  {
    if (b.ptr == NULL || b.dim == 0)
      {
	b.ptr = new char[size];
	b.dim = size;
      }
    else if (size <= b.dim)
      {
	// do not reduce
      }
    else
      {
	char *new_ptr = new char[size];
	std::memcpy (new_ptr, b.ptr, b.dim);

	delete[] b.ptr;

	b.ptr = new_ptr;
	b.dim = size;
      }
  }

  void
  standard_dealloc (block &b)
  {
    delete [] b.ptr;
    b.ptr = NULL;
    b.dim = 0;
  }


  const struct block_allocator STANDARD_BLOCK_ALLOCATOR
  {
    standard_alloc, standard_dealloc
  };
  block_allocator::block_allocator (const alloc_func &alloc_f, const dealloc_func &dealloc_f)
    : m_alloc_f (alloc_f)
    , m_dealloc_f (dealloc_f)
  {
  }
}

mvcc_info::mvcc_info ()
  : snapshot ()
  , id (MVCCID_NULL)
  , recent_snapshot_lowest_active_mvccid (MVCCID_NULL)
  , sub_ids ()
{
}

mvcc_snapshot::mvcc_snapshot ()
  : lowest_active_mvccid (MVCCID_NULL)
  , highest_completed_mvccid (MVCCID_NULL)
  , m_active_mvccs ()
  , snapshot_fnc (NULL)
  , valid (false)
{
}
