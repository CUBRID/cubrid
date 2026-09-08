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
 * pgbuf_inspector_wire.hpp - canonical serializer for the page-buffer inspector wire contract
 *
 * This is the producer side of docs/pgbuf-inspector/v1/contract.md. Every value here is a plain
 * scalar or an enumeration; nothing depends on the page buffer, the server, the thread manager or
 * any engine header, so the conformance unit test builds this source directly. The later bounded
 * scan only has to fill these values in, and joins this file to the server build when it does.
 *
 * The encoder writes the contract's canonical form byte for byte, which is what the conformance
 * corpus pins. It enforces the contract's frame limits at encode time: a frame that would exceed
 * its limit, or a value the contract cannot represent, is reported and nothing is appended.
 */

#ifndef _PGBUF_INSPECTOR_WIRE_HPP_
#define _PGBUF_INSPECTOR_WIRE_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cubpgbuf
{
  namespace inspector
  {
    /* Contract limits. Binary bytes, including the terminating line feed. */
    constexpr std::size_t WIRE_CONTROL_FRAME_MAX_BYTES = 4096;
    constexpr std::size_t WIRE_PAGE_FRAME_MAX_BYTES = 4096;
    constexpr std::size_t WIRE_HANDSHAKE_FRAME_MAX_BYTES = 65536;

    constexpr int WIRE_PROTOCOL_MAJOR = 1;
    constexpr int WIRE_PROTOCOL_MINOR = 0;

    enum class encode_status
    {
      OK,
      FRAME_TOO_LARGE,		/* the canonical frame would exceed its limit; nothing was appended */
      INVALID_VALUE		/* a value cannot be represented under the contract; nothing was appended */
    };

    /* Stable refusal codes of the error frame, contract section 9. */
    enum class refusal_code
    {
      VERSION_UNSUPPORTED,
      BUSY,
      INCARNATION_CHANGED,
      IDENTITY_OVERSIZED,
      RATE_LIMITED
    };

    const char *refusal_code_wire_name (refusal_code code);

    /* Maps a wire name back to its code; false when the name is not a version 1 refusal code. */
    bool refusal_code_from_wire_name (const char *name, refusal_code &code);

    struct error_frame
    {
      refusal_code code = refusal_code::BUSY;
      std::vector<int> supported_majors;	/* present only with VERSION_UNSUPPORTED */
      std::optional<std::uint32_t> retry_after_ms;	/* present only with RATE_LIMITED */
    };

    /* Appends the canonical error frame to out, or appends nothing and reports why. */
    encode_status encode_error_frame (const error_frame &frame, std::string &out);

    /*
     * canonical_frame_writer - builds one frame in the contract's canonical form
     *
     * Keys are written in call order after the leading type key. Strings must be printable ASCII;
     * anything else makes the frame invalid. finish () appends the completed frame only when it fits.
     */
    class canonical_frame_writer
    {
      public:
	explicit canonical_frame_writer (const char *type);

	void add_string (const char *key, const char *value);
	void add_int (const char *key, std::int64_t value);
	void add_uint (const char *key, std::uint64_t value);
	void add_bool (const char *key, bool value);
	void add_null (const char *key);
	void add_int_array (const char *key, const std::vector<int> &values);

	encode_status finish (std::size_t max_bytes, std::string &out);

      private:
	void append_key (const char *key);
	void append_quoted_ascii (const char *value);

	std::string m_text;
	bool m_invalid;
    };
  }
}

#endif /* _PGBUF_INSPECTOR_WIRE_HPP_ */
