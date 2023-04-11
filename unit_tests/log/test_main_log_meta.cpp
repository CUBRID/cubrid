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

// add checks on private members of log_meta
#include "log_meta.hpp"

#include <cstdint>

// LSA higher than any other LSA's used in this test
constexpr log_lsa MAX_UT_LSA = { 1024, 1024 };

class meta_file
{
  public:
    meta_file ();
    ~meta_file ();

    std::FILE *get_file ();
    void reload ();

  private:
    static constexpr char FILENAME[] = "meta_log";

    std::FILE *m_file;
};
static void match_meta_log (const cublog::meta &left, const cublog::meta &right);
static void match_meta_log_after_flush_and_load (const cublog::meta &meta_log, meta_file &mf);
static void match_checkpoint_info (const cublog::checkpoint_info *left, const cublog::checkpoint_info *right);

using test_chkpt_lsa_and_info_t = std::pair<log_lsa, cublog::checkpoint_info>;
static test_chkpt_lsa_and_info_t create_chkpt_lsa_and_info (std::int16_t value);

TEST_CASE ("Load meta from empty file", "")
{
  meta_file mf;
  cublog::meta meta_log;

  meta_log.load_from_file (mf.get_file ());
  REQUIRE (meta_log.get_checkpoint_count () == 0);
}

TEST_CASE ("Test meta load_from_file and flush_to_file", "")
{
  meta_file mf;
  cublog::meta meta_log;

  // match empty meta log
  match_meta_log_after_flush_and_load (meta_log, mf);

  // match meta log with one checkpoint info
  test_chkpt_lsa_and_info_t keyval1 = create_chkpt_lsa_and_info (1);
  meta_log.add_checkpoint_info (keyval1.first, std::move (keyval1.second));
  match_meta_log_after_flush_and_load (meta_log, mf);

  // match meta log with two checkpoint info
  test_chkpt_lsa_and_info_t keyval2 = create_chkpt_lsa_and_info (2);
  meta_log.add_checkpoint_info (keyval2.first, std::move (keyval2.second));
  match_meta_log_after_flush_and_load (meta_log, mf);

  // match meta log with three checkpoint info
  test_chkpt_lsa_and_info_t keyval3 = create_chkpt_lsa_and_info (3);
  meta_log.add_checkpoint_info (keyval3.first, std::move (keyval3.second));
  match_meta_log_after_flush_and_load (meta_log, mf);
}

TEST_CASE ("Test checkpoint_info functions", "")
{
  cublog::meta meta_log;

  const log_lsa key1 = { 1, 1 };
  const log_lsa key2 = { 2, 2 };
  const log_lsa key3 = { 3, 3 };

  // Add key1,val1
  {
    test_chkpt_lsa_and_info_t keyval1 = create_chkpt_lsa_and_info (1);
    meta_log.add_checkpoint_info (keyval1.first, std::move (keyval1.second));
  }
  REQUIRE (meta_log.get_checkpoint_count () == 1);
  // key1 exists and its value matches
  REQUIRE (meta_log.get_checkpoint_info (key1) != nullptr);
  const cublog::checkpoint_info *val1 = meta_log.get_checkpoint_info (key1);
  match_checkpoint_info (meta_log.get_checkpoint_info (key1), val1);
  // val2 of key2 does not exist
  REQUIRE (meta_log.get_checkpoint_info (key2) == nullptr);

  // Add key2,val2
  {
    test_chkpt_lsa_and_info_t keyval2 = create_chkpt_lsa_and_info (2);
    meta_log.add_checkpoint_info (keyval2.first, std::move (keyval2.second));
  }
  REQUIRE (meta_log.get_checkpoint_count () == 2);
  // Both key1,val1 and key2,val2 exist and values are matching
  REQUIRE (meta_log.get_checkpoint_info (key1) != nullptr);
  match_checkpoint_info (meta_log.get_checkpoint_info (key1), val1);
  REQUIRE (meta_log.get_checkpoint_info (key2) != nullptr);
  const cublog::checkpoint_info *val2 = meta_log.get_checkpoint_info (key2);
  match_checkpoint_info (meta_log.get_checkpoint_info (key2), val2);

  // Erase all before key2. That's key1.
  REQUIRE (meta_log.remove_checkpoint_info_before_lsa (key2) == 1);
  REQUIRE (meta_log.get_checkpoint_count () == 1);
  // key2,val2 exists, key1,val1 no longer
  REQUIRE (meta_log.get_checkpoint_info (key1) == nullptr);
  REQUIRE (meta_log.get_checkpoint_info (key2) != nullptr);
  match_checkpoint_info (meta_log.get_checkpoint_info (key2), val2);

  // Add key3,val3
  {
    test_chkpt_lsa_and_info_t keyval3 = create_chkpt_lsa_and_info (3);
    meta_log.add_checkpoint_info (keyval3.first, std::move (keyval3.second));
  }
  REQUIRE (meta_log.get_checkpoint_count () == 2);
  // key2,val2 and key3,val3 exist
  REQUIRE (meta_log.get_checkpoint_info (key2) != nullptr);
  match_checkpoint_info (meta_log.get_checkpoint_info (key2), val2);
  REQUIRE (meta_log.get_checkpoint_info (key3) != nullptr);
  const cublog::checkpoint_info *val3 = meta_log.get_checkpoint_info (key3);
  match_checkpoint_info (meta_log.get_checkpoint_info (key3), val3);

  // Remove all
  REQUIRE (meta_log.remove_checkpoint_info_before_lsa (MAX_UT_LSA) == 2);
  // No keys exist
  REQUIRE (meta_log.get_checkpoint_count () == 0);
  REQUIRE (meta_log.get_checkpoint_info (key2) == nullptr);
  REQUIRE (meta_log.get_checkpoint_info (key3) == nullptr);
}

