/*
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

#include "dbtype_def.h"

/*
 * decode_binary_row () - Decode one row from a binary buffer
 *   return: NO_ERROR or error code
 *   buf(in): pointer to start of row data
 *   buf_len(in): remaining bytes in buffer
 *   types(in): expected column types
 *   ncols(in): number of columns
 *   out_vals(out): decoded DB_VALUE array (caller-allocated, size >= ncols)
 *   bytes_consumed(out): number of bytes consumed from buf
 */
extern int decode_binary_row (const char *buf, int buf_len, const DB_TYPE *types, int ncols,
			      DB_VALUE *out_vals, int *bytes_consumed);

#endif /* _COPY_BINARY_DECODER_HPP_ */
