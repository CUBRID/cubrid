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
 * btree_sort_radix.cpp - MSD radix sorter of the in-memory run
 *
 * Orders a run by a fixed width key and breaks key ties by instance OID, so the order is that of
 * btsort_compare_records (). init () sets the context and the MSD stack aside at the top of the sort buffer;
 * below them the run is laid out as the merge sorter does, and sort () keeps its radix words in the free space
 * accept () claims, so nothing is allocated.
 *
 *   top  ┌───────────────────┐
 *        │ context           │  set aside by init ()
 *        │ MSD stack         │
 *        ├───────────────────┤
 *        │ slots             │  the newest record's slot at the lowest address
 *        ├───────────────────┤
 *        │ spare slots       │
 *        ├───────────────────┤
 *        │ free              │  handed out by reserve (), which keeps the radix words below in reserve
 *        ├───────────────────┤
 *        │ radix words spare │  laid out by sort () in the free space, from the records up
 *        │ radix words       │
 *        ├───────────────────┤
 *        │ records           │  the oldest at the lowest address
 *  base  └───────────────────┘
 *
 * Terms:
 *   record (rec)   a sort record: header, instance OID and the stored key; what a run holds and hands out
 *   key            the stored key in a record; the comparator reads word_width bytes of it
 *   radix key      the key as unsigned bytes that order as the comparator does; RADIX_KEY_FORMAT names the rewrite
 *   radix word     the radix key as word_width bytes (2, 4 or 8), one per record; the engine's unit of work
 *   slot           a record's offset from the buffer start; the sort moves slots, never records
 *   home, spare    the two copies of the slots and of the radix words; a split moves a range to the other side
 *   words area     the free space between the records and the slots, where sort () lays both copies of the radix words
 *   range          slots whose radix words agree up to byte_index, split 256 ways on that byte
 *   bucket         the records of a range that share one value of that byte
 *   MSD stack      the ranges waiting for a split, one msd_range each; radix_sort_stack_bytes () sizes it
 *   context        what init () keeps for every run: the key encoding, rec_overhead and the stack area
 *   rec_overhead   what a record claims above the records: a slot and a radix word on each side
 *
 * 1. sorter: the hooks
 * 2. key encoding: which keys, and their radix words
 * 3. engine: the MSD radix sort of the slots by their radix words
 * 4. finish range: the key bytes left, then the OID tie-break
 */

#include "config.h"

#include "btree_sort_radix.hpp"

#include "byte_order.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "object_representation_constants.h"
#include "oid.h"
#include "porting.h"

#include <algorithm>
#include <array>
#include <assert.h>
#include <climits>
#include <string.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* below this a counting pass costs more than a few compares; 64 measured best of 16/24/32/48/64/96/128 */
#define RADIX_MSD_CUTOFF 64

/*
 * 1. SORTER: THE HOOKS
 */

namespace
{
  /* what the sorter keeps from init () on, at the top of the sort buffer */
  struct radix_sorter_ctx
  {
    RADIX_KEY_ENCODING key_encoding;	/* the key encoding the engine sorts by */
    int rec_overhead;		/* bytes a record claims above the records: a slot in each array and two radix words */
    char *stack_area;		/* MSD stack, sized for the most records a run can hold */
    size_t stack_bytes;
  };
}

static void btsort_radix_sorter_init (BTSORT_RUN *run, const TP_DOMAIN *key_domain);
static void btsort_radix_sorter_reset (BTSORT_RUN *run);
static char *btsort_radix_sorter_reserve (BTSORT_RUN *run, int maxlen, int *room);
static void btsort_radix_sorter_accept (BTSORT_RUN *run, int length);
static int btsort_radix_sorter_sort (THREAD_ENTRY *thread_p, BTSORT_RUN *run);
static const char *btsort_radix_sorter_next (BTSORT_RUN *run);

const BTSORT_RUN_SORTER btsort_radix_sorter =
{
  "radix",
  btsort_radix_sorter_init,
  btsort_radix_sorter_reset,
  btsort_radix_sorter_reserve,
  btsort_radix_sorter_accept,
  btsort_radix_sorter_sort,
  btsort_radix_sorter_next
};

/*
 * btsort_radix_sorter_init () - Sets the context and the MSD stack aside at the top of the sort buffer, then
 *                               starts the first run
 *   return: void
 *   run(in/out):
 *   key_domain(in): a domain radix_sort_supports_key () takes
 */
static void
btsort_radix_sorter_init (BTSORT_RUN *run, const TP_DOMAIN *key_domain)
{
  radix_sorter_ctx *ctx = (radix_sorter_ctx *) (run->top - DB_ALIGN (sizeof (radix_sorter_ctx), DOUBLE_ALIGNMENT));
  size_t smallest_rec;
  size_t max_records;

  assert (PTR_ALIGN (run->top, DOUBLE_ALIGNMENT) == run->top);

  if (!radix_sort_resolve_key (&ctx->key_encoding, key_domain))
    {
      /* btsort_run_pick_sorter () picks this sorter for a radix key only */
      assert (false);
    }

  /* each split moves the records from one side to the other, slots and radix words alike, so a record claims a slot
   * and a radix word on each side */
  ctx->rec_overhead = 2 * (int) sizeof (BTSORT_RUN_SLOT) + 2 * ctx->key_encoding.word_width;

  /* the smallest record: a header, the instance OID and a key no shorter than its word, plus rec_overhead above
   * the records */
  smallest_rec = DB_ALIGN (BTSORT_REC_HEADER_SIZE + OR_OID_SIZE + ctx->key_encoding.word_width, INT_ALIGNMENT);
  max_records = (size_t) ((char *) ctx - run->base) / (smallest_rec + ctx->rec_overhead);

  /* a slot is a 32-bit offset into a buffer under 2GB */
  assert (max_records <= (size_t) INT_MAX);

  ctx->stack_bytes = radix_sort_stack_bytes (&ctx->key_encoding, (int) max_records);
  ctx->stack_area = (char *) ctx - DB_ALIGN (ctx->stack_bytes, DOUBLE_ALIGNMENT);

  run->sorter_ctx = ctx;

  btsort_radix_sorter_reset (run);
}

