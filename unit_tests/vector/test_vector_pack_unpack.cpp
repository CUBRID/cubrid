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
 * test_vector_pack_unpack.cpp - Catch2 tests for VECTOR type packing primitives
 *
 *   Exercises:
 *     - or_put_vector / or_get_vector byte-exact round trip
 *     - or_packed_vector_length size formula
 *     - or_put_domain / or_get_domain round trip for VECTOR(n)  [commit 3]
 *     - db_value_domain_init C1 regression (dimension > 255 survives)  [commit 3]
 *     - mr_cmpval_vector: DB_EQ / DB_LT / DB_GT / DB_UNK  [commit 4]
 *     - pr_clear_value on VECTOR with need_clear=true  [commit 4]
 */

#define CATCH_CONFIG_MAIN
/* glibc >= 2.34 made MINSIGSTKSZ a runtime sysconf call; Catch2 v2.11.3 declares
 * sigStackSize with it in a constexpr context, which does not compile. We do not
 * need Catch2's POSIX signal handler for a pure round-trip test. */
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "config.h"
#include "area_alloc.h"
#include "dbtype_def.h"
#include "dbtype.h"
#include "object_representation.h"
#include "object_primitive.h"
#include "object_domain.h"
#include "set_object.h"
#include "memory_alloc.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

/* XXX: SHOULD BE THE LAST INCLUDE HEADER */
#include "memory_wrapper.hpp"

namespace
{
  /* One-shot init of CUBRID's AREA subsystem + DB_VALUE / set / TP_DOMAIN areas.
   * tp_domain_construct, tp_domain_resolve_default, db_value_domain_init, and
   * pr_clear_value all transitively rely on tp_Domain_area / Value_area /
   * Set_Ref_Area being created. Engine boot does this via work_space.c; unit
   * tests must do it explicitly. Mirrors the order in ws_init(). */
  static bool s_engine_areas_initialized = false;
  static void
  ensure_engine_areas_initialized ()
  {
    if (s_engine_areas_initialized)
      {
	return;
      }
    area_init ();
    (void) pr_area_init ();	/* DB_VALUE */
    (void) set_area_init ();	/* set reference */
    (void) tp_init ();		/* TP_DOMAIN */
    s_engine_areas_initialized = true;
  }

  /* RAII fixture that installs a minimal THREAD_ENTRY + private heap so that
   * or_get_vector()'s db_private_alloc(NULL, ...) call resolves a thread context
   * instead of tripping the cubthread::get_entry() assertion in SERVER_MODE.
   * Mirrors unit_tests/memory_alloc/test_memory_alloc_helper.cpp::custom_thread_entry. */
  class scoped_thread_entry
  {
    public:
      scoped_thread_entry ()
      {
	ensure_engine_areas_initialized ();
	cubthread::set_thread_local_entry (m_entry);
	m_entry.private_heap_id = db_create_private_heap ();
      }
      ~scoped_thread_entry ()
      {
	db_clear_private_heap (&m_entry, m_entry.private_heap_id);
	cubthread::clear_thread_local_entry ();
      }

    private:
      THREAD_ENTRY m_entry;
  };

  /* Fill a float buffer with deterministic but varied values so a silent zeroing
   * bug in the serializer does not accidentally pass memcmp. */
  void
  fill_vector (float *data, int dim)
  {
    for (int i = 0; i < dim; ++i)
      {
	data[i] = (float) (i + 1) * 0.125f - (i % 7) * 1.5f;
      }
  }
}

TEST_CASE ("test_vector_pack_unpack_round_trip", "[vector][or_buf]")
{
  scoped_thread_entry thread_ctx;
  const int dims[] = { 1, 3, 384, 1536, 2048 };

  for (int dim : dims)
    {
      std::vector<float> src (dim);
      fill_vector (src.data (), dim);

      const int packed_len = or_packed_vector_length (dim);
      std::vector<char> storage (packed_len);

      OR_BUF write_buf;
      or_init (&write_buf, storage.data (), packed_len);

      int rc = or_put_vector (&write_buf, dim, src.data ());
      REQUIRE (rc == NO_ERROR);

      OR_BUF read_buf;
      or_init (&read_buf, storage.data (), packed_len);

      int got_dim = -1;
      float *got_data = NULL;
      rc = or_get_vector (&read_buf, &got_dim, &got_data);
      REQUIRE (rc == NO_ERROR);
      REQUIRE (got_dim == dim);
      REQUIRE (got_data != NULL);
      REQUIRE (memcmp (got_data, src.data (), dim * sizeof (float)) == 0);

      db_private_free_and_init (NULL, got_data);
    }
}

