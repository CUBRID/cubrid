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

/* Private-function experiment: compile the production implementation, without adding a production hook.
 * The companion runner uses the existing CS compile command and GNU allocator wrapping. */
#include "config.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../../src/transaction/log_applier.c"

static thread_local size_t fail_realloc_size;
static thread_local size_t fail_malloc_size;
static thread_local int realloc_failures;
static thread_local int malloc_failures;

extern "C" void *__real_realloc (void *, size_t);
extern "C" void *__real_malloc (size_t);

extern "C" void *
__wrap_realloc (void *ptr, size_t size)
{
  if (fail_realloc_size != 0 && size == fail_realloc_size)
    {
      fail_realloc_size = 0;
      realloc_failures++;
      return NULL;
    }
  return __real_realloc (ptr, size);
}

extern "C" void *
__wrap_malloc (size_t size)
{
  if (fail_malloc_size != 0 && size == fail_malloc_size)
    {
      fail_malloc_size = 0;
      malloc_failures++;
      return NULL;
    }
  return __real_malloc (size);
}

#define CHECK_FAULT(condition) \
  do \
    { \
      if (!(condition)) \
	{ \
	  std::fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
	  std::abort (); \
	} \
    } \
  while (false)

static void
expect_pin_allocation_failure (LA_CACHE_BUFFER *page, bool retrying_reader)
{
  const int before_count = la_Pinned_page_buffer_count;
  const int before_capacity = la_Pinned_page_buffer_capacity;
  const int before_fixes = page->fix_count;
  LA_PINNED_PAGE_BUFFER *before_list = la_Pinned_page_buffers;
  const int failures = realloc_failures;
  fail_realloc_size = sizeof (LA_PINNED_PAGE_BUFFER)
		      * (before_capacity == 0 ? LA_MIN_PINNED_PAGE_BUFFERS : static_cast<size_t> (before_capacity) * 2);
  if (retrying_reader)
    {
      /* The wrapper fails once. A mistaken retry would succeed and return a non-NULL page. */
      CHECK_FAULT (la_get_page (page->pageid) == NULL);
    }
  else
    {
      int error = NO_ERROR;
      CHECK_FAULT (la_get_page_buffer (page->pageid, &error) == NULL);
      CHECK_FAULT (error == ER_OUT_OF_VIRTUAL_MEMORY);
    }
  CHECK_FAULT (realloc_failures == failures + 1);
  CHECK_FAULT (fail_realloc_size == 0);
  CHECK_FAULT (page->fix_count == before_fixes);
  CHECK_FAULT (la_Pinned_page_buffers == before_list);
  CHECK_FAULT (la_Pinned_page_buffer_count == before_count);
  CHECK_FAULT (la_Pinned_page_buffer_capacity == before_capacity);
  for (int i = 0; i < before_count; i++)
    {
      CHECK_FAULT (la_Pinned_page_buffers[i].buffer == page);
      CHECK_FAULT (la_Pinned_page_buffers[i].pageid == page->pageid);
    }
}