/*
 * btsort_radix_sorter_reset () - Starts a new run
 *   return: void
 *   run(in/out):
 *
 * Note: Every record claims rec_overhead in accept (). The last sorted run stays readable until the next accept ().
 */
static void
btsort_radix_sorter_reset (BTSORT_RUN *run)
{
  radix_sorter_ctx *ctx = (radix_sorter_ctx *) run->sorter_ctx;

  run->numrecs = 0;
  run->cursor = 0;
  run->item_ptr = run->base;
  run->slot_ptr = ctx->stack_area - sizeof (BTSORT_RUN_SLOT);
  /* and room to align the radix words */
  run->slot_low = run->slot_ptr - sizeof (BTSORT_RUN_SLOT) - DOUBLE_ALIGNMENT;
}

/*
 * btsort_radix_sorter_reserve () - Where to write the next record
 *   return: the address, NULL when the run is full
 *   run(in):
 *   maxlen(in):
 *   room(out): writable bytes, at most maxlen
 *
 * Note: Keeps the share of two records in reserve.
 */
static char *
btsort_radix_sorter_reserve (BTSORT_RUN *run, int maxlen, int *room)
{
  radix_sorter_ctx *ctx = (radix_sorter_ctx *) run->sorter_ctx;
  ptrdiff_t free_bytes = (run->slot_low - run->item_ptr) - 2 * (ptrdiff_t) ctx->rec_overhead;

  if (free_bytes <= 0)
    {
      *room = 0;
      return NULL;
    }

  *room = (free_bytes < (ptrdiff_t) maxlen) ? (int) free_bytes : maxlen;

  return run->item_ptr;
}

/*
 * btsort_radix_sorter_accept () - Adds the record written at reserve ()
 *   return: void
 *   run(in/out):
 *   length(in): record length, header included
 */
static void
btsort_radix_sorter_accept (BTSORT_RUN *run, int length)
{
  radix_sorter_ctx *ctx = (radix_sorter_ctx *) run->sorter_ctx;
  BTSORT_RUN_SLOT *slot = (BTSORT_RUN_SLOT *) run->slot_ptr;

  assert (run->item_ptr + length <= run->slot_low);

  *slot = CAST_BUFLEN (run->item_ptr - run->base);
  run->numrecs++;

  run->slot_ptr -= sizeof (BTSORT_RUN_SLOT);
  run->slot_low -= ctx->rec_overhead;
  run->item_ptr += DB_ALIGN (length, INT_ALIGNMENT);
}

#if !defined (NDEBUG)
/* the comparator never returns 0, as it breaks key ties by OID */
static void
btsort_radix_validate (const BTSORT_RUN *run, const BTSORT_RUN_SLOT *slots)
{
  long at;

  for (at = 0; at + 1 < run->numrecs; at++)
    {
      assert ((*run->cmp_fn) (run->base + slots[at], run->base + slots[at + 1], run->cmp_arg) < 0);
    }
}
#endif /* !NDEBUG */

/*
 * btsort_radix_sorter_sort () - Orders the run
 *   return: NO_ERROR
 *   thread_p(in):
 *   run(in/out):
 *
 * Note: A single record, such as the placeholder of a BIGONE record, has no key to read.
 */
static int
btsort_radix_sorter_sort (THREAD_ENTRY *thread_p, BTSORT_RUN *run)
{
  BTSORT_RUN_SLOT *slots = (BTSORT_RUN_SLOT *) (run->slot_ptr + sizeof (BTSORT_RUN_SLOT));	/* the newest slot */

  /* a slot is a 32-bit offset into a buffer under 2GB */
  assert (run->numrecs <= INT_MAX);

  if (run->numrecs > 1)
    {
      radix_sorter_ctx *ctx = (radix_sorter_ctx *) run->sorter_ctx;
      BTSORT_RUN_SLOT *spare = (BTSORT_RUN_SLOT *) run->slot_ptr - run->numrecs;
      char *words_area = run->item_ptr;

      assert (words_area <= run->slot_low && run->slot_low <= (char *) spare);

      /* in arrival order, the reverse of how accept () fills them, equal keys come mostly in OID order already,
       * so insertion_sort () and sort_by_oid () find them in place instead of turning them round */
      std::reverse (slots, slots + run->numrecs);

      radix_sort_run (&ctx->key_encoding, run->base, slots, spare, (int) run->numrecs, words_area,
		      (size_t) ((char *) spare - words_area), ctx->stack_area, ctx->stack_bytes);

#if !defined (NDEBUG)
      btsort_radix_validate (run, slots);
#endif /* !NDEBUG */
    }

  /* for next (), kept across reset () */
  run->sorted = (const char *) slots;
  run->readrecs = run->numrecs;
  run->cursor = 0;

  return NO_ERROR;
}

/*
 * btsort_radix_sorter_next () - Next record of the sorted run
 *   return: the record, NULL at the end
 *   run(in/out):
 */
static const char *
btsort_radix_sorter_next (BTSORT_RUN *run)
{
  const BTSORT_RUN_SLOT *slots = (const BTSORT_RUN_SLOT *) run->sorted;

  /* read before new records are accepted */
  assert (run->numrecs == 0 || run->numrecs == run->readrecs);

  if (run->cursor >= run->readrecs)
    {
      return NULL;
    }

  return run->base + slots[run->cursor++];
}

/*
 * 2. KEY ENCODING: WHICH KEYS, AND THEIR RADIX WORDS
 */

/*
 * radix_sort_supports_key () - Whether the radix sorter can order a key of the domain
 *   return: true when the domain has a fixed width radix key
 *   key_domain(in):
 */
