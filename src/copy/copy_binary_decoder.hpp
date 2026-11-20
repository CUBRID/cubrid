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
 * copy_binary_decoder.hpp - Decode binary rows for COPY FROM STDIN
 */

#ifndef _COPY_BINARY_DECODER_HPP_
#define _COPY_BINARY_DECODER_HPP_

#include "copy_decode_common.hpp"
#include "dbtype_def.h"

#include <cstdint>

/* What decode_binary_row () returns, beside NO_ERROR, an error code and
 * COPY_DECODE_NEED_MORE, when it consumed the footer: the stream is complete. */
#define COPY_DECODE_FOOTER     1

/*
 * decode_binary_row () - Decode one row from a binary buffer
 *   return: NO_ERROR, COPY_DECODE_FOOTER, COPY_DECODE_NEED_MORE, or error code
 *   buf(in): pointer to start of row data
 *   buf_len(in): remaining bytes in buffer
 *   types(in): expected column types
 *   domains(in): per-column target domain (precision / codeset / collation)
 *   ncols(in): number of columns
 *   out_vals(out): decoded DB_VALUE array (caller-allocated, size >= ncols)
 *   bytes_consumed(out): number of bytes consumed from buf
 *   row(in): this row's 1-based position in the stream, for the error message
 */
extern int decode_binary_row (const char *buf, int buf_len, const DB_TYPE *types, const COPY_COL_DOMAIN *domains,
			      int ncols, DB_VALUE *out_vals, int *bytes_consumed, std::int64_t row);

#endif /* _COPY_BINARY_DECODER_HPP_ */
