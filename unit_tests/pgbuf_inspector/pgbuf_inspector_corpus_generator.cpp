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
 * pgbuf_inspector_corpus_generator.cpp - writes the conformance corpus streams and checksum file
 *
 * Usage: pgbuf_inspector_corpus_generator <contract directory>
 *
 * For every case under <contract directory>/corpus that has a semantic input, the generator encodes
 * the frames through the producer serializer and writes stream.jsonl. It then rewrites SHA256SUMS
 * and the aggregate hash in <contract directory>/manifest.json. It never touches expected.json or
 * semantic.json, and it leaves corpus_revision to the person changing the corpus.
 */

#include "corpus_support.hpp"
#include "sha256.hpp"

#include "db_rapidjson.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace cubpgbuf::inspector;

namespace
{
  const char *MANIFEST_FILE_NAME = "manifest.json";

  bool
  rewrite_manifest (const std::string &contract_dir, const std::string &corpus_sha256)
  {
    const std::string path = (std::filesystem::path (contract_dir) / MANIFEST_FILE_NAME).string ();

    std::string contract_version = "1.0";
    long long corpus_revision = 1;

    std::string existing;
    if (corpus::read_file (path, existing))
      {
	rapidjson::Document document;
	document.Parse (existing.c_str ());
	if (document.HasParseError () || !document.IsObject ())
	  {
	    fprintf (stderr, "manifest is not a JSON object: %s\n", path.c_str ());
	    return false;
	  }
	if (document.HasMember ("contract_version") && document["contract_version"].IsString ())
	  {
	    contract_version = document["contract_version"].GetString ();
	  }
	if (document.HasMember ("corpus_revision") && document["corpus_revision"].IsInt64 ())
	  {
	    corpus_revision = document["corpus_revision"].GetInt64 ();
	  }
      }

    std::string text = "{\n";
    text += "  \"contract_version\": \"" + contract_version + "\",\n";
    text += "  \"corpus_revision\": " + std::to_string (corpus_revision) + ",\n";
    text += "  \"corpus_sha256\": \"" + corpus_sha256 + "\"\n";
    text += "}\n";
    return corpus::write_file (path, text);
  }
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <contract directory>\n", argv[0]);
      return 2;
    }

  const std::string contract_dir = argv[1];
  const std::string corpus_dir = (std::filesystem::path (contract_dir) / "corpus").string ();
  if (!std::filesystem::is_directory (corpus_dir))
    {
      fprintf (stderr, "not a corpus directory: %s\n", corpus_dir.c_str ());
      return 1;
    }

  int generated = 0;
  for (const std::string &dir : corpus::semantic_case_directories (corpus_dir))
    {
      std::string semantic_text;
      if (!corpus::read_file ((std::filesystem::path (dir) / corpus::SEMANTIC_FILE_NAME).string (), semantic_text))
	{
	  fprintf (stderr, "cannot read semantic input in %s\n", dir.c_str ());
	  return 1;
	}

      corpus::semantic_case sc;
      std::string why;
      if (!corpus::parse_semantic_case (semantic_text, sc, why))
	{
	  fprintf (stderr, "%s: %s\n", dir.c_str (), why.c_str ());
	  return 1;
	}
      if (sc.case_id != std::filesystem::path (dir).filename ().string ())
	{
	  fprintf (stderr, "%s: case identifier %s does not match its directory\n", dir.c_str (), sc.case_id.c_str ());
	  return 1;
	}

      std::string stream;
      const encode_status status = corpus::encode_semantic_case (sc, stream);
      if (status != encode_status::OK)
	{
	  fprintf (stderr, "%s: the serializer refused a frame (%s)\n", dir.c_str (),
		   status == encode_status::FRAME_TOO_LARGE ? "frame too large" : "invalid value");
	  return 1;
	}
      if (!corpus::write_file ((std::filesystem::path (dir) / corpus::STREAM_FILE_NAME).string (), stream))
	{
	  fprintf (stderr, "cannot write stream in %s\n", dir.c_str ());
	  return 1;
	}
      generated++;
    }

  const std::string checksums = corpus::checksum_file_text (corpus_dir);
  const std::string checksum_path = (std::filesystem::path (corpus_dir) / corpus::CHECKSUM_FILE_NAME).string ();
  if (!corpus::write_file (checksum_path, checksums))
    {
      fprintf (stderr, "cannot write %s\n", checksum_path.c_str ());
      return 1;
    }

  const std::string aggregate = corpus::sha256_hex (checksums);
  if (!rewrite_manifest (contract_dir, aggregate))
    {
      return 1;
    }

  printf ("generated %d case stream(s); corpus aggregate sha256 %s\n", generated, aggregate.c_str ());
  return 0;
}