bool
radix_sort_supports_key (const TP_DOMAIN *key_domain)
{
  RADIX_KEY_ENCODING encoding;

  return radix_sort_resolve_key (&encoding, key_domain);
}

/*
 * radix_sort_resolve_key () - Works the key encoding out of the key domain
 *   return: false when the domain has no fixed width radix key
 *   encoding(out): word_width, format and is_desc, all zero when declined
 *   key_domain(in):
 *
 * Note: One fixed width value, or the part of one the comparator reads, ascending or descending.
 */
bool
radix_sort_resolve_key (RADIX_KEY_ENCODING *encoding, const TP_DOMAIN *key_domain)
{
  int word_width;
  RADIX_KEY_FORMAT format;

  encoding->word_width = 0;
  encoding->format = RADIX_KEY_UNSIGNED;
  encoding->is_desc = false;

  if (key_domain == NULL)
    {
      return false;
    }

  switch (TP_DOMAIN_TYPE (key_domain))
    {
    case DB_TYPE_SHORT:
      word_width = OR_SHORT_SIZE;
      format = RADIX_KEY_SIGNED;
      break;
    case DB_TYPE_INTEGER:
      word_width = OR_INT_SIZE;
      format = RADIX_KEY_SIGNED;
      break;
    case DB_TYPE_BIGINT:
      word_width = OR_BIGINT_SIZE;
      format = RADIX_KEY_SIGNED;
      break;

    case DB_TYPE_FLOAT:
      word_width = OR_FLOAT_SIZE;
      format = RADIX_KEY_IEEE;
      break;
    case DB_TYPE_DOUBLE:
      word_width = OR_DOUBLE_SIZE;
      format = RADIX_KEY_IEEE;
      break;
    case DB_TYPE_MONETARY:
      /* the currency, then the amount, which mr_index_cmpdisk_money () reads alone */
      word_width = OR_DOUBLE_SIZE;
      format = RADIX_KEY_MONETARY;
      break;

    case DB_TYPE_DATE:
      word_width = OR_DATE_SIZE;
      format = RADIX_KEY_UNSIGNED;
      break;
    case DB_TYPE_TIME:
      word_width = OR_TIME_SIZE;
      format = RADIX_KEY_UNSIGNED;
      break;
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      /* LTZ is stored as the plain type, and TZ puts the zone after the time, which the comparator never reads */
      word_width = OR_UTIME_SIZE;
      format = RADIX_KEY_UNSIGNED;
      break;
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      /* as for TIMESTAMP: the date and the time, then a zone the comparator never reads */
      word_width = OR_DATETIME_SIZE;
      format = RADIX_KEY_DATETIME;
      break;

    case DB_TYPE_OBJECT:
    case DB_TYPE_OID:
      /* stored as page, slot and volume, which oid_compare () reads as volume, page and slot */
      word_width = OR_OID_SIZE;
      format = RADIX_KEY_OID;
      break;

    case DB_TYPE_ENUMERATION:
      /* a short index, which the comparators widen to an unsigned short */
      word_width = OR_SHORT_SIZE;
      format = RADIX_KEY_UNSIGNED;
      break;

    default:
      return false;
    }

  /* the engine is a template on these widths only */
  if (word_width != 2 && word_width != 4 && word_width != 8)
    {
      return false;
    }

  encoding->word_width = word_width;
  encoding->format = format;
  encoding->is_desc = (key_domain->is_desc != 0);

  return true;
}

namespace
{
  /* the unsigned integer a radix word is carried in, so a word is one load and one store */
  template <int WORD_WIDTH> struct radix_word_of;
  template <> struct radix_word_of<2>
  {
    using type = UINT16;
  };
  template <> struct radix_word_of<4>
  {
    using type = UINT32;
  };
  template <> struct radix_word_of<8>
  {
    using type = UINT64;
  };
  template <int WORD_WIDTH> using radix_word = typename radix_word_of<WORD_WIDTH>::type;
}

/* a signed or IEEE key's sign bit in its radix word, built from the word's bytes as the host's byte order places it */
template <int WORD_WIDTH>
static inline radix_word<WORD_WIDTH>
word_sign_bit ()
{
  const unsigned char bytes[WORD_WIDTH] = { 0x80 };
  radix_word<WORD_WIDTH> word;

  memcpy (&word, bytes, WORD_WIDTH);

  return word;
}

/* big-endian bytes to a host integer, as OR_GET_SHORT (), OR_GET_INT () and OR_GET_INT64 () read them */
static inline UINT16
big_endian_to_host (UINT16 word)
{
  return ntohs (word);
}

static inline UINT32
big_endian_to_host (UINT32 word)
{
  return ntohl (word);
}

static inline UINT64
big_endian_to_host (UINT64 word)
{
  return swap64 (word);
}

/* a radix word read as one unsigned integer: its bytes are big-endian, so the integers compare as the radix words do */
template <int WORD_WIDTH>
static inline radix_word<WORD_WIDTH>
word_from_bytes (const unsigned char *bytes)
{
  radix_word<WORD_WIDTH> word;

  memcpy (&word, bytes, WORD_WIDTH);

  return big_endian_to_host (word);
}

/* one field of a value, which the index image keeps in host order, put big-endian */
template <typename FIELD>
static inline void
put_field_big_endian (unsigned char *to, const unsigned char *from)
{
  FIELD field;

  memcpy (&field, from, sizeof (field));
  field = big_endian_to_host (field);	/* the swap goes both ways */
  memcpy (to, &field, sizeof (field));
}

/*
 * fixed_key_word () - The word of a fixed width key with each of its fields big-endian
 *   return: the word, as the transforms of the format take it
 *   format(in): the key's, a constant where the caller is a template on it
 *   key(in): the stored key
 */
