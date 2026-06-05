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

#ifndef _OOS_FILE_HPP_
#define _OOS_FILE_HPP_

#include "compressor.hpp"
#include "dbtype_def.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "span.hpp"
#include "storage_common.h"
#include "thread_compat.hpp"

/*
 * OOS payload compression codec (CBRD-26756)
 *
 * Every OOS blob stored by oos_insert has the layout:
 *   [ OOS_COMP_HEADER (8B) | image-or-lz4-bytes ]
 *
 * The inline heap stub length (oos_lengths[i]) == total blob size ==
 * OOS_COMP_HEADER_SIZE + stored_bytes, satisfying the oos_read invariant
 * (oos_record_header.total_data_length == dest.size()).
 *
 * This header is included by every OOS-payload reader (heap read path,
 * replication applier) so the codec lives in exactly one place.
 */

/* OOS compression algorithm identifiers stored in OOS_COMP_HEADER.algo. */
enum oos_comp_algo
{
  OOS_COMP_NONE = 0,		/* raw image, no compression */
  OOS_COMP_LZ4 = 1		/* LZ4 fast compression */
};

/*
 * OOS_COMP_HEADER — 8 bytes.  MAX_ALIGNMENT-sized so the raw image that
 * follows remains 8-aligned for data_readval.
 *
 * On-disk layout (explicit byte ops below; do not rely on struct padding):
 *   byte 0        : algo  (oos_comp_algo)
 *   bytes 1-3     : zero (reserved)
 *   bytes 4-7     : uncompressed_len (int, little-endian via OR_PUT_INT)
 *
 * Note: OOS files are not cross-endian-portable today (matches
 * oos_record_header which is memcpy'd as a POD). OR_PUT_INT/OR_GET_INT make
 * the on-blob byte order explicit and independent of struct padding.
 */
typedef struct oos_comp_header
{
  unsigned char algo;		/* oos_comp_algo value */
  unsigned char reserved[3];	/* zero-filled; pads uncompressed_len to offset 4 */
  int uncompressed_len;		/* byte length of the serialized image before compression */
} OOS_COMP_HEADER;

#define OOS_COMP_HEADER_SIZE ((int) sizeof (OOS_COMP_HEADER))

/*
 * OOS_MIN_COMPRESS_LEN — minimum payload size before attempting LZ4.
 * OOS columns are >512B by construction so this rarely excludes anything,
 * but keeps us from wasting CPU on tiny payloads where LZ4 framing cannot win.
 */
#define OOS_MIN_COMPRESS_LEN 255

/*
 * OOS_COMP_MIN_GAIN — minimum bytes LZ4 must save to be worth keeping.
 * NOT header accounting: the OOS_COMP_HEADER is prepended in BOTH the compressed
 * and the raw branch, so it cancels out of the size comparison. This is a
 * separate policy margin: storing compressed costs a decompress on every read,
 * so we only keep the compressed form when it saves at least this many bytes.
 * Mirrors the >=8-byte gain rule in pr_data_compress_string (object_primitive.c).
 */
#define OOS_COMP_MIN_GAIN 8

/*
 * oos_comp_header_put () - serialize algo + uncompressed_len into the first
 *   OOS_COMP_HEADER_SIZE bytes of dest.
 */
inline void
oos_comp_header_put (char *dest, unsigned char algo, int uncompressed_len)
{
  dest[0] = (char) algo;
  dest[1] = 0;
  dest[2] = 0;
  dest[3] = 0;
  OR_PUT_INT (dest + 4, uncompressed_len);
}

/*
 * oos_comp_header_get () - deserialize the first OOS_COMP_HEADER_SIZE bytes
 *   of src into hdr.
 */
inline void
oos_comp_header_get (const char *src, OOS_COMP_HEADER *hdr)
{
  hdr->algo = (unsigned char) src[0];
  hdr->reserved[0] = (unsigned char) src[1];
  hdr->reserved[1] = (unsigned char) src[2];
  hdr->reserved[2] = (unsigned char) src[3];
  hdr->uncompressed_len = OR_GET_INT (src + 4);
}

