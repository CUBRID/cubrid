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
 * copy_stream_kind.h - COPY's tag and open request on the byte-stream transport.
 *
 * The transport declares only the bound of the tag space; the value is this
 * consumer's to name. It is shared because the two halves of the COPY binding
 * sit on opposite sides of the wire: the client packs the open request
 * (copy_from_init) and the server registers the factory that answers it
 * (copy_session). The value is reserved in the allocation list beside
 * STREAM_KIND_MAX in stream_session.hpp.
 */

#ifndef _COPY_STREAM_KIND_H_
#define _COPY_STREAM_KIND_H_

#include "dbtype_def.h"

#define STREAM_KIND_COPY 0

/* The first int of COPY's config blob. A server accepts every version up to its own.
 * 2: the CSV NULL string follows bulk.
 * 3: ON_ERROR IGNORE (int) and REJECT_LIMIT (int64) follow the NULL string. */
#define COPY_CONFIG_VERSION 3

#define COPY_FORMAT_BINARY 0
#define COPY_FORMAT_CSV    1

/*
 * What COPY's open request carries. The client packs it in copy_from_init () and
 * the server unpacks it in copy_session_create (), after the version and in this
 * order: table_name (string), num_cols, format, delimiter, quote, header, bulk
 * (int), null_str (string), on_error_ignore (int), reject_limit (int64), then
 * col_types and attr_ids (int each, num_cols of them).
 */
typedef struct copy_config COPY_CONFIG;
struct copy_config
{
  const char *table_name;
  int num_cols;
  int format;			/* COPY_FORMAT_BINARY or COPY_FORMAT_CSV */
  int delimiter;		/* CSV; 0 for ',' */
  int quote;			/* CSV; 0 for '"' */
  int header;			/* CSV: the first line is a header to skip */
  int bulk;
  const char *null_str;		/* CSV: the unquoted field that reads as NULL */
  int on_error_ignore;		/* CSV: skip a row in error instead of failing */
  DB_BIGINT reject_limit;	/* the most rows ON_ERROR IGNORE may skip; 0 for no limit */
  const DB_TYPE *col_types;	/* the target columns in the statement's order */
  const int *attr_ids;		/* carry the column list, which the types alone cannot express */
};

#endif /* _COPY_STREAM_KIND_H_ */
