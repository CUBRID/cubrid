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

#include "stream_client.h"
#include "stream_handle.hpp"
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

  int test_Authorized = 0;

  int
  authorize_all (const char *config, int config_len)
  {
    (void) config;
    (void) config_len;

    test_Authorized++;
    return NO_ERROR;
  }

  int
  authorize_none (const char *config, int config_len)
  {
    (void) config;
    (void) config_len;

    test_Authorized++;
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_SELECT_FAILURE, 0);
    return ER_AU_SELECT_FAILURE;
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
	  stream_kind_client_desc download_client = { true, authorize_all };
	  stream_kind_client_desc staged_client = { false, authorize_none };

	  REQUIRE (stream_kind_register (KIND_A, upload_desc (make_fake<'A'>, true)) == NO_ERROR);
	  REQUIRE (stream_kind_register (KIND_STAGED, staged) == NO_ERROR);
	  REQUIRE (stream_kind_register (KIND_REFUSING, upload_desc (refuse, true)) == NO_ERROR);
	  REQUIRE (stream_kind_register (KIND_DOWNLOAD, download) == NO_ERROR);
	  REQUIRE (stream_kind_client_register (KIND_DOWNLOAD, download_client) == NO_ERROR);
	  REQUIRE (stream_kind_client_register (KIND_STAGED, staged_client) == NO_ERROR);
	  registered = true;
	}

      test_Last_error = NO_ERROR;
      test_Factory_calls = 0;
      test_Closed = 0;
      test_Authorized = 0;
    }
  };

  std::string
  read_all (stream_handle_table &table, std::int64_t handle, int max)
  {
    std::string out;
    std::vector<char> buf (stream_read_bound (max));
    int nread = 0;
    int reply_flags = 0;

    do
      {
	REQUIRE (stream_download_read (NULL, table, handle, TRAN_1, max, 0, buf.data (), &nread, &reply_flags)
		 == NO_ERROR);
	out.append (buf.data (), (size_t) nread);
      }
    while (! (reply_flags & STREAM_FLAG_EOF));

    return out;
  }
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
  stream_result result = { -1, NULL };

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
  stream_kind_desc sourced_upload = { STREAM_UPLOAD, make_fake<'X'>, make_source, true, false, STREAM_SCOPE_TRAN };
  stream_kind_desc sinkless_upload = { STREAM_UPLOAD, NULL, NULL, true, false, STREAM_SCOPE_TRAN };

  /* nothing a download ends or stages, and a factory only of the kind's own direction */
  CHECK (stream_kind_register (KIND_UNREGISTERED, ends_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, staging_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, sinking_download) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_register (KIND_UNREGISTERED, sourceless_download) == ER_STREAM_SESSION_ERROR);
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

TEST_CASE_METHOD (registry_fixture, "the client half answers who may open a kind", "[stream_session]")
{
  stream_kind_client_desc unchecked = { true, NULL };

  /* a driver-openable kind has to say who may open it */
  CHECK (stream_kind_client_register (KIND_UNREGISTERED, unchecked) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_client_check_driver_open (KIND_UNREGISTERED) == ER_STREAM_SESSION_ERROR);

  CHECK (stream_kind_client_check_driver_open (KIND_DOWNLOAD) == NO_ERROR);
  CHECK (stream_kind_client_check_driver_open (KIND_STAGED) == ER_STREAM_SESSION_ERROR);
  /* a kind that no client half names -- COPY's, opened by its statement -- is not a driver's to open */
  CHECK (stream_kind_client_check_driver_open (KIND_A) == ER_STREAM_SESSION_ERROR);
  CHECK (stream_kind_client_check_driver_open (STREAM_KIND_MAX) == ER_STREAM_SESSION_ERROR);

  CHECK (stream_kind_client_authorize (KIND_DOWNLOAD, "x", 1) == NO_ERROR);
  CHECK (stream_kind_client_authorize (KIND_STAGED, "x", 1) == ER_AU_SELECT_FAILURE);
  CHECK (test_Authorized == 2);

  /* the question was never answered, so it is not taken as yes */
  CHECK (stream_kind_client_authorize (KIND_A, "x", 1) == ER_STREAM_SESSION_ERROR);
  CHECK (test_Authorized == 2);
}

