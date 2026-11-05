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
 * The trailer is read backwards from the record end.  It describes the record without the class representation,
 * so that this codec can expand a hot record, or splice values into it, with no server state:
 *
 *   UINT16 ref_location[n_pad];  OR_ATTRIBUTE.location of each ref slot, ascending; n_pad is n_refs rounded
 *                                up to odd, and the padding entry is SIDECAR_TRAILER_PAD
 *   UINT16 n_variable;           the variable attributes of the representation the record was written with: its
 *                                offset table has n_variable + 1 entries
 *   UINT16 n_refs;               1 .. n_variable
 *   UINT16 magic;                SIDECAR_TRAILER_MAGIC
 *
 * Its size, 2 * n_pad + 6, is a multiple of 4: 8 bytes for one ref, 12 for two or three, 16 for four or five.
 *
 * A store record is an ordinary heap record of the store file:
 *
 *   [MVCC header with repid SIDECAR_STORE_REPID, written by the heap][SIDECAR_VALUE_HEADER][value bytes]
 *
 * Refs, trailers and value headers are big-endian and unaligned (OR_GET_SHORT, OR_GET_INT).  Only this codec and
 * storage/sidecar.cpp parse them; other code asks sidecar_record_has_refs ().
 *
 * Building a hot record takes the number of variable attributes from the caller, who has the representation, and
 * writes it to the trailer; expanding one reads it back.  The codec keeps the record's offset size, so the inline
 * image a hot record was built from expands back to the same bytes.
 *
 * Every function here is pure: no server state, no allocation and no error manager.  A negative return is
 * SIDECAR_FORMAT_ERROR, a record or an argument that does not have the form the function documents.
 */

#ifndef _SIDECAR_FORMAT_H_
#define _SIDECAR_FORMAT_H_

#ident "$Id$"

#include "object_representation.h"
#include "porting_inline.hpp"
#include "storage_common.h"

#define SIDECAR_TRAILER_MAGIC 0x5343
#define SIDECAR_TRAILER_PAD 0xFFFF

#define SIDECAR_TRAILER_N_PAD(n_refs) ((n_refs) | 1)
#define SIDECAR_TRAILER_SIZE(n_refs) (OR_SHORT_SIZE * (SIDECAR_TRAILER_N_PAD (n_refs) + 3))

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

/* A variable slot of a hot record that holds a ref, or will once the record is built */
typedef struct sidecar_slot SIDECAR_SLOT;
struct sidecar_slot
{
  int location;			/* OR_ATTRIBUTE.location of a variable attribute */
  SIDECAR_REF ref;
};

/* The repid of every store record: no class representation has it.  It is the largest positive value of
 * MVCC_REC_HEADER.repid, a signed 24-bit field: 0x00FFFFFF would read back as -1, and a header rewrite through the
 * struct (vacuum's INSID removal) would then set the bound bit, the offset size bits and every MVCC flag. */
#define SIDECAR_STORE_REPID 0x007FFFFF

/* The value header of a store record, SIDECAR_VALUE_HEADER_SIZE bytes after the record's MVCC header */
typedef struct sidecar_value_header SIDECAR_VALUE_HEADER;
struct sidecar_value_header
{
  INT32 attrid;			/* the owning attribute: tripwire and verifier */
  INT16 format;			/* SIDECAR_VALUE_FORMAT */
  INT16 reserved;
};
#define SIDECAR_VALUE_HEADER_SIZE 8
#define SIDECAR_VALUE_FORMAT 1

/* A store record as the module hands it to the heap: a minimal MVCC header, the value header and the value */
#define SIDECAR_STORE_RECORD_LENGTH(value_length) \
  (OR_MVCC_MIN_HEADER_SIZE + SIDECAR_VALUE_HEADER_SIZE + (value_length))

#define SIDECAR_FORMAT_ERROR (-1)

/* the record has OR_MVCC_FLAG_HAS_SIDECAR; recdes holds an object header (a REC_HOME or REC_NEWHOME record, or a
 * record fetched through one) */
STATIC_INLINE bool
sidecar_record_has_refs (const RECDES * recdes)
{
  return (OR_GET_MVCC_FLAG (recdes->data) & OR_MVCC_FLAG_HAS_SIDECAR) != 0;
}

/* refs */
extern void sidecar_ref_encode (const SIDECAR_REF * ref, char *slot);
extern void sidecar_ref_decode (const char *slot, SIDECAR_REF * ref);

/* The number of refs a hot record's trailer lists, or SIDECAR_FORMAT_ERROR when the record has the flag and no
 * trailer.  0 when the record has no OR_MVCC_FLAG_HAS_SIDECAR. */
extern int sidecar_trailer_n_refs (const RECDES * recdes);
/* the n_variable of a record whose trailer sidecar_trailer_n_refs () accepted */
extern int sidecar_trailer_n_variable (const RECDES * recdes);
/* the location of the index-th ref of a record whose trailer sidecar_trailer_n_refs () accepted */
extern int sidecar_trailer_location (const RECDES * recdes, int index);
/* the index of the ref that the slot at location holds, or -1 when the slot holds a value */
extern int sidecar_trailer_find (const RECDES * recdes, int location);
/* the same, for a record with a trailer accepted before, from the end of the record only (the attribute layer
 * keeps the record's variable table and end, not its start) */
extern int sidecar_trailer_find_from_end (const char *end, int location);
/* Writes n_variable, n_refs and the magic, the last 6 bytes of a trailer that ends at end; the writer puts
 * the n_pad locations before them */
