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
 * corpus_support.hpp - reading, decoding and hashing the conformance corpus
 *
 * Shared by the unit test, which proves the checked-in corpus, and the generator, which writes it.
 * The semantic input of a case is decoded here into the serializer's own values, so the only way to
 * produce a canonical stream is through the serializer.
 */

#ifndef _TEST_PGBUF_INSPECTOR_CORPUS_SUPPORT_HPP_
#define _TEST_PGBUF_INSPECTOR_CORPUS_SUPPORT_HPP_

#include "pgbuf_inspector_wire.hpp"

#include <string>
#include <vector>

namespace corpus
{
  constexpr const char *CHECKSUM_FILE_NAME = "SHA256SUMS";
  constexpr const char *SEMANTIC_FILE_NAME = "semantic.json";
  constexpr const char *STREAM_FILE_NAME = "stream.jsonl";

  /* The frame kinds a semantic input may name; later corpus revisions add the remaining kinds. */
  enum class frame_kind
  {
    ERROR_FRAME
  };

  /* One frame of a case's semantic input. Only the member selected by kind is meaningful. */
  struct semantic_frame
  {
    frame_kind kind = frame_kind::ERROR_FRAME;
    cubpgbuf::inspector::error_frame error;
  };

  struct semantic_case
  {
    std::string case_id;
    std::vector<semantic_frame> frames;
  };

  bool read_file (const std::string &path, std::string &contents);
  bool write_file (const std::string &path, const std::string &contents);

  /* Case directories directly under corpus_dir that hold a semantic input, sorted by name. */
  std::vector<std::string> semantic_case_directories (const std::string &corpus_dir);

  /* Every regular file under corpus_dir except the checksum file, as sorted paths relative to it. */
  std::vector<std::string> corpus_file_paths (const std::string &corpus_dir);

  /* Decodes semantic.json text. On failure, why names the first problem. */
  bool parse_semantic_case (const std::string &json_text, semantic_case &out, std::string &why);

  /* Encodes every frame in order through the serializer, stopping at the first refusal. */
  cubpgbuf::inspector::encode_status encode_semantic_case (const semantic_case &sc, std::string &stream);

  /* The checksum file text in the format sha256sum reads, one sorted corpus file per line. */
  std::string checksum_file_text (const std::string &corpus_dir);
}

#endif /* _TEST_PGBUF_INSPECTOR_CORPUS_SUPPORT_HPP_ */