template <int WORD_WIDTH>
static inline radix_word<WORD_WIDTH>
fixed_key_word (RADIX_KEY_FORMAT format, const unsigned char *key)
{
  unsigned char bytes[OR_BIGINT_SIZE];
  radix_word<WORD_WIDTH> word;

  switch (format)
    {
    case RADIX_KEY_DATETIME:
      put_field_big_endian<UINT32> (bytes + OR_DATETIME_DATE, key + OR_DATETIME_DATE);
      put_field_big_endian<UINT32> (bytes + OR_DATETIME_TIME, key + OR_DATETIME_TIME);
      break;

    case RADIX_KEY_OID:
      put_field_big_endian<UINT32> (bytes + OR_OID_PAGEID, key + OR_OID_PAGEID);
      put_field_big_endian<UINT16> (bytes + OR_OID_SLOTID, key + OR_OID_SLOTID);
      put_field_big_endian<UINT16> (bytes + OR_OID_VOLID, key + OR_OID_VOLID);
      break;

    case RADIX_KEY_MONETARY:
      /* the amount, which the comparator reads alone */
      put_field_big_endian<UINT64> (bytes, key + OR_MONETARY_AMOUNT);
      break;

    default:
      /* a value of one field */
      put_field_big_endian<radix_word<WORD_WIDTH>> (bytes, key);
      break;
    }

  memcpy (&word, bytes, WORD_WIDTH);

  return word;
}

/*
 * ieee_to_order () - Puts a big-endian IEEE 754 value in unsigned byte order
 *   return: the radix word in order
 *   word(in): the stored value, as a radix word holds it
 *   sign_bit(in): word_sign_bit ()
 */
template <typename RADIX_WORD>
static inline RADIX_WORD
ieee_to_order (RADIX_WORD word, RADIX_WORD sign_bit)
{
  /* a positive value: flipping the first bit puts it above every negative value */
  if ((word & sign_bit) == 0)
    {
      return word ^ sign_bit;
    }

  /* -0.0, the sign bit alone: left as it is, so it equals +0.0 */
  if (word == sign_bit)
    {
      return word;
    }

  /* a negative value: flip every bit */
  return (RADIX_WORD) ~word;
}

/*
 * oid_to_order () - Puts a stored OID in the order oid_compare () reads it
 *   return: the radix word in order
 *   word(in): the stored OID, as a radix word holds it: the page, the slot, then the volume
 *
 * Note: The fields order as unsigned bytes, as no field of a key OID is negative.
 */
static inline UINT64
oid_to_order (UINT64 word)
{
  assert (OR_OID_SIZE == 8);

  /* a NULL OID is a NULL key, which never reaches the sort */
  assert ((big_endian_to_host (word) & 0x8000000080008000ULL) == 0);

  /* the last two bytes, the volume, move to the front: one rotation of the word */
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN
  return (word << 16) | (word >> 48);
#else
  return (word >> 16) | (word << 48);
#endif
}

/*
 * encode_radix_words () - Writes the radix word of each record
 *   return: void
 *   encoding(in):
 *   rec_base(in): the sort buffer the slots are offsets into
 *   slots(in):
 *   record_count(in):
 *   radix_words(out): one word of WORD_WIDTH bytes per slot
 *   varying_bits(out): the bits the radix words differ in
 *
 * Note: The key is read big-endian field by field. Flipping the bits its format names puts its bytes in unsigned
 *       order, and flipping every bit of a descending key's word turns that order round. A word is built in a
 *       register and written once. The format is a template argument, so its switch folds to one case.
 */
template <int WORD_WIDTH, RADIX_KEY_FORMAT FORMAT>
static void
encode_radix_words (const RADIX_KEY_ENCODING *encoding, const char *rec_base, const BTSORT_RUN_SLOT *slots,
		    int record_count, unsigned char *radix_words, UINT64 *varying_bits)
{
  const radix_word<WORD_WIDTH> sign_bit = word_sign_bit<WORD_WIDTH> ();
  const radix_word<WORD_WIDTH> desc_flip = encoding->is_desc ? (radix_word<WORD_WIDTH>) ~0ULL : 0;
  constexpr RADIX_KEY_FORMAT format = FORMAT;
  const char *rec;
  radix_word<WORD_WIDTH> word, any_set = 0, all_set = (radix_word<WORD_WIDTH>) ~0ULL;
  int rec_index;

  assert (encoding->word_width == WORD_WIDTH);

  for (rec_index = 0; rec_index < record_count; rec_index++)
    {
      rec = rec_base + slots[rec_index];

      assert (!BTSORT_REC_HDR (rec)->is_bigone && !BTSORT_REC_HDR (rec)->has_null);

      word = fixed_key_word<WORD_WIDTH> (format, (const unsigned char *) BTSORT_REC_KEY (rec));

      switch (format)
	{
	case RADIX_KEY_UNSIGNED:
	case RADIX_KEY_DATETIME:
	  break;		/* already in unsigned order */
	case RADIX_KEY_SIGNED:
	  word ^= sign_bit;
	  break;
	case RADIX_KEY_IEEE:
	case RADIX_KEY_MONETARY:
	  word = ieee_to_order (word, sign_bit);
	  break;
	case RADIX_KEY_OID:
	  word = oid_to_order (word);
	  break;
	default:
	  assert (false);
	  break;
	}

      word ^= desc_flip;

      memcpy (radix_words + rec_index * WORD_WIDTH, &word, WORD_WIDTH);

      /* a bit set in some radix words but not all marks a byte the run varies in */
      any_set |= word;
      all_set &= word;
    }

  *varying_bits = any_set ^ all_set;
}

