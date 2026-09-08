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
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "pgbuf_inspector_wire.hpp"
#include "corpus_support.hpp"
#include "sha256.hpp"

#include "db_rapidjson.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace cubpgbuf::inspector;

/* The expected bytes below are the contract's own examples, typed as literals
 * so that the test can disagree with the encoder. */

TEST_CASE ("A refusal without details encodes as the canonical error frame", "[pgbuf_inspector][wire]")
{
  error_frame frame;
  frame.code = refusal_code::BUSY;

  std::string out;
  REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
  REQUIRE (out == "{\"type\":\"error\",\"code\":\"busy\"}\n");
}

TEST_CASE ("Every stable refusal code encodes with exactly its own details", "[pgbuf_inspector][wire]")
{
  std::string out;

  SECTION ("version-unsupported carries the producer's majors")
  {
    error_frame frame;
    frame.code = refusal_code::VERSION_UNSUPPORTED;
    frame.supported_majors = { 1 };
    REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"error\",\"code\":\"version-unsupported\",\"supported_majors\":[1]}\n");
  }
  SECTION ("rate-limited carries the wait in milliseconds")
  {
    error_frame frame;
    frame.code = refusal_code::RATE_LIMITED;
    frame.retry_after_ms = 60;
    REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"error\",\"code\":\"rate-limited\",\"retry_after_ms\":60}\n");
  }
  SECTION ("incarnation-changed and identity-oversized carry nothing else")
  {
    error_frame frame;
    frame.code = refusal_code::INCARNATION_CHANGED;
    REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"error\",\"code\":\"incarnation-changed\"}\n");

    out.clear ();
    frame.code = refusal_code::IDENTITY_OVERSIZED;
    REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"error\",\"code\":\"identity-oversized\"}\n");
  }
}

TEST_CASE ("A detail that belongs to another code, or is missing for its own, is not encodable",
	   "[pgbuf_inspector][wire]")
{
  std::string out = "kept";

  SECTION ("version-unsupported must list at least one positive major")
  {
    error_frame frame;
    frame.code = refusal_code::VERSION_UNSUPPORTED;
    REQUIRE (encode_error_frame (frame, out) == encode_status::INVALID_VALUE);
    frame.supported_majors = { 1, 0 };
    REQUIRE (encode_error_frame (frame, out) == encode_status::INVALID_VALUE);
  }
  SECTION ("rate-limited must carry a positive wait")
  {
    error_frame frame;
    frame.code = refusal_code::RATE_LIMITED;
    REQUIRE (encode_error_frame (frame, out) == encode_status::INVALID_VALUE);
    frame.retry_after_ms = 0;
    REQUIRE (encode_error_frame (frame, out) == encode_status::INVALID_VALUE);
  }
  SECTION ("details never travel with a code they do not belong to")
  {
    error_frame frame;
    frame.code = refusal_code::BUSY;
    frame.supported_majors = { 1 };
    REQUIRE (encode_error_frame (frame, out) == encode_status::INVALID_VALUE);

    frame.supported_majors.clear ();
    frame.retry_after_ms = 60;
    REQUIRE (encode_error_frame (frame, out) == encode_status::INVALID_VALUE);
  }

  /* Nothing is appended when encoding is refused. */
  REQUIRE (out == "kept");
}

namespace
{
  /* Majors whose canonical version-unsupported frame is exactly target bytes long: one-digit majors
   * add two bytes each (",1"), and a two-digit first major adds one more, so both parities are reachable. */
  std::vector<int>
  majors_for_exact_frame_size (std::size_t target)
  {
    error_frame probe;
    probe.code = refusal_code::VERSION_UNSUPPORTED;
    probe.supported_majors = { 1 };
    std::string one;
    REQUIRE (encode_error_frame (probe, one) == encode_status::OK);
    REQUIRE (target >= one.size ());

    std::size_t extra = target - one.size ();
    std::vector<int> majors = { 1 };
    if (extra % 2 == 1)
      {
	majors[0] = 10;
	extra -= 1;
      }
    for (; extra > 0; extra -= 2)
      {
	majors.push_back (1);
      }
    return majors;
  }
}

