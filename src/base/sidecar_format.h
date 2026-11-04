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

/*
 * sidecar_format.h - the form of a hot record whose large values live in its heap's Sidecar store
 *
 * A hot record is an ordinary heap record with three changes.  OR_MVCC_FLAG_HAS_SIDECAR is set in its MVCC
 * flags.  The variable slot of each value held in the store has a SIDECAR_REF instead of the value.  A
 * trailer at the record end names those slots:
 *
 *   [repid word, OR_MVCC_FLAG_HAS_SIDECAR among the flags][CHN][INSID?][DELID?][PREV_LSA?]
 *   [variable offset table: unchanged, entry[n_variable] still ends the variable area]
 *   [fixed area][bound bits]
 *   [variable area: a ref slot holds SIDECAR_REF_SIZE bytes]
 *   [trailer]
 *
 * The trailer is read backwards from the record end and needs no class representation:
 *
 *   UINT16 ref_location[n_pad];  OR_ATTRIBUTE.location of each ref slot, ascending; n_pad is n_refs rounded
 *                                up to even, and the padding entry is SIDECAR_TRAILER_PAD
 *   UINT16 n_refs;               1 .. n_variable
 *   UINT16 magic;                SIDECAR_TRAILER_MAGIC
 *
 * Refs and trailers are big-endian and unaligned (OR_GET_SHORT, OR_GET_INT).  Only storage/sidecar.cpp parses
 * them; other code asks sidecar_record_has_refs ().
 */

#ifndef _SIDECAR_FORMAT_H_
#define _SIDECAR_FORMAT_H_

#ident "$Id$"

#include "object_representation.h"
#include "porting_inline.hpp"
#include "storage_common.h"

#define SIDECAR_TRAILER_MAGIC 0x5343
#define SIDECAR_TRAILER_PAD 0xFFFF

/* 8 bytes for one or two refs, 12 for three or four */
#define SIDECAR_TRAILER_SIZE(n_refs) (2 * (((n_refs) + 1) & ~1) + 2 * OR_SHORT_SIZE)

/* A ref as its slot holds it, SIDECAR_REF_SIZE bytes */
typedef struct sidecar_ref SIDECAR_REF;
struct sidecar_ref
{
  INT32 pageid;			/* store record OID: page */
  INT16 volid;			/* store record OID: volume */
  INT16 slotid;			/* store record OID: slot (its home slot when the store record is REC_BIGONE) */
  INT32 length;			/* bytes of the value as it would sit inline: its disk encoding with prefix */
  UINT16 flags;			/* SIDECAR_REF_BIGONE; the other bits are spare */
  UINT16 attrid_check;		/* low 16 bits of the attribute id: a tripwire, never protocol */
};
#define SIDECAR_REF_SIZE 16
#define SIDECAR_REF_BIGONE 0x0001

/* the record has OR_MVCC_FLAG_HAS_SIDECAR; recdes holds an object header (a REC_HOME or REC_NEWHOME record, or a
 * record fetched through one) */
STATIC_INLINE bool
sidecar_record_has_refs (const RECDES * recdes)
{
  return (OR_GET_MVCC_FLAG (recdes->data) & OR_MVCC_FLAG_HAS_SIDECAR) != 0;
}

#endif /* _SIDECAR_FORMAT_H_ */