static void
test_cache_hit_pin_failures ()
{
  LA_CACHE_PB cache {};
  LA_CACHE_BUFFER page {};
  CHECK_FAULT (pthread_mutex_init (&cache.mutex, NULL) == 0);
  page.pageid = 42;
  page.logpage.hdr.logical_pageid = page.pageid;
  cache.hash_table = mht_create ("allocation fault cache", 8, log_pageid_hash, mht_compare_logpageids_are_equal);
  CHECK_FAULT (cache.hash_table != NULL);
  CHECK_FAULT (mht_put (cache.hash_table, &page.pageid, &page) != NULL);
  la_Info.cache_pb = &cache;

  expect_pin_allocation_failure (&page, false);
  CHECK_FAULT (page.fix_count == 0);
  std::puts ("PASS first cache-hit pin OOM rolls back its fix");

  expect_pin_allocation_failure (&page, true);
  CHECK_FAULT (page.fix_count == 0);
  CHECK_FAULT (la_get_page (page.pageid) == &page.logpage);
  la_release_pinned_page_buffers (NULL);
  la_free_pinned_page_buffers ();
  std::puts ("PASS la_get_page stops on pin OOM and the next read can succeed");

  /* A second thread holds an actual fix of the same cached page, with its own TLS pin list. */
  std::atomic<bool> ready { false };
  std::atomic<bool> release { false };
  std::thread other ([&]
  {
    int error = NO_ERROR;
    CHECK_FAULT (la_get_page_buffer (page.pageid, &error) == &page);
    CHECK_FAULT (error == NO_ERROR);
    CHECK_FAULT (la_Pinned_page_buffer_count == 1);
    ready.store (true);
    while (!release.load ())
      {
	std::this_thread::yield ();
      }
    CHECK_FAULT (la_Pinned_page_buffer_count == 1);
    la_release_pinned_page_buffers (NULL);
    la_free_pinned_page_buffers ();
  });
  while (!ready.load ())
    {
      std::this_thread::yield ();
    }
  expect_pin_allocation_failure (&page, false);
  CHECK_FAULT (page.fix_count == 1);
  std::puts ("PASS first pin OOM preserves the other thread's existing fix");

  for (int i = 0; i < LA_MIN_PINNED_PAGE_BUFFERS; i++)
    {
      int error = NO_ERROR;
      CHECK_FAULT (la_get_page_buffer (page.pageid, &error) == &page);
      CHECK_FAULT (error == NO_ERROR);
    }
  CHECK_FAULT (la_Pinned_page_buffer_count == LA_MIN_PINNED_PAGE_BUFFERS);
  CHECK_FAULT (page.fix_count == LA_MIN_PINNED_PAGE_BUFFERS + 1);
  expect_pin_allocation_failure (&page, false);
  CHECK_FAULT (page.fix_count == LA_MIN_PINNED_PAGE_BUFFERS + 1);
  la_release_pinned_page_buffers (NULL);
  CHECK_FAULT (page.fix_count == 1);
  la_free_pinned_page_buffers ();
  release.store (true);
  other.join ();
  CHECK_FAULT (page.fix_count == 0);
  std::puts ("PASS full-list growth OOM preserves all same-page and other-thread fixes");

  mht_destroy (cache.hash_table);
  CHECK_FAULT (pthread_mutex_destroy (&cache.mutex) == 0);
  la_Info.cache_pb = NULL;
}

static void
test_capacity_boundaries ()
{
  LA_CACHE_BUFFER page {};
  CHECK_FAULT (la_Pinned_page_buffers == NULL);
  const int too_large[] = { INT_MAX / 2 + 1, INT_MAX };
  for (int capacity : too_large)
    {
      la_Pinned_page_buffer_count = la_Pinned_page_buffer_capacity = capacity;
      const int failures = realloc_failures;
      CHECK_FAULT (la_pin_page_buffer (&page, 42) == ER_OUT_OF_VIRTUAL_MEMORY);
      CHECK_FAULT (la_Pinned_page_buffer_count == capacity);
      CHECK_FAULT (la_Pinned_page_buffer_capacity == capacity);
      CHECK_FAULT (la_Pinned_page_buffers == NULL);
      CHECK_FAULT (realloc_failures == failures);
    }
  /* The largest double below INT_MAX reaches realloc with a size_t product, then fails safely. */
  la_Pinned_page_buffer_count = la_Pinned_page_buffer_capacity = INT_MAX / 2;
  const int failures = realloc_failures;
  fail_realloc_size = static_cast<size_t> (INT_MAX / 2) * 2 * sizeof (LA_PINNED_PAGE_BUFFER);
  CHECK_FAULT (la_pin_page_buffer (&page, 42) == ER_OUT_OF_VIRTUAL_MEMORY);
  CHECK_FAULT (realloc_failures == failures + 1);
  CHECK_FAULT (la_Pinned_page_buffer_count == INT_MAX / 2);
  CHECK_FAULT (la_Pinned_page_buffer_capacity == INT_MAX / 2);
  CHECK_FAULT (la_Pinned_page_buffers == NULL);
  la_free_pinned_page_buffers ();
  std::puts ("PASS INT_MAX growth guards and size_t allocation size boundary");
}

static void
test_string_allocation ()
{
  alignas (MAX_ALIGNMENT) char packed[64];
  char *end = or_pack_string (packed, NULL);
  char *ptr = packed;
  char *value = NULL;
  const int failures = malloc_failures;
  CHECK_FAULT (la_unpack_repl_string (&ptr, &value) == NO_ERROR);
  CHECK_FAULT (value == NULL && ptr == end);
  CHECK_FAULT (malloc_failures == failures);

  end = or_pack_string (packed, "abc");
  ptr = packed;
  fail_malloc_size = static_cast<size_t> (OR_GET_INT (packed));
  CHECK_FAULT (la_unpack_repl_string (&ptr, &value) == ER_OUT_OF_VIRTUAL_MEMORY);
  CHECK_FAULT (value == NULL && ptr == end);
  CHECK_FAULT (malloc_failures == failures + 1);
  CHECK_FAULT (fail_malloc_size == 0);

  ptr = packed;
  CHECK_FAULT (la_unpack_repl_string (&ptr, &value) == NO_ERROR);
  CHECK_FAULT (value != NULL && strcmp (value, "abc") == 0 && ptr == end);
  free_and_init (value);
  std::puts ("PASS actual unpack distinguishes encoded NULL, malloc OOM and a valid string");
}

