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
 * corpus_support.cpp - reading, decoding and hashing the conformance corpus
 */

#include "corpus_support.hpp"

#include "sha256.hpp"

#include "db_rapidjson.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace corpus
{
  using namespace cubpgbuf::inspector;

  bool
  read_file (const std::string &path, std::string &contents)
  {
    std::ifstream in (path, std::ios::binary);
    if (!in)
      {
	return false;
      }
    contents.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char> ());
    return true;
  }

  bool
  write_file (const std::string &path, const std::string &contents)
  {
    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (!out)
      {
	return false;
      }
    out.write (contents.data (), static_cast<std::streamsize> (contents.size ()));
    return static_cast<bool> (out);
  }

  std::vector<std::string>
  semantic_case_directories (const std::string &corpus_dir)
  {
    std::vector<std::string> dirs;
    for (const auto &entry : std::filesystem::directory_iterator (corpus_dir))
      {
	if (entry.is_directory () && std::filesystem::is_regular_file (entry.path () / SEMANTIC_FILE_NAME))
	  {
	    dirs.push_back (entry.path ().string ());
	  }
      }
    std::sort (dirs.begin (), dirs.end ());
    return dirs;
  }

  std::vector<std::string>
  corpus_file_paths (const std::string &corpus_dir)
  {
    std::vector<std::string> paths;
    for (const auto &entry : std::filesystem::recursive_directory_iterator (corpus_dir))
      {
	if (!entry.is_regular_file ())
	  {
	    continue;
	  }
	const std::string relative = std::filesystem::relative (entry.path (), corpus_dir).generic_string ();
	if (relative == CHECKSUM_FILE_NAME)
	  {
	    continue;
	  }
	paths.push_back (relative);
      }
    std::sort (paths.begin (), paths.end ());
    return paths;
  }

  static bool
  parse_error_frame (const rapidjson::Value &node, error_frame &frame, std::string &why)
  {
    if (!node.HasMember ("code") || !node["code"].IsString ())
      {
	why = "error frame without a string code";
	return false;
      }
    if (!refusal_code_from_wire_name (node["code"].GetString (), frame.code))
      {
	why = std::string ("unknown refusal code: ") + node["code"].GetString ();
	return false;
      }
    if (node.HasMember ("supported_majors"))
      {
	const rapidjson::Value &majors = node["supported_majors"];
	if (!majors.IsArray ())
	  {
	    why = "supported_majors is not an array";
	    return false;
	  }
	for (const auto &major : majors.GetArray ())
	  {
	    if (!major.IsInt ())
	      {
		why = "supported_majors holds a non-integer";
		return false;
	      }
	    frame.supported_majors.push_back (major.GetInt ());
	  }
      }
    if (node.HasMember ("retry_after_ms"))
      {
	if (!node["retry_after_ms"].IsUint ())
	  {
	    why = "retry_after_ms is not an unsigned integer";
	    return false;
	  }
	frame.retry_after_ms = node["retry_after_ms"].GetUint ();
      }
    return true;
  }

  bool
  parse_semantic_case (const std::string &json_text, semantic_case &out, std::string &why)
  {
    rapidjson::Document document;
    document.Parse (json_text.c_str ());
    if (document.HasParseError ())
      {
	std::ostringstream message;
	message << "semantic input is not valid JSON at offset " << document.GetErrorOffset ();
	why = message.str ();
	return false;
      }
    if (!document.IsObject () || !document.HasMember ("case") || !document["case"].IsString ()
	|| !document.HasMember ("frames") || !document["frames"].IsArray ())
      {
	why = "semantic input needs a string case and an array frames";
	return false;
      }

    out.case_id = document["case"].GetString ();
    out.frames.clear ();
    for (const auto &node : document["frames"].GetArray ())
      {
	if (!node.IsObject () || !node.HasMember ("frame") || !node["frame"].IsString ())
	  {
	    why = "every frame needs a string frame kind";
	    return false;
	  }
	semantic_frame frame;
	const std::string kind_name = node["frame"].GetString ();
	if (kind_name == "error")
	  {
	    frame.kind = frame_kind::ERROR_FRAME;
	    if (!parse_error_frame (node, frame.error, why))
	      {
		return false;
	      }
	  }
	else
	  {
	    why = "unsupported frame kind: " + kind_name;
	    return false;
	  }
	out.frames.push_back (frame);
      }
    return true;
  }

  encode_status
  encode_semantic_case (const semantic_case &sc, std::string &stream)
  {
    for (const semantic_frame &frame : sc.frames)
      {
	encode_status status = encode_status::INVALID_VALUE;
	switch (frame.kind)
	  {
	  case frame_kind::ERROR_FRAME:
	    status = encode_error_frame (frame.error, stream);
	    break;
	  }
	if (status != encode_status::OK)
	  {
	    return status;
	  }
      }
    return encode_status::OK;
  }

  std::string
  checksum_file_text (const std::string &corpus_dir)
  {
    std::string text;
    for (const std::string &relative : corpus_file_paths (corpus_dir))
      {
	std::string contents;
	if (!read_file ((std::filesystem::path (corpus_dir) / relative).string (), contents))
	  {
	    continue;
	  }
	text += sha256_hex (contents);
	text += "  ";
	text += relative;
	text += '\n';
      }
    return text;
  }
}