extern void sidecar_trailer_put_counts (char *end, int n_variable, int n_refs);
/* The whole form of a hot record: trailer, ascending locations below the trailer's n_variable, a SIDECAR_REF_SIZE
 * slot for each ref, and the trailer right after entry[n_variable] of the offset table */
extern bool sidecar_hot_is_well_formed (const RECDES * recdes);
/* the ref of the index-th trailer entry */
extern void sidecar_hot_get_ref (const RECDES * recdes, int index, SIDECAR_REF * ref);

/* the bytes the variable slot at location holds in a record with an object header */
extern int sidecar_var_slot_length (const RECDES * recdes, int location);

/* The length of the hot record that sidecar_build_hot () makes of image with a ref in each slot of slots[]
 * (ascending, distinct locations below n_variable), or SIDECAR_FORMAT_ERROR */
extern int sidecar_hot_length (const RECDES * image, int n_variable, int n_slots, const SIDECAR_SLOT * slots);
/* Builds into hot->data (hot->area_size at least sidecar_hot_length ()) the hot record of an image whose
 * representation has n_variable variable attributes: each slot of slots[] (ascending locations) gets its ref, the
 * trailer is appended and the flag set.  An image that has refs already (an UPDATE image with kept refs) must be a
 * well-formed hot record of the same n_variable whose every ref slots[] names again, with its ref; the build then
 * replaces its trailer.  NO_ERROR or SIDECAR_FORMAT_ERROR. */
extern int sidecar_build_hot (const RECDES * image, int n_variable, int n_slots, const SIDECAR_SLOT * slots,
			      RECDES * hot);

/* the length of the inline record a well-formed hot record expands to, or SIDECAR_FORMAT_ERROR */
extern int sidecar_expanded_length (const RECDES * hot);
/* Writes into out->data (out->area_size at least sidecar_expanded_length ()) the inline record of a well-formed hot
 * record, except the bytes of the values its refs name: the value of the index-th ref belongs at
 * out->data + value_offsets[index].  Sets out->length.  NO_ERROR or SIDECAR_FORMAT_ERROR. */
extern int sidecar_expand_frame (const RECDES * hot, RECDES * out, int *value_offsets);

/* Writes into out->data (out->area_size at least SIDECAR_STORE_RECORD_LENGTH (length)) the store record of a value */
extern void sidecar_store_record_build (int attrid, const char *value, int length, RECDES * out);
/* The value header and the value of a store record read back from the heap, or SIDECAR_FORMAT_ERROR when the
 * record is not one */
extern int sidecar_store_record_parse (const RECDES * recdes, SIDECAR_VALUE_HEADER * header, const char **value,
				       int *length);

/*
 * The write policy (AUTO).  An image of at most inline_target bytes with no EXTERNAL attribute passes through.
 * Otherwise EXTERNAL attributes go first, then the other eligible ones, largest first, each at least
 * min_value_size, until the estimated hot record is at most inline_target.  If the hot record body still exceeds
 * headroom, nothing is externalized and the row takes the stock path.  force_external makes every eligible value of
 * at least SIDECAR_FORCE_MIN_VALUE_SIZE bytes EXTERNAL.
 */
#define SIDECAR_FORCE_MIN_VALUE_SIZE 64

typedef struct sidecar_policy SIDECAR_POLICY;
struct sidecar_policy
{
  int inline_target;		/* sidecar_inline_target */
  int min_value_size;		/* sidecar_min_value_size */
  bool force_external;		/* sidecar_force_external */
  int headroom;			/* the largest body (record less its header) of a hot record, so that growth to
				 * OR_MVCC_MAX_HEADER_SIZE keeps it slotted */
};

/* what the server knows about an INSERT image when it asks the policy */
typedef struct sidecar_image_facts SIDECAR_IMAGE_FACTS;
struct sidecar_image_facts
{
  int length;			/* the image's length */
  int header_size;		/* the image's object header size */
  bool mvcc_user_class;		/* the class is a user class with MVCC */
  bool supplemental_log;	/* supplemental_log > 0: CDC and flashback read row images (deferred) */
  bool ha_enabled;		/* replication of stored refs comes in P4 */
  bool has_delid;		/* an insert that carries a DELID: partition redistribution */
  bool page_held;		/* the thread holds a permanent page, so the store must not be written now */
};

/* a non-NULL variable value of the image */
typedef struct sidecar_candidate SIDECAR_CANDIDATE;
struct sidecar_candidate
{
  int location;			/* OR_ATTRIBUTE.location */
  int length;			/* bytes of its slot */
  bool eligible;		/* its type may live in the store: not BLOB, CLOB or a collection */
  bool external;		/* STORAGE EXTERNAL */
};

/* false when a gate keeps the image inline whatever its values: the class, supplemental logging, HA, a DELID or a
 * page held */
extern bool sidecar_policy_admits (const SIDECAR_IMAGE_FACTS * facts);
/* Writes the locations to externalize to chosen_locations (room for n_candidates), ascending, and returns how many;
 * 0 when the image passes through. */
extern int sidecar_policy_choose (const SIDECAR_POLICY * policy, const SIDECAR_IMAGE_FACTS * facts,
				  const SIDECAR_CANDIDATE * candidates, int n_candidates, int *chosen_locations);

#endif /* _SIDECAR_FORMAT_H_ */