struct packed_repl_fixture
{
  alignas (MAX_ALIGNMENT) char storage[IO_MAX_PAGE_SIZE];
  LOG_LSA lsa;
  int payload_size;
  int string_sizes[4];
  const char *strings[4];

  LOG_PAGE *page ()
  {
    return reinterpret_cast<LOG_PAGE *> (storage);
  }
};

/* Match the production replication record layout. All payload bytes stay in this one synthetic log page. */
static void
pack_repl_fixture (packed_repl_fixture &fixture, bool statement, int null_string = -1)
{
  memset (&fixture, 0, sizeof (fixture));
  fixture.lsa.pageid = 500;
  fixture.lsa.offset = 0;
  fixture.page ()->hdr.logical_pageid = fixture.lsa.pageid;
  const int record_offset = DB_ALIGN (DB_SIZEOF (LOG_RECORD_HEADER), MAX_ALIGNMENT);
  LOG_REC_REPLICATION *record = reinterpret_cast<LOG_REC_REPLICATION *> (fixture.page ()->area + record_offset);
  const int payload_offset = DB_ALIGN (record_offset + DB_SIZEOF (LOG_REC_REPLICATION), MAX_ALIGNMENT);
  char *payload = fixture.page ()->area + payload_offset;
  char *ptr = or_pack_int (payload, statement ? CUBRID_STMT_UPDATE : 37);
  const char *statement_strings[] =
  {
    "statement_class_123456789",
    "UPDATE fixture SET value=123456789 WHERE id=987654321",
    "fixture_user_123456789012345678901234",
    "ha_setting_123456789012345678901234567890123456789012345678901234567890"
  };

  const int count = statement ? 4 : 1;
  for (int i = 0; i < count; i++)
    {
      fixture.strings[i] = statement ? statement_strings[i] : "row_class_12345";
      if (i == null_string)
	{
	  fixture.strings[i] = NULL;
	}
      char *start = ptr;
      ptr = or_pack_string (ptr, fixture.strings[i]);
      fixture.string_sizes[i] = OR_GET_INT (start);
    }
  if (!statement)
    {
      ptr = reinterpret_cast<char *> (DB_ALIGN (reinterpret_cast<UINTPTR> (ptr), MAX_ALIGNMENT));
      /* make_repl_item only copies the key; DB_VALUE decoding is deliberately outside this experiment. */
      memset (ptr, 0x5a, 37);
      ptr += 37;
    }
  fixture.payload_size = static_cast<int> (ptr - payload);
  CHECK_FAULT (payload_offset + fixture.payload_size < LA_LOGAREA_SIZE);
  record->length = fixture.payload_size;
  record->rcvindex = RVREPL_DATA_INSERT;
  LSA_COPY (&record->lsa, &fixture.lsa);
}

static LA_ITEM *
make_fixture_item (packed_repl_fixture &fixture, bool statement)
{
  return la_make_repl_item (fixture.page (), statement ? LOG_REPLICATION_STATEMENT : LOG_REPLICATION_DATA,
			    123, &fixture.lsa);
}

static void
free_fixture_item (LA_ITEM *item, bool statement)
{
  LA_APPLY apply {};
  la_add_repl_item (&apply, item);
  CHECK_FAULT (apply.num_items == 1);
  if (statement)
    {
      /* Real statement items are created by the coordinator and later freed on a worker. */
      std::thread worker ([&]
      {
	la_clear_repl_item_key (item);
	CHECK_FAULT (DB_IS_NULL (&item->key));
	/* The normal destructor calls clear again: this must not free the text twice. */
	la_free_repl_item (&apply, item);
      });
      worker.join ();
    }
  else
    {
      la_free_repl_item (&apply, item);
    }
  CHECK_FAULT (apply.num_items == 0 && apply.head == NULL && apply.tail == NULL);
}