/* the run's format as the constant the radix words are encoded with */
template <int WORD_WIDTH>
static void
encode_radix_words (const RADIX_KEY_ENCODING *encoding, const char *rec_base, const BTSORT_RUN_SLOT *slots,
		    int record_count, unsigned char *radix_words, UINT64 *varying_bits)
{
  switch (encoding->format)
    {
    case RADIX_KEY_UNSIGNED:
      encode_radix_words<WORD_WIDTH, RADIX_KEY_UNSIGNED> (encoding, rec_base, slots, record_count, radix_words,
	  varying_bits);
      break;
    case RADIX_KEY_DATETIME:
      encode_radix_words<WORD_WIDTH, RADIX_KEY_DATETIME> (encoding, rec_base, slots, record_count, radix_words,
	  varying_bits);
      break;
    case RADIX_KEY_SIGNED:
      encode_radix_words<WORD_WIDTH, RADIX_KEY_SIGNED> (encoding, rec_base, slots, record_count, radix_words,
	  varying_bits);
      break;
    case RADIX_KEY_IEEE:
      encode_radix_words<WORD_WIDTH, RADIX_KEY_IEEE> (encoding, rec_base, slots, record_count, radix_words,
	  varying_bits);
      break;
    case RADIX_KEY_OID:
      encode_radix_words<WORD_WIDTH, RADIX_KEY_OID> (encoding, rec_base, slots, record_count, radix_words,
	  varying_bits);
      break;
    case RADIX_KEY_MONETARY:
      encode_radix_words<WORD_WIDTH, RADIX_KEY_MONETARY> (encoding, rec_base, slots, record_count, radix_words,
	  varying_bits);
      break;
    default:
      *varying_bits = 0;
      assert (false);
      break;
    }
}

/*
 * 3. ENGINE: THE MSD RADIX SORT OF THE SLOTS BY THEIR RADIX WORDS
 */

namespace
{
  enum side : UINT8
  {
    HOME,			/* the slots accept () filled, and their radix words */
    SPARE			/* the arrays a split moves into */
  };

  /* a range whose keys share their first byte_index bytes, awaiting its next split */
  struct msd_range
  {
    int start;			/* first slot of the range */
    int record_count;
    UINT8 byte_index;		/* the key byte it splits on next, 8 at most */
    side where;
  };
  /* up to 255 per key byte are reserved out of the sort buffer, so every field is as narrow as its bound */
  static_assert (sizeof (msd_range) == 12, "the MSD stack entry must stay 12 bytes wide");

  using bucket_counts = std::array<int, 256>;	/* one count per value of a key byte */

  /* one MSD sort of a run: its slots and radix words, and the spare of each it moves into */
  template <int WORD_WIDTH>
  class msd_sort
  {
    public:
      msd_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, BTSORT_RUN_SLOT *slots_spare, unsigned char *radix_words,
		unsigned char *radix_words_spare, int record_count, UINT64 varying_bits, msd_range *stack,
		int stack_entries);

      void sort ();	/* the result ends in the home slots */

    private:
      bool is_ordered (bool *is_descending, bool *has_ties) const;
      void finish_ordered (bool is_descending, bool has_ties);
      void count_buckets (const msd_range &range, bucket_counts &bucket_count) const;
      bool is_all_in_one (const msd_range &range, const bucket_counts &bucket_count) const;
      void move_to_buckets (const msd_range &range, const bucket_counts &bucket_count);
      int push_buckets (const msd_range &range, const bucket_counts &bucket_count, UINT8 next_byte_index,
			int stack_top);
      void finish_range (const msd_range &range);

      const char *m_rec_base;	/* the sort buffer the slots are offsets into */
      BTSORT_RUN_SLOT *m_slots[2];	/* by side */
      unsigned char *m_radix_words[2];	/* radix words by side, in step with the slots */
      int m_record_count;	/* two or more, see radix_sort_run () */
      UINT64 m_varying_bits;	/* the bits the radix words differ in, from encode_radix_words () */
      msd_range *m_stack;
      int m_stack_entries;
  };
}

/* a range split no further: its key bytes left, then the OID tie-break (section 4) */
template <int WORD_WIDTH>
static void insertion_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, unsigned char *radix_words, int record_count);
static void sort_by_oid (const char *rec_base, BTSORT_RUN_SLOT *slots, int record_count);
static inline bool oid_less (const char *rec_base, BTSORT_RUN_SLOT left, BTSORT_RUN_SLOT right);

/* the side a split moves a range into */
static inline side
other (side s)
{
  return s == HOME ? SPARE : HOME;
}

/* each stacked range but the first holds over RADIX_MSD_CUTOFF records.
 * a split pushes at most 255 per key byte */
static inline int
msd_stack_entries (int word_width, int record_count)
{
  return 1 + std::min (255 * word_width, record_count / (RADIX_MSD_CUTOFF + 1));
}

/*
 * radix_sort_stack_bytes () - The MSD stack a run needs
 *   return: bytes
 *   encoding(in):
 *   max_records(in): the most records a run can hold
 */
size_t
radix_sort_stack_bytes (const RADIX_KEY_ENCODING *encoding, int max_records)
{
  assert (encoding->word_width >= 1);
  assert (max_records >= 0);

  return (size_t) msd_stack_entries (encoding->word_width, max_records) * sizeof (msd_range);
}

/*
 * radix_sort_run () - Orders one run by its key encoding
 *   return: void
 *   encoding(in):
 *   rec_base(in): the sort buffer the slots are offsets into
 *   slots(in/out): one per record, in arrival order so equal keys come in OID order
 *   slots_spare(in): as many slots, for scratch
 *   record_count(in):
 *   words_area(in): two radix words per record and DOUBLE_ALIGNMENT, or more
 *   words_bytes(in):
 *   stack_area(in): radix_sort_stack_bytes () for record_count or more, aligned for an int
 *   stack_bytes(in):
 *
 * Note: Counts and radix word offsets fit an int, as a slot is a 32-bit offset into a buffer under 2GB and a record
 *       costs at least 40 bytes at the widest word.
 */