meta_file::meta_file ()
{
  m_file = std::fopen (FILENAME, "w+");
  assert (m_file);
}

meta_file::~meta_file ()
{
  std::fclose (m_file);
  std::remove (FILENAME);
}

std::FILE *
meta_file::get_file ()
{
  return m_file;
}

void
meta_file::reload ()
{
  std::fclose (m_file);
  m_file = std::fopen (FILENAME, "r+");
  assert (m_file);
}

void
match_meta_log (const cublog::meta &left, const cublog::meta &right)
{
  REQUIRE (left.get_checkpoint_count () == right.get_checkpoint_count ());
  match_checkpoint_info (left.get_checkpoint_info ({ 1, 1 }), right.get_checkpoint_info ({ 1, 1 }));
  match_checkpoint_info (left.get_checkpoint_info ({ 2, 2 }), right.get_checkpoint_info ({ 2, 2 }));
  match_checkpoint_info (left.get_checkpoint_info ({ 3, 3 }), right.get_checkpoint_info ({ 3, 3 }));
}

void
match_meta_log_after_flush_and_load (const cublog::meta &meta_log, meta_file &mf)
{
  std::rewind (mf.get_file ());
  meta_log.flush_to_file (mf.get_file ());

  mf.reload ();
  cublog::meta meta_log_from_file;
  meta_log_from_file.load_from_file (mf.get_file ());

  match_meta_log (meta_log, meta_log_from_file);
}

void
match_checkpoint_info (const cublog::checkpoint_info *left, const cublog::checkpoint_info *right)
{
  if (left == nullptr)
    {
      REQUIRE (right == nullptr);
    }
  else
    {
      REQUIRE (left->get_start_redo_lsa () == right->get_start_redo_lsa ());
    }
}

static test_chkpt_lsa_and_info_t
create_chkpt_lsa_and_info (std::int16_t value)
{
  log_lsa chk_lsa = { value, value };
  cublog::checkpoint_info chk_info;
  chk_info.set_start_redo_lsa (chk_lsa);
  return std::make_pair (chk_lsa, std::move (chk_info));
}

bool
operator== (const cublog::checkpoint_info &left, const cublog::checkpoint_info &right)
{
  return left.get_start_redo_lsa () == right.get_start_redo_lsa ();
}

// Declarations for CUBRID stuff required by linker
#include "object_representation.h"

int or_packed_value_size (const DB_VALUE *value, int collapse_null, int include_domain, int include_domain_classoids)
{
  assert (false);
  return 0;
}

char *
or_pack_value (char *buf, DB_VALUE *value)
{
  assert (false);
  return nullptr;
}

char *
or_unpack_value (const char *buf, DB_VALUE *value)
{
  assert (false);
  return nullptr;
}

int
or_put_value (OR_BUF *, DB_VALUE *, int, int, int)
{
  assert (false);
  return 0;
}

void
er_set (int, const char *, const int, int, int, ...)
{
  assert (false);
  // nop
}

// checkpoint_info
void
cublog::checkpoint_info::pack (cubpacking::packer &serializer) const
{
  serializer.pack_bigint (m_start_redo_lsa.pageid);
  serializer.pack_int (m_start_redo_lsa.offset);
}

size_t
cublog::checkpoint_info::get_packed_size (cubpacking::packer &serializer, std::size_t start_offset) const
{
  size_t size = serializer.get_packed_bigint_size (start_offset);
  size += serializer.get_packed_int_size (start_offset + size);
  return size;
}

void
cublog::checkpoint_info::unpack (cubpacking::unpacker &deserializer)
{
  int64_t bigint;
  int i;
  deserializer.unpack_bigint (bigint);
  deserializer.unpack_int (i);
  m_start_redo_lsa = { bigint, static_cast<std::int16_t> (i) };
}

log_lsa
cublog::checkpoint_info::get_start_redo_lsa () const
{
  return m_start_redo_lsa;
}

void
cublog::checkpoint_info::set_start_redo_lsa (const log_lsa &start_redo_lsa)
{
  m_start_redo_lsa = start_redo_lsa;
}
