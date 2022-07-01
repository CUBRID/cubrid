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

#include "log_append.hpp"

#include "fake_packable_object.hpp"

#include <algorithm>
#include <array>
#include <random>

#undef strlen

class test_env
{
  public:
    test_env ();
    ~test_env ();

    template <typename DataType>
    void log_append_undoredo (const DataType &data, const std::string &undo, const std::string &redo);
    template <typename DataType>
    void log_append_redo (const DataType &data, const std::string &redo);
    template <typename DataType>
    void log_append_undo (const DataType &data, const std::string &undo);
    template <typename DataType>
    void log_append_noundoredo (const DataType &data);

    void serialize_deserialize_list ();

  private:
    static void free_list (log_prior_node *listp);
    static void require_equal (log_prior_node *left, log_prior_node *rite);
    static void require_mem_equal (const char *memleft, const char *memrite, size_t memsz);

    log_prior_lsa_info m_prior_lsa_info;
};

std::string
random_data (size_t min_size, size_t max_size)
{
  static std::random_device r;
  std::seed_seq seed2 { r (), r (), r (), r (), r (), r (), r (), r () };
  static std::mt19937 mt (seed2);

  std::uniform_int_distribution<> szdist ((int) min_size, (int) max_size);
  size_t string_size = szdist (mt);
  std::string str;
  str.resize (string_size);

  std::uniform_int_distribution<> chdist (0, 255);
  std::generate (str.begin (), str.end (), std::bind (chdist, mt));

  return str;
}

TEST_CASE ("Test prior list serialization/deserialization", "")
{
  test_env env;

  std::uint64_t data1 = 0;
  int data2 = 0;

  std::array<std::string, 5> randstr =
  {
    random_data (10, 100),
    random_data (1, 5),
    random_data (5, 50),
    random_data (20, 30),
    random_data (100, 200)
  };

  // serialize/deserialize one node
  env.log_append_noundoredo (++data1);
  env.serialize_deserialize_list ();

  // serialize/deserialize each undo/redo combo
  env.log_append_noundoredo (++data2);
  env.log_append_undoredo (++data1, randstr[0], randstr[0]);
  env.log_append_redo (++data1, randstr[1]);
  env.log_append_undo (++data2, randstr[2]);
  env.log_append_undoredo (++data2, randstr[3], randstr[4]);
  env.serialize_deserialize_list ();
}

test_env::test_env ()
{
  m_prior_lsa_info.prior_lsa = { 0, 0 };
  m_prior_lsa_info.prior_list_header = nullptr;
  m_prior_lsa_info.prior_list_tail = nullptr;
}

test_env::~test_env ()
{
  free_list (m_prior_lsa_info.prior_list_header);
}

template <typename DataType>
void
test_env::log_append_undoredo (const DataType &data, const std::string &undo, const std::string &redo)
{
  log_prior_node *nodep = (log_prior_node *) malloc (sizeof (log_prior_node));
  assert (nodep != nullptr);

  static int trid = 0;
  nodep->log_header.trid = ++trid;
  nodep->log_header.type = (LOG_RECTYPE) (trid % 3);
  nodep->tde_encrypted = (trid % 10) != 0;

  nodep->data_header_length = sizeof (data);
  nodep->data_header = (char *) malloc (nodep->data_header_length);
  std::memcpy (nodep->data_header, &data, nodep->data_header_length);

  if (!undo.empty ())
    {
      nodep->ulength = (int) undo.size ();
      nodep->udata = (char *) malloc (nodep->ulength);
      std::memcpy (nodep->udata, undo.c_str (), nodep->ulength);
    }
  else
    {
      nodep->ulength = 0;
      nodep->udata = nullptr;
    }

  if (!redo.empty ())
    {
      nodep->rlength = (int) redo.size ();
      nodep->rdata = (char *) malloc (nodep->rlength);
      std::memcpy (nodep->rdata, redo.c_str (), nodep->rlength);
    }
  else
    {
      nodep->rlength = 0;
      nodep->rdata = nullptr;
    }

  nodep->start_lsa = m_prior_lsa_info.prior_lsa;

  m_prior_lsa_info.prior_lsa.pageid++;

  nodep->log_header.back_lsa = { nodep->start_lsa.pageid - 1, 0 };
  nodep->log_header.forw_lsa = m_prior_lsa_info.prior_lsa;
  nodep->log_header.prev_tranlsa = nodep->log_header.back_lsa;

  // append to list
  if (m_prior_lsa_info.prior_list_header == nullptr)
    {
      m_prior_lsa_info.prior_list_header = nodep;
      m_prior_lsa_info.prior_list_tail = nodep;
    }
  else
    {
      assert (m_prior_lsa_info.prior_list_tail != nullptr);
      m_prior_lsa_info.prior_list_tail->next = nodep;
      m_prior_lsa_info.prior_list_tail = nodep;
    }
  nodep->next = nullptr;
}