void
radix_sort_run (const RADIX_KEY_ENCODING *encoding, const char *rec_base, BTSORT_RUN_SLOT *slots,
		BTSORT_RUN_SLOT *slots_spare, int record_count, char *words_area, size_t words_bytes, char *stack_area,
		size_t stack_bytes)
{
  const int word_width = encoding->word_width;
  const size_t one_side = (size_t) record_count * (size_t) word_width;
  unsigned char *radix_words, *radix_words_spare;
  msd_range *stack;
  int stack_entries;
  UINT64 varying_bits;

  assert (word_width >= 1);
  assert (record_count >= 0 && record_count <= INT_MAX / word_width);

  if (record_count <= 1)
    {
      return;
    }

  /* from the bottom of the words area: the radix words and their spare */
  radix_words = (unsigned char *) PTR_ALIGN (words_area, DOUBLE_ALIGNMENT);
  radix_words_spare = radix_words + one_side;
  assert ((char *) radix_words_spare + one_side <= words_area + words_bytes);

  stack = (msd_range *) stack_area;
  stack_entries = msd_stack_entries (word_width, record_count);
  assert (PTR_ALIGN (stack_area, INT_ALIGNMENT) == stack_area);
  assert ((size_t) stack_entries * sizeof (msd_range) <= stack_bytes);

  /* the width as a constant from here on: a word moves in one load and store, and its offsets are shifts */
  switch (word_width)
    {
    case 2:
      encode_radix_words<2> (encoding, rec_base, slots, record_count, radix_words, &varying_bits);
      msd_sort<2> (rec_base, slots, slots_spare, radix_words, radix_words_spare, record_count, varying_bits, stack,
		   stack_entries).sort ();
      break;
    case 4:
      encode_radix_words<4> (encoding, rec_base, slots, record_count, radix_words, &varying_bits);
      msd_sort<4> (rec_base, slots, slots_spare, radix_words, radix_words_spare, record_count, varying_bits, stack,
		   stack_entries).sort ();
      break;
    case 8:
      encode_radix_words<8> (encoding, rec_base, slots, record_count, radix_words, &varying_bits);
      msd_sort<8> (rec_base, slots, slots_spare, radix_words, radix_words_spare, record_count, varying_bits, stack,
		   stack_entries).sort ();
      break;
    default:
      /* radix_sort_resolve_key () resolves these widths only */
      assert (false);
      break;
    }
}

template <int WORD_WIDTH>
msd_sort<WORD_WIDTH>::msd_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, BTSORT_RUN_SLOT *slots_spare,
				unsigned char *radix_words, unsigned char *radix_words_spare, int record_count,
				UINT64 varying_bits, msd_range *stack, int stack_entries)
  : m_rec_base (rec_base)
  , m_slots { slots, slots_spare }
  , m_radix_words { radix_words, radix_words_spare }
  , m_record_count (record_count)
  , m_varying_bits (varying_bits)
  , m_stack (stack)
  , m_stack_entries (stack_entries)
{
}

/*
 * find_varying_bytes () - Works out the key byte to split on at or after each byte
 *   return: WORD_WIDTH + 1 entries, WORD_WIDTH when no byte is left
 *   varying_bits(in): from encode_radix_words ()
 *
 * Note: A byte the whole run shares puts every range in one bucket, so the walk steps over it without counting.
 */
template <int WORD_WIDTH>
static std::array<UINT8, WORD_WIDTH + 1>
find_varying_bytes (UINT64 varying_bits)
{
  using word = radix_word<WORD_WIDTH>;
  const word varying_word = (word) varying_bits;
  unsigned char varying_bytes[WORD_WIDTH];
  std::array<UINT8, WORD_WIDTH + 1> next_byte;
  int byte_index;

  /* by word byte, as the host lays a radix word out */
  memcpy (varying_bytes, &varying_word, WORD_WIDTH);

  next_byte[WORD_WIDTH] = WORD_WIDTH;
  for (byte_index = WORD_WIDTH - 1; byte_index >= 0; byte_index--)
    {
      next_byte[byte_index] = (varying_bytes[byte_index] != 0) ? byte_index : next_byte[byte_index + 1];
    }

  return next_byte;
}

/*
 * msd_sort::sort () - Orders the run by its radix words, most significant byte first
 *   return: void
 *
 * Note: Each range is finished or split 256 ways on its current byte, and an explicit stack holds the pending ones.
 *       A run the keys already order, either way, skips the walk, and the walk skips the bytes the run shares.
 */
template <int WORD_WIDTH>
void
msd_sort<WORD_WIDTH>::sort ()
{
  int stack_top = 0;
  msd_range range;
  bucket_counts bucket_count;	/* a count fits an int, see radix_sort_run () */
  std::array<UINT8, WORD_WIDTH + 1> next_byte;
  bool is_descending, has_ties;

  if (is_ordered (&is_descending, &has_ties))
    {
      finish_ordered (is_descending, has_ties);
      return;
    }

  /* the byte to split on at or after each, or WORD_WIDTH when none is left */
  next_byte = find_varying_bytes<WORD_WIDTH> (m_varying_bits);

  m_stack[stack_top++] = { 0, m_record_count, next_byte[0], HOME };

  while (stack_top > 0)
    {
      range = m_stack[--stack_top];

      assert (range.byte_index <= WORD_WIDTH);

      /* too few records to pay for a counting pass, or no key byte left to split on */
      if (range.record_count <= RADIX_MSD_CUTOFF || range.byte_index >= WORD_WIDTH)
	{
	  finish_range (range);
	  continue;
	}

      count_buckets (range, bucket_count);

      /* every record shares this byte: descend without moving anything */
      if (is_all_in_one (range, bucket_count))
	{
	  range.byte_index = next_byte[range.byte_index + 1];
	  m_stack[stack_top++] = range;
	  continue;
	}

      move_to_buckets (range, bucket_count);
      stack_top = push_buckets (range, bucket_count, next_byte[range.byte_index + 1], stack_top);
    }
}

/*
 * msd_sort::is_ordered () - Whether the keys of the run already run one way, up or down
 *   return: false at the first record that contradicts both directions
 *   is_descending(out): the keys run down
 *   has_ties(out): two neighbouring keys are equal
 */