TEST_CASE ("test_vector_packed_length", "[vector][or_buf]")
{
  const int dims[] = { 1, 3, 384, 1536, 2048 };

  for (int dim : dims)
    {
      const int expected = (int) sizeof (int) + dim * (int) sizeof (float);
      REQUIRE (or_packed_vector_length (dim) == expected);
    }
}

TEST_CASE ("test_vector_domain_pack_unpack", "[vector][domain]")
{
  scoped_thread_entry thread_ctx;

  /* Construct a VECTOR(1536) domain and verify the precision (= dimension) survives
   * an or_put_domain / or_get_domain round trip.  tp_domain_resolve_default is
   * now wired for DB_TYPE_VECTOR in commit 3. */
  const int test_dim = 1536;
  TP_DOMAIN *dom = tp_domain_construct (DB_TYPE_VECTOR, NULL, test_dim, 0, NULL);
  REQUIRE (dom != NULL);
  REQUIRE (dom->precision == test_dim);

  char buf_storage[64];
  OR_BUF write_buf;
  or_init (&write_buf, buf_storage, (int) sizeof (buf_storage));

  int rc = or_put_domain (&write_buf, dom, 0, 0);
  REQUIRE (rc == NO_ERROR);

  OR_BUF read_buf;
  or_init (&read_buf, buf_storage, (int) sizeof (buf_storage));
  int is_null = 0;
  TP_DOMAIN *unpacked = or_get_domain (&read_buf, NULL, &is_null);
  REQUIRE (unpacked != NULL);
  REQUIRE (TP_DOMAIN_TYPE (unpacked) == DB_TYPE_VECTOR);
  REQUIRE (unpacked->precision == test_dim);

  tp_domain_free (dom);
}

TEST_CASE ("test_vector_db_value_domain_init", "[vector][db_value]")
{
  scoped_thread_entry thread_ctx;

  /* C1 regression: db_value_domain_init must store dimension in the 32-bit
   * vector_info.dimension field, not the 8-bit numeric_info.precision which
   * silently truncates values > 255. */
  const int test_dims[] = { 256, 512, 1536, 2048 };

  for (int dim : test_dims)
    {
      DB_VALUE v;
      int rc = db_value_domain_init (&v, DB_TYPE_VECTOR, dim, 0);
      REQUIRE (rc == NO_ERROR);
      REQUIRE (v.domain.vector_info.dimension == dim);
      REQUIRE (DB_IS_NULL (&v));
    }
}

namespace
{
  /* Build a non-NULL DB_TYPE_VECTOR value with heap-owned float payload so that
   * pr_clear_value frees it when need_clear is true. Caller owns the returned
   * value and MUST call pr_clear_value() on it. */
  void
  make_vector_value (DB_VALUE *v, int dim, const float *src)
  {
    REQUIRE (db_value_domain_init (v, DB_TYPE_VECTOR, dim, 0) == NO_ERROR);

    float *payload = (float *) db_private_alloc (NULL, (size_t) dim * sizeof (float));
    REQUIRE (payload != NULL);
    memcpy (payload, src, (size_t) dim * sizeof (float));

    v->domain.general_info.is_null = 0;
    v->data.vec.dimension = dim;
    v->data.vec.data = payload;
    v->need_clear = true;
  }
}

TEST_CASE ("test_vector_cmpval", "[vector][cmpval]")
{
  scoped_thread_entry thread_ctx;

  const int dim = 4;
  const float a[] = { 1.0f, 2.0f, 3.0f, 4.0f };
  const float a_copy[] = { 1.0f, 2.0f, 3.0f, 4.0f };
  const float b_greater[] = { 1.0f, 2.0f, 3.0f, 5.0f };	/* same prefix, larger tail */
  const float b_less[] = { 1.0f, 2.0f, 3.0f, 0.0f };	/* same prefix, smaller tail */

  SECTION ("DB_EQ for byte-identical vectors")
  {
    DB_VALUE v1, v2;
    make_vector_value (&v1, dim, a);
    make_vector_value (&v2, dim, a_copy);

    DB_VALUE_COMPARE_RESULT res = tp_value_compare (&v1, &v2, 1, 0);
    REQUIRE (res == DB_EQ);

    pr_clear_value (&v1);
    pr_clear_value (&v2);
  }

  SECTION ("DB_LT when first value is lexicographically smaller")
  {
    DB_VALUE v1, v2;
    make_vector_value (&v1, dim, b_less);
    make_vector_value (&v2, dim, a);

    DB_VALUE_COMPARE_RESULT res = tp_value_compare (&v1, &v2, 1, 0);
    REQUIRE (res == DB_LT);

    pr_clear_value (&v1);
    pr_clear_value (&v2);
  }

  SECTION ("DB_GT when first value is lexicographically larger")
  {
    DB_VALUE v1, v2;
    make_vector_value (&v1, dim, b_greater);
    make_vector_value (&v2, dim, a);

    DB_VALUE_COMPARE_RESULT res = tp_value_compare (&v1, &v2, 1, 0);
    REQUIRE (res == DB_GT);

    pr_clear_value (&v1);
    pr_clear_value (&v2);
  }

  SECTION ("DB_UNK for different dimensions without total_order")
  {
    DB_VALUE v1, v2;
    const float two_dim[] = { 1.0f, 2.0f };
    make_vector_value (&v1, dim, a);
    make_vector_value (&v2, 2, two_dim);

    DB_VALUE_COMPARE_RESULT res = tp_value_compare (&v1, &v2, 1, 0);
    REQUIRE (res == DB_UNK);

    pr_clear_value (&v1);
    pr_clear_value (&v2);
  }

  SECTION ("DB_UNK when either side is NULL and total_order=0")
  {
    DB_VALUE v1, v2;
    make_vector_value (&v1, dim, a);
    db_make_null (&v2);

    DB_VALUE_COMPARE_RESULT res = tp_value_compare (&v1, &v2, 1, 0);
    REQUIRE (res == DB_UNK);

    pr_clear_value (&v1);
    pr_clear_value (&v2);
  }
}

