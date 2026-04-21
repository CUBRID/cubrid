/*
 *
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
 * copy_binary_format.hpp - Binary wire format constants for COPY FROM STDIN
 */

#ifndef _COPY_BINARY_FORMAT_HPP_
#define _COPY_BINARY_FORMAT_HPP_

#include <cstdint>

/*
 * Binary wire format for COPY FROM STDIN WITH (FORMAT BINARY):
 *
 * Per-row layout:
 *   int16_t  num_fields    -- number of fields in this row
 *   For each field:
 *     int32_t  field_len   -- byte length of field data, or -1 for NULL
 *     byte[]   data        -- raw field bytes (only if field_len >= 0)
 *
 * Footer sentinel:
 *   int16_t  -1            -- marks end of data stream
 *
 * Field encoding by type:
 *   DB_TYPE_INTEGER:  4 bytes, network byte order (big-endian)
 *   DB_TYPE_BIGINT:   8 bytes, network byte order
 *   DB_TYPE_FLOAT:    4 bytes, IEEE 754
 *   DB_TYPE_DOUBLE:   8 bytes, IEEE 754
 *   DB_TYPE_VARCHAR:  raw UTF-8 bytes (no NUL terminator)
 *   DB_TYPE_VECTOR:   4-byte dimension count (network order) + dim * 4 bytes float32
 *   NULL:             field_len = -1, no data bytes
 */

#define COPY_BINARY_NULL_FIELD_LEN  (-1)
#define COPY_BINARY_FOOTER_SENTINEL ((int16_t) -1)

#endif /* _COPY_BINARY_FORMAT_HPP_ */