template <int WORD_WIDTH>
bool
msd_sort<WORD_WIDTH>::is_ordered (bool *is_descending, bool *has_ties) const
{
  const unsigned char *radix_words = m_radix_words[HOME];
  radix_word<WORD_WIDTH> prev_integer, cur_integer;
  bool rising = false, falling = false, tied = false;
  int rec_index;

  prev_integer = word_from_bytes<WORD_WIDTH> (radix_words);
  for (rec_index = 1; rec_index < m_record_count; rec_index++)
    {
      cur_integer = word_from_bytes<WORD_WIDTH> (radix_words + rec_index * WORD_WIDTH);

      rising |= (prev_integer < cur_integer);
      falling |= (prev_integer > cur_integer);
      tied |= (prev_integer == cur_integer);

      /* a shuffled run gets this far within a few records */
      if (rising && falling)
	{
	  return false;
	}

      prev_integer = cur_integer;
    }

  *is_descending = falling;
  *has_ties = tied;

  return true;
}

/*
 * msd_sort::finish_ordered () - Finishes a run is_ordered () accepts
 *   return: void
 *   is_descending(in):
 *   has_ties(in):
 *
 * Note: Ordered input arrives descending, as accept () fills the slots from their far end.
 */
template <int WORD_WIDTH>
void
msd_sort<WORD_WIDTH>::finish_ordered (bool is_descending, bool has_ties)
{
  BTSORT_RUN_SLOT *slots = m_slots[HOME];
  const unsigned char *radix_words = m_radix_words[HOME];
  radix_word<WORD_WIDTH> group_integer;
  int group_start, group_end, first;

  if (is_descending)
    {
      std::reverse (slots, slots + m_record_count);
    }

  if (!has_ties)
    {
      return;
    }

  /* each group of equal keys, neighbours in an ordered run, still owes the OID order */
  for (group_start = 0; group_start < m_record_count; group_start = group_end)
    {
      group_integer = word_from_bytes<WORD_WIDTH> (radix_words + group_start * WORD_WIDTH);
      for (group_end = group_start + 1; group_end < m_record_count; group_end++)
	{
	  if (word_from_bytes<WORD_WIDTH> (radix_words + group_end * WORD_WIDTH) != group_integer)
	    {
	      break;
	    }
	}

      if (group_end - group_start > 1)
	{
	  /* the radix words were not reversed with the slots, so a descending run holds the group at the mirror place */
	  first = is_descending ? m_record_count - group_end : group_start;
	  sort_by_oid (m_rec_base, slots + first, group_end - group_start);
	}
    }
}

/*
 * msd_sort::count_buckets () - Counts the records of each value of the range's current key byte
 *   return: void
 *   range(in):
 *   bucket_count(out): 256 counts
 */
template <int WORD_WIDTH>
void
msd_sort<WORD_WIDTH>::count_buckets (const msd_range &range, bucket_counts &bucket_count) const
{
  const unsigned char *radix_words = m_radix_words[range.where];
  const int byte_index = range.byte_index;
  const int end = range.start + range.record_count;
  int rec_index;

  memset (bucket_count.data (), 0, sizeof (bucket_count));

  for (rec_index = range.start; rec_index < end; rec_index++)
    {
      bucket_count[radix_words[rec_index * WORD_WIDTH + byte_index]]++;
    }
}

/*
 * msd_sort::is_all_in_one () - Whether every record of the range shares its current key byte
 *   return: true when one bucket holds the whole range
 *   range(in):
 *   bucket_count(in): from count_buckets ()
 */
template <int WORD_WIDTH>
bool
msd_sort<WORD_WIDTH>::is_all_in_one (const msd_range &range, const bucket_counts &bucket_count) const
{
  const unsigned char first_byte = m_radix_words[range.where][range.start * WORD_WIDTH + range.byte_index];

  return bucket_count[first_byte] == range.record_count;
}

/*
 * msd_sort::move_to_buckets () - Moves the range into its 256 buckets on the other side
 *   return: void
 *   range(in):
 *   bucket_count(in):
 *
 * Note: One left to right pass, so records sharing the byte keep their order.
 */
template <int WORD_WIDTH>
void
msd_sort<WORD_WIDTH>::move_to_buckets (const msd_range &range, const bucket_counts &bucket_count)
{
  const BTSORT_RUN_SLOT *from_slots = m_slots[range.where];
  BTSORT_RUN_SLOT *to_slots = m_slots[other (range.where)];
  const unsigned char *from_words = m_radix_words[range.where];
  unsigned char *to_words = m_radix_words[other (range.where)];
  const int byte_index = range.byte_index;
  const int end = range.start + range.record_count;
  bucket_counts bucket_next;	/* where the next record of each bucket goes */
  const unsigned char *from_word;
  int rec_index, next_free, byte_value, put_at;

  /* each bucket starts where the buckets of the smaller byte values end */
  next_free = range.start;
  for (byte_value = 0; byte_value < 256; byte_value++)
    {
      bucket_next[byte_value] = next_free;
      next_free += bucket_count[byte_value];
    }

  /* each record and its radix word to the next free place of its bucket */
  for (rec_index = range.start; rec_index < end; rec_index++)
    {
      from_word = from_words + rec_index * WORD_WIDTH;
      put_at = bucket_next[from_word[byte_index]]++;

      to_slots[put_at] = from_slots[rec_index];
      memcpy (to_words + put_at * WORD_WIDTH, from_word, WORD_WIDTH);
    }
}

/*
 * msd_sort::push_buckets () - Stacks the buckets that still need sorting, on the next key byte
 *   return: the new stack top
 *   range(in):
 *   bucket_count(in):
 *   next_byte_index(in): the byte_index of the buckets, from next_byte
 *   stack_top(in):
 *
 * Note: A bucket of one record is final and only has to come home.
 */