/*
 * oos_payload_decode () - Decode a layer-2 OOS blob that was just read from
 *   oos_read into buf[0..blob_len).
 *
 *   blob_len     : total bytes read (== OOS_COMP_HEADER_SIZE + stored_bytes).
 *   buf          : the buffer holding the blob (may be scratch or heap-alloc'd).
 *   scratch      : the caller's optional fast-path buffer; NULL means every
 *                  buffer is heap-alloc'd and must be freed.
 *   data_out     : set to the start of the decoded image on success; the caller
 *                  must NOT free this pointer directly — use free_out.
 *   length_out   : set to the decoded image byte length on success.
 *   free_out     : set to the pointer that the caller must free via
 *                  db_private_free_and_init(NULL, free_out) when non-NULL and
 *                  != scratch.  If free_out == buf this is a memmove-in-place
 *                  result; if free_out != buf it is a fresh allocation.
 *
 *   Returns NO_ERROR on success.  On failure sets er and returns ER_FAILED;
 *   buf is freed (if heap-backed) and data_out is set to NULL.
 *
 * NONE path:  memmove image over header in-place; data_out = buf; free_out = buf.
 * LZ4  path:  fresh db_private_alloc; data_out = fresh; free_out = fresh;
 *             buf (if heap-backed) is freed by this function.
 */
inline int
oos_payload_decode (int blob_len, char *buf, const char *scratch,
		    char **data_out, int *length_out, char **free_out)
{
  OOS_COMP_HEADER h;
  int payload_len = blob_len - OOS_COMP_HEADER_SIZE;

  *data_out = NULL;
  *length_out = 0;
  *free_out = NULL;

  if (payload_len < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      if (buf != scratch)
	{
	  db_private_free_and_init (NULL, buf);
	}
      return ER_FAILED;
    }

  oos_comp_header_get (buf, &h);

  if (h.algo == OOS_COMP_NONE)
    {
      /* Validate consistency. */
      if (h.uncompressed_len != payload_len)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  if (buf != scratch)
	    {
	      db_private_free_and_init (NULL, buf);
	    }
	  return ER_FAILED;
	}
      /* memmove image over the header so buf stays the freeable base. */
      memmove (buf, buf + OOS_COMP_HEADER_SIZE, (size_t) payload_len);
      *data_out = buf;
      *length_out = payload_len;
      *free_out = buf;
      return NO_ERROR;
    }
  else if (h.algo == OOS_COMP_LZ4)
    {
      char *dst;
      int n;

      if (h.uncompressed_len <= 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  if (buf != scratch)
	    {
	      db_private_free_and_init (NULL, buf);
	    }
	  return ER_FAILED;
	}

      /* Decompress into a fresh owned buffer (LZ4 src/dst must not overlap). */
      dst = (char *) db_private_alloc (NULL, h.uncompressed_len);
      if (dst == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) h.uncompressed_len);
	  if (buf != scratch)
	    {
	      db_private_free_and_init (NULL, buf);
	    }
	  return ER_FAILED;
	}

      n = cubcompress::decompress<cubcompress::LZ4> (buf + OOS_COMP_HEADER_SIZE, payload_len, dst,
	  h.uncompressed_len);
      if (n != h.uncompressed_len)
	{
	  db_private_free_and_init (NULL, dst);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  if (buf != scratch)
	    {
	      db_private_free_and_init (NULL, buf);
	    }
	  return ER_FAILED;
	}

      /* Release the read buffer if heap-allocated; hand off the fresh base. */
      if (buf != scratch)
	{
	  db_private_free_and_init (NULL, buf);
	}
      *data_out = dst;
      *length_out = h.uncompressed_len;
      *free_out = dst;
      return NO_ERROR;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      if (buf != scratch)
	{
	  db_private_free_and_init (NULL, buf);
	}
      return ER_FAILED;
    }
}

struct oos_record_header
{
  int total_data_length;	/* total length of user data across all chunks (excluding OOS headers) */
  int chunk_index;		/* 0-based index of this chunk in the chain */
  OID next_chunk_oid;		/* OID of next chunk, or NULL OID if this is the last */
};
using OOS_RECORD_HEADER = struct oos_record_header;