TEST_CASE ("A control frame may fill its 4,096-byte limit exactly but not exceed it", "[pgbuf_inspector][wire]")
{
  error_frame frame;
  frame.code = refusal_code::VERSION_UNSUPPORTED;
  std::string out;

  SECTION ("4,095 bytes are accepted")
  {
    frame.supported_majors = majors_for_exact_frame_size (4095);
    REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
    REQUIRE (out.size () == 4095);
  }
  SECTION ("4,096 bytes are accepted")
  {
    frame.supported_majors = majors_for_exact_frame_size (4096);
    REQUIRE (encode_error_frame (frame, out) == encode_status::OK);
    REQUIRE (out.size () == 4096);
    REQUIRE (out.back () == '\n');
  }
  SECTION ("4,097 bytes are refused and nothing is appended")
  {
    frame.supported_majors = majors_for_exact_frame_size (4097);
    REQUIRE (encode_error_frame (frame, out) == encode_status::FRAME_TOO_LARGE);
    REQUIRE (out.empty ());
  }
}

TEST_CASE ("The canonical writer follows the contract's canonical form", "[pgbuf_inspector][wire]")
{
  std::string out;

  SECTION ("keys keep call order after type, with no whitespace anywhere")
  {
    canonical_frame_writer writer ("probe");
    writer.add_int ("b", 2);
    writer.add_int ("a", 1);
    REQUIRE (writer.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"probe\",\"b\":2,\"a\":1}\n");
  }
  SECTION ("integers are base 10 with a sign only when negative, across the 64-bit range")
  {
    canonical_frame_writer writer ("probe");
    writer.add_int ("zero", 0);
    writer.add_int ("negative", -1);
    writer.add_int ("min", INT64_MIN);
    writer.add_int ("max", INT64_MAX);
    writer.add_uint ("umax", UINT64_MAX);
    REQUIRE (writer.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"probe\",\"zero\":0,\"negative\":-1,\"min\":-9223372036854775808,"
	     "\"max\":9223372036854775807,\"umax\":18446744073709551615}\n");
  }
  SECTION ("booleans, null and an empty array are the bare literals")
  {
    canonical_frame_writer writer ("probe");
    writer.add_bool ("yes", true);
    writer.add_bool ("no", false);
    writer.add_null ("none");
    writer.add_int_array ("empty", {});
    writer.add_int_array ("some", { 3, 1, 2 });
    REQUIRE (writer.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"probe\",\"yes\":true,\"no\":false,\"none\":null,\"empty\":[],\"some\":[3,1,2]}\n");
  }
  SECTION ("strings escape only the quote and the backslash")
  {
    canonical_frame_writer writer ("probe");
    writer.add_string ("s", "a\"b\\c/d");
    REQUIRE (writer.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out) == encode_status::OK);
    REQUIRE (out == "{\"type\":\"probe\",\"s\":\"a\\\"b\\\\c/d\"}\n");
  }
  SECTION ("a string outside printable ASCII makes the frame unencodable and appends nothing")
  {
    canonical_frame_writer control ("probe");
    control.add_string ("s", "tab\there");
    REQUIRE (control.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out) == encode_status::INVALID_VALUE);

    canonical_frame_writer high ("probe");
    high.add_string ("s", "caf\xC3\xA9");
    REQUIRE (high.finish (WIRE_CONTROL_FRAME_MAX_BYTES, out) == encode_status::INVALID_VALUE);

    REQUIRE (out.empty ());
  }
  SECTION ("a frame over the requested limit appends nothing")
  {
    canonical_frame_writer writer ("probe");
    REQUIRE (writer.finish (10, out) == encode_status::FRAME_TOO_LARGE);
    REQUIRE (out.empty ());
  }
}