TEST_CASE_METHOD (registry_fixture, "a handle resolves only where it was issued", "[stream_handle]")
{
  stream_handle_table table, other_session;
  std::int64_t download = 0, staged = 0;

  REQUIRE (table.issue (NULL, KIND_DOWNLOAD, TRAN_1, 0, new fake_source ("v"), &download) == NO_ERROR);
  REQUIRE (table.issue (NULL, KIND_STAGED, TRAN_1, 10, new fake_object (), &staged) == NO_ERROR);
  CHECK (download > 0);
  CHECK (staged > 0);
  CHECK (download != staged);

  /* the issuing transaction for a STREAM_SCOPE_TRAN handle, any of the session's for STREAM_SCOPE_OWNER */
  stream_source *source = table.get_source (download, TRAN_1);
  CHECK (source != NULL);
  table.put (NULL, download);
  CHECK (table.get_source (download, TRAN_2) == NULL);
  CHECK (table.get (staged, KIND_STAGED, TRAN_2) != NULL);
  table.put (NULL, staged);

  /* another kind, another direction, another session */
  CHECK (table.get (staged, KIND_A, TRAN_1) == NULL);
  CHECK (table.get_source (staged, TRAN_1) == NULL);
  CHECK (table.get (download, KIND_STAGED, TRAN_1) == NULL);
  CHECK (other_session.get_source (download, TRAN_1) == NULL);
  CHECK (other_session.get (staged, KIND_STAGED, TRAN_1) == NULL);
  CHECK (test_Last_error == ER_STREAM_SESSION_ERROR);

  table.clear (NULL);
  CHECK (test_Closed == 2);
}

TEST_CASE_METHOD (registry_fixture, "handle numbers are positive and never repeat across sessions", "[stream_handle]")
{
  std::set<std::int64_t> seen;
  stream_handle_table tables[3];

  for (int round = 0; round < 100; round++)
    {
      for (stream_handle_table &t : tables)
	{
	  std::int64_t h = 0;

	  REQUIRE (t.issue (NULL, KIND_STAGED, TRAN_1, 0, new fake_object (), &h) == NO_ERROR);
	  CHECK (h > 0);
	  CHECK (seen.insert (h).second);
	  t.release (NULL, h);
	}
    }
}

TEST_CASE_METHOD (registry_fixture, "a download pin is exclusive and an upload pin is shared", "[stream_handle]")
{
  stream_handle_table table;
  std::int64_t download = 0, staged = 0;

  REQUIRE (table.issue (NULL, KIND_DOWNLOAD, TRAN_1, 0, new fake_source ("v"), &download) == NO_ERROR);
  REQUIRE (table.issue (NULL, KIND_STAGED, TRAN_1, 0, new fake_object (), &staged) == NO_ERROR);

  REQUIRE (table.get_source (download, TRAN_1) != NULL);
  /* a second READ sent before the first one's reply: refused, not serialized */
  CHECK (table.get_source (download, TRAN_1) == NULL);
  table.put (NULL, download);
  CHECK (table.get_source (download, TRAN_1) != NULL);
  table.put (NULL, download);

  /* several rows of one statement read the same staged value */
  CHECK (table.get (staged, KIND_STAGED, TRAN_1) != NULL);
  CHECK (table.get (staged, KIND_STAGED, TRAN_1) != NULL);
  table.put (NULL, staged);
  table.put (NULL, staged);

  table.clear (NULL);
}

TEST_CASE_METHOD (registry_fixture, "a release during a pin waits for the last put and refuses new pins",
		  "[stream_handle]")
{
  stream_handle_table table;
  std::int64_t staged = 0;

  REQUIRE (table.issue (NULL, KIND_STAGED, TRAN_1, 0, new fake_object (), &staged) == NO_ERROR);
  REQUIRE (table.get (staged, KIND_STAGED, TRAN_1) != NULL);
  REQUIRE (table.get (staged, KIND_STAGED, TRAN_1) != NULL);

  table.release (NULL, staged);
  CHECK (test_Closed == 0);
  CHECK (table.get (staged, KIND_STAGED, TRAN_1) == NULL);

  table.put (NULL, staged);
  CHECK (test_Closed == 0);
  table.put (NULL, staged);
  CHECK (test_Closed == 1);

  /* gone, and releasing it again is not an error */
  table.release (NULL, staged);
  table.release (NULL, 12345);
  CHECK (test_Closed == 1);
}

TEST_CASE_METHOD (registry_fixture, "a transaction end releases only that transaction's STREAM_SCOPE_TRAN handles",
		  "[stream_handle]")
{
  stream_handle_table table;
  std::int64_t d1 = 0, d2 = 0, staged = 0;

  REQUIRE (table.issue (NULL, KIND_DOWNLOAD, TRAN_1, 0, new fake_source ("a"), &d1) == NO_ERROR);
  REQUIRE (table.issue (NULL, KIND_DOWNLOAD, TRAN_2, 0, new fake_source ("b"), &d2) == NO_ERROR);
  REQUIRE (table.issue (NULL, KIND_STAGED, TRAN_1, 0, new fake_object (), &staged) == NO_ERROR);

  table.end_tran (NULL, TRAN_1);
  CHECK (test_Closed == 1);
  CHECK (table.get_source (d1, TRAN_1) == NULL);

  /* a staged upload outlives the commit, so a statement can run again with it */
  CHECK (table.get (staged, KIND_STAGED, TRAN_1) != NULL);
  table.put (NULL, staged);
  CHECK (table.get_source (d2, TRAN_2) != NULL);
  table.put (NULL, d2);

  /* an end that finds the handle pinned leaves it to the put */
  REQUIRE (table.get_source (d2, TRAN_2) != NULL);
  table.end_tran (NULL, TRAN_2);
  CHECK (test_Closed == 1);
  table.put (NULL, d2);
  CHECK (test_Closed == 2);

  table.clear (NULL);
  CHECK (test_Closed == 3);
}