template <typename DataType>
void
test_env::log_append_redo (const DataType &data, const std::string &redo)
{
  log_append_undoredo (data, std::string (), redo);
}

template <typename DataType>
void
test_env::log_append_undo (const DataType &data, const std::string &undo)
{
  log_append_undoredo (data, undo, std::string ());
}

template <typename DataType>
void
test_env::log_append_noundoredo (const DataType &data)
{
  log_append_undoredo (data, std::string (), std::string ());
}

void
test_env::serialize_deserialize_list ()
{
  if (m_prior_lsa_info.prior_list_header == nullptr)
    {
      return;
    }

  // pull list
  log_prior_node *list_headp = m_prior_lsa_info.prior_list_header;
  m_prior_lsa_info.prior_list_header = nullptr;
  m_prior_lsa_info.prior_list_tail = nullptr;

  // serialize/deserialize
  std::string serialized = prior_list_serialize (list_headp);

  log_prior_node *deserialized_headp = nullptr;
  log_prior_node *deserialized_tailp = nullptr;

  prior_list_deserialize (serialized, deserialized_headp, deserialized_tailp);

  // compare lists
  log_prior_node *list_nodep = list_headp;
  log_prior_node *deserialized_nodep = deserialized_headp;
  while (list_nodep != nullptr)
    {
      REQUIRE (deserialized_nodep != nullptr);
      require_equal (list_nodep, deserialized_nodep);

      if (deserialized_nodep->next == nullptr)
	{
	  REQUIRE (deserialized_nodep == deserialized_tailp);
	}

      list_nodep = list_nodep->next;
      deserialized_nodep = deserialized_nodep->next;
    }

  free_list (list_headp);
  free_list (deserialized_headp);
}

void
test_env::free_list (log_prior_node *listp)
{
  log_prior_node *save_next = nullptr;
  for (log_prior_node *nodep = listp; nodep != nullptr; nodep = save_next)
    {
      save_next = nodep->next;

      if (nodep->data_header != nullptr)
	{
	  free (nodep->data_header);
	}
      if (nodep->udata != nullptr)
	{
	  free (nodep->udata);
	}
      if (nodep->rdata != nullptr)
	{
	  free (nodep->rdata);
	}
      free (nodep);
    }
}

void
test_env::require_equal (log_prior_node *left, log_prior_node *rite)
{
  REQUIRE (left->log_header.back_lsa == rite->log_header.back_lsa);
  REQUIRE (left->log_header.forw_lsa == rite->log_header.forw_lsa);
  REQUIRE (left->log_header.prev_tranlsa == rite->log_header.prev_tranlsa);
  REQUIRE (left->log_header.trid == rite->log_header.trid);

  REQUIRE (left->start_lsa == rite->start_lsa);
  REQUIRE (left->tde_encrypted == rite->tde_encrypted);

  REQUIRE (left->data_header_length == rite->data_header_length);
  require_mem_equal (left->data_header, rite->data_header, left->data_header_length);

  REQUIRE (left->ulength == rite->ulength);
  require_mem_equal (left->udata, rite->udata, left->ulength);

  REQUIRE (left->rlength == rite->rlength);
  require_mem_equal (left->rdata, rite->rdata, left->rlength);
}

void
test_env::require_mem_equal (const char *memleft, const char *memrite, size_t memsz)
{
  if (memsz == 0)
    {
      REQUIRE (memleft == nullptr);
      REQUIRE (memrite == nullptr);
    }
  const char *leftp = memleft;
  const char *ritep = memrite;
  for ( ; leftp < memleft + memsz; ++leftp, ++ritep)
    {
      REQUIRE (*leftp == *ritep);
    }
}

