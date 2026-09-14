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

#include "config.h"
#include "pgbuf_inspector_wire.hpp"
#include "db_rapidjson.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpgbuf
{
  namespace inspector
  {
    namespace
    {
      using value = rapidjson::Value;
      using writer = rapidjson::Writer<rapidjson::StringBuffer>;

      std::string_view
      text (const value &v)
      {
	return v.IsString () ? std::string_view (v.GetString (), v.GetStringLength ()) : std::string_view ();
      }

      bool
      uint_string (const value &v, std::uint64_t maximum = UINT64_MAX, bool positive = false)
      {
	auto s = text (v);
	if (s.empty () || (s.size () > 1 && s.front () == '0'))
	  {
	    return false;
	  }
	std::uint64_t n = 0;
	for (char c : s)
	  {
	    if (c < '0' || c > '9' || n > (maximum - (c - '0')) / 10)
	      {
		return false;
	      }
	    n = n * 10 + c - '0';
	  }
	return !positive || n != 0;
      }

      bool
      hex_id (const value &v)
      {
	auto s = text (v);
	return s.size () == 32 && std::all_of (s.begin (), s.end (), [] (char c)
	{
	  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
	});
      }

      bool
      integer (const value &v, int maximum = INT32_MAX)
      {
	return v.IsInt () && v.GetInt () >= 0 && v.GetInt () <= maximum;
      }

      bool
      unique_members (const value &v)
      {
	if (v.IsObject ())
	  {
	    std::set<std::string_view> names;
	    for (auto it = v.MemberBegin (); it != v.MemberEnd (); ++it)
	      if (!names.insert (text (it->name)).second || !unique_members (it->value))
		{
		  return false;
		}
	  }
	if (v.IsArray ())
	  for (const auto &item : v.GetArray ()) if (!unique_members (item))
	      {
		return false;
	      }
	return true;
      }

      struct depth_guard : rapidjson::BaseReaderHandler<rapidjson::UTF8<>, depth_guard>
      {
	unsigned depth = 0;
	bool StartObject ()
	{
	  return ++depth <= 16;
	}
	bool StartArray ()
	{
	  return ++depth <= 16;
	}
	bool EndObject (rapidjson::SizeType)
	{
	  --depth;
	  return true;
	}
	bool EndArray (rapidjson::SizeType)
	{
	  --depth;
	  return true;
	}
      };

      bool
      parse (std::string_view input, rapidjson::Document &d)
      {
	if (input.empty () || input.size () > WIRE_HANDSHAKE_FRAME_MAX_BYTES
	    || input.find ('\0') != std::string_view::npos)
	  {
	    return false;
	  }
	rapidjson::MemoryStream stream (input.data (), input.size ());
	rapidjson::Reader reader;
	depth_guard guard;
	if (!reader.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag> (stream, guard))
	  {
	    return false;
	  }
	d.Parse<rapidjson::kParseValidateEncodingFlag> (input.data (), input.size ());
	return !d.HasParseError () && d.IsObject () && unique_members (d)
	       && d.HasMember ("type") && d["type"].IsString ();
      }

      enum class shape { INT, RETRY, VOLID, COUNT, UINT_STRING, SEQUENCE, HEX, BOOL, STRING, INDEX, LSA, VOLUMES, MAJORS };
      struct field
      {
	const char *name;
	shape kind;
	bool optional = false;
      };
      const std::vector<field> IDENTITY = {{"volid", shape::VOLID}, {"volume_creation", shape::UINT_STRING},
	{"device", shape::UINT_STRING}, {"inode", shape::UINT_STRING}
      };
      const std::vector<field> LSA = {{"pageid", shape::UINT_STRING}, {"offset", shape::VOLID}};
      const std::vector<field> CLIENT = {{"supported_majors", shape::MAJORS}, {"expected_incarnation", shape::HEX, true}};
      const std::vector<field> SERVER = {{"protocol_major", shape::INT}, {"protocol_minor", shape::INT},
	{"incarnation", shape::HEX}, {"database_creation", shape::UINT_STRING}, {"volumes", shape::VOLUMES},
	{"shared_lru_count", shape::INT}, {"private_lru_count", shape::INT}
      };
      const std::vector<field> REQUEST = {{"incarnation", shape::HEX}};
      const std::vector<field> HEADER = {{"incarnation", shape::HEX}, {"scan_seq", shape::SEQUENCE},
	{"start_time_us", shape::UINT_STRING}
      };
      const std::vector<field> PAGE = {{"incarnation", shape::HEX}, {"scan_seq", shape::SEQUENCE},
	{"volid", shape::VOLID}, {"pageid", shape::INT}, {"latch_mode", shape::STRING, true},
	{"waiter_present", shape::BOOL, true}, {"fix_count", shape::INT, true}, {"dirty", shape::BOOL, true},
	{"flushing", shape::BOOL, true}, {"async_flush_requested", shape::BOOL, true}, {"to_vacuum", shape::BOOL, true},
	{"lru_zone", shape::STRING, true}, {"lru_list_kind", shape::STRING, true}, {"lru_list_index", shape::INDEX, true},
	{"page_lsa", shape::LSA, true}, {"oldest_unflush_lsa", shape::LSA, true}, {"page_kind", shape::STRING, true}
      };
      const std::vector<field> FOOTER = {{"incarnation", shape::HEX}, {"scan_seq", shape::SEQUENCE},
	{"end_time_us", shape::UINT_STRING}, {"record_count", shape::COUNT}, {"visited_slots", shape::COUNT},
	{"truncated", shape::BOOL}
      };
      const std::vector<field> ERROR = {{"code", shape::STRING}, {"supported_majors", shape::MAJORS, true},
	{"retry_after_ms", shape::RETRY, true}
      };

      const std::vector<field> *
      fields (std::string_view kind)
      {
	if (kind == "client_hello")
	  {
	    return &CLIENT;
	  }
	if (kind == "server_hello")
	  {
	    return &SERVER;
	  }
	if (kind == "scan_request")
	  {
	    return &REQUEST;
	  }
	if (kind == "scan_header")
	  {
	    return &HEADER;
	  }
	if (kind == "page")
	  {
	    return &PAGE;
	  }
	if (kind == "scan_footer")
	  {
	    return &FOOTER;
	  }
	if (kind == "error")
	  {
	    return &ERROR;
	  }
	return nullptr;
      }

      bool matches (const value &v, const std::vector<field> &schema);

      bool
      matches (const value &v, shape kind)
      {
	switch (kind)
	  {
	  case shape::INT:
	    return integer (v);
	  case shape::RETRY:
	    return v.IsUint () && v.GetUint () > 0;
	  case shape::VOLID:
	    return integer (v, 32767);
	  case shape::COUNT:
	    return integer (v, 65536);
	  case shape::UINT_STRING:
	    return uint_string (v);
	  case shape::SEQUENCE:
	    return uint_string (v, UINT64_MAX, true);
	  case shape::HEX:
	    return hex_id (v);
	  case shape::BOOL:
	    return v.IsBool ();
	  case shape::STRING:
	    return v.IsString () && v.GetStringLength () > 0;
	  case shape::INDEX:
	    return v.IsNull () || integer (v);
	  case shape::LSA:
	    return v.IsNull () || (matches (v, LSA) && uint_string (v["pageid"], INT64_MAX));
	  case shape::MAJORS:
	    if (!v.IsArray () || v.Empty ())
	      {
		return false;
	      }
	    for (const auto &n : v.GetArray ()) if (!integer (n) || n.GetInt () == 0)
		{
		  return false;
		}
	    return true;
	  case shape::VOLUMES:
	    if (!v.IsArray () || v.Empty ())
	      {
		return false;
	      }
	    {
	      std::set<int> ids;
	      for (const auto &id : v.GetArray ())
		if (!matches (id, IDENTITY) || !ids.insert (id["volid"].GetInt ()).second)
		  {
		    return false;
		  }
	    }
	    return true;
	  }
	return false;
      }

      bool
      matches (const value &v, const std::vector<field> &schema)
      {
	if (!v.IsObject ())
	  {
	    return false;
	  }
	for (const auto &f : schema)
	  {
	    auto m = v.FindMember (f.name);
	    if (m == v.MemberEnd ())
	      {
		if (!f.optional)
		  {
		    return false;
		  }
	      }
	    else if (!matches (m->value, f.kind))
	      {
		return false;
	      }
	  }
	return true;
      }

      bool
      one_of (std::string_view name, std::initializer_list<const char *> values)
      {
	for (const char *v : values) if (name == v)
	    {
	      return true;
	    }
	return false;
      }

      bool
      valid_frame (const value &d, bool producer)
      {
	auto kind = text (d["type"]);
	auto schema = fields (kind);
	if (!schema || !matches (d, *schema))
	  {
	    return false;
	  }
	if (kind == "server_hello" && d["protocol_major"].GetInt () != 1)
	  {
	    return false;
	  }
	if (kind == "scan_footer" && d["record_count"].GetInt () > d["visited_slots"].GetInt ())
	  {
	    return false;
	  }
	if (kind == "error")
	  {
	    if (producer)
	      {
		error_frame error;
		if (!refusal_code_from_wire_name (d["code"].GetString (), error.code)
		    || text (d["code"]) != refusal_code_wire_name (error.code))
		  {
		    return false;
		  }
		if (d.HasMember ("supported_majors"))
		  for (const auto &major : d["supported_majors"].GetArray ())
		    {
		      error.supported_majors.push_back (major.GetInt ());
		    }
		if (d.HasMember ("retry_after_ms"))
		  {
		    error.retry_after_ms = d["retry_after_ms"].GetUint ();
		  }
		std::string encoded;
		/* Size is checked after canonical emission; preserve the caller's FRAME_TOO_LARGE result. */
		if (encode_error_frame (error, encoded) == encode_status::INVALID_VALUE)
		  {
		    return false;
		  }
	      }
	  }
	if (kind != "page")
	  {
	    return true;
	  }
	if (producer)
	  {
	    if (d.HasMember ("latch_mode") && !one_of (text (d["latch_mode"]),
	    {"none", "read", "write", "flush", "unknown"})) return false;
	    if (d.HasMember ("lru_zone") && !one_of (text (d["lru_zone"]),
	    {"lru1", "lru2", "lru3", "void", "invalid"})) return false;
	    if (d.HasMember ("lru_list_kind") && !one_of (text (d["lru_list_kind"]),
	    {"shared", "private", "none", "invalid"})) return false;
	    if (d.HasMember ("page_kind") && !one_of (text (d["page_kind"]),
	    {
	      "unknown", "ftab", "heap", "volheader", "volbitmap", "qresult", "ehash", "overflow", "oos",
	      "area", "catalog", "btree", "log", "dropped_files", "vacuum_data"
	    })) return false;
	  }
	if (d.HasMember ("lru_list_kind") && d.HasMember ("lru_list_index"))
	  {
	    auto k = text (d["lru_list_kind"]);
	    if (one_of (k, {"shared", "private"}) && d["lru_list_index"].IsNull ()) return false;
	    if (one_of (k, {"none", "invalid"}) && !d["lru_list_index"].IsNull ()) return false;
	    if (d.HasMember ("lru_zone") && one_of (k, {"shared", "private", "none", "invalid"}))
	      {
		auto zone = text (d["lru_zone"]);
		if (one_of (zone, {"void", "invalid"}) && k != "none") return false;
		if (one_of (zone, {"lru1", "lru2", "lru3"}) && k == "none") return false;
	      }
	  }
	return true;
      }

      void
      emit (writer &w, const value &v, const std::vector<field> &schema, bool frame)
      {
	w.StartObject ();
	if (frame)
	  {
	    w.Key ("type");
	    v["type"].Accept (w);
	  }
	for (const auto &f : schema)
	  {
	    if (!v.HasMember (f.name))
	      {
		continue;
	      }
	    const auto &item = v[f.name];
	    w.Key (f.name);
	    if (f.kind == shape::LSA && !item.IsNull ())
	      {
		emit (w, item, LSA, false);
	      }
	    else if (f.kind == shape::VOLUMES)
	      {
		std::vector<const value *> ids;
		for (const auto &id : item.GetArray ())
		  {
		    ids.push_back (&id);
		  }
		std::sort (ids.begin (), ids.end (), [] (const value *a, const value *b)
		{
		  return (*a)["volid"].GetInt () < (*b)["volid"].GetInt ();
		});
		w.StartArray ();
		for (const auto *id : ids)
		  {
		    emit (w, *id, IDENTITY, false);
		  }
		w.EndArray ();
	      }
	    else
	      {
		item.Accept (w);
	      }
	  }
	w.EndObject ();
      }
    }

    const char *
    page_kind_name (int native_type, page_type_layout layout)
    {
      static const char *const names[] = {"unknown", "ftab", "heap", "volheader", "volbitmap", "qresult", "ehash",
					  "overflow", "oos", "area", "catalog", "btree", "log", "dropped_files", "vacuum_data"
					 };
      if (layout != page_type_layout::DEVELOP && layout != page_type_layout::OOS)
	{
	  return "unknown";
	}
      const int last = layout == page_type_layout::OOS ? 14 : 13;
      if (native_type < 0 || native_type > last)
	{
	  return "unknown";
	}
      if (layout == page_type_layout::DEVELOP && native_type >= 8)
	{
	  ++native_type;
	}
      return names[native_type];
    }

    bool
    decode_client_hello (std::string_view line, client_hello &hello)
    {
      rapidjson::Document d;
      if (line.size () > WIRE_CONTROL_FRAME_MAX_BYTES || !parse (line, d)
	  || text (d["type"]) != "client_hello" || !valid_frame (d, false))
	{
	  return false;
	}
      hello = client_hello {};
      for (const auto &major : d["supported_majors"].GetArray ())
	if (major.GetInt () == WIRE_PROTOCOL_MAJOR)
	  {
	    hello.supports_v1 = true;
	  }
      if (d.HasMember ("expected_incarnation"))
	{
	  hello.expected_incarnation = std::string (text (d["expected_incarnation"]));
	}
      return true;
    }

    bool decode_scan_request (std::string_view line, std::string &incarnation)
    {
      rapidjson::Document d;
      if (line.size () > WIRE_CONTROL_FRAME_MAX_BYTES || !parse (line, d)
	  || text (d["type"]) != "scan_request" || !valid_frame (d, false))
	{
	  return false;
	}
      incarnation = std::string (text (d["incarnation"]));
      return true;
    }

    encode_status
    encode_frame (std::string_view json, std::string &out)
    {
      if (json.size () > WIRE_HANDSHAKE_FRAME_MAX_BYTES)
	{
	  return encode_status::FRAME_TOO_LARGE;
	}
      rapidjson::Document d;
      if (!parse (json, d) || !valid_frame (d, true))
	{
	  return encode_status::INVALID_VALUE;
	}
      rapidjson::StringBuffer buffer;
      writer w (buffer);
      emit (w, d, *fields (text (d["type"])), true);
      auto max = text (d["type"]) == "server_hello" ? WIRE_HANDSHAKE_FRAME_MAX_BYTES : WIRE_CONTROL_FRAME_MAX_BYTES;
      if (buffer.GetSize () + 1 > max)
	{
	  return encode_status::FRAME_TOO_LARGE;
	}
      out.append (buffer.GetString (), buffer.GetSize ());
      out += '\n';
      return encode_status::OK;
    }
    bool
    exchange_validator::fail ()
    {
      m_phase = phase::FAILED;
      m_published = false;
      m_pages.clear ();
      m_pending.clear ();
      return false;
    }

    bool
    exchange_validator::feed (std::string_view bytes)
    {
      if (m_phase == phase::FAILED)
	{
	  return false;
	}
      for (char c : bytes)
	{
	  const auto max = m_phase == phase::HELLO ? WIRE_HANDSHAKE_FRAME_MAX_BYTES : WIRE_CONTROL_FRAME_MAX_BYTES;
	  if (m_pending.size () >= max)
	    {
	      return fail ();
	    }
	  m_pending += c;
	  if (c == '\n')
	    {
	      if (!accept (m_pending))
		{
		  return fail ();
		}
	      m_pending.clear ();
	    }
	}
      return true;
    }

    bool
    exchange_validator::finish ()
    {
      if (!m_pending.empty () || (m_phase != phase::READY && m_phase != phase::CLOSED))
	{
	  return fail ();
	}
      return true;
    }

    observation
    exchange_validator::lookup (int volid, int pageid, bool evaluated) const
    {
      if (!m_published || !evaluated || volid < 0 || volid > 32767 || pageid < 0)
	{
	  return observation::UNKNOWN;
	}
      auto page = m_pages.find ({volid, pageid});
      if (page != m_pages.end ())
	{
	  return page->second ? observation::AMBIGUOUS : observation::RESIDENT;
	}
      return m_truncated ? observation::UNKNOWN : observation::NOT_RESIDENT;
    }

    bool
    exchange_validator::accept (std::string_view line)
    {
      rapidjson::Document d;
      if (!parse (line, d))
	{
	  return false;
	}
      const auto kind = text (d["type"]);
      const auto maximum = kind == "server_hello" ? WIRE_HANDSHAKE_FRAME_MAX_BYTES : WIRE_CONTROL_FRAME_MAX_BYTES;
      if (line.size () > maximum)
	{
	  return false;
	}
      if (m_phase == phase::SCAN)
	{
	  if (line.size () > 1073741824 - m_scan_bytes)
	    {
	      return false;
	    }
	  m_scan_bytes += line.size ();
	}
      if (!fields (kind))
	{
	  return m_phase == phase::SCAN;
	}
      if (!valid_frame (d, false))
	{
	  return false;
	}
      if (kind == "error")
	{
	  const auto code = text (d["code"]);
	  if (m_phase != phase::HELLO && m_phase != phase::HEADER)
	    {
	      return false;
	    }
	  if (code == "rate-limited" && m_phase != phase::HEADER)
	    {
	      return false;
	    }
	  m_phase = code == "rate-limited" ? phase::READY : phase::CLOSED;
	  m_published = false;
	  m_pages.clear ();
	  return true;
	}
      if (m_phase == phase::CLIENT && kind == "client_hello")
	{
	  if (d.HasMember ("expected_incarnation"))
	    {
	      m_expected = text (d["expected_incarnation"]);
	    }
	  for (const auto &major : d["supported_majors"].GetArray ()) if (major.GetInt () == 1)
	      {
		m_offers_v1 = true;
	      }
	  m_phase = phase::HELLO;
	  return true;
	}
      if (m_phase == phase::HELLO && kind == "server_hello")
	{
	  if (!m_offers_v1 || (!m_expected.empty () && m_expected != text (d["incarnation"])))
	    {
	      return false;
	    }
	  m_incarnation = text (d["incarnation"]);
	  m_shared = d["shared_lru_count"].GetInt ();
	  m_private = d["private_lru_count"].GetInt ();
	  m_phase = phase::READY;
	  return true;
	}
      if (!d.HasMember ("incarnation") || text (d["incarnation"]) != m_incarnation)
	{
	  return false;
	}
      if (m_phase == phase::READY && kind == "scan_request")
	{
	  m_published = false;
	  m_pages.clear ();
	  m_records = 0;
	  m_phase = phase::HEADER;
	  return true;
	}
      std::uint64_t sequence = 0;
      if (d.HasMember ("scan_seq"))
	for (char c : text (d["scan_seq"]))
	  {
	    sequence = sequence * 10 + c - '0';
	  }
      if (m_phase == phase::HEADER && kind == "scan_header")
	{
	  if (sequence <= m_sequence)
	    {
	      return false;
	    }
	  m_sequence = sequence;
	  m_scan_bytes = line.size ();
	  m_phase = phase::SCAN;
	  return true;
	}
      if (m_phase != phase::SCAN || sequence != m_sequence)
	{
	  return false;
	}
      if (kind == "page")
	{
	  if (m_records >= 65536)
	    {
	      return false;
	    }
	  if (d.HasMember ("lru_list_kind") && d.HasMember ("lru_list_index")
	      && !d["lru_list_index"].IsNull ())
	    {
	      auto k = text (d["lru_list_kind"]);
	      int index = d["lru_list_index"].GetInt ();
	      if ((k == "shared" && index >= m_shared) || (k == "private" && index >= m_private))
		{
		  return false;
		}
	    }
	  ++m_records;
	  auto entry = m_pages.emplace (std::make_pair (d["volid"].GetInt (), d["pageid"].GetInt ()), false);
	  if (!entry.second)
	    {
	      entry.first->second = true;
	    }
	  return true;
	}
      if (kind == "scan_footer")
	{
	  if (d["record_count"].GetUint () != m_records)
	    {
	      return false;
	    }
	  m_truncated = d["truncated"].GetBool ();
	  m_published = true;
	  m_phase = phase::READY;
	  return true;
	}
      return false;
    }
  }
}
