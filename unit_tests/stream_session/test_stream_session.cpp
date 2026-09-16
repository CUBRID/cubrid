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
 * test_stream_session.cpp - The consumer registry of the byte-stream transport.
 *
 * The transport opens a session it cannot name, so the only thing standing
 * between a wire tag and a function pointer call is this registry. These cases
 * cover what the transport asks of it: a registered kind reaches its own
 * factory, and every tag that is not one is refused rather than dispatched.
 */

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "stream_session.hpp"

#include "error_code.h"
#include "error_manager.h"

#include <cstdint>
#include <string>

/* The registry reports refusals through the engine's error stack, which a unit
 * test has no use for; record the last code so the cases can assert on it. */
static int test_Last_error = NO_ERROR;

void
er_set (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...)
{
  (void) severity;
  (void) file_name;
  (void) line_no;
  (void) num_args;

  test_Last_error = err_id;
}

namespace
{
  /* A consumer stands in for COPY / internal-LOB: it records what the transport
   * handed it, which is all the seam promises to carry. */
  class fake_session : public stream_session
  {
    public:
      explicit fake_session (int tag)
	: m_tag (tag)
      {
      }

      int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override
      {
	(void) thread_p;
	m_received.append (data, (size_t) data_len);
	return NO_ERROR;
      }

      int finish (THREAD_ENTRY *thread_p, std::int64_t *count) override
      {
	(void) thread_p;
	*count = (std::int64_t) m_received.size ();
	return NO_ERROR;
      }

      void abort (THREAD_ENTRY *thread_p) override
      {
	(void) thread_p;
	m_received.clear ();
      }

      int tag () const
      {
	return m_tag;
      }

    private:
      int m_tag;
      std::string m_received;
  };

  /* Each factory stamps its own tag on what it builds, so a case can tell which
   * one the registry dispatched to. */
  template <int TAG>
  stream_session *
  make_fake (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
  {
    (void) thread_p;

    /* the blob comes straight off the wire: decode it bounded by config_len */
    if (config_len < 0 || (config_len > 0 && config == NULL))
      {
	*error_code = ER_STREAM_SESSION_ERROR;
	return NULL;
      }

    *error_code = NO_ERROR;
    return new fake_session (TAG);
  }

  /* A factory that refuses, the way a consumer rejects a malformed config. */
  stream_session *
  refuse (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
  {
    (void) thread_p;
    (void) config;
    (void) config_len;

    *error_code = ER_STREAM_SESSION_ERROR;
    return NULL;
  }

  const int KIND_A = STREAM_KIND_MIN;
  const int KIND_B = STREAM_KIND_MIN + 1;
  const int KIND_UNREGISTERED = STREAM_KIND_MIN + 2;

  /* The registry is process-wide and registration is one-way, so the cases share
   * one arrangement rather than fighting over it. */
  struct registry_fixture
  {
    registry_fixture ()
    {
      static bool registered = false;

      if (!registered)
	{
	  stream_session_register (KIND_A, make_fake<'A'>);
	  stream_session_register (KIND_B, make_fake<'B'>);
	  registered = true;
	}

      test_Last_error = NO_ERROR;
    }
  };
}

TEST_CASE_METHOD (registry_fixture, "a registered kind reaches its own factory", "[stream_session]")
{
  int error_code = ER_FAILED;

  stream_session *a = stream_session_create (NULL, KIND_A, NULL, 0, &error_code);
  REQUIRE (a != NULL);
  CHECK (error_code == NO_ERROR);
  CHECK (static_cast<fake_session *> (a)->tag () == 'A');
  delete a;

  error_code = ER_FAILED;
  stream_session *b = stream_session_create (NULL, KIND_B, NULL, 0, &error_code);
  REQUIRE (b != NULL);
  CHECK (error_code == NO_ERROR);
  CHECK (static_cast<fake_session *> (b)->tag () == 'B');
  delete b;
}

TEST_CASE_METHOD (registry_fixture, "a tag outside the bound is refused", "[stream_session]")
{
  const int out_of_range[] = { STREAM_KIND_MIN - 1, -1, STREAM_KIND_MAX, STREAM_KIND_MAX + 1, 1 << 30 };

  for (int kind : out_of_range)
    {
      int error_code = NO_ERROR;

      CHECK (stream_session_create (NULL, kind, NULL, 0, &error_code) == NULL);
      CHECK (error_code == ER_STREAM_SESSION_ERROR);
      CHECK (test_Last_error == ER_STREAM_SESSION_ERROR);
    }
}

TEST_CASE_METHOD (registry_fixture, "an in-range tag with no factory is refused", "[stream_session]")
{
  int error_code = NO_ERROR;

  REQUIRE (KIND_UNREGISTERED < STREAM_KIND_MAX);

  CHECK (stream_session_create (NULL, KIND_UNREGISTERED, NULL, 0, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);
  CHECK (test_Last_error == ER_STREAM_SESSION_ERROR);
}

TEST_CASE_METHOD (registry_fixture, "the factory's own refusal is passed through", "[stream_session]")
{
  int error_code = NO_ERROR;

  stream_session_register (KIND_UNREGISTERED, refuse);

  CHECK (stream_session_create (NULL, KIND_UNREGISTERED, NULL, 0, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);

  /* put the slot back so the case above keeps meaning what it says */
  stream_session_register (KIND_UNREGISTERED, NULL);
}

TEST_CASE_METHOD (registry_fixture, "the config blob reaches the factory bounded by its length", "[stream_session]")
{
  const char config[] = "kind-specific bytes";
  int error_code = ER_FAILED;

  stream_session *s = stream_session_create (NULL, KIND_A, config, (int) sizeof (config), &error_code);
  REQUIRE (s != NULL);
  CHECK (error_code == NO_ERROR);
  delete s;

  /* a length the blob cannot honour is the factory's to refuse, not the
   * registry's to guess at */
  error_code = NO_ERROR;
  CHECK (stream_session_create (NULL, KIND_A, NULL, -1, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);
}

TEST_CASE_METHOD (registry_fixture, "the seam carries bytes through to the binding's count", "[stream_session]")
{
  int error_code = ER_FAILED;
  std::int64_t count = -1;

  stream_session *s = stream_session_create (NULL, KIND_A, NULL, 0, &error_code);
  REQUIRE (s != NULL);

  CHECK (s->receive_chunk (NULL, "abc", 3) == NO_ERROR);
  CHECK (s->receive_chunk (NULL, "de", 2) == NO_ERROR);
  CHECK (s->finish (NULL, &count) == NO_ERROR);
  CHECK (count == 5);

  /* abort () drops whatever had not been reported yet */
  CHECK (s->receive_chunk (NULL, "fgh", 3) == NO_ERROR);
  s->abort (NULL);
  CHECK (s->finish (NULL, &count) == NO_ERROR);
  CHECK (count == 0);

  delete s;
}