#define OOS_RECORD_HEADER_SIZE ((int) sizeof (OOS_RECORD_HEADER))

/* Alias for a RECDES whose first OOS_RECORD_HEADER_SIZE bytes are the OOS header.
 * Documentation only — no compile-time distinction from RECDES. */
using OOS_RECDES = RECDES;

/* Caller-owned byte span for OOS payloads. size() is the authoritative length;
 * oos_insert only reads from it, oos_read only writes. Named alias because the
 * .c-file formatter mangles `cubbase::span<char>(...)`'s angle brackets. */
using oos_buffer = cubbase::span<char>;

#define OOS_NUM_BEST_SPACESTATS 10

#define OOS_STATS_NEXT_BEST_INDEX(i) \
  (((i) + 1) % OOS_NUM_BEST_SPACESTATS)
#define OOS_STATS_PREV_BEST_INDEX(i) \
  (((i) == 0) ? (OOS_NUM_BEST_SPACESTATS - 1) : ((i) - 1))

typedef struct oos_bestspace OOS_BESTSPACE;
struct oos_bestspace
{
  VPID vpid;
  int freespace;
};

typedef struct oos_hdr_stats OOS_HDR_STATS;
struct oos_hdr_stats
{
  VFID oos_vfid;
  struct
  {
    int num_pages;
    int num_recs;
    float recs_sumlen;
    int num_other_high_best;
    int num_high_best;
    int num_substitutions;
    int num_second_best;
    int head_second_best;
    int tail_second_best;
    int head;
    VPID full_search_vpid;
    VPID second_best[OOS_NUM_BEST_SPACESTATS];
    OOS_BESTSPACE best[OOS_NUM_BEST_SPACESTATS];
  } estimates;

  int reserve0_for_future;
  int reserve1_for_future;
};

extern int oos_create_file (THREAD_ENTRY *thread_p, VFID &oos_vfid);
extern int oos_remove_file (THREAD_ENTRY *thread_p, const VFID &oos_vfid);
extern int oos_remove_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const VPID &vpid);
/* Inserts src.size() bytes; on multi-page payloads, oid is the head-chunk OID. */
extern int oos_insert (THREAD_ENTRY *thread_p, const VFID &oos_vfid, oos_buffer src, OID &oid);
/* Reads exactly dest.size() bytes; the caller obtains the length from the
 * heap record's inline 8B field (or oos_get_length in tests) and sizes dest. */
extern int oos_read (THREAD_ENTRY *thread_p, const OID &oid, oos_buffer dest);
extern int oos_delete (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const OID &oid);
extern int oos_get_length (THREAD_ENTRY *thread_p, const OID &oid);

extern int oos_rv_redo_delete (THREAD_ENTRY *thread_p, LOG_RCV *rcv);
extern int oos_rv_redo_insert (THREAD_ENTRY *thread_p, LOG_RCV *rcv);

typedef enum
{
  OOS_FINDSPACE_FOUND = 0,
  OOS_FINDSPACE_NOTFOUND,
  OOS_FINDSPACE_ERROR
} OOS_FINDSPACE;

extern int oos_bestspace_initialize (void);
extern int oos_bestspace_finalize (void);

struct oos_stats_info
{
  int has_oos_file;		/* 0 if class has no OOS file, 1 otherwise */
  VFID oos_vfid;
  int num_user_pages;		/* physical user pages allocated to OOS file */
  int page_size;		/* DB_PAGESIZE */
  int num_recs;			/* live OOS records tracked by OOS_HDR_STATS */
  INT64 recs_sumlen;		/* sum of live OOS record body bytes */
};
using OOS_STATS_INFO = struct oos_stats_info;

extern int xoos_get_stats_by_class_oid (THREAD_ENTRY *thread_p, const OID *class_oid, OOS_STATS_INFO *out);

#ifdef __cplusplus
extern "C"
{
#endif

extern void oos_push_oos_oid (THREAD_ENTRY *thread_p, const OID *oid);

#ifdef __cplusplus
}
#endif

#endif /* _OOS_FILE_HPP_ */