TEST_CASE_METHOD (registry_fixture, "the limits refuse an issue and close what was refused", "[stream_handle]")
{
  stream_handle_table table;
  std::vector<std::int64_t> handles;
  std::int64_t h = 0;

  for (int i = 0; i < STREAM_HANDLE_MAX_PER_TRAN; i++)
    {
      REQUIRE (table.issue (NULL, KIND_DOWNLOAD, TRAN_1, 0, new fake_source ("v"), &h) == NO_ERROR);
      handles.push_back (h);
    }

  CHECK (table.issue (NULL, KIND_DOWNLOAD, TRAN_1, 0, new fake_source ("v"), &h) == ER_STREAM_SESSION_ERROR);
  CHECK (h == 0);
  CHECK (test_Closed == 1);

  /* per transaction: another one is not charged for it */
  CHECK (table.issue (NULL, KIND_DOWNLOAD, TRAN_2, 0, new fake_source ("v"), &h) == NO_ERROR);

  /* a release requested during a pin frees the slot at once */
  REQUIRE (table.get_source (handles[0], TRAN_1) != NULL);
  table.release (NULL, handles[0]);
  CHECK (table.issue (NULL, KIND_DOWNLOAD, TRAN_1, 0, new fake_source ("v"), &h) == NO_ERROR);
  table.put (NULL, handles[0]);

  /* staged bytes are counted per session */
  CHECK (table.issue (NULL, KIND_STAGED, TRAN_1, STREAM_HANDLE_MAX_OWNER_BYTES, new fake_object (), &h) == NO_ERROR);
  CHECK (table.issue (NULL, KIND_STAGED, TRAN_2, 1, new fake_object (), &h) == ER_STREAM_SESSION_ERROR);

  table.clear (NULL);
}

TEST_CASE_METHOD (registry_fixture, "a value within one read comes back with the open and leaves no handle",
		  "[stream_download]")
{
  stream_handle_table table;
  std::vector<char> buf (stream_read_bound (64));
  int nread = 0, reply_flags = 0;
  std::int64_t handle = -1;

  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, "hello", 5, TRAN_1, 64, STREAM_FLAG_RELEASE_AT_EOF,
				 buf.data (), &nread, &reply_flags, &handle) == NO_ERROR);
  CHECK (handle == 0);
  CHECK (std::string (buf.data (), (size_t) nread) == "hello");
  CHECK (reply_flags == (STREAM_FLAG_EOF | STREAM_FLAG_RELEASED));
  CHECK (test_Closed == 1);

  /* without RELEASE_AT_EOF the client keeps the handle and releases it itself */
  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, "hello", 5, TRAN_1, 64, 0, buf.data (), &nread,
				 &reply_flags, &handle) == NO_ERROR);
  CHECK (handle > 0);
  CHECK (reply_flags == STREAM_FLAG_EOF);

  /* a read after eof is not an error */
  REQUIRE (stream_download_read (NULL, table, handle, TRAN_1, 64, 0, buf.data (), &nread, &reply_flags) == NO_ERROR);
  CHECK (nread == 0);
  CHECK (reply_flags == STREAM_FLAG_EOF);

  table.release (NULL, handle);
  CHECK (test_Closed == 2);
}

TEST_CASE_METHOD (registry_fixture, "a larger value reads to eof and releases at eof when asked",
		  "[stream_download]")
{
  stream_handle_table table;
  std::string value (10000, 'x');
  std::vector<char> buf (stream_read_bound (4096));
  int nread = 0, reply_flags = 0;
  std::int64_t handle = 0;

  for (size_t i = 0; i < value.size (); i++)
    {
      value[i] = (char) ('a' + i % 26);
    }

  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, value.data (), (int) value.size (), TRAN_1, 4096,
				 STREAM_FLAG_RELEASE_AT_EOF, buf.data (), &nread, &reply_flags, &handle) == NO_ERROR);
  REQUIRE (handle > 0);
  CHECK (nread == 4096);
  CHECK (reply_flags == 0);

  std::string got (buf.data (), (size_t) nread);

  REQUIRE (stream_download_read (NULL, table, handle, TRAN_1, 4096, 0, buf.data (), &nread, &reply_flags) == NO_ERROR);
  got.append (buf.data (), (size_t) nread);
  REQUIRE (stream_download_read (NULL, table, handle, TRAN_1, 4096, STREAM_FLAG_RELEASE_AT_EOF, buf.data (), &nread,
				 &reply_flags) == NO_ERROR);
  got.append (buf.data (), (size_t) nread);
  CHECK (reply_flags == (STREAM_FLAG_EOF | STREAM_FLAG_RELEASED));
  CHECK (got == value);
  CHECK (test_Closed == 1);

  /* released: the next READ does not resolve */
  CHECK (stream_download_read (NULL, table, handle, TRAN_1, 4096, 0, buf.data (), &nread, &reply_flags)
	 == ER_STREAM_SESSION_ERROR);
}

