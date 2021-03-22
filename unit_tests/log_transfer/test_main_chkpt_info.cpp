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

#define private public
#include "log_checkpoint_info.hpp"
#undef private
#include "log_lsa.hpp"
#include "log_record.hpp"
#include "system_parameter.h"

#include <algorithm>
#include <vector>
#include <random>
#include <cstdlib>

using namespace cublog;

class test_env_chkpt
{
  public:
    test_env_chkpt ();
    test_env_chkpt (int size_trans, int size_sysops);
    ~test_env_chkpt ();

    LOG_LSA generate_log_lsa();
    LOG_INFO_CHKPT_TRANS generate_log_info_chkpt_trans();
    LOG_INFO_CHKPT_SYSOP generate_log_info_chkpt_sysop();
    std::vector<LOG_LSA> used_logs;

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

test_env_chkpt::test_env_chkpt () : test_env_chkpt (100, 100)
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

namespace cubpacking
{
  void
  packer::align (const size_t req_alignment)
  {
    m_ptr = PTR_ALIGN (m_ptr, req_alignment);
  }

  void
  unpacker::align (const size_t req_alignment)
  {
    m_ptr = PTR_ALIGN (m_ptr, req_alignment);
  }

  static void
  check_range (const char *ptr, const char *endptr, const size_t amount)
  {
    assert (ptr + amount <= endptr);
    if (ptr + amount > endptr)
      {
	abort ();
      }
  }