TEST_CASE ("SHA-256 matches the FIPS 180 known answers", "[pgbuf_inspector][corpus]")
{
  REQUIRE (corpus::sha256_hex ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  REQUIRE (corpus::sha256_hex ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  REQUIRE (corpus::sha256_hex ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
	   == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  /* Exactly one block of padding boundary: 56 bytes forces a second block. */
  REQUIRE (corpus::sha256_hex (std::string (56,
			       'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

namespace
{
  const std::string CONTRACT_DIR = PGBUF_INSPECTOR_CONTRACT_DIR;
  const std::string CORPUS_DIR = CONTRACT_DIR + "/corpus";
}

TEST_CASE ("Every generated corpus stream is exactly what the serializer produces from its semantic input",
	   "[pgbuf_inspector][corpus]")
{
  const std::vector<std::string> case_dirs = corpus::semantic_case_directories (CORPUS_DIR);
  REQUIRE (!case_dirs.empty ());

  for (const std::string &dir : case_dirs)
    {
      INFO ("case directory " << dir);

      std::string semantic_text;
      REQUIRE (corpus::read_file (dir + "/semantic.json", semantic_text));
      std::string checked_in_stream;
      REQUIRE (corpus::read_file (dir + "/stream.jsonl", checked_in_stream));

      corpus::semantic_case sc;
      std::string why;
      REQUIRE (corpus::parse_semantic_case (semantic_text, sc, why));
      REQUIRE (dir.size () > sc.case_id.size ());
      REQUIRE (dir.compare (dir.size () - sc.case_id.size (), sc.case_id.size (), sc.case_id) == 0);

      std::string produced;
      REQUIRE (corpus::encode_semantic_case (sc, produced) == encode_status::OK);
      REQUIRE (produced == checked_in_stream);
    }
}

TEST_CASE ("The checksum file lists every corpus file with its digest, and the manifest pins the checksum file",
	   "[pgbuf_inspector][corpus]")
{
  std::string checksum_text;
  REQUIRE (corpus::read_file (CORPUS_DIR + "/SHA256SUMS", checksum_text));

  SECTION ("every listed digest matches the file, in the format sha256sum reads")
  {
    std::vector<std::string> listed_paths;
    std::size_t line_start = 0;
    while (line_start < checksum_text.size ())
      {
	const std::size_t line_end = checksum_text.find ('\n', line_start);
	REQUIRE (line_end != std::string::npos);
	const std::string line = checksum_text.substr (line_start, line_end - line_start);
	line_start = line_end + 1;

	INFO ("checksum line " << line);
	REQUIRE (line.size () > 66);
	REQUIRE (line.compare (64, 2, "  ") == 0);
	const std::string digest = line.substr (0, 64);
	const std::string relative = line.substr (66);

	std::string contents;
	REQUIRE (corpus::read_file (CORPUS_DIR + "/" + relative, contents));
	REQUIRE (corpus::sha256_hex (contents) == digest);
	listed_paths.push_back (relative);
      }

    /* Sorted by path, no duplicates, and nothing under the corpus is missing or extra. */
    REQUIRE (listed_paths == corpus::corpus_file_paths (CORPUS_DIR));
    for (std::size_t i = 1; i < listed_paths.size (); ++i)
      {
	REQUIRE (listed_paths[i - 1] < listed_paths[i]);
      }
    REQUIRE (std::find (listed_paths.begin (), listed_paths.end (), "SHA256SUMS") == listed_paths.end ());
  }

  SECTION ("the manifest carries the contract version, a positive revision and the aggregate hash")
  {
    std::string manifest_text;
    REQUIRE (corpus::read_file (CONTRACT_DIR + "/manifest.json", manifest_text));

    rapidjson::Document manifest;
    manifest.Parse (manifest_text.c_str ());
    REQUIRE (!manifest.HasParseError ());
    REQUIRE (manifest.IsObject ());

    REQUIRE (manifest["contract_version"].IsString ());
    REQUIRE (std::string (manifest["contract_version"].GetString ())
	     == std::to_string (WIRE_PROTOCOL_MAJOR) + "." + std::to_string (WIRE_PROTOCOL_MINOR));

    REQUIRE (manifest["corpus_revision"].IsInt64 ());
    REQUIRE (manifest["corpus_revision"].GetInt64 () >= 1);

    REQUIRE (manifest["corpus_sha256"].IsString ());
    REQUIRE (std::string (manifest["corpus_sha256"].GetString ()) == corpus::sha256_hex (checksum_text));
  }
}

TEST_CASE ("Refusal codes round-trip through their wire names", "[pgbuf_inspector][wire]")
{
  const refusal_code all[] =
  {
    refusal_code::VERSION_UNSUPPORTED, refusal_code::BUSY, refusal_code::INCARNATION_CHANGED,
    refusal_code::IDENTITY_OVERSIZED, refusal_code::RATE_LIMITED
  };
  for (refusal_code code : all)
    {
      refusal_code parsed = refusal_code::BUSY;
      REQUIRE (refusal_code_from_wire_name (refusal_code_wire_name (code), parsed));
      REQUIRE (parsed == code);
    }

  refusal_code parsed = refusal_code::BUSY;
  REQUIRE_FALSE (refusal_code_from_wire_name ("parameter-off", parsed));
  REQUIRE_FALSE (refusal_code_from_wire_name ("", parsed));
}

namespace
{
  /* Contract section 9, typed from its table rather than read from the corpus, so the expected outcomes
   * are checked against an independent statement of which refusals end the connection. */
  bool
  refusal_closes_connection (refusal_code code)
  {
    return code != refusal_code::RATE_LIMITED;
  }
}

TEST_CASE ("Every expected outcome agrees with the stream it describes", "[pgbuf_inspector][corpus]")
{
  const std::vector<std::string> case_dirs = corpus::semantic_case_directories (CORPUS_DIR);
  REQUIRE (!case_dirs.empty ());

  for (const std::string &dir : case_dirs)
    {
      INFO ("case directory " << dir);

      std::string semantic_text, stream, expected_text;
      REQUIRE (corpus::read_file (dir + "/semantic.json", semantic_text));
      REQUIRE (corpus::read_file (dir + "/stream.jsonl", stream));
      REQUIRE (corpus::read_file (dir + "/expected.json", expected_text));

      corpus::semantic_case sc;
      std::string why;
      REQUIRE (corpus::parse_semantic_case (semantic_text, sc, why));

      rapidjson::Document expected;
      expected.Parse (expected_text.c_str ());
      REQUIRE (!expected.HasParseError ());
      REQUIRE (expected.IsObject ());

      REQUIRE (std::string (expected["case"].GetString ()) == sc.case_id);
      REQUIRE (expected["frame_count"].GetInt () == std::count (stream.begin (), stream.end (), '\n'));
      const std::string reply_to = expected["reply_to"].GetString ();
      REQUIRE ((reply_to == "client_hello" || reply_to == "scan_request"));

      if (std::string (expected["category"].GetString ()) == "refusal")
	{
	  REQUIRE (sc.frames.size () == 1);
	  REQUIRE (sc.frames[0].kind == corpus::frame_kind::ERROR_FRAME);
	  const error_frame &frame = sc.frames[0].error;

	  REQUIRE (std::string (expected["outcome"].GetString ()) == "refused");
	  REQUIRE (std::string (expected["refusal_code"].GetString ()) == refusal_code_wire_name (frame.code));
	  REQUIRE (std::string (expected["connection"].GetString ())
		   == (refusal_closes_connection (frame.code) ? "closed" : "open"));

	  REQUIRE (expected.HasMember ("retry_after_ms") == frame.retry_after_ms.has_value ());
	  if (frame.retry_after_ms.has_value ())
	    {
	      REQUIRE (expected["retry_after_ms"].GetUint () == *frame.retry_after_ms);
	    }

	  REQUIRE (expected.HasMember ("supported_majors") == !frame.supported_majors.empty ());
	  if (!frame.supported_majors.empty ())
	    {
	      std::vector<int> listed;
	      for (const auto &major : expected["supported_majors"].GetArray ())
		{
		  listed.push_back (major.GetInt ());
		}
	      REQUIRE (listed == frame.supported_majors);
	    }
	}
    }
}