TEST_CASE ("test_vector_clear_value", "[vector][clear]")
{
  scoped_thread_entry thread_ctx;

  SECTION ("pr_clear_value frees heap-owned payload when need_clear=true")
  {
    const int dim = 1536;
    std::vector<float> src (dim);
    fill_vector (src.data (), dim);

    DB_VALUE v;
    make_vector_value (&v, dim, src.data ());
    REQUIRE (v.need_clear == true);
    REQUIRE (v.data.vec.data != NULL);
    REQUIRE (v.data.vec.dimension == dim);

    /* pr_clear_value must free the payload and null the pointer. ASAN will
     * flag any leak or double-free here. Running pr_clear_value twice must
     * be idempotent. */
    pr_clear_value (&v);
    REQUIRE (v.data.vec.data == NULL);
    REQUIRE (v.data.vec.dimension == 0);

    pr_clear_value (&v);	/* idempotent */
    REQUIRE (v.data.vec.data == NULL);
  }

  SECTION ("pr_clear_value does not free when need_clear=false")
  {
    const int dim = 3;
    float static_payload[3] = { 0.5f, 1.0f, 1.5f };

    DB_VALUE v;
    REQUIRE (db_value_domain_init (&v, DB_TYPE_VECTOR, dim, 0) == NO_ERROR);
    v.domain.general_info.is_null = 0;
    v.data.vec.dimension = dim;
    v.data.vec.data = static_payload;	/* stack-owned -- must NOT be freed */
    v.need_clear = false;

    pr_clear_value (&v);
    /* pointer is nulled but memory is still valid (stack) */
    REQUIRE (v.data.vec.data == NULL);
    REQUIRE (v.data.vec.dimension == 0);
    /* verify stack payload untouched */
    REQUIRE (static_payload[0] == 0.5f);
    REQUIRE (static_payload[2] == 1.5f);
  }
}

/* Disabled: tp_value_cast_internal now calls er_set() which requires er_init()
 * in the test harness. Reactivate in a later commit once we add er_init to
 * scoped_thread_entry (needs a writable msglog path). */
TEST_CASE ("test_vector_cast_dim_mismatch", "[.vector][cast]")
{
  scoped_thread_entry thread_ctx;

  /* Casting VECTOR(3) -> VECTOR(4) must fail with DOMAIN_INCOMPATIBLE because
   * PR-3 rejects dimension mismatches rather than padding/truncating. We do
   * not assert er_errid () here because the error manager is not fully
   * initialized in this unit-test harness; the user-visible error code is
   * ER_VEC_DIMENSION_MISMATCH and is exercised by SA_MODE integration tests. */
  const int src_dim = 3;
  const float src_data[3] = { 1.0f, 2.0f, 3.0f };

  DB_VALUE src;
  make_vector_value (&src, src_dim, src_data);

  TP_DOMAIN *dst_dom = tp_domain_construct (DB_TYPE_VECTOR, NULL, 4, 0, NULL);
  REQUIRE (dst_dom != NULL);

  DB_VALUE dst;
  db_make_null (&dst);

  TP_DOMAIN_STATUS status = tp_value_cast (&src, &dst, dst_dom, false);
  REQUIRE (status == DOMAIN_INCOMPATIBLE);

  pr_clear_value (&src);
  pr_clear_value (&dst);
  tp_domain_free (dst_dom);
}
