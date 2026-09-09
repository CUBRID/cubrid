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
 * pgbuf_inspector_wire.cpp - canonical serializer for the page-buffer inspector wire contract
 */

#include "pgbuf_inspector_wire.hpp"

#include <cstdio>
#include <cstring>

namespace cubpgbuf
{
  namespace inspector
  {
    namespace
    {
      struct refusal_code_name
      {
	refusal_code code;
	const char *name;
      };

      /* The one place the refusal vocabulary of contract section 9 is spelled out. */
      constexpr refusal_code_name REFUSAL_CODE_NAMES[] =
      {
	{ refusal_code::VERSION_UNSUPPORTED, "version-unsupported" },
	{ refusal_code::BUSY, "busy" },
	{ refusal_code::INCARNATION_CHANGED, "incarnation-changed" },
	{ refusal_code::IDENTITY_OVERSIZED, "identity-oversized" },
	{ refusal_code::RATE_LIMITED, "rate-limited" }
      };
    }

    const char *
    refusal_code_wire_name (refusal_code code)
    {
      for (const refusal_code_name &entry : REFUSAL_CODE_NAMES)
	{
	  if (entry.code == code)
	    {
	      return entry.name;
	    }
	}
      return "";
    }

    bool
    refusal_code_from_wire_name (const char *name, refusal_code &code)
    {
      for (const refusal_code_name &entry : REFUSAL_CODE_NAMES)
	{
	  if (strcmp (entry.name, name) == 0)
	    {
	      code = entry.code;
	      return true;
	    }
	}
      return false;
    }

    encode_status
    encode_error_frame (const error_frame &frame, std::string &out)
    {
      if (refusal_code_wire_name (frame.code)[0] == '\0')
	{
	  return encode_status::INVALID_VALUE;
	}

      /* Contract section 9: each detail appears exactly with its own code and never otherwise. */
      const bool wants_majors = (frame.code == refusal_code::VERSION_UNSUPPORTED);
      const bool wants_retry = (frame.code == refusal_code::RATE_LIMITED);

      if (wants_majors != !frame.supported_majors.empty ())
	{
	  return encode_status::INVALID_VALUE;
	}
      if (wants_retry != frame.retry_after_ms.has_value ())
	{
	  return encode_status::INVALID_VALUE;
	}
      for (int major : frame.supported_majors)
	{
	  if (major <= 0)
	    {
	      return encode_status::INVALID_VALUE;
	    }
	}
      if (wants_retry && *frame.retry_after_ms == 0)
	{
	  return encode_status::INVALID_VALUE;
	}

      /* Even one-digit majors need two bytes each; refuse before constructing an oversized frame. */
      if (frame.supported_majors.size () > WIRE_CONTROL_FRAME_MAX_BYTES / 2)
	{
	  return encode_status::FRAME_TOO_LARGE;
	}
      canonical_frame_writer writer ("error");
      writer.add_string ("code", refusal_code_wire_name (frame.code));
      if (wants_majors)
	{
	  writer.add_int_array ("supported_majors", frame.supported_majors);
	}
      if (wants_retry)
	{
	  writer.add_uint ("retry_after_ms", *frame.retry_after_ms);
	}
      return writer.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out);
    }

    canonical_frame_writer::canonical_frame_writer (const char *type)
      : m_text ()
      , m_invalid (false)
    {
      m_text.reserve (128);
      m_text += '{';
      append_key ("type");
      append_quoted_ascii (type);
    }

    void
    canonical_frame_writer::append_key (const char *key)
    {
      if (m_text.size () > 1)
	{
	  m_text += ',';
	}
      append_quoted_ascii (key);
      m_text += ':';
    }

    void
    canonical_frame_writer::append_quoted_ascii (const char *value)
    {
      m_text += '"';
      for (const char *p = value; *p != '\0'; ++p)
	{
	  const unsigned char c = static_cast<unsigned char> (*p);
	  if (c < 0x20 || c > 0x7E)
	    {
	      /* The contract's producer strings are printable ASCII by construction. */
	      m_invalid = true;
	      break;
	    }
	  if (c == '"' || c == '\\')
	    {
	      m_text += '\\';
	    }
	  m_text += static_cast<char> (c);
	}
      m_text += '"';
    }

    void
    canonical_frame_writer::add_string (const char *key, const char *value)
    {
      append_key (key);
      append_quoted_ascii (value);
    }

    void
    canonical_frame_writer::add_int (const char *key, std::int64_t value)
    {
      char buffer[32];
      snprintf (buffer, sizeof (buffer), "%lld", static_cast<long long> (value));
      append_key (key);
      m_text += buffer;
    }

    void
    canonical_frame_writer::add_uint (const char *key, std::uint64_t value)
    {
      char buffer[32];
      snprintf (buffer, sizeof (buffer), "%llu", static_cast<unsigned long long> (value));
      append_key (key);
      m_text += buffer;
    }

    void
    canonical_frame_writer::add_bool (const char *key, bool value)
    {
      append_key (key);
      m_text += value ? "true" : "false";
    }

    void
    canonical_frame_writer::add_null (const char *key)
    {
      append_key (key);
      m_text += "null";
    }

    void
    canonical_frame_writer::add_int_array (const char *key, const std::vector<int> &values)
    {
      append_key (key);
      m_text += '[';
      for (std::size_t i = 0; i < values.size (); ++i)
	{
	  char buffer[16];
	  snprintf (buffer, sizeof (buffer), "%d", values[i]);
	  if (i > 0)
	    {
	      m_text += ',';
	    }
	  m_text += buffer;
	}
      m_text += ']';
    }

    encode_status
    canonical_frame_writer::finish (std::size_t max_bytes, std::string &out)
    {
      if (m_invalid)
	{
	  return encode_status::INVALID_VALUE;
	}
      m_text += '}';
      m_text += '\n';
      if (m_text.size () > max_bytes)
	{
	  return encode_status::FRAME_TOO_LARGE;
	}
      out += m_text;
      return encode_status::OK;
    }
  }
}
