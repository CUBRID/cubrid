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
 * test_stream_session.cpp - The registry, the handle table and the download operations of the byte-stream transport.
 *
 * The transport opens what it cannot name and hands the client numbers it will
 * send back, so these cases cover what stands between a wire value and a
 * consumer's object: a registered kind reaches its own factory in its own
 * direction, a handle resolves only where it was issued, and a release or a
 * purge never frees an object a call still holds.
 */

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "stream_session.hpp"

#include "error_code.h"
#include "error_manager.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

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

      int finish (THREAD_ENTRY *thread_p, stream_result *result) override
      {
	(void) thread_p;
	result->count = (std::int64_t) m_received.size ();
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

  int test_Factory_calls = 0;
  int test_Closed = 0;

  /* Each factory stamps its own tag on what it builds, so a case can tell which
   * one the registry dispatched to. */
  template <int TAG>
  stream_session *
  make_fake (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
  {
    (void) thread_p;

    test_Factory_calls++;

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

    test_Factory_calls++;
    *error_code = ER_STREAM_SESSION_ERROR;
    return NULL;
  }

  /* What a staged upload or a download's source is to the table: something to close once. */
  class fake_object : public stream_handle_object
  {
    public:
      void close (THREAD_ENTRY *thread_p) override
      {
	(void) thread_p;
	test_Closed++;
      }
  };

  /* A download of the config bytes themselves. Configs that start with a mark misbehave on purpose:
   * '!' refuses at open, '?' fails its first read, '~' returns nothing without eof. */
  class fake_source : public stream_source
  {
    public:
      explicit fake_source (const std::string &value)
	: m_value (value)
	, m_pos (0)
      {
      }

      int read (THREAD_ENTRY *thread_p, char *buf, int max, int *nread, bool *eof) override
      {
	(void) thread_p;

	if (!m_value.empty () && m_value[0] == '?')
	  {
	    return ER_STREAM_SESSION_ERROR;
	  }
	if (!m_value.empty () && m_value[0] == '~')
	  {
	    *nread = 0;
	    *eof = false;
	    return NO_ERROR;
	  }

	size_t n = std::min ((size_t) max, m_value.size () - m_pos);

	std::memcpy (buf, m_value.data () + m_pos, n);
	m_pos += n;
	*nread = (int) n;
	*eof = (m_pos == m_value.size ());
	return NO_ERROR;
      }

      void close (THREAD_ENTRY *thread_p) override
      {
	(void) thread_p;
	test_Closed++;
      }

    private:
      std::string m_value;
      size_t m_pos;
  };

  stream_source *
  make_source (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
  {
    (void) thread_p;

    test_Factory_calls++;
    if (config_len > 0 && config[0] == '!')
      {
	*error_code = ER_STREAM_SESSION_ERROR;
	return NULL;
      }

    *error_code = NO_ERROR;
    return new fake_source (std::string (config, (size_t) config_len));
  }

  const int KIND_A = STREAM_KIND_MIN;		/* upload whose END finishes the statement */
  const int KIND_STAGED = STREAM_KIND_MIN + 1;	/* upload that stages a value for a later statement */
  const int KIND_UNREGISTERED = STREAM_KIND_MIN + 2;
  /* its own tag, so the case below need not hand the slot back -- a registry
   * slot is never registered back to empty */
  const int KIND_REFUSING = STREAM_KIND_MIN + 3;
  const int KIND_DOWNLOAD = STREAM_KIND_MIN + 4;

  const int TRAN_1 = 7;
  const int TRAN_2 = 8;

  stream_kind_desc
  upload_desc (stream_session_factory sink, bool ends_unit_of_work)
  {
    stream_kind_desc desc = { STREAM_UPLOAD, sink, NULL, ends_unit_of_work, false, STREAM_SCOPE_TRAN };

    return desc;
  }

  /* The registry is process-wide and registration is one-way, so the cases share
   * one arrangement rather than fighting over it. */
  struct registry_fixture
  {
    registry_fixture ()
    {
      static bool registered = false;

      if (!registered)
	{
	  stream_kind_desc staged = { STREAM_UPLOAD, make_fake<'B'>, NULL, false, true, STREAM_SCOPE_OWNER };
	  stream_kind_desc download = { STREAM_DOWNLOAD, NULL, make_source, false, false, STREAM_SCOPE_TRAN };

	  REQUIRE (stream_kind_register (KIND_A, upload_desc (make_fake<'A'>, true)) == NO_ERROR);
	  REQUIRE (stream_kind_register (KIND_STAGED, staged) == NO_ERROR);
	  REQUIRE (stream_kind_register (KIND_REFUSING, upload_desc (refuse, true)) == NO_ERROR);
	  REQUIRE (stream_kind_register (KIND_DOWNLOAD, download) == NO_ERROR);
	  registered = true;
	}

      test_Last_error = NO_ERROR;
      test_Factory_calls = 0;
      test_Closed = 0;
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
  stream_session *b = stream_session_create (NULL, KIND_STAGED, NULL, 0, &error_code);
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

      error_code = NO_ERROR;
      CHECK (stream_source_create (NULL, kind, NULL, 0, &error_code) == NULL);
      CHECK (error_code == ER_STREAM_SESSION_ERROR);
      CHECK (stream_kind_find (kind) == NULL);
    }
}

TEST_CASE_METHOD (registry_fixture, "an in-range tag with no factory is refused", "[stream_session]")
{
  int error_code = NO_ERROR;

  REQUIRE (KIND_UNREGISTERED < STREAM_KIND_MAX);

  CHECK (stream_session_create (NULL, KIND_UNREGISTERED, NULL, 0, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);
  CHECK (test_Last_error == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_find (KIND_UNREGISTERED) == NULL);
}

TEST_CASE_METHOD (registry_fixture, "the factory's own refusal is passed through", "[stream_session]")
{
  int error_code = NO_ERROR;

  CHECK (stream_session_create (NULL, KIND_REFUSING, NULL, 0, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);
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
  stream_result result = { -1 };

  stream_session *s = stream_session_create (NULL, KIND_A, NULL, 0, &error_code);
  REQUIRE (s != NULL);

  CHECK (s->receive_chunk (NULL, "abc", 3) == NO_ERROR);
  CHECK (s->receive_chunk (NULL, "de", 2) == NO_ERROR);
  CHECK (s->finish (NULL, &result) == NO_ERROR);
  CHECK (result.count == 5);

  /* abort () drops whatever had not been reported yet */
  CHECK (s->receive_chunk (NULL, "fgh", 3) == NO_ERROR);
  s->abort (NULL);
  CHECK (s->finish (NULL, &result) == NO_ERROR);
  CHECK (result.count == 0);

  delete s;
}

TEST_CASE_METHOD (registry_fixture, "each kind answers for its own unit of work", "[stream_session]")
{
  /* the transport reads this off the registry rather than off the wire, so a
   * client cannot claim a commit its consumer never promised */
  REQUIRE (stream_kind_find (KIND_A) != NULL);
  REQUIRE (stream_kind_find (KIND_STAGED) != NULL);
  CHECK (stream_kind_find (KIND_A)->ends_unit_of_work == true);
  CHECK (stream_kind_find (KIND_STAGED)->ends_unit_of_work == false);
  CHECK (stream_kind_find (KIND_STAGED)->result_is_handle == true);
  CHECK (stream_kind_find (KIND_DOWNLOAD)->direction == STREAM_DOWNLOAD);
}

TEST_CASE_METHOD (registry_fixture, "a record that does not hold together is refused", "[stream_session]")
{
  stream_kind_desc ends_download = { STREAM_DOWNLOAD, NULL, make_source, true, false, STREAM_SCOPE_TRAN };
  stream_kind_desc staging_download = { STREAM_DOWNLOAD, NULL, make_source, false, true, STREAM_SCOPE_TRAN };
  stream_kind_desc sinking_download = { STREAM_DOWNLOAD, make_fake<'X'>, make_source, false, false,
					STREAM_SCOPE_TRAN
				      };
  stream_kind_desc sourceless_download = { STREAM_DOWNLOAD, NULL, NULL, false, false, STREAM_SCOPE_TRAN };
  stream_kind_desc lasting_download = { STREAM_DOWNLOAD, NULL, make_source, false, false, STREAM_SCOPE_OWNER };
  stream_kind_desc sourced_upload = { STREAM_UPLOAD, make_fake<'X'>, make_source, true, false, STREAM_SCOPE_TRAN };
  stream_kind_desc sinkless_upload = { STREAM_UPLOAD, NULL, NULL, true, false, STREAM_SCOPE_TRAN };

  /* nothing a download ends or stages, and a factory only of the kind's own direction */
  CHECK (stream_kind_register (KIND_UNREGISTERED, ends_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, staging_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, sinking_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, sourceless_download) == ER_STREAM_SESSION_ERROR);
  /* the client forgets its downloads at every transaction end, so one that outlived it could never be released */
  CHECK (stream_kind_register (KIND_UNREGISTERED, lasting_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, sourced_upload) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, sinkless_upload) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (STREAM_KIND_MAX, upload_desc (make_fake<'X'>, true)) == ER_STREAM_SESSION_ERROR);

  CHECK (stream_kind_find (KIND_UNREGISTERED) == NULL);
}

TEST_CASE_METHOD (registry_fixture, "a kind opened in the wrong direction is refused before its factory runs",
		  "[stream_session]")
{
  int error_code = NO_ERROR;

  CHECK (stream_source_create (NULL, KIND_A, NULL, 0, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);

  error_code = NO_ERROR;
  CHECK (stream_session_create (NULL, KIND_DOWNLOAD, NULL, 0, &error_code) == NULL);
  CHECK (error_code == ER_STREAM_SESSION_ERROR);

  CHECK (test_Factory_calls == 0);
}
