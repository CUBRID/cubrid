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

#include "dbtype_def.h"

/* Return-code sentinels used by decode_binary_row (in addition to NO_ERROR
 * and negative error codes).
 * COPY_DECODE_FOOTER     — footer sentinel row consumed; end of stream.
 * COPY_DECODE_NEED_MORE  — buffer ended mid-row; caller should buffer the
 *                          remaining bytes and combine with the next chunk.
 */
#define COPY_DECODE_FOOTER     1
#define COPY_DECODE_NEED_MORE  2

/* The target column's domain, copied out of the class representation when the
 * session opens (the representation itself is released before the first row
 * arrives, so the domain pointer cannot be kept). Character values are built
 * against this instead of a fixed codeset, which is both what the column
 * actually holds and what lets heap_attrinfo_set take its no-copy path. */
typedef struct copy_col_domain COPY_COL_DOMAIN;
struct copy_col_domain
{
  int precision;
  int codeset;
  int collation_id;
};

/*
 * copy_fit_char_precision () - Check a character value against the column's
 *   declared precision, allowing trailing blanks to be truncated away, exactly
 *   as loaddb does. Building the value on the column's own domain means
 *   heap_attrinfo_set stores it without coercing - and therefore without
 *   checking precision itself - so the check belongs here.
 *   return: NO_ERROR or ER_IT_DATA_OVERFLOW
 *   fitted_len(out): byte length to store (<= str_len)
 */
extern int copy_fit_char_precision (DB_TYPE type, const char *str, int str_len, const COPY_COL_DOMAIN *dom,
				    int *fitted_len);

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
 */
extern int decode_binary_row (const char *buf, int buf_len, const DB_TYPE *types, const COPY_COL_DOMAIN *domains,
			      int ncols, DB_VALUE *out_vals, int *bytes_consumed);

#endif /* _COPY_BINARY_DECODER_HPP_ */