static void
test_make_repl_item_allocation_failures ()
{
  la_Info.act_log.db_logpagesize = IO_MAX_PAGE_SIZE;
  packed_repl_fixture fixture;
  const char *sites[] = { "packed_key", "row_class", "statement_class", "statement_text", "db_user", "ha_sys_prm" };
  for (int site = 0; site < 6; site++)
    {
      const bool statement = site >= 2;
      pack_repl_fixture (fixture, statement);
      const int string_index = statement ? site - 2 : 0;
      const size_t target = site == 0 ? 37 : fixture.string_sizes[string_index];
      CHECK_FAULT (target > 0 && target != static_cast<size_t> (fixture.payload_size));
      CHECK_FAULT (target != sizeof (LA_ITEM));
      if (statement)
	{
	  for (int i = 0; i < 4; i++)
	    {
	      CHECK_FAULT (i == string_index || fixture.string_sizes[i] != static_cast<int> (target));
	    }
	}
      const int failures = malloc_failures;
      er_clear ();
      fail_malloc_size = target;
      CHECK_FAULT (make_fixture_item (fixture, statement) == NULL);
      CHECK_FAULT (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY);
      CHECK_FAULT (fail_malloc_size == 0 && malloc_failures == failures + 1);
      /* Failure must not corrupt the source page or later item assembly. */
      er_clear ();
      LA_ITEM *item = make_fixture_item (fixture, statement);
      CHECK_FAULT (item != NULL && er_errid () == NO_ERROR);
      CHECK_FAULT (strcmp (item->class_name, fixture.strings[0]) == 0);
      if (statement)
	{
	  CHECK_FAULT (strcmp (db_get_string (&item->key), fixture.strings[1]) == 0);
	  CHECK_FAULT (strcmp (item->db_user, fixture.strings[2]) == 0);
	  CHECK_FAULT (strcmp (item->ha_sys_prm, fixture.strings[3]) == 0);
	}
      else
	{
	  CHECK_FAULT (item->packed_key_value_length == 37);
	  for (int i = 0; i < 37; i++)
	    {
	      CHECK_FAULT (item->packed_key_value[i] == 0x5a);
	    }
	}
      free_fixture_item (item, statement);
      std::printf ("PASS make_repl_item site=%s malloc_bytes=%zu failures=1 NULL+OOM, next assembly+free OK\n",
		   sites[site], target);
    }
  std::puts ("PASS actual make_repl_item rejects packed-key and all five string-site malloc failures as OOM");

  for (int null_string = 0; null_string < 4; null_string++)
    {
      pack_repl_fixture (fixture, true, null_string);
      er_clear ();
      LA_ITEM *item = make_fixture_item (fixture, true);
      CHECK_FAULT (item != NULL && er_errid () == NO_ERROR);
      if (null_string == 0)
	{
	  CHECK_FAULT (item->class_name == NULL);
	}
      else if (null_string == 1)
	{
	  CHECK_FAULT (DB_IS_NULL (&item->key));
	}
      else if (null_string == 2)
	{
	  CHECK_FAULT (item->db_user == NULL);
	}
      else
	{
	  CHECK_FAULT (item->ha_sys_prm == NULL);
	}
      free_fixture_item (item, true);
    }
  std::puts ("PASS actual make_repl_item accepts encoded NULL statement fields and cross-thread text cleanup");
}

static void
test_replica_worker_limit ()
{
  const int saved_mode = prm_get_integer_value (PRM_ID_HA_MODE);
  const int saved_count = prm_get_integer_value (PRM_ID_HA_APPLY_WORKER_COUNT);

  for (int mode : { HA_MODE_OFF, HA_MODE_FAIL_BACK, HA_MODE_ROLE_CHANGE, HA_MODE_REPLICA })
    {
      prm_set_integer_value (PRM_ID_HA_MODE, mode);
      for (int count : { 1, 2, 4, LA_MAX_WORKER_COUNT })
	{
	  prm_set_integer_value (PRM_ID_HA_APPLY_WORKER_COUNT, count);
	  la_init ("replica-worker-test", 0);
#if defined (CS_MODE) && defined (MULTI_CONN_TO_A_SERVER)
	  CHECK_FAULT (la_Info.worker_count == (mode == HA_MODE_REPLICA ? 1 : count));
#else
	  CHECK_FAULT (la_Info.worker_count == 1);
#endif
	  CHECK_FAULT (prm_get_integer_value (PRM_ID_HA_APPLY_WORKER_COUNT) == count);
	}
    }

  prm_set_integer_value (PRM_ID_HA_MODE, saved_mode);
  prm_set_integer_value (PRM_ID_HA_APPLY_WORKER_COUNT, saved_count);
  std::puts ("PASS replica initialization limits workers to one; other modes retain their configured count");
}

int
main ()
{
  (void) er_init (NULL, ER_NEVER_EXIT);
  CHECK_FAULT (lang_init () == NO_ERROR);
  CHECK_FAULT (lang_set_charset_lang ("en_US.utf8") == NO_ERROR);
  test_replica_worker_limit ();
  test_cache_hit_pin_failures ();
  test_capacity_boundaries ();
  test_string_allocation ();
  test_make_repl_item_allocation_failures ();
  lang_final ();
  er_final (ER_ALL_FINAL);
  std::puts ("All production-source allocation fault checks passed; no server or log files were used.");
}