template <int WORD_WIDTH>
int
msd_sort<WORD_WIDTH>::push_buckets (const msd_range &range, const bucket_counts &bucket_count, UINT8 next_byte_index,
				    int stack_top)
{
  const side to = other (range.where);
  int bucket_start = range.start;
  int byte_value;

  for (byte_value = 0; byte_value < 256; byte_value++)
    {
      if (bucket_count[byte_value] > 1)
	{
	  /* only a bucket worth splitting is stacked;
	   * one of RADIX_MSD_CUTOFF records or fewer is finished at once */
	  if (bucket_count[byte_value] > RADIX_MSD_CUTOFF)
	    {
	      assert (stack_top < m_stack_entries);
	      m_stack[stack_top++] = { bucket_start, bucket_count[byte_value], next_byte_index, to };
	    }
	  else
	    {
	      finish_range ({ bucket_start, bucket_count[byte_value], next_byte_index, to });
	    }
	}
      else if (bucket_count[byte_value] == 1 && to != HOME)
	{
	  m_slots[HOME][bucket_start] = m_slots[to][bucket_start];
	}

      bucket_start += bucket_count[byte_value];
    }

  return stack_top;
}

/*
 * msd_sort::finish_range () - Sorts a range split no further and brings it home
 *   return: void
 *   range(in):
 */
template <int WORD_WIDTH>
void
msd_sort<WORD_WIDTH>::finish_range (const msd_range &range)
{
  BTSORT_RUN_SLOT *slots = m_slots[range.where] + range.start;
  unsigned char *radix_words = m_radix_words[range.where] + range.start * WORD_WIDTH;

  if (range.record_count > 1)
    {
      if (range.byte_index < WORD_WIDTH)
	{
	  /* at most RADIX_MSD_CUTOFF records: by the key bytes left, then by OID within equal keys */
	  insertion_sort<WORD_WIDTH> (m_rec_base, slots, radix_words, range.record_count);
	}
      else
	{
	  /* no key byte left: the radix words, and so the keys, are all equal */
	  sort_by_oid (m_rec_base, slots, range.record_count);
	}
    }

  if (range.where == SPARE)
    {
      std::copy (slots, slots + range.record_count, m_slots[HOME] + range.start);
    }
}

/*
 * 4. FINISH RANGE: THE KEY BYTES LEFT, THEN THE OID TIE-BREAK
 */

/*
 * insertion_sort () - Orders a range by its radix words, then by OID within equal keys
 *   return: void
 *   rec_base(in): the sort buffer the slots are offsets into
 *   slots(in/out): the range's slots
 *   radix_words(in/out): the range's radix words, in step with the slots
 *   record_count(in):
 */
template <int WORD_WIDTH>
static void
insertion_sort (const char *rec_base, BTSORT_RUN_SLOT *slots, unsigned char *radix_words, int record_count)
{
  using word = radix_word<WORD_WIDTH>;
  word taken_integer, hole_integer, taken_word;
  int taken, hole;
  BTSORT_RUN_SLOT taken_slot;

  for (taken = 1; taken < record_count; taken++)
    {
      /* the record being placed waits in words while the records it passes shift up */
      taken_integer = word_from_bytes<WORD_WIDTH> (radix_words + taken * WORD_WIDTH);
      taken_slot = slots[taken];
      memcpy (&taken_word, radix_words + taken * WORD_WIDTH, WORD_WIDTH);

      for (hole = taken - 1; hole >= 0; hole--)
	{
	  /* whole radix words compare as the bytes left do, the bytes before being equal across the range */
	  hole_integer = word_from_bytes<WORD_WIDTH> (radix_words + hole * WORD_WIDTH);

	  /* equal radix words hold equal keys, so a key tie goes by OID */
	  if (hole_integer < taken_integer
	      || (hole_integer == taken_integer && oid_less (rec_base, slots[hole], taken_slot)))
	    {
	      break;
	    }

	  memcpy (radix_words + (hole + 1) * WORD_WIDTH, radix_words + hole * WORD_WIDTH, WORD_WIDTH);
	  slots[hole + 1] = slots[hole];
	}

      if (hole + 1 != taken)
	{
	  memcpy (radix_words + (hole + 1) * WORD_WIDTH, &taken_word, WORD_WIDTH);
	  slots[hole + 1] = taken_slot;
	}
    }
}

/*
 * sort_by_oid () - Orders records of equal keys by instance OID
 *   return: void
 *   rec_base(in):
 *   slots(in/out):
 *   record_count(in):
 */
static void
sort_by_oid (const char *rec_base, BTSORT_RUN_SLOT *slots, int record_count)
{
  auto ascending = [rec_base] (BTSORT_RUN_SLOT left, BTSORT_RUN_SLOT right)
  {
    return oid_less (rec_base, left, right);
  };
  auto descending = [rec_base] (BTSORT_RUN_SLOT left, BTSORT_RUN_SLOT right)
  {
    return oid_less (rec_base, right, left);
  };

  assert (record_count >= 2);

  /* one run, as a group mostly arrives: in order already, or reversed into it */
  if (std::is_sorted (slots, slots + record_count, ascending))
    {
      return;
    }
  if (std::is_sorted (slots, slots + record_count, descending))
    {
      std::reverse (slots, slots + record_count);
      return;
    }

  /* any other order */
  std::sort (slots, slots + record_count, ascending);
}

/* whether the left record's instance OID is the smaller, as btsort_compare_records () breaks a key tie */
static inline bool
oid_less (const char *rec_base, BTSORT_RUN_SLOT left, BTSORT_RUN_SLOT right)
{
  OID left_oid, right_oid;

  OR_GET_OID (BTSORT_REC_OID (rec_base + left), &left_oid);
  OR_GET_OID (BTSORT_REC_OID (rec_base + right), &right_oid);

  return OID_LT (&left_oid, &right_oid);
}