//
// Definitions of CUBRID stuff that is not used, but is needed by the linker
//

#include "error_manager.h"
#include "log_compress.h"
#include "perf_monitor.h"
#include "server_type.hpp"
#include "tde.h"
#include "vacuum.h"

TDE_CIPHER tde_Cipher;
log_global log_Gl;
pstat_global pstat_Global;
pstat_metadata pstat_Metadata[1];

PGLENGTH db_Io_page_size = IO_DEFAULT_PAGE_SIZE;
PGLENGTH db_Log_page_size = IO_DEFAULT_PAGE_SIZE;

PGLENGTH
db_io_page_size ()
{
  assert (false);
  return 0;
}

SERVER_TYPE
get_server_type ()
{
  assert (false);
  return SERVER_TYPE_TRANSACTION;
}

bool
is_active_transaction_server ()
{
  return true;
}

PGLENGTH
db_log_page_size ()
{
  assert (false);
  return 0;
}

size_t
logpb_get_memsize ()
{
  assert (false);
  return 0;
}

bool
log_zip (LOG_ZIP *log_zip, LOG_ZIP_SIZE_T length, const void *data)
{
  assert (false);
  return false;
}

bool
log_diff (LOG_ZIP_SIZE_T undo_length, const void *undo_data, LOG_ZIP_SIZE_T redo_length, void *redo_data)
{
  assert (false);
  return false;
}

bool
log_is_in_crash_recovery ()
{
  assert (false);
  return false;
}

void
log_wakeup_log_flush_daemon ()
{
  assert (false);
}

struct log_zip *
log_zip_alloc (LOG_ZIP_SIZE_T size)
{
  assert (false);
  return nullptr;
}

int
logpb_prior_lsa_append_all_list (THREAD_ENTRY *thread_p)
{
  assert (false);
  return NO_ERROR;
}

int
logtb_get_current_tran_index ()
{
  assert (false);
  return 0;
}

log_tdes *
logtb_get_system_tdes (THREAD_ENTRY *thread_p /* = NULL */)
{
  assert (false);
  return nullptr;
}

VPID *
pgbuf_get_vpid_ptr (PAGE_PTR pgptr)
{
  assert (false);
  return nullptr;
}

const char *
rv_rcvindex_string (LOG_RCVINDEX rcvindex)
{
  assert (false);
  return nullptr;
}

VACUUM_LOG_BLOCKID
vacuum_get_log_blockid (LOG_PAGEID pageid)
{
  assert (false);
  return VACUUM_NULL_LOG_BLOCKID;
}

void
vacuum_produce_log_block_data (THREAD_ENTRY *thread_p)
{
  assert (false);
}

void
LOG_CS_ENTER (THREAD_ENTRY *thread_p)
{
  assert (false);
}

void
LOG_CS_EXIT (THREAD_ENTRY *thread_p)
{
  assert (false);
}

namespace cublog
{
  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (meta);

  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (checkpoint_info);

  prior_recver::prior_recver (log_prior_lsa_info &prior_lsa_info)
    : m_prior_lsa_info (prior_lsa_info)
  {
  }
  prior_recver::~prior_recver () = default;
}

log_global::log_global ()
  : m_prior_recver (std::make_unique<cublog::prior_recver> (prior_info))
{
}
log_global::~log_global () = default;

mvcc_active_tran::mvcc_active_tran () = default;
mvcc_active_tran::~mvcc_active_tran () = default;

mvcc_trans_status::mvcc_trans_status () = default;
mvcc_trans_status::~mvcc_trans_status () = default;

mvcctable::mvcctable () = default;
mvcctable::~mvcctable () = default;

MVCCID
mvcctable::get_global_oldest_visible () const
{
  assert (false);
  return MVCCID_NULL;
}

bool
log_tdes::is_system_worker_transaction () const
{
  assert (false);
  return false;
}

bool
log_tdes::is_under_sysop () const
{
  assert (false);
  return false;
}

namespace cubthread
{
  entry &
  get_entry ()
  {
    entry *ent = nullptr;
    assert (false);
    return (*ent);
  }
}

bool
tde_is_loaded ()
{
  assert (false);
  return false;
}
