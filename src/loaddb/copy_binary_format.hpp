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
 *   DB_TYPE_DATE:     4 bytes, network byte order: a julian day number, NOT
 *                     unix epoch seconds. 0 is the zero date; otherwise
 *                     1721424 (0001-01-01) .. 5373484 (9999-12-31).
 *   DB_TYPE_TIME:     4 bytes, network byte order: seconds since midnight,
 *                     0 .. 86399.
 *   DB_TYPE_TIMESTAMP: 4 bytes, network byte order: unix epoch seconds (UTC),
 *                     0 .. 2147483647.
 *   DB_TYPE_DATETIME: 8 bytes, network byte order: the DATE julian day above,
 *                     then milliseconds since midnight, 0 .. 86399999.
 *   NULL:             field_len = -1, no data bytes
 *
 * A date or time value outside its range is refused, so a stream carrying epoch
 * seconds where a julian day belongs fails instead of loading a year like 4000.
 */

#define COPY_BINARY_NULL_FIELD_LEN  (-1)
#define COPY_BINARY_FOOTER_SENTINEL ((int16_t) -1)

#endif /* _COPY_BINARY_FORMAT_HPP_ */