TEST_CASE_METHOD (registry_fixture, "an unbundled open issues a handle and reads nothing", "[stream_download]")
{
  stream_handle_table table;
  int nread = -1, reply_flags = -1;
  std::int64_t handle = 0;

  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, "abc", 3, TRAN_1, 0, 0, NULL, &nread, &reply_flags,
				 &handle) == NO_ERROR);
  CHECK (handle > 0);
  CHECK (nread == 0);
  CHECK (reply_flags == 0);
  CHECK (read_all (table, handle, 2) == "abc");

  table.clear (NULL);
}

TEST_CASE_METHOD (registry_fixture, "a failed read releases its handle", "[stream_download]")
{
  stream_handle_table table;
  std::vector<char> buf (16);
  int nread = 0, reply_flags = 0;
  std::int64_t handle = 0;

  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, "?fail", 5, TRAN_1, 0, 0, NULL, &nread, &reply_flags,
				 &handle) == NO_ERROR);
  CHECK (stream_download_read (NULL, table, handle, TRAN_1, 16, 0, buf.data (), &nread, &reply_flags)
	 == ER_STREAM_SESSION_ERROR);
  CHECK (test_Closed == 1);
  CHECK (table.get_source (handle, TRAN_1) == NULL);

  /* a failed first read in a bundled open leaves no handle either */
  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, "?fail", 5, TRAN_1, 16, 0, buf.data (), &nread,
				 &reply_flags, &handle) == ER_STREAM_SESSION_ERROR);
  CHECK (handle == 0);
  CHECK (test_Closed == 2);
}

TEST_CASE_METHOD (registry_fixture, "the core holds a source to its read contract and its own bound",
		  "[stream_download]")
{
  stream_handle_table table;
  std::vector<char> buf (16);
  int nread = 0, reply_flags = 0;
  std::int64_t handle = 0;

  /* nothing and no eof would keep a client reading forever */
  CHECK (stream_download_open (NULL, table, KIND_DOWNLOAD, "~", 1, TRAN_1, 16, 0, buf.data (), &nread, &reply_flags,
			       &handle) == ER_STREAM_SESSION_ERROR);
  CHECK (handle == 0);
  CHECK (test_Closed == 1);

  CHECK (stream_read_bound (STREAM_READ_MAX_CHUNK + 1) == STREAM_READ_MAX_CHUNK);
  CHECK (stream_read_bound (1 << 30) == STREAM_READ_MAX_CHUNK);
  CHECK (stream_read_bound (100) == 100);

  REQUIRE (stream_download_open (NULL, table, KIND_DOWNLOAD, "abc", 3, TRAN_1, 0, 0, NULL, &nread, &reply_flags,
				 &handle) == NO_ERROR);
  CHECK (stream_download_read (NULL, table, handle, TRAN_1, 0, 0, buf.data (), &nread, &reply_flags)
	 == ER_STREAM_SESSION_ERROR);
  CHECK (stream_download_open (NULL, table, KIND_DOWNLOAD, "abc", 3, TRAN_1, -1, 0, NULL, &nread, &reply_flags,
			       &handle) == ER_STREAM_SESSION_ERROR);
  table.clear (NULL);
}

TEST_CASE_METHOD (registry_fixture, "an open refused by its factory or its kind issues nothing", "[stream_download]")
{
  stream_handle_table table;
  int nread = 0, reply_flags = 0;
  std::int64_t handle = -1;

  CHECK (stream_download_open (NULL, table, KIND_DOWNLOAD, "!no", 3, TRAN_1, 0, 0, NULL, &nread, &reply_flags,
			       &handle) == ER_STREAM_SESSION_ERROR);
  CHECK (handle == 0);
  CHECK (stream_download_open (NULL, table, KIND_A, "abc", 3, TRAN_1, 0, 0, NULL, &nread, &reply_flags, &handle)
	 == ER_STREAM_SESSION_ERROR);
  CHECK (handle == 0);
  CHECK (test_Closed == 0);
}
