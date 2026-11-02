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
#include <cstdint>

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
  /* The attribute's domain itself, for the types cast from text. The
   * representation hands out cached domains, which live as long as the server,
   * so this pointer may outlive the representation it came from. */
  const struct tp_domain *domain;
};

/*
 * copy_fit_char_precision () - Check a character value against the column's
 *   declared precision, allowing trailing blanks to be truncated away, exactly
 *   as loaddb does. Building the value on the column's own domain means
 *   heap_attrinfo_set stores it without coercing - and therefore without
 *   checking precision itself - so the check belongs here.
 *   return: whether the value fits. Nothing is raised: only the caller knows
 *           the row and column the error has to name.
 *   fitted_len(out): byte length to store (<= str_len)
 */
extern bool copy_fit_char_precision (const char *str, int str_len, const COPY_COL_DOMAIN *dom, int *fitted_len);

/*
 * copy_check_char_encoding () - Check a character value's bytes against the
 *   column's codeset, as INSERT through a CAS and loaddb check theirs: only
 *   while intl_check_input_string is on.
 *   return: whether the bytes are valid. Nothing is raised, as above.
 */
extern bool copy_check_char_encoding (const char *str, int str_len, const COPY_COL_DOMAIN *dom);

/*
 * copy_format_error () - Raise a COPY format error that says where it happened.
 *
 * The reason alone ("BIGINT expects 8 bytes") does not say which of ten
 * million rows to look at. Rows are numbered from the start of the stream (a
 * skipped CSV HEADER line is not a row) and columns as the statement lists
 * them, both 1-based. col < 0 names the row only.
 *   return: error_code, which is ER_COPY_CSV_FORMAT_ERROR or ER_COPY_BINARY_FORMAT_ERROR
 */
extern int copy_format_error (int error_code, std::int64_t row, int col, const char *reason);

/*
 * copy_text_to_column () - Cast the text of a field to its column's domain, for
 *   the types copy_type_is_text_cast () names. The cast is the one INSERT
 *   applies to a string, so a value that would have to be truncated, or that
 *   is out of the column's range, is refused rather than stored.
 *   return: COPY_CAST_OK, or why it failed. Nothing is raised: only the caller
 *           knows the format, row and column the error has to name.
 */
enum copy_cast_result
{
  COPY_CAST_OK = 0,
  COPY_CAST_BAD_VALUE,
  COPY_CAST_OUT_OF_RANGE
};
extern copy_cast_result copy_text_to_column (const char *text, int text_len, const COPY_COL_DOMAIN *dom,
    DB_VALUE *val);

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
