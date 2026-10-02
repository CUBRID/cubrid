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
 * COPY FROM STDIN WITH (FORMAT BINARY) wire format: each row is int16 num_fields, then per field an int32
 * field_len (-1 = NULL) followed by field_len raw bytes; an int16 -1 in place of num_fields ends the stream.
 * Numbers are network byte order (FLOAT/DOUBLE as IEEE 754 bits), VARCHAR raw bytes without a NUL terminator.
 */

#define COPY_BINARY_NULL_FIELD_LEN  (-1)
#define COPY_BINARY_FOOTER_SENTINEL ((int16_t) -1)

#endif /* _COPY_BINARY_FORMAT_HPP_ */
