/*
 * Copyright 2024 CUBRID Corporation
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
 * copy_csv_decoder.hpp - Decode CSV text rows for COPY FROM STDIN (FORMAT CSV)
 *
 * Default dialect (PostgreSQL CSV): comma delimiter, double-quote quoting with
 * "" escape, empty unquoted field = NULL, LF / CRLF line terminator, UTF-8.
 */

#ifndef _COPY_CSV_DECODER_HPP_
#define _COPY_CSV_DECODER_HPP_

#include "dbtype_def.h"

#include <string>
#include <vector>

/*
 * decode_csv_row () - Decode one CSV record from a text buffer.
 *   return: NO_ERROR, COPY_DECODE_NEED_MORE (no complete line yet), or
 *           ER_COPY_CSV_FORMAT_ERROR.
 *   buf/buf_len(in): remaining text
 *   types/ncols(in): expected column types
 *   out_vals(out): decoded DB_VALUE array (caller-allocated, size >= ncols)
 *   field_storage/quoted(in/out): caller-owned scratch reused per row. VARCHAR
 *     out_vals point into field_storage, so it must outlive the row's insert.
 *   bytes_consumed(out): bytes consumed (including the line terminator)
 */
extern int decode_csv_row (const char *buf, int buf_len, const DB_TYPE *types, int ncols,
			   DB_VALUE *out_vals, std::vector<std::string> &field_storage,
			   std::vector<char> &quoted, int *bytes_consumed);

#endif /* _COPY_CSV_DECODER_HPP_ */