  size_t
  packer::get_packed_bigint_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, MAX_ALIGNMENT) - curr_offset + OR_BIGINT_SIZE;
  }

  void
  packer::pack_bigint (const std::int64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_PUT_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  void
  unpacker::unpack_bigint (std::int64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_GET_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  size_t
  packer::get_packed_short_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, SHORT_ALIGNMENT) - curr_offset + OR_SHORT_SIZE;
  }

  void
  packer::pack_short (const short value)
  {
    align (SHORT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_SHORT_SIZE);

    OR_PUT_SHORT (m_ptr, value);
    m_ptr += OR_SHORT_SIZE;
  }

  void
  unpacker::unpack_short (short &value)
  {
    align (SHORT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_SHORT_SIZE);

    value = OR_GET_SHORT (m_ptr);
    m_ptr += OR_SHORT_SIZE;
  }

  size_t
  packer::get_packed_int_size (size_t curr_offset)
  {
    return DB_ALIGN (curr_offset, INT_ALIGNMENT) - curr_offset + OR_INT_SIZE;
  }

  void
  packer::pack_int (const int value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    OR_PUT_INT (m_ptr, value);
    m_ptr += OR_INT_SIZE;
  }

  void
  unpacker::unpack_int (int &value)
  {
    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_INT_SIZE);

    value = OR_GET_INT (m_ptr);
    m_ptr += OR_INT_SIZE;
  }

  size_t
  packer::get_packed_c_string_size (const char *str, const size_t str_size, const size_t curr_offset)
  {
    size_t entry_size;

    if (str_size < MAX_SMALL_STRING_SIZE)
      {
	entry_size = OR_BYTE_SIZE + str_size;
      }
    else
      {
	entry_size = DB_ALIGN (OR_BYTE_SIZE, INT_ALIGNMENT) + OR_INT_SIZE + str_size;
      }

    return DB_ALIGN (curr_offset + entry_size, INT_ALIGNMENT) - curr_offset;
  }

  void
  packer::pack_c_string (const char *str, const size_t str_size)
  {
    if (str_size < MAX_SMALL_STRING_SIZE)
      {
	pack_small_string (str, str_size);
      }
    else
      {
	check_range (m_ptr, m_end_ptr, str_size + 1 + OR_INT_SIZE);

	OR_PUT_BYTE (m_ptr, LARGE_STRING_CODE);
	m_ptr++;

	pack_large_c_string (str, str_size);
      }
  }

  void
  unpacker::unpack_c_string (char *str, const size_t max_str_size)
  {
    size_t len = 0;

    unpack_string_size (len);

    if (len >= max_str_size)
      {
	assert (false);
	return;
      }
    if (len > 0)
      {
	std::memcpy (str, m_ptr, len);
	m_ptr += len;
      }

    str[len] = '\0';

    align (INT_ALIGNMENT);
  }

  void
  packer::pack_small_string (const char *string, const size_t str_size)
  {
    size_t len;

    if (str_size == 0)
      {
	len = strlen (string);
      }
    else
      {
	len = str_size;
      }

    if (len > MAX_SMALL_STRING_SIZE)
      {
	assert (false);
	pack_c_string (string, len);
	return;
      }

    check_range (m_ptr, m_end_ptr, len + 1);

    OR_PUT_BYTE (m_ptr, len);
    m_ptr += OR_BYTE_SIZE;
    if (len > 0)
      {
	std::memcpy (m_ptr, string, len);
	m_ptr += len;
      }

    align (INT_ALIGNMENT);
  }

  void
  packer::pack_large_c_string (const char *string, const size_t str_size)
  {
    size_t len;

    if (str_size == 0)
      {
	len = strlen (string);
      }
    else
      {
	len = str_size;
      }

    align (INT_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, len + OR_INT_SIZE);

    OR_PUT_INT (m_ptr, len);
    m_ptr += OR_INT_SIZE;

    std::memcpy (m_ptr, string, len);
    m_ptr += len;

    align (INT_ALIGNMENT);
  }

  size_t
  packer::get_packed_string_size (const std::string &str, const size_t curr_offset)
  {
    return get_packed_c_string_size (str.c_str (), str.size (), curr_offset);
  }

  void
  unpacker::unpack_string_size (size_t &len)
  {
    check_range (m_ptr, m_end_ptr, 1);
    len = OR_GET_BYTE (m_ptr);
    if (len == LARGE_STRING_CODE)
      {
	m_ptr++;

	align (OR_INT_SIZE);

	len = OR_GET_INT (m_ptr);
	m_ptr += OR_INT_SIZE;
      }
    else
      {
	m_ptr++;
      }
    if (len > 0)
      {
	check_range (m_ptr, m_end_ptr, len);
      }
  }

  size_t
  packer::get_packed_bool_size (size_t curr_offset)
  {
    return get_packed_int_size (curr_offset);
  }

  void
  unpacker::unpack_bigint (std::uint64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_GET_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  void
  unpacker::unpack_bool (bool &value)
  {
    int int_val;
    unpack_int (int_val);
    assert (int_val == 1 || int_val == 0);
    value = int_val != 0;
  }

  void
  packer::pack_bigint (const std::uint64_t &value)
  {
    align (MAX_ALIGNMENT);
    check_range (m_ptr, m_end_ptr, OR_BIGINT_SIZE);

    OR_PUT_INT64 (m_ptr, &value);
    m_ptr += OR_BIGINT_SIZE;
  }

  void
  packer::pack_bool (bool value)
  {
    pack_int (value ? 1 : 0);
  }

  void
  packer::set_buffer (char *storage, const size_t amount)
  {
    m_start_ptr = storage;
    m_ptr = storage;
    m_end_ptr = m_start_ptr + amount;
  }

  void
  unpacker::set_buffer (const char *storage, const size_t amount)
  {
    m_start_ptr = storage;
    m_ptr = storage;
    m_end_ptr = m_start_ptr + amount;
  }

  packer::packer (void)
  {
    // all pointers are initialized to NULL
  }
}

//unused stuff needed by the linker
log_global log_Gl;

bool
prm_get_bool_value (PARAM_ID prmid)
{
  return false;
}

const char *
clientids::get_db_user () const
{
  return nullptr;
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
  assert (false);
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
  assert (false);
}

void
log_system_tdes::map_all_tdes (const map_func &func)
{
  assert (false);
}
