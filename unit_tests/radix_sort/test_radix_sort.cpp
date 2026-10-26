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
 * test_radix_sort.cpp - the radix run sorter against an independent oracle
 *
 * - the oracle decodes the stored key into a native value and sorts with C++ operators, so it is derived from the disk
 *   format, not from the sorter's transforms. The property is KEY in the domain's direction, instance OID ascending
 *   within a key, which is what btsort_compare_records () answers
 */

#include "catch2/catch.hpp"

#include "btree_sort_radix.hpp"

#include "btree_sort.h"		/* BTSORT_REC_HEADER, BTSORT_RUN_SLOT: the sort record and the run the sorter sees */
#include "dbtype_def.h"
#include "object_representation.h"	/* OR_*_SIZE, OR_MVCCID_SIZE */
#include "object_representation_constants.h"	/* OR_OID_PAGEID, OR_OID_SLOTID, OR_OID_VOLID */
#include "string_opfunc.h"	/* bit_compare, varbit_compare: the oracles the bit string keys are checked against */
#include "language_support.h"	/* lang_init, LANG_COLL_*: a string key is checked against its collation's compare */
#include "object_primitive.h"	/* pr_type: the index comparator of a string domain is the oracle, NUMERIC_HEADER_SIZE */
#include "system_parameter.h"	/* ignore_trailing_space, which says whether a shorter VARCHAR orders first */
#include "dbtype.h"		/* db_make_numeric: a NUMERIC key is written by the type's own writer */
#include "numeric_opfunc.h"	/* numeric_coerce_dec_str_to_num */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace
{
  /* what the radix sorter hands the engine for a run of up to max_records: a spare slot array, the free space it
   * kept for two radix words a record and the MSD stack it set aside */
  struct run_work
  {
    std::vector<BTSORT_RUN_SLOT> slots_spare;
    std::vector<char> words_area;
    std::vector<char> stack;

    run_work (const RADIX_KEY_ENCODING &encoding, int max_records)
      : slots_spare ((size_t) std::max (max_records, 1)),
	words_area ((size_t) std::max (max_records, 1) * 2 * encoding.word_width + DOUBLE_ALIGNMENT),
	stack (radix_sort_stack_bytes (&encoding, std::max (max_records, 1)))
    {
    }

    void sort (const RADIX_KEY_ENCODING *encoding, const char *rec_base, BTSORT_RUN_SLOT *slots, int n)
    {
      radix_sort_run (encoding, rec_base, slots, slots_spare.data(), n, words_area.data(), words_area.size(),
		      stack.data(), stack.size());
    }

    /* a MIDXKEY, its work memory sized by the encoding radix_sort_resolve_midxkey () gave */
    void sort (const RADIX_MIDXKEY *key, BTSORT_CMP_FUNC *cmp_fn, void *cmp_arg, const char *rec_base,
	       BTSORT_RUN_SLOT *slots, int n)
    {
      radix_sort_midxkey_run (key, cmp_fn, cmp_arg, rec_base, slots, slots_spare.data(), n, words_area.data(),
			      words_area.size(), stack.data(), stack.size());
    }
  };

  /* the index-build sort record, as bt_load_put_buf_to_record () lays it out:
   *   [BTSORT_REC_HEADER][instance OID (+ class OID when unique)][0, 1 or 2 MVCCIDs][key]
   * the MVCCID count varies per record, so the key offset does too: header.key_off says where each key starts */
  const int MVCC_WORD_MODES = 3;

  int
  key_offset (bool unique, int mvcc_words)
  {
    int oid_size = unique ? (2 * OR_OID_SIZE) : OR_OID_SIZE;

    return BTSORT_REC_HEADER_SIZE + oid_size + mvcc_words * OR_MVCCID_SIZE;
  }

  void
  put_be (unsigned char *p, unsigned long long v, int width)
  {
    for (int b = 0; b < width; b++)
      {
	p[b] = (unsigned char) (v >> (8 * (width - 1 - b)));
      }
  }

  unsigned long long
  get_be (const unsigned char *p, int width)
  {
    unsigned long long v = 0;

    for (int b = 0; b < width; b++)
      {
	v = (v << 8) | p[b];
      }
    return v;
  }

  /* a field of a key as index_writeval () stores it: the value in host order */
  void
  put_field (unsigned char *p, unsigned long long v, int width)
  {
    UINT16 v16 = (UINT16) v;
    UINT32 v32 = (UINT32) v;
    UINT64 v64 = (UINT64) v;

    switch (width)
      {
      case OR_SHORT_SIZE:
	memcpy (p, &v16, sizeof (v16));
	break;
      case OR_INT_SIZE:
	memcpy (p, &v32, sizeof (v32));
	break;
      default:
	memcpy (p, &v64, sizeof (v64));
	break;
      }
  }

  unsigned long long
  get_field (const unsigned char *p, int width)
  {
    UINT16 v16;
    UINT32 v32;
    UINT64 v64;

    switch (width)
      {
      case OR_SHORT_SIZE:
	memcpy (&v16, p, sizeof (v16));
	return v16;
      case OR_INT_SIZE:
	memcpy (&v32, p, sizeof (v32));
	return v32;
      default:
	memcpy (&v64, p, sizeof (v64));
	return v64;
      }
  }

  /* the key of a fixed width type: the fields of the value from its high end, each as put_field () stores it */
  void
  put_key (unsigned char *p, DB_TYPE type, unsigned long long v, int width)
  {
    switch (type)
      {
      case DB_TYPE_DATETIME:
      case DB_TYPE_DATETIMELTZ:
      case DB_TYPE_DATETIMETZ:
	put_field (p + OR_DATETIME_DATE, v >> 32, OR_INT_SIZE);
	put_field (p + OR_DATETIME_TIME, v, OR_INT_SIZE);
	break;
      case DB_TYPE_OBJECT:
      case DB_TYPE_OID:
	put_field (p + OR_OID_PAGEID, v >> 32, OR_INT_SIZE);
	put_field (p + OR_OID_SLOTID, v >> 16, OR_SHORT_SIZE);
	put_field (p + OR_OID_VOLID, v, OR_SHORT_SIZE);
	break;
      default:
	put_field (p, v, width);
	break;
      }
  }

  /* a rank spread over volid, pageid and slotid, so that OID_LT order (volid, pageid, slotid) is rank order */
  void
  put_oid (unsigned char *body, unsigned int rank)
  {
    put_be (body + OR_OID_PAGEID, (rank >> 5) & 0x1F, OR_INT_SIZE);
    put_be (body + OR_OID_SLOTID, rank & 0x1F, OR_SHORT_SIZE);
    put_be (body + OR_OID_VOLID, rank >> 10, OR_SHORT_SIZE);
  }

  unsigned int
  oid_rank_of (const unsigned char *rec)
  {
    const unsigned char *body = (const unsigned char *) BTSORT_REC_BODY (rec);
    unsigned int pageid = (unsigned int) get_be (body + OR_OID_PAGEID, OR_INT_SIZE);
    unsigned int slotid = (unsigned int) get_be (body + OR_OID_SLOTID, OR_SHORT_SIZE);
    unsigned int volid = (unsigned int) get_be (body + OR_OID_VOLID, OR_SHORT_SIZE);

    return (volid << 10) | (pageid << 5) | slotid;
  }

  /* the instance OID tie-break every full comparator here ends with, as btsort_compare_records () does */
  int
  cmp_oid (const unsigned char *ra, const unsigned char *rb)
  {
    unsigned int oa = oid_rank_of (ra), ob = oid_rank_of (rb);

    return (oa < ob) ? -1 : (oa > ob) ? 1 : 0;
  }

  const unsigned char *
  key_of (const unsigned char *rec)
  {
    return (const unsigned char *) BTSORT_REC_KEY (rec);
  }

  /* one generated record: the bytes stored as the key, the OID that breaks its ties */
  struct sample
  {
    std::vector<unsigned char> key_bytes;
    unsigned int oid_rank = 0;
    bool has_null = false;	/* a MIDXKEY with a NULL column */
  };

  /* orders two stored keys as the type's comparator does, decoding the disk bytes, not via the sorter's transforms */
  typedef std::function<int (const unsigned char *, const unsigned char *)> key_compare;

  /* ---------------------------------------------------------------- oracles */

  template <typename SIGNED_T>
  int
  cmp_signed (const unsigned char *a, const unsigned char *b)
  {
    SIGNED_T va = (SIGNED_T) get_field (a, (int) sizeof (SIGNED_T));
    SIGNED_T vb = (SIGNED_T) get_field (b, (int) sizeof (SIGNED_T));

    return (va < vb) ? -1 : (va > vb) ? 1 : 0;
  }

  template <typename UNSIGNED_T>
  int
  cmp_unsigned (const unsigned char *a, const unsigned char *b)
  {
    UNSIGNED_T va = (UNSIGNED_T) get_field (a, (int) sizeof (UNSIGNED_T));
    UNSIGNED_T vb = (UNSIGNED_T) get_field (b, (int) sizeof (UNSIGNED_T));

    return (va < vb) ? -1 : (va > vb) ? 1 : 0;
  }

  /* a stored float or double is the IEEE 754 bits in host order, so the oracle reads the bits back into the type */
  int
  cmp_float (const unsigned char *a, const unsigned char *b)
  {
    UINT32 ba = (UINT32) get_field (a, OR_FLOAT_SIZE), bb = (UINT32) get_field (b, OR_FLOAT_SIZE);
    float va, vb;

    memcpy (&va, &ba, sizeof (va));
    memcpy (&vb, &bb, sizeof (vb));
    return (va < vb) ? -1 : (va > vb) ? 1 : 0;	/* MR_CMP (): -0.0 and +0.0 are equal, so the OID breaks the tie */
  }

  int
  cmp_double (const unsigned char *a, const unsigned char *b)
  {
    UINT64 ba = (UINT64) get_field (a, OR_DOUBLE_SIZE), bb = (UINT64) get_field (b, OR_DOUBLE_SIZE);
    double va, vb;

    memcpy (&va, &ba, sizeof (va));
    memcpy (&vb, &bb, sizeof (vb));
    return (va < vb) ? -1 : (va > vb) ? 1 : 0;
  }

  /* a stored monetary is the currency, then the amount as a double, which mr_index_cmpdisk_money () reads alone */
  int
  cmp_monetary (const unsigned char *a, const unsigned char *b)
  {
    return cmp_double (a + OR_MONETARY_AMOUNT, b + OR_MONETARY_AMOUNT);
  }

  /* a stored DATETIME is the date, then the time, which the comparator reads in that order, each unsigned */
  int
  cmp_datetime (const unsigned char *a, const unsigned char *b)
  {
    int c = cmp_unsigned<unsigned int> (a + OR_DATETIME_DATE, b + OR_DATETIME_DATE);

    if (c == 0)
      {
	c = cmp_unsigned<unsigned int> (a + OR_DATETIME_TIME, b + OR_DATETIME_TIME);
      }
    return c;
  }

  /* a stored OID is page, slot and volume, which oid_compare () reads as volume, page and slot, each signed */
  int
  cmp_oid_key (const unsigned char *a, const unsigned char *b)
  {
    int c = cmp_signed<short> (a + OR_OID_VOLID, b + OR_OID_VOLID);

    if (c == 0)
      {
	c = cmp_signed<int> (a + OR_OID_PAGEID, b + OR_OID_PAGEID);
      }
    if (c == 0)
      {
	c = cmp_signed<short> (a + OR_OID_SLOTID, b + OR_OID_SLOTID);
      }
    return c;
  }

  /* a stored BIT is its bytes as they lie, every record of the domain carrying the same count of them */
  key_compare
  cmp_bit (int bytes)
  {
    return [bytes] (const unsigned char *a, const unsigned char *b)
    {
      int c = bit_compare (a, bytes, b, bytes);

      return (c < 0) ? -1 : (c > 0) ? 1 : 0;
    };
  }

  /* a stored VARBIT: its bit length, one byte of it or a byte of 0xff and an int, then the bytes those bits fill */
  int
  varbit_bits (const unsigned char *image, int *value_at)
  {
    if (image[0] < 0xff)
      {
	*value_at = OR_BYTE_SIZE;
	return image[0];
      }
    *value_at = OR_BYTE_SIZE + OR_INT_SIZE;
    return (int) get_be (image + OR_BYTE_SIZE, OR_INT_SIZE);
  }

  int
  cmp_varbit (const unsigned char *a, const unsigned char *b)
  {
    int at_a, at_b;
    int bits_a = varbit_bits (a, &at_a), bits_b = varbit_bits (b, &at_b);
    int c = varbit_compare (a + at_a, BITS_TO_BYTES (bits_a), b + at_b, BITS_TO_BYTES (bits_b));

    return (c < 0) ? -1 : (c > 0) ? 1 : 0;
  }

  /* ------------------------------------------------------------ the harness */

  /* a run as the sorter sees it: the records in the sort buffer, one slot (offset) per record */
  struct run_image
  {
    std::vector<unsigned char> arena;
    std::vector<BTSORT_RUN_SLOT> slots;
    size_t rec_size;

    const char *rec_base() const
    {
      return (const char *) arena.data();
    }

    BTSORT_RUN_SLOT slot_of (size_t i) const
    {
      return (BTSORT_RUN_SLOT) (i * rec_size);
    }

    const unsigned char *rec_at (BTSORT_RUN_SLOT slot) const
    {
      return arena.data() + slot;
    }
  };

  /* lays the samples out as records, MVCCID count i % 3, so the key offset differs from record to record */
  run_image
  build_run (bool unique, const std::vector<sample> &samples)
  {
    run_image run;
    size_t n = samples.size();
    size_t blob = 0;		/* the widest key: a VARBIT varies from record to record */
    size_t widest;

    for (const sample &one : samples)
      {
	blob = std::max (blob, one.key_bytes.size());
      }
    widest = (size_t) key_offset (true, MVCC_WORD_MODES - 1) + blob;

    run.rec_size = (widest + 7) & ~ (size_t) 7;
    run.arena.assign ((n > 0 ? n : 1) * run.rec_size, 0);
    run.slots.assign (n > 0 ? n : 1, 0);

    for (size_t i = 0; i < n; i++)
      {
	unsigned char *rec = &run.arena[i * run.rec_size];
	BTSORT_REC_HEADER *header = BTSORT_REC_HDR (rec);
	int mvcc_words = (int) (i % MVCC_WORD_MODES);

	header->length = (UINT16) run.rec_size;
	header->is_bigone = 0;
	header->has_null = samples[i].has_null ? 1 : 0;
	header->key_off = (UINT8) key_offset (unique, mvcc_words);
	header->flags = (UINT8) ((mvcc_words >= 1 ? BTSORT_REC_HAS_INSID : 0)
				 | (mvcc_words >= 2 ? BTSORT_REC_HAS_DELID : 0));
	put_oid ((unsigned char *) BTSORT_REC_BODY (rec), samples[i].oid_rank);
	memcpy (BTSORT_REC_KEY (rec), samples[i].key_bytes.data(), samples[i].key_bytes.size());
	run.slots[i] = run.slot_of (i);
      }
    return run;
  }

  /* the domain the loader hands the sort: the default one, marked descending for a DESC index */
  TP_DOMAIN
  domain_of (DB_TYPE type, bool desc)
  {
    TP_DOMAIN domain = *tp_domain_resolve_default (type);

    domain.is_desc = desc ? 1 : 0;
    return domain;
  }

  /* build, sort by the encoding, check against the oracle */
  void
  check_run (const RADIX_KEY_ENCODING &encoding, bool desc, bool unique, const std::vector<sample> &samples,
	     const key_compare &oracle, const std::string &what)
  {
    int n = (int) samples.size();
    run_image run = build_run (unique, samples);
    std::vector<size_t> expected ((size_t) n);

    /* the oracle: key in the domain's direction, instance OID ascending between equal keys */
    std::iota (expected.begin(), expected.end(), (size_t) 0);
    std::sort (expected.begin(), expected.end(), [&] (size_t x, size_t y)
    {
      int c = oracle (samples[x].key_bytes.data(), samples[y].key_bytes.data());

      if (c != 0)
	{
	  return desc ? c > 0 : c < 0;
	}
      return samples[x].oid_rank < samples[y].oid_rank;
    });

    INFO (what);
    run_work work (encoding, n);
    work.sort (&encoding, run.rec_base(), run.slots.data(), n);

    for (size_t i = 0; i < (size_t) n; i++)
      {
	BTSORT_RUN_SLOT want = run.slot_of (expected[i]);

	if (run.slots[i] != want)
	  {
	    INFO (what << ": wrong record at " << i << " of " << n);
	    REQUIRE (run.slots[i] == want);
	  }
      }
  }

  /* the encoding of the domain the loader hands the sort */
  void
  check_run (DB_TYPE type, bool desc, bool unique, const std::vector<sample> &samples, const key_compare &oracle,
	     const std::string &what)
  {
    TP_DOMAIN domain = domain_of (type, desc);
    RADIX_KEY_ENCODING encoding;

    INFO (what);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    check_run (encoding, desc, unique, samples, oracle, what);
  }

  /* every neighbouring pair in comparator order: key, then the instance OID */
  void
  require_sorted (const run_image &run, int n, const key_compare &oracle)
  {
    for (int i = 1; i < n; i++)
      {
	const unsigned char *a = run.rec_at (run.slots[ (size_t) i - 1]);
	const unsigned char *b = run.rec_at (run.slots[ (size_t) i]);
	int c = oracle (key_of (a), key_of (b));

	REQUIRE ((c < 0 || (c == 0 && cmp_oid (a, b) < 0)));
      }
  }

  /* record counts either side of the MSD small-bucket cutoff and of a full 256-way split, plus a few that recurse */
  const int TEST_SIZES[] = { 0, 1, 2, 3, 23, 24, 25, 63, 64, 65, 129, 255, 256, 257, 1000, 4096 };

  /* an index build packs a second OID into the record when it is unique, which moves the key */
  const bool UNIQUE_MODES[] = { false, true };

  /* a single column index is declared ASC or DESC, and the key turns round while the instance OID does not */
  const bool KEY_DIRECTIONS[] = { false, true };

  /* one encoding serves every run of a sort, and the runs do not arrive in size order */
  const int REUSE_SIZES[] = { 10, 5000, 7, 20000, 1, 300 };

  /* column value shapes: spread, all alike, few distinct, ordered, narrow; *-dup adds ties (see make_oids ()) */
  enum value_shape
  {
    SHAPE_RANDOM,
    SHAPE_ALL_EQUAL,
    SHAPE_FEW_DISTINCT,
    SHAPE_ASCENDING,
    SHAPE_DESCENDING,
    SHAPE_NARROW,
    SHAPE_EXTREMES,
    SHAPE_ASC_DUP,
    SHAPE_DESC_DUP,
    SHAPE_TRAIL_SHARED,
    SHAPE_INTERIOR_SHARED,
    SHAPE_TRAIL_FEW,
    SHAPE_MIXED_SIGN,
    SHAPE_COUNT
  };

  const char *
  shape_name (int shape)
  {
    static const char *const names[] =
    {
      "random", "all-equal", "few-distinct", "ascending", "descending", "narrow", "extremes", "asc-dup", "desc-dup",
      "trail-shared", "interior-shared", "trail-few", "mixed-sign"
    };

    return names[shape];
  }

  /* draws a raw value for the shape, which the caller narrows to the key width */
  unsigned long long
  shape_value (int shape, int i, int n, std::mt19937_64 &rng)
  {
    switch (shape)
      {
      case SHAPE_ALL_EQUAL:
	return 0x4242424242424242ULL;
      case SHAPE_FEW_DISTINCT:
	return rng() % 5;
      case SHAPE_ASCENDING:
	return (unsigned long long) i;
      case SHAPE_DESCENDING:
	return (unsigned long long) (n - 1 - i);
      case SHAPE_NARROW:
	return 1000 + (rng() % 256);	/* only the low bytes move */
      case SHAPE_EXTREMES:
	return (rng() & 1) ? ~0ULL : 0ULL;
      case SHAPE_ASC_DUP:
	return (unsigned long long) (i / 4);
      case SHAPE_DESC_DUP:
	return (unsigned long long) ((n - 1 - i) / 4);
      case SHAPE_TRAIL_SHARED:
	return (rng() % 37) << 8;	/* the low byte shared, ties left to the OID */
      case SHAPE_INTERIOR_SHARED:
	return ((rng() % 7) << 24) | (rng() % 200);	/* shared bytes between moving ones */
      case SHAPE_TRAIL_FEW:
	return (rng() % 3) << 8;	/* ranges above the cutoff reach the last key byte */
      case SHAPE_MIXED_SIGN:
	return (unsigned long long) ((long long) (rng() % 601) - 300);
      case SHAPE_RANDOM:
      default:
	return rng();
      }
  }

  /*
   * the same shapes as real values: finite only, because MR_CMP () calls a NaN equal to everything, which is not an
   * order at all. Both zeros are in, since the comparator calls them equal and the two must encode alike.
   */
  double
  shape_real (int shape, int i, int n, std::mt19937_64 &rng)
  {
    static const double FEW[] = { -1.0, -0.0, 0.0, 1.0, 2.0 };
    static const double EDGE[] = { -1e30, -1.0, -1e-30, -0.0, 0.0, 1e-30, 1.0, 1e30 };

    switch (shape)
      {
      case SHAPE_ALL_EQUAL:
	return 42.5;
      case SHAPE_FEW_DISTINCT:
	return FEW[rng() % (sizeof (FEW) / sizeof (FEW[0]))];
      case SHAPE_ASCENDING:
	return (double) i - (double) n / 2.0;
      case SHAPE_DESCENDING:
	return (double) (n - 1 - i) - (double) n / 2.0;
      case SHAPE_NARROW:
	return 1000.0 + (double) (rng() % 256) / 256.0;
      case SHAPE_EXTREMES:
	return EDGE[rng() % (sizeof (EDGE) / sizeof (EDGE[0]))];
      case SHAPE_ASC_DUP:
	return (double) (i / 4);
      case SHAPE_DESC_DUP:
	return (double) ((n - 1 - i) / 4);
      case SHAPE_TRAIL_SHARED:
	return (double) (rng() % 37) * 1024.0;	/* integral: the low mantissa bytes shared */
      case SHAPE_INTERIOR_SHARED:
	return (double) (rng() % 7) * 65536.0 + (double) (rng() % 200);
      case SHAPE_TRAIL_FEW:
	return (double) (rng() % 3) - 1.0;
      case SHAPE_MIXED_SIGN:
	return (double) ((long long) (rng() % 601) - 300) / 4.0;	/* the negative flip moves the shared bytes */
      case SHAPE_RANDOM:
      default:
	return ldexp ((double) (long long) (rng() % 2000001) / 1000000.0 - 1.0, (int) (rng() % 120) - 60);
      }
  }

  /* the stored image of a real value: its IEEE 754 bits, which put_field () then writes in host order */
  unsigned long long
  real_bits (double v, int width)
  {
    if (width == OR_FLOAT_SIZE)
      {
	float f = (float) v;
	UINT32 b;

	memcpy (&b, &f, sizeof (b));
	return b;
      }
    else
      {
	UINT64 b;

	memcpy (&b, &v, sizeof (b));
	return b;
      }
  }

  /* the sorter hands the slots over in arrival order, so equal keys arrive as one ascending run, and shuffled is the
   * hard case */
  std::vector<unsigned int>
  make_oids (int n, bool shuffled, std::mt19937_64 &rng)
  {
    std::vector<unsigned int> oids ((size_t) n);

    for (int i = 0; i < n; i++)
      {
	oids[ (size_t) i] = (unsigned int) i;
      }
    if (shuffled)
      {
	std::shuffle (oids.begin(), oids.end(), rng);
      }
    return oids;
  }

  /* n random bigint keys with distinct, shuffled OIDs */
  std::vector<sample>
  bigint_samples (int n, std::mt19937_64 &rng)
  {
    std::vector<unsigned int> oids = make_oids (n, true, rng);
    std::vector<sample> samples ((size_t) n);

    for (int i = 0; i < n; i++)
      {
	size_t ix = (size_t) i;

	samples[ix].key_bytes.assign (OR_BIGINT_SIZE, 0);
	samples[ix].oid_rank = oids[ix];
	put_field (samples[ix].key_bytes.data(), rng(), OR_BIGINT_SIZE);
      }
    return samples;
  }

  /* more than RADIX_MSD_CUTOFF records, so a bucket this large is stacked */
  const int STACKED_BUCKET_RECORDS = 65;

  /* the bit string domain of an index column: the precision in bits, declared ASC or DESC */
  TP_DOMAIN
  bit_domain (int precision, bool desc, DB_TYPE type = DB_TYPE_BIT)
  {
    TP_DOMAIN domain = *tp_domain_resolve_default (type);

    domain.precision = precision;
    domain.is_desc = desc ? 1 : 0;
    return domain;
  }

  bool
  resolve_bit (RADIX_KEY_ENCODING &encoding, int precision, DB_TYPE type = DB_TYPE_BIT)
  {
    TP_DOMAIN domain = bit_domain (precision, false, type);

    return radix_sort_resolve_key (&encoding, &domain);
  }

  /* the encoding the key table hands out for a bit string column */
  RADIX_KEY_ENCODING
  bit_encoding (int precision, bool desc, DB_TYPE type = DB_TYPE_BIT)
  {
    TP_DOMAIN domain = bit_domain (precision, desc, type);
    RADIX_KEY_ENCODING encoding;

    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    return encoding;
  }

  /* the image mr_data_writeval_bit () stores: the bytes as they lie, the first lead of them the same in every record */
  sample
  bit_sample (int key_bytes, int lead, int shape, int i, int n, std::mt19937_64 &rng, unsigned int oid)
  {
    sample one;

    one.key_bytes.assign ((size_t) key_bytes, 0);
    for (int at = 0; at < key_bytes; at++)
      {
	/* every key opens with the same bytes, so a wide key has to ask for the words past them */
	one.key_bytes[ (size_t) at] = (at < lead) ? 0xa5 : (unsigned char) shape_value (shape, i, n, rng);
      }
    one.oid_rank = oid;
    return one;
  }

  /* the image or_put_varbit () stores: the bit length, one byte of it or a byte of 0xff and an int, then the bytes */
  std::vector<unsigned char>
  varbit_image (int bit_length, const std::vector<unsigned char> &value)
  {
    std::vector<unsigned char> image;

    if (bit_length < 0xff)
      {
	image.push_back ((unsigned char) bit_length);
      }
    else
      {
	image.assign ((size_t) (OR_BYTE_SIZE + OR_INT_SIZE), 0xff);
	put_be (image.data() + OR_BYTE_SIZE, (unsigned long long) bit_length, OR_INT_SIZE);
      }
    image.insert (image.end(), value.begin(), value.begin() + BITS_TO_BYTES (bit_length));
    return image;
  }

  /* a VARBIT of any bit length up to the precision, the first lead bytes the same in every record, so a short value
   * opens a longer one and only the length tells them apart */
  sample
  varbit_sample (int precision, int lead, int shape, int i, int n, std::mt19937_64 &rng, unsigned int oid)
  {
    const int bit_length = (int) (shape_value (shape, i, n, rng) % (unsigned long long) (precision + 1));
    std::vector<unsigned char> value ((size_t) BITS_TO_BYTES (precision), 0);
    sample one;

    for (int at = 0; at < BITS_TO_BYTES (bit_length); at++)
      {
	value[ (size_t) at] = (at < lead) ? 0xa5 : (unsigned char) shape_value (shape, i, n, rng);
      }
    one.key_bytes = varbit_image (bit_length, value);
    one.oid_rank = oid;
    return one;
  }

  /* unsigned keys that fill the MSD stack: STACKED_BUCKET_RECORDS records under every leading byte, or with along_key
   * 255 such siblings at every key byte on the way down the 0xFF path, which fills it to the key's bound */
  std::vector<sample>
  stack_filling_samples (DB_TYPE type, int width, bool along_key, std::mt19937_64 &rng)
  {
    int depth = along_key ? width : 1;
    std::vector<sample> samples;
    std::vector<unsigned int> oids;

    for (int level = 0; level < depth; level++)
      {
	/* 0xFF leads on to the next level, but at the last */
	int top_byte = (level == depth - 1) ? 255 : 254;

	for (int byte = 0; byte <= top_byte; byte++)
	  {
	    for (int k = 0; k < STACKED_BUCKET_RECORDS; k++)
	      {
		sample s;

		s.key_bytes.assign ((size_t) width, 0xFF);
		s.key_bytes[ (size_t) level] = (unsigned char) byte;
		for (int b = level + 1; b < width; b++)
		  {
		    s.key_bytes[ (size_t) b] = (unsigned char) rng();
		  }
		/* the bytes above are the radix key, stored as the type stores it */
		put_key (s.key_bytes.data(), type, get_be (s.key_bytes.data(), width), width);
		samples.push_back (s);
	      }
	  }
      }

    /* out of order, or the sort takes its ordered run shortcut */
    std::shuffle (samples.begin(), samples.end(), rng);
    oids = make_oids ((int) samples.size(), true, rng);
    for (size_t ix = 0; ix < samples.size(); ix++)
      {
	samples[ix].oid_rank = oids[ix];
      }
    return samples;
  }
}

/* every scalar type the single-column path supports, at every width it produces */
TEST_CASE ("radix sorts every supported scalar type", "[radix_sort]")
{
  struct scalar_case
  {
    const char *name;
    DB_TYPE type;
    int expect_width;
    key_compare oracle;
    int zone_bytes;		/* stored after the word, never read by the comparator */
  };

  const std::vector<scalar_case> cases =
  {
    { "short", DB_TYPE_SHORT, OR_SHORT_SIZE, cmp_signed<short>, 0 },
    /* an enum index is compared as an unsigned short - see mr_index_cmpdisk_enumeration */
    { "enumeration", DB_TYPE_ENUMERATION, OR_SHORT_SIZE, cmp_unsigned<unsigned short>, 0 },
    { "integer", DB_TYPE_INTEGER, OR_INT_SIZE, cmp_signed<int>, 0 },
    { "bigint", DB_TYPE_BIGINT, OR_BIGINT_SIZE, cmp_signed<long long>, 0 },
    { "date", DB_TYPE_DATE, OR_DATE_SIZE, cmp_unsigned<unsigned int>, 0 },
    { "time", DB_TYPE_TIME, OR_TIME_SIZE, cmp_unsigned<unsigned int>, 0 },
    { "timestamp", DB_TYPE_TIMESTAMP, OR_UTIME_SIZE, cmp_unsigned<unsigned int>, 0 },
    { "datetime", DB_TYPE_DATETIME, OR_DATETIME_SIZE, cmp_datetime, 0 },
    { "timestampltz", DB_TYPE_TIMESTAMPLTZ, OR_UTIME_SIZE, cmp_unsigned<unsigned int>, 0 },
    { "datetimeltz", DB_TYPE_DATETIMELTZ, OR_DATETIME_SIZE, cmp_datetime, 0 },
    /* the zone follows the time, and random zones leave equal times to the OID */
    {
      "timestamptz", DB_TYPE_TIMESTAMPTZ, OR_UTIME_SIZE, cmp_unsigned<unsigned int>,
      OR_TIMESTAMPTZ_SIZE - OR_UTIME_SIZE
    },
    {
      "datetimetz", DB_TYPE_DATETIMETZ, OR_DATETIME_SIZE, cmp_datetime,
      OR_DATETIMETZ_SIZE - OR_DATETIME_SIZE
    },
    { "object", DB_TYPE_OBJECT, OR_OID_SIZE, cmp_oid_key, 0 },
    { "oid", DB_TYPE_OID, OR_OID_SIZE, cmp_oid_key, 0 },
  };

  for (const scalar_case &c : cases)
    {
      RADIX_KEY_ENCODING encoding;

      INFO ("type " << c.name);
      REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (c.type)));
      REQUIRE (encoding.word_width == c.expect_width);

      for (bool desc : KEY_DIRECTIONS)
	{
	  for (bool unique : UNIQUE_MODES)
	    {
	      for (int n : TEST_SIZES)
		{
		  for (int shape = 0; shape < SHAPE_COUNT; shape++)
		    {
		      std::mt19937_64 rng (0x5eed0000u + (unsigned) n * 31u + (unsigned) shape);
		      std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		      std::vector<sample> samples ((size_t) n);

		      for (int i = 0; i < n; i++)
			{
			  size_t ix = (size_t) i;
			  unsigned long long value = shape_value (shape, i, n, rng);

			  /* a valid key OID only, see oid_to_order () */
			  if (c.type == DB_TYPE_OBJECT || c.type == DB_TYPE_OID)
			    {
			      value &= 0x7fffffff7fff7fffULL;
			    }

			  samples[ix].key_bytes.assign ((size_t) (c.expect_width + c.zone_bytes), 0);
			  samples[ix].oid_rank = oids[ix];
			  put_key (samples[ix].key_bytes.data(), c.type, value, c.expect_width);
			  if (c.zone_bytes > 0)
			    {
			      put_be (samples[ix].key_bytes.data() + c.expect_width, rng(), c.zone_bytes);
			    }
			}

		      check_run (c.type, desc, unique, samples, c.oracle,
				 std::string (c.name) + " n=" + std::to_string (n) + " " + shape_name (shape)
				 + (unique ? " unique" : " non-unique") + (desc ? " desc" : " asc"));
		    }
		}
	    }
	}
    }
}

/* float, double and a monetary's amount: the sign bit rules the order, a negative value runs backwards, and the two
 * zeros are one key */
TEST_CASE ("radix sorts IEEE 754 keys", "[radix_sort]")
{
  struct real_case
  {
    const char *name;
    DB_TYPE type;
    RADIX_KEY_FORMAT format;
    int width;
    key_compare oracle;
    int head_bytes;		/* stored before the word, never read by the comparator */
  };

  const std::vector<real_case> cases =
  {
    { "float", DB_TYPE_FLOAT, RADIX_KEY_IEEE, OR_FLOAT_SIZE, cmp_float, 0 },
    { "double", DB_TYPE_DOUBLE, RADIX_KEY_IEEE, OR_DOUBLE_SIZE, cmp_double, 0 },
    /* the currency leads the amount, and random currencies leave equal amounts to the OID */
    { "monetary", DB_TYPE_MONETARY, RADIX_KEY_MONETARY, OR_DOUBLE_SIZE, cmp_monetary, OR_MONETARY_AMOUNT },
  };

  for (const real_case &c : cases)
    {
      RADIX_KEY_ENCODING encoding;

      INFO ("type " << c.name);
      REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (c.type)));
      REQUIRE (encoding.word_width == c.width);
      REQUIRE (encoding.format == c.format);

      for (bool desc : KEY_DIRECTIONS)
	{
	  for (bool unique : UNIQUE_MODES)
	    {
	      for (int n : TEST_SIZES)
		{
		  for (int shape = 0; shape < SHAPE_COUNT; shape++)
		    {
		      std::mt19937_64 rng (0x1eee0000u + (unsigned) n * 31u + (unsigned) shape);
		      std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		      std::vector<sample> samples ((size_t) n);

		      for (int i = 0; i < n; i++)
			{
			  size_t ix = (size_t) i;
			  double v = shape_real (shape, i, n, rng);

			  samples[ix].key_bytes.assign ((size_t) (c.head_bytes + c.width), 0);
			  samples[ix].oid_rank = oids[ix];
			  put_field (samples[ix].key_bytes.data() + c.head_bytes, real_bits (v, c.width), c.width);
			  if (c.head_bytes > 0)
			    {
			      put_be (samples[ix].key_bytes.data(), rng(), c.head_bytes);
			    }
			}

		      check_run (c.type, desc, unique, samples, c.oracle,
				 std::string (c.name) + " n=" + std::to_string (n) + " " + shape_name (shape)
				 + (unique ? " unique" : " non-unique") + (desc ? " desc" : " asc"));
		    }
		}
	    }
	}
    }
}

/* a BIT key: the bytes the precision asks for, as they lie, taken a word at a time when wider than a word */
TEST_CASE ("radix sorts BIT keys as bit_compare () does", "[radix_sort]")
{
  struct bit_case
  {
    int precision;		/* bits */
    int width;			/* the word width the key table picks */
    std::vector<int> leads;	/* shared opening bytes: none, part of the first word, a whole word, more */
  };

  const std::vector<bit_case> cases =
  {
    { 1, 2, { 0 } },
    { 9, 2, { 0, 1 } },		/* two bytes, the second part used */
    { 24, 4, { 0, 2 } },
    { 33, 8, { 0, 4 } },	/* five bytes: one word, part filled */
    { 64, 8, { 0, 4 } },	/* one word exactly */
    { 72, 8, { 0, 8 } },	/* a second word of one byte */
    { 160, 8, { 0, 4, 8, 12, 16 } },
    { 192, 8, { 0, 8, 16 } },	/* three words exactly */
    { 504, 8, { 0, 48 } },
    { 2032, 8, { 0, 200 } },
    { 16384, 8, { 0, 2040 } },	/* 256 words: the last word index a byte would hold */
    { 16392, 8, { 2048 } },	/* 257 words: one past it */
  };

  for (const bit_case &c : cases)
    {
      const int bytes = BITS_TO_BYTES (c.precision);
      const key_compare oracle = cmp_bit (bytes);
      RADIX_KEY_ENCODING encoding;

      INFO ("bit(" << c.precision << ")");
      REQUIRE (resolve_bit (encoding, c.precision));
      REQUIRE (encoding.key_length == bytes);
      REQUIRE (encoding.word_width == c.width);
      REQUIRE (encoding.format == RADIX_KEY_BIT);

      for (int lead : c.leads)
	{
	  for (bool desc : KEY_DIRECTIONS)
	    {
	      for (int n : TEST_SIZES)
		{
		  for (int shape = 0; shape < SHAPE_COUNT; shape++)
		    {
		      std::mt19937_64 rng (0xb1700000u + (unsigned) c.precision * 977u + (unsigned) lead * 101u
					   + (unsigned) n * 31u + (unsigned) shape);
		      std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		      std::vector<sample> samples ((size_t) n);
		      std::string what = "bit(" + std::to_string (c.precision) + ") lead=" + std::to_string (lead);

		      for (int i = 0; i < n; i++)
			{
			  samples[ (size_t) i] = bit_sample (bytes, lead, shape, i, n, rng, oids[ (size_t) i]);
			}

		      what += " n=" + std::to_string (n) + " " + shape_name (shape) + (desc ? " desc" : " asc");
		      check_run (bit_encoding (c.precision, desc), desc, false, samples, oracle, what);
		    }
		}
	    }
	}
    }
}

/* a VARBIT key: the bytes its bit length reaches in whole words, zero past them, then a word of their count */
TEST_CASE ("radix sorts VARBIT keys as varbit_compare () does", "[radix_sort]")
{
  struct varbit_case
  {
    int precision;		/* bits */
    int width;			/* the word width the key table picks */
    std::vector<int> leads;	/* shared opening bytes: none, or enough for a short value to open a longer one */
  };

  const std::vector<varbit_case> cases =
  {
    { 8, 8, { 0 } },		/* one byte: a word of it, a word of the count */
    { 16, 8, { 0, 1 } },
    { 48, 8, { 0, 3 } },
    { 64, 8, { 0, 4 } },	/* a word exactly, the count in the next */
    { 72, 8, { 0, 8 } },	/* a second word of one byte */
    { 400, 8, { 0, 8 } },	/* a length of 255 bits or more takes five bytes of the image */
    { 2040, 8, { 0, 200 } },
    { 4096, 8, { 0, 512 } },	/* every value opens every longer one, so the count alone tells them apart */
  };

  for (const varbit_case &c : cases)
    {
      RADIX_KEY_ENCODING encoding;

      INFO ("varbit(" << c.precision << ")");
      REQUIRE (resolve_bit (encoding, c.precision, DB_TYPE_VARBIT));
      REQUIRE (encoding.key_length == DB_ALIGN (BITS_TO_BYTES (c.precision), OR_BIGINT_SIZE) + OR_BIGINT_SIZE);
      REQUIRE (encoding.word_width == c.width);
      REQUIRE (encoding.format == RADIX_KEY_VARBIT);

      for (int lead : c.leads)
	{
	  for (bool desc : KEY_DIRECTIONS)
	    {
	      const RADIX_KEY_ENCODING run_encoding = bit_encoding (c.precision, desc, DB_TYPE_VARBIT);

	      for (int n : TEST_SIZES)
		{
		  for (int shape = 0; shape < SHAPE_COUNT; shape++)
		    {
		      std::mt19937_64 rng (0x7a1b0000u + (unsigned) c.precision * 977u + (unsigned) lead * 101u
					   + (unsigned) n * 31u + (unsigned) shape);
		      std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		      std::vector<sample> samples ((size_t) n);
		      std::string what = "varbit(" + std::to_string (c.precision) + ") lead=" + std::to_string (lead);

		      for (int i = 0; i < n; i++)
			{
			  samples[ (size_t) i] = varbit_sample (c.precision, lead, shape, i, n, rng, oids[ (size_t) i]);
			}

		      what += " n=" + std::to_string (n) + " " + shape_name (shape) + (desc ? " desc" : " asc");
		      check_run (run_encoding, desc, false, samples, cmp_varbit, what);
		    }
		}
	    }
	}
    }
}

/* the widest VARBIT: its radix key stops where a sort record does, and short values are told apart all the same */
TEST_CASE ("radix sorts the widest VARBIT by the bytes a sort record holds", "[radix_sort]")
{
  std::mt19937_64 rng (0x7a1bca9);
  const RADIX_KEY_ENCODING encoding = bit_encoding (DB_MAX_VARBIT_PRECISION, false, DB_TYPE_VARBIT);
  const int SIZES[] = { 100, 1000 };
  const int DISTINCT_KEYS[] = { 1, 3, 200 };

  REQUIRE (encoding.key_length == DB_ALIGN (BTSORT_REC_MAX_LENGTH, OR_BIGINT_SIZE) + OR_BIGINT_SIZE);

  for (int n : SIZES)
    {
      for (int distinct : DISTINCT_KEYS)
	{
	  std::vector<unsigned int> oids = make_oids (n, true, rng);
	  std::vector<sample> samples ((size_t) n);

	  for (int i = 0; i < n; i++)
	    {
	      size_t ix = (size_t) i;
	      int group = i % distinct;
	      /* one to three bytes: a short group opens a longer one, which the count alone tells apart */
	      std::vector<unsigned char> value = { 'x', (unsigned char) (group / 7), (unsigned char) (group % 7) };

	      samples[ix].key_bytes = varbit_image (8 * (1 + group % 3), value);
	      samples[ix].oid_rank = oids[ix];
	    }

	  std::string what = "widest varbit n=" + std::to_string (n) + " distinct=" + std::to_string (distinct);

	  check_run (encoding, false, false, samples, cmp_varbit, what);
	}
    }
}

/* a wide key the records share for words on end: every word of it is asked for, then the OID settles the group */
TEST_CASE ("radix settles a wide key the records share by instance OID", "[radix_sort]")
{
  std::mt19937_64 rng (0xd0b1e);
  const int precision = 504;	/* 63 bytes: eight words of key, the last one part full */
  const int key_bytes = BITS_TO_BYTES (precision);
  const key_compare oracle = cmp_bit (key_bytes);
  /* few distinct keys over many records: every group is settled by the OID after eight words of the key */
  const int SHARED_SIZES[] = { 100, 5000, 40000 };
  const int DISTINCT_KEYS[] = { 1, 3, 200 };

  for (int n : SHARED_SIZES)
    {
      for (int distinct : DISTINCT_KEYS)
	{
	  std::vector<unsigned int> oids = make_oids (n, true, rng);
	  std::vector<sample> samples ((size_t) n);

	  for (int i = 0; i < n; i++)
	    {
	      size_t ix = (size_t) i;
	      int distinct_at = i % distinct;

	      /* the two last bytes carry the group, so the words agree until the last word of the key */
	      samples[ix].key_bytes.assign ((size_t) key_bytes, 'x');
	      samples[ix].key_bytes[ (size_t) key_bytes - 2] = (unsigned char) ('a' + (distinct_at / 26) % 26);
	      samples[ix].key_bytes[ (size_t) key_bytes - 1] = (unsigned char) ('a' + distinct_at % 26);
	      samples[ix].oid_rank = oids[ix];
	    }

	  std::string what = "shared wide key n=" + std::to_string (n) + " distinct=" + std::to_string (distinct);

	  check_run (bit_encoding (precision, false), false, false, samples, oracle, what);
	}
    }
}

namespace
{
  /* the domain of a string index column: the type, its precision in characters and a collation, in its codeset */
  TP_DOMAIN
  string_domain (DB_TYPE type, int precision, int collation_id, bool desc)
  {
    TP_DOMAIN domain = *tp_domain_resolve_default (type);

    domain.precision = precision;
    domain.codeset = (unsigned char) lang_get_collation (collation_id)->codeset;
    domain.collation_id = collation_id;
    domain.is_desc = desc ? 1 : 0;
    return domain;
  }

  /* the index comparator of the domain, which the sort has to agree with */
  key_compare
  cmp_disk_image (const TP_DOMAIN &domain)
  {
    return [domain] (const unsigned char *a, const unsigned char *b)
    {
      DB_VALUE_COMPARE_RESULT c = domain.type->index_cmpdisk (a, b, &domain, 0, 1, NULL);

      assert (c != DB_UNK);
      return (c == DB_LT) ? -1 : (c == DB_GT) ? 1 : 0;
    };
  }

  /* the characters a value is spelt in: a space and a tab among them, for a collation that treats a space as zero,
   * both cases of a letter and the bytes either side of the letters, for one that treats 'a'..'z' as 'A'..'Z', bytes
   * of the high half whose low seven bits spell a small letter, which that one leaves alone, and multibyte ones for
   * UTF-8 */
  const std::vector<std::string> ISO_LETTERS =
  {
    "a", "b", "z", "A", "M", "Z", "@", "[", "`", "{", " ", "\t", "0", "9", "~", "\xc9", "\xe9"
  };
  const std::vector<std::string> UTF8_LETTERS =
  {
    "a", "b", "z", "A", "Z", "@", "[", "`", "{", " ", "\t", "0", "\xc3\xa9", "\xc3\x84", "\xd0\x96", "\xed\x95\x9c"
  };
  const std::vector<std::string> RAW_LETTERS = { "a", "b", " ", "\t", std::string ("\0", 1), "\x7f", "\x80", "\xff" };

  const std::vector<std::string> &
  letters_of (int collation_id)
  {
    switch (lang_get_collation (collation_id)->codeset)
      {
      case INTL_CODESET_UTF8:
	return UTF8_LETTERS;
      case INTL_CODESET_ISO88591:
	return ISO_LETTERS;
      default:
	return RAW_LETTERS;
      }
  }

  /* the image the string writers store: a length byte, then the characters, a CHAR holding its precision of them and a
   * VARCHAR up to it, the first lead of them the same in every record */
  sample
  string_sample (bool is_char, int precision, const std::vector<std::string> &letters, int lead, int shape, int i,
		 int n, std::mt19937_64 &rng, unsigned int oid)
  {
    const int chars = is_char ? precision
		      : (int) (shape_value (shape, i, n, rng) % (unsigned long long) (precision + 1));
    std::string value;
    sample one;

    for (int at = 0; at < chars; at++)
      {
	size_t pick = (at < lead) ? 0 : (size_t) (shape_value (shape, i, n, rng) % letters.size ());

	value += letters[pick];
      }

    REQUIRE (value.size () < (size_t) OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION);
    one.key_bytes.assign (1, (unsigned char) value.size ());
    one.key_bytes.insert (one.key_bytes.end (), value.begin (), value.end ());
    one.oid_rank = oid;
    return one;
  }
}

/* a CHAR or a VARCHAR key: a length byte then the bytes, zero past them and where a space is, then the count when the
 * length orders */
TEST_CASE ("radix sorts CHAR and VARCHAR keys as their collation compares", "[radix_sort]")
{
  struct string_case
  {
    const char *name;
    DB_TYPE type;
    int precision;
    int collation_id;
    bool space_as_zero;
    bool case_insensitive;
    bool shorter_first_when_kept;	/* the shorter value first, with trailing spaces kept */
  };

  const std::vector<string_case> cases =
  {
    { "varchar(20) utf8_bin", DB_TYPE_VARCHAR, 20, LANG_COLL_UTF8_BINARY, true, false, true },
    /* the widest utf8 domain */
    { "varchar(63) utf8_bin", DB_TYPE_VARCHAR, 63, LANG_COLL_UTF8_BINARY, true, false, true },
    /* the widest iso one */
    { "varchar(254) iso88591_bin", DB_TYPE_VARCHAR, 254, LANG_COLL_ISO_BINARY, true, false, true },
    { "varchar(16) iso88591_en_cs", DB_TYPE_VARCHAR, 16, LANG_COLL_ISO_EN_CS, true, false, true },
    { "varchar(16) iso88591_en_ci", DB_TYPE_VARCHAR, 16, LANG_COLL_ISO_EN_CI, true, true, true },
    { "varchar(20) utf8_en_ci", DB_TYPE_VARCHAR, 20, LANG_COLL_UTF8_EN_CI, true, true, true },
    { "varchar(32) binary", DB_TYPE_VARCHAR, 32, LANG_COLL_BINARY, false, false, false },
    { "char(10) utf8_bin", DB_TYPE_CHAR, 10, LANG_COLL_UTF8_BINARY, true, false, false },
    { "char(10) utf8_en_ci", DB_TYPE_CHAR, 10, LANG_COLL_UTF8_EN_CI, true, true, false },
    { "char(3) iso88591_bin", DB_TYPE_CHAR, 3, LANG_COLL_ISO_BINARY, true, false, false },
    { "char(24) binary", DB_TYPE_CHAR, 24, LANG_COLL_BINARY, false, false, false },
  };
  const int LEADS[] = { 0, 4, 9 };
  const bool spaces_kept = !prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE);

  REQUIRE (lang_init () == NO_ERROR);

  for (const string_case &c : cases)
    {
      const bool is_char = (c.type == DB_TYPE_CHAR);
      const TP_DOMAIN domain = string_domain (c.type, c.precision, c.collation_id, false);
      const std::vector<std::string> &letters = letters_of (c.collation_id);
      const key_compare oracle = cmp_disk_image (domain);
      RADIX_KEY_ENCODING encoding;

      INFO (c.name);
      REQUIRE (radix_sort_resolve_key (&encoding, &domain));
      REQUIRE (encoding.format == RADIX_KEY_STRING);
      REQUIRE (encoding.word_width == OR_BIGINT_SIZE);
      REQUIRE (encoding.space_as_zero == c.space_as_zero);
      REQUIRE (encoding.case_insensitive == c.case_insensitive);
      REQUIRE (encoding.shorter_first == (c.shorter_first_when_kept && spaces_kept));
      REQUIRE (encoding.key_length == DB_ALIGN (c.precision * INTL_CODESET_MULT (TP_DOMAIN_CODESET (&domain)),
	       OR_BIGINT_SIZE) + (encoding.shorter_first ? OR_BIGINT_SIZE : 0));
      REQUIRE (encoding.min_key_length == OR_BYTE_SIZE + (is_char ? c.precision : 0));

      for (int lead : LEADS)
	{
	  for (bool desc : KEY_DIRECTIONS)
	    {
	      const TP_DOMAIN run_domain = string_domain (c.type, c.precision, c.collation_id, desc);
	      RADIX_KEY_ENCODING run_encoding;

	      REQUIRE (radix_sort_resolve_key (&run_encoding, &run_domain));

	      for (int n : TEST_SIZES)
		{
		  for (int shape = 0; shape < SHAPE_COUNT; shape++)
		    {
		      std::mt19937_64 rng (0x57a10000u + (unsigned) c.precision * 977u
					   + (unsigned) c.collation_id * 131u + (unsigned) lead * 101u
					   + (unsigned) n * 31u + (unsigned) shape);
		      std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		      std::vector<sample> samples ((size_t) n);
		      std::string what = std::string (c.name) + " lead=" + std::to_string (lead);

		      for (int i = 0; i < n; i++)
			{
			  samples[ (size_t) i] = string_sample (is_char, c.precision, letters,
								std::min (lead, c.precision), shape, i, n, rng,
								oids[ (size_t) i]);
			}

		      what += " n=" + std::to_string (n) + " " + shape_name (shape) + (desc ? " desc" : " asc");
		      check_run (run_encoding, desc, false, samples, oracle, what);
		    }
		}
	    }
	}
    }
}

namespace
{
  /* the domain of a NUMERIC index column of a fixed precision and scale */
  TP_DOMAIN
  numeric_domain (int precision, int scale, bool desc)
  {
    TP_DOMAIN domain = *tp_domain_resolve_default (DB_TYPE_NUMERIC);

    domain.precision = precision;
    domain.scale = scale;
    domain.is_desc = desc ? 1 : 0;
    return domain;
  }

  /* up to precision decimal digits of the shape, 19 at a time from the top, so the shape's order carries over */
  std::string
  numeric_digits (int precision, int shape, int i, int n, std::mt19937_64 &rng, bool *negative)
  {
    const int LIMB_DIGITS = 19;	/* 10^19 fits an unsigned 64-bit value */
    std::string digits;

    *negative = false;
    for (int left = precision; left > 0; left -= LIMB_DIGITS)
      {
	const int take = std::min (left, LIMB_DIGITS);
	const long long drawn = (long long) shape_value (shape, i, n, rng);
	unsigned long long limb = (drawn < 0) ? 0 - (unsigned long long) drawn : (unsigned long long) drawn;
	unsigned long long bound = 1;
	std::string spelt;

	if (left == precision)
	  {
	    *negative = (drawn < 0);	/* the first draw signs the value, so an ordered shape stays ordered */
	  }
	for (int digit = 0; digit < take; digit++)
	  {
	    bound *= 10;
	  }
	spelt = std::to_string (limb % bound);
	digits += std::string ((size_t) take - spelt.size (), '0') + spelt;
      }
    return digits;
  }

  /* the image the NUMERIC writer stores for the digits: a byte of size and sign, the precision, the scale, then the
   * magnitude with its leading zero bytes dropped. A NUMERIC without a precision (is_float) keeps the value's own */
  sample
  numeric_sample (const std::string &digits, int precision, int scale, bool is_float, bool negative, unsigned int oid)
  {
    unsigned char num[DB_NUMERIC_BUF_SIZE];
    char image[NUMERIC_HEADER_SIZE + DB_NUMERIC_BUF_SIZE];
    bool spelt_negative;
    DB_VALUE value;
    OR_BUF buf;
    sample one;

    numeric_coerce_dec_str_to_num (digits.c_str (), num, &spelt_negative);
    if (db_make_numeric (&value, num, precision, scale, DB_NUMERIC_BUF_SIZE, negative, is_float) != NO_ERROR)
      {
	FAIL ("db_make_numeric: " << digits);
      }
    or_init (&buf, image, (int) sizeof (image));
    if (tp_Numeric.index_writeval (&buf, &value) != NO_ERROR)
      {
	FAIL ("index_writeval: " << digits);
      }

    one.key_bytes.assign (image, buf.ptr);
    one.oid_rank = oid;
    return one;
  }
}

/* a NUMERIC key: the magnitude of the value scaled by the domain's scale, right aligned behind a byte for the sign */
TEST_CASE ("radix sorts NUMERIC keys as numeric_db_value_compare () does", "[radix_sort]")
{
  struct numeric_case
  {
    int precision;
    int scale;
    int key_length;		/* the widest magnitude the writer keeps for the precision, and a byte for the sign */
  };

  const std::vector<numeric_case> cases =
  {
    { 1, 0, 2 }, { 2, 0, 2 }, { 9, 2, 6 }, { 12, 4, 6 }, { 13, 0, 10 }, { 20, 5, 10 }, { 21, 3, 10 }, { 31, 0, 14 },
    { 32, 8, 18 }, { DB_MAX_FIXED_NUMERIC_PRECISION, 10, 18 },
  };

  for (const numeric_case &c : cases)
    {
      const TP_DOMAIN domain = numeric_domain (c.precision, c.scale, false);
      const key_compare oracle = cmp_disk_image (domain);
      RADIX_KEY_ENCODING encoding;

      INFO ("numeric(" << c.precision << "," << c.scale << ")");
      REQUIRE (radix_sort_resolve_key (&encoding, &domain));
      REQUIRE (encoding.format == RADIX_KEY_FIXED_NUMERIC);
      REQUIRE (encoding.key_length == c.key_length);
      REQUIRE (encoding.word_width == ((c.key_length <= OR_SHORT_SIZE) ? OR_SHORT_SIZE : OR_BIGINT_SIZE));
      REQUIRE (encoding.min_key_length == NUMERIC_HEADER_SIZE + OR_BYTE_SIZE);
      REQUIRE_FALSE (encoding.shorter_first);

      for (bool desc : KEY_DIRECTIONS)
	{
	  const TP_DOMAIN run_domain = numeric_domain (c.precision, c.scale, desc);
	  RADIX_KEY_ENCODING run_encoding;

	  REQUIRE (radix_sort_resolve_key (&run_encoding, &run_domain));

	  for (bool unique : UNIQUE_MODES)
	    {
	      for (int n : TEST_SIZES)
		{
		  for (int shape = 0; shape < SHAPE_COUNT; shape++)
		    {
		      std::mt19937_64 rng (0x0e0c0000u + (unsigned) c.precision * 977u + (unsigned) n * 31u
					   + (unsigned) shape);
		      std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		      std::vector<sample> samples ((size_t) n);
		      std::string what = "numeric(" + std::to_string (c.precision) + "," + std::to_string (c.scale)
					 + ") n=" + std::to_string (n) + " " + shape_name (shape)
					 + (unique ? " unique" : " non-unique") + (desc ? " desc" : " asc");

		      for (int i = 0; i < n; i++)
			{
			  bool negative;
			  std::string digits = numeric_digits (c.precision, shape, i, n, rng, &negative);

			  samples[ (size_t) i] = numeric_sample (digits, c.precision, c.scale, false, negative,
								 oids[ (size_t) i]);
			}

		      check_run (run_encoding, desc, unique, samples, oracle, what);
		    }
		}
	    }
	}
    }
}

/* a NUMERIC without a precision: each value its own scale, so the decimal exponent orders first, then the magnitude
 * made 40 digits */
TEST_CASE ("radix sorts NUMERIC keys without a precision as numeric_db_value_compare () does", "[radix_sort]")
{
  struct float_case
  {
    const char *name;
    int max_digits;		/* a value has 1 to this many digits */
    std::vector<int> scales;	/* a value has one of these scales */
  };

  std::vector<int> wide_scales;

  for (int scale = -20; scale <= 60; scale++)
    {
      wide_scales.push_back (scale);
    }

  const std::vector<float_case> cases =
  {
    { "typical", 12, { 0, 1, 2, 3, 4, 5, 6 } },
    { "one scale", 15, { 2 } },
    { "wide", DB_MAX_NUMERIC_PRECISION, wide_scales },
    {
      "extreme scales", DB_MAX_NUMERIC_PRECISION,
      { DB_MIN_NUMERIC_SCALE, DB_MIN_NUMERIC_SCALE + 1, -1, 0, 1, DB_MAX_NUMERIC_SCALE - 1, DB_MAX_NUMERIC_SCALE }
    },
  };
  const TP_DOMAIN domain = *tp_domain_resolve_default (DB_TYPE_NUMERIC);
  const key_compare oracle = cmp_disk_image (domain);
  RADIX_KEY_ENCODING encoding;

  REQUIRE (domain.precision == DB_DEFAULT_NUMERIC_PRECISION);
  REQUIRE (radix_sort_resolve_key (&encoding, &domain));
  REQUIRE (encoding.format == RADIX_KEY_FLOAT_NUMERIC);
  REQUIRE (encoding.key_length == DB_ALIGN (OR_BYTE_SIZE + OR_SHORT_SIZE + DB_NUMERIC_BUF_SIZE, OR_BIGINT_SIZE));
  REQUIRE (encoding.word_width == OR_BIGINT_SIZE);
  REQUIRE (encoding.min_key_length == NUMERIC_HEADER_SIZE + OR_BYTE_SIZE);
  REQUIRE_FALSE (encoding.shorter_first);

  for (const float_case &c : cases)
    {
      for (bool desc : KEY_DIRECTIONS)
	{
	  TP_DOMAIN run_domain = domain;
	  RADIX_KEY_ENCODING run_encoding;

	  run_domain.is_desc = desc ? 1 : 0;
	  REQUIRE (radix_sort_resolve_key (&run_encoding, &run_domain));

	  for (int n : TEST_SIZES)
	    {
	      for (int shape = 0; shape < SHAPE_COUNT; shape++)
		{
		  std::mt19937_64 rng (0xf10a0000u + (unsigned) c.max_digits * 977u + (unsigned) n * 31u
				       + (unsigned) shape);
		  std::vector<unsigned int> oids = make_oids (n, (shape & 1) != 0, rng);
		  std::vector<sample> samples ((size_t) n);
		  std::vector<std::string> all_digits ((size_t) n);
		  std::vector<int> all_scales ((size_t) n);
		  std::vector<char> all_negative ((size_t) n);
		  std::string what = std::string (c.name) + " n=" + std::to_string (n) + " " + shape_name (shape)
				     + (desc ? " desc" : " asc");

		  for (int i = 0; i < n; i++)
		    {
		      const size_t at = (size_t) i;
		      bool negative;

		      if (i % 5 == 4 && all_digits[at - 4].size () < (size_t) DB_MAX_NUMERIC_PRECISION
			  && all_scales[at - 4] < DB_MAX_NUMERIC_SCALE)
			{
			  /* the value four back once more, a digit longer and its scale one more */
			  all_digits[at] = all_digits[at - 4] + "0";
			  all_scales[at] = all_scales[at - 4] + 1;
			  negative = all_negative[at - 4] != 0;
			}
		      else
			{
			  /* every 31st a zero, of a scale and a sign mark like any other value */
			  const int digits = (i % 31 == 30) ? 1 : 1 + (int) (rng () % (unsigned) c.max_digits);

			  all_digits[at] = numeric_digits (digits, shape, i, n, rng, &negative);
			  if (i % 31 == 30)
			    {
			      all_digits[at] = "0";
			    }
			  all_scales[at] = c.scales[rng () % c.scales.size ()];
			}
		      all_negative[at] = negative ? 1 : 0;
		      samples[at] = numeric_sample (all_digits[at], (int) all_digits[at].size (), all_scales[at], true,
						    negative, oids[at]);
		    }

		  check_run (run_encoding, desc, false, samples, oracle, what);
		}
	    }
	}
    }
}

/* the work memory is the caller's: one sizing serves every run up to that size */
TEST_CASE ("radix sort works in the memory its caller sized", "[radix_sort]")
{
  std::mt19937_64 rng (20260821);

  SECTION ("one sizing serves runs of growing and shrinking size")
  {
    int largest = *std::max_element (std::begin (REUSE_SIZES), std::end (REUSE_SIZES));
    RADIX_KEY_ENCODING encoding;

    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_BIGINT)));
    run_work work (encoding, largest);
    for (int n : REUSE_SIZES)
      {
	std::vector<sample> samples = bigint_samples (n, rng);
	run_image run = build_run (false, samples);

	work.sort (&encoding, run.rec_base(), run.slots.data(), n);
	require_sorted (run, n, cmp_signed<long long>);
      }
  }

  SECTION ("the stack is bounded by the key and by the records")
  {
    RADIX_KEY_ENCODING encoding;

    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_INTEGER)));
    /* the MSD stack: 255 pending siblings per key byte bound a large run, the records a small one */
    REQUIRE (radix_sort_stack_bytes (&encoding, 1000000) == radix_sort_stack_bytes (&encoding, 100000000));
    REQUIRE (radix_sort_stack_bytes (&encoding, 10) < radix_sort_stack_bytes (&encoding, 1000000));
    REQUIRE (radix_sort_stack_bytes (&encoding, 1) > 0);

    /* a wide key splits on every byte of it in turn, so its bound grows with the key, not the word */
    RADIX_KEY_ENCODING wide = bit_encoding (504, false);
    RADIX_KEY_ENCODING one_word = bit_encoding (64, false);

    REQUIRE (radix_sort_stack_bytes (&wide, 100000000) > radix_sort_stack_bytes (&one_word, 100000000));
  }

  SECTION ("keys that fill the stack to either bound sort within it")
  {
    const struct
    {
      DB_TYPE type;
      int width;
      key_compare oracle;
    } cases[] =
    {
      { DB_TYPE_ENUMERATION, OR_SHORT_SIZE, cmp_unsigned<unsigned short> },
      { DB_TYPE_DATE, OR_DATE_SIZE, cmp_unsigned<unsigned int> },
      { DB_TYPE_DATETIME, OR_DATETIME_SIZE, cmp_datetime },
    };
    const bool bound_by_key[] = { false, true };

    for (const auto &c : cases)
      {
	for (bool along_key : bound_by_key)
	  {
	    check_run (c.type, false, false, stack_filling_samples (c.type, c.width, along_key, rng), c.oracle,
		       std::string ("width ") + std::to_string (c.width) + (along_key ? " along the key" : " by records"));
	  }
      }
  }

  SECTION ("a run takes one record at the least, the placeholder of a key wider than the buffer")
  {
    /* a sort buffer of three 16KB pages, aligned as the sorter expects, and a BIT key of 50,000 bytes */
    std::vector<double> buffer (3 * 16384 / sizeof (double));
    char *base = (char *) buffer.data ();
    char *top = base + buffer.size () * sizeof (double);
    TP_DOMAIN domain = bit_domain (400000, false);
    RADIX_KEY_ENCODING encoding = bit_encoding (400000, false);
    /* the placeholder of a record of the multipage file: a header and the VPID of the record */
    const int placeholder = BTSORT_REC_HEADER_SIZE + (int) sizeof (VPID);
    BTSORT_RUN run;
    char *rec;
    int room = 0;

    REQUIRE (encoding.min_key_length > top - base);

    BTSORT_RUN_INIT (&run, base, top, NULL, NULL);
    btsort_radix_sorter.init (&run, &domain);

    /* the empty run offers the placeholder a place, so the loader learns the key is wider than the buffer */
    rec = btsort_radix_sorter.reserve (&run, placeholder, &room);
    REQUIRE (rec == base);
    REQUIRE (room == placeholder);

    memset (rec, 0, (size_t) placeholder);
    BTSORT_REC_HDR (rec)->length = placeholder;
    BTSORT_REC_HDR (rec)->is_bigone = 1;
    btsort_radix_sorter.accept (&run, placeholder);
    REQUIRE (run.numrecs == 1);

    /* and the run is full at the one record its stack is sized for */
    REQUIRE (btsort_radix_sorter.reserve (&run, placeholder, &room) == NULL);
    REQUIRE (room == 0);

    REQUIRE (btsort_radix_sorter.sort (NULL, &run) == NO_ERROR);
    REQUIRE (btsort_radix_sorter.next (&run) == rec);
    REQUIRE (btsort_radix_sorter.next (&run) == NULL);

    btsort_radix_sorter.reset (&run);
    REQUIRE (btsort_radix_sorter.reserve (&run, placeholder, &room) == base);
  }
}

/* the key table itself: what it accepts, what it refuses, and the widths it hands out */
TEST_CASE ("radix key table", "[radix_sort]")
{
  RADIX_KEY_ENCODING encoding;

  SECTION ("types without a fixed-width key fall back to the compare sort")
  {
    const DB_TYPE unsupported[] =
    {
      DB_TYPE_VARNCHAR_DEPRECATED, DB_TYPE_NCHAR_DEPRECATED, DB_TYPE_MIDXKEY, DB_TYPE_SET, DB_TYPE_BLOB, DB_TYPE_CLOB,
      DB_TYPE_JSON, DB_TYPE_NULL
    };

    for (DB_TYPE t : unsupported)
      {
	INFO ("type " << (int) t);
	REQUIRE_FALSE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (t)));
      }
  }

  /* a NUMERIC: the widest magnitude the writer keeps for the precision and a byte for the sign, or without a precision
   * a sign, an exponent and 40 digits */
  SECTION ("a numeric is taken up to the fixed precision, and without a precision")
  {
    TP_DOMAIN numeric = *tp_domain_resolve_default (DB_TYPE_NUMERIC);
    const int TAKEN[] = { 1, 2, 3, 12, 13, 21, 22, 31, 32, DB_MAX_FIXED_NUMERIC_PRECISION };
    const int KEY_LENGTH[] = { 2, 2, 6, 6, 10, 10, 14, 14, 18, 18 };
    const int DECLINED[] = { TP_FLOATING_PRECISION_VALUE, 0, DB_MAX_FIXED_NUMERIC_PRECISION + 1 };

    numeric.precision = DB_DEFAULT_NUMERIC_PRECISION;
    REQUIRE (radix_sort_resolve_key (&encoding, &numeric));
    REQUIRE (encoding.format == RADIX_KEY_FLOAT_NUMERIC);
    REQUIRE (encoding.key_length == DB_ALIGN (OR_BYTE_SIZE + OR_SHORT_SIZE + DB_NUMERIC_BUF_SIZE, OR_BIGINT_SIZE));
    REQUIRE (encoding.min_key_length == NUMERIC_HEADER_SIZE + OR_BYTE_SIZE);

    numeric.scale = 0;
    for (size_t at = 0; at < sizeof (TAKEN) / sizeof (TAKEN[0]); at++)
      {
	INFO ("precision " << TAKEN[at]);
	numeric.precision = TAKEN[at];
	REQUIRE (radix_sort_resolve_key (&encoding, &numeric));
	REQUIRE (encoding.format == RADIX_KEY_FIXED_NUMERIC);
	REQUIRE (encoding.key_length == KEY_LENGTH[at]);
	REQUIRE (encoding.min_key_length == NUMERIC_HEADER_SIZE + OR_BYTE_SIZE);
	REQUIRE (encoding.word_width == ((KEY_LENGTH[at] <= OR_SHORT_SIZE) ? OR_SHORT_SIZE : OR_BIGINT_SIZE));
      }
    for (int precision : DECLINED)
      {
	INFO ("precision " << precision);
	numeric.precision = precision;
	REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &numeric));
      }
  }

  SECTION ("a key with a zone is as wide as its time")
  {
    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_TIMESTAMPTZ)));
    REQUIRE (encoding.word_width == OR_UTIME_SIZE);
    REQUIRE (encoding.key_length == OR_UTIME_SIZE);
    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_DATETIMETZ)));
    REQUIRE (encoding.word_width == OR_DATETIME_SIZE);
    REQUIRE (encoding.key_length == OR_DATETIME_SIZE);
    REQUIRE (encoding.format == RADIX_KEY_DATETIME);
  }

  SECTION ("a BIT is its bytes, in the word they fit or a word at a time")
  {
    REQUIRE (resolve_bit (encoding, 16));
    REQUIRE (encoding.word_width == 2);
    REQUIRE (encoding.key_length == 2);
    REQUIRE (encoding.format == RADIX_KEY_BIT);
    REQUIRE (resolve_bit (encoding, 17));
    REQUIRE (encoding.word_width == 4);
    REQUIRE (encoding.key_length == 3);
    REQUIRE (resolve_bit (encoding, 64));
    REQUIRE (encoding.word_width == 8);
    REQUIRE (encoding.key_length == 8);
    REQUIRE (resolve_bit (encoding, 72));
    REQUIRE (encoding.word_width == 8);
    REQUIRE (encoding.key_length == 9);

    /* no width is too much: a key as long as a sort record still has its words counted, and the stack bound stays
     * within the records */
    REQUIRE (resolve_bit (encoding, 16392));
    REQUIRE (encoding.key_length == 2049);
    REQUIRE (resolve_bit (encoding, DB_MAX_BIT_PRECISION));
    REQUIRE (encoding.key_length == BITS_TO_BYTES (DB_MAX_BIT_PRECISION));
    /* the records bound the stack long before the key does, for a BIGINT and the widest BIT alike */
    RADIX_KEY_ENCODING bigint;

    REQUIRE (radix_sort_resolve_key (&bigint, tp_domain_resolve_default (DB_TYPE_BIGINT)));
    REQUIRE (radix_sort_stack_bytes (&encoding, 49000) == radix_sort_stack_bytes (&bigint, 49000));

    /* no fixed length: a floating precision is a literal's, not a column's, and 0 stands for it in a domain */
    REQUIRE_FALSE (resolve_bit (encoding, TP_FLOATING_PRECISION_VALUE));
    REQUIRE_FALSE (resolve_bit (encoding, 0));
  }

  SECTION ("a CHAR or a VARCHAR under a byte collation is its bytes, a word at a time, with the length when it orders")
  {
    const bool spaces_kept = !prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE);
    TP_DOMAIN domain;

    REQUIRE (lang_init () == NO_ERROR);

    /* every byte its own weight but a space, and a shorter VARCHAR first unless trailing spaces are ignored */
    domain = string_domain (DB_TYPE_VARCHAR, 10, LANG_COLL_UTF8_BINARY, false);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    REQUIRE (encoding.format == RADIX_KEY_STRING);
    REQUIRE (encoding.word_width == OR_BIGINT_SIZE);
    REQUIRE (encoding.space_as_zero);
    REQUIRE (encoding.shorter_first == spaces_kept);
    REQUIRE (encoding.key_length == 40 + (spaces_kept ? OR_BIGINT_SIZE : 0));
    REQUIRE (encoding.min_key_length == OR_BYTE_SIZE);

    /* a CHAR always drops its trailing spaces, and holds its precision in characters at the least */
    domain = string_domain (DB_TYPE_CHAR, 10, LANG_COLL_UTF8_BINARY, false);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    REQUIRE (encoding.space_as_zero);
    REQUIRE_FALSE (encoding.shorter_first);
    REQUIRE (encoding.key_length == 40);
    REQUIRE (encoding.min_key_length == OR_BYTE_SIZE + 10);

    /* the binary collation reads the bytes as they lie */
    domain = string_domain (DB_TYPE_VARCHAR, 10, LANG_COLL_BINARY, false);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    REQUIRE_FALSE (encoding.space_as_zero);
    REQUIRE_FALSE (encoding.shorter_first);
    REQUIRE (encoding.key_length == 16);

    const int iso_byte_collations[] = { LANG_COLL_ISO_BINARY, LANG_COLL_ISO_EN_CS };

    for (int collation_id : iso_byte_collations)
      {
	domain = string_domain (DB_TYPE_VARCHAR, 10, collation_id, false);
	REQUIRE (radix_sort_resolve_key (&encoding, &domain));
	REQUIRE (encoding.key_length == 16 + (spaces_kept ? OR_BIGINT_SIZE : 0));
      }
    domain = string_domain (DB_TYPE_VARCHAR, 10, LANG_COLL_UTF8_EN_CS, false);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    REQUIRE_FALSE (encoding.case_insensitive);

    /* en_ci treats 'a'..'z' as 'A'..'Z' and the rest as en_cs does */
    const int case_insensitive_collations[] = { LANG_COLL_ISO_EN_CI, LANG_COLL_UTF8_EN_CI };

    for (int collation_id : case_insensitive_collations)
      {
	domain = string_domain (DB_TYPE_VARCHAR, 10, collation_id, false);
	REQUIRE (radix_sort_resolve_key (&encoding, &domain));
	REQUIRE (encoding.case_insensitive);
	REQUIRE (encoding.space_as_zero);
	REQUIRE (encoding.shorter_first == spaces_kept);
      }

    /* a collation of other weights, or reading a character at a time, keeps the compare sort */
    domain = string_domain (DB_TYPE_VARCHAR, 10, LANG_COLL_EUCKR_BINARY, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));
    domain = string_domain (DB_TYPE_VARCHAR, 10, LANG_COLL_UTF8_KO_CS, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));
    domain = string_domain (DB_TYPE_VARCHAR, 10, LANG_COLL_UTF8_TR_CS, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));

    /* a domain whose values can reach the length stored in another form keeps the compare sort */
    domain = string_domain (DB_TYPE_VARCHAR, 63, LANG_COLL_UTF8_BINARY, false);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    REQUIRE (encoding.key_length == 256 + (spaces_kept ? OR_BIGINT_SIZE : 0));
    domain = string_domain (DB_TYPE_VARCHAR, 64, LANG_COLL_UTF8_BINARY, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));
    domain = string_domain (DB_TYPE_VARCHAR, 254, LANG_COLL_ISO_BINARY, false);
    REQUIRE (radix_sort_resolve_key (&encoding, &domain));
    domain = string_domain (DB_TYPE_VARCHAR, 255, LANG_COLL_ISO_BINARY, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));
    domain = string_domain (DB_TYPE_CHAR, 255, LANG_COLL_ISO_BINARY, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));

    /* no precision, or the widest one of the default domain */
    domain = string_domain (DB_TYPE_VARCHAR, TP_FLOATING_PRECISION_VALUE, LANG_COLL_UTF8_BINARY, false);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, &domain));
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_VARCHAR)));
    REQUIRE (encoding.key_length == 0);
    REQUIRE_FALSE (encoding.shorter_first);
  }

  SECTION ("a VARBIT is its bytes in whole words, then a word of their count, as far as a sort record holds")
  {
    REQUIRE (resolve_bit (encoding, 8, DB_TYPE_VARBIT));
    REQUIRE (encoding.word_width == 8);
    REQUIRE (encoding.key_length == 2 * OR_BIGINT_SIZE);	/* a word of the byte, a word of the count */
    REQUIRE (encoding.min_key_length == OR_BYTE_SIZE);	/* the length byte of a value with no bits */
    REQUIRE (encoding.format == RADIX_KEY_VARBIT);
    REQUIRE (resolve_bit (encoding, 64, DB_TYPE_VARBIT));
    REQUIRE (encoding.key_length == 2 * OR_BIGINT_SIZE);
    REQUIRE (resolve_bit (encoding, 72, DB_TYPE_VARBIT));
    REQUIRE (encoding.key_length == 3 * OR_BIGINT_SIZE);

    /* a value past a sort record is a BIGONE, which skips the sort: the widest precision resolves and ends there */
    REQUIRE (resolve_bit (encoding, BTSORT_REC_MAX_LENGTH * 8, DB_TYPE_VARBIT));
    REQUIRE (encoding.key_length == DB_ALIGN (BTSORT_REC_MAX_LENGTH, OR_BIGINT_SIZE) + OR_BIGINT_SIZE);
    REQUIRE (resolve_bit (encoding, DB_MAX_VARBIT_PRECISION, DB_TYPE_VARBIT));
    REQUIRE (encoding.key_length == DB_ALIGN (BTSORT_REC_MAX_LENGTH, OR_BIGINT_SIZE) + OR_BIGINT_SIZE);
    REQUIRE (encoding.min_key_length == OR_BYTE_SIZE);

    REQUIRE_FALSE (resolve_bit (encoding, TP_FLOATING_PRECISION_VALUE, DB_TYPE_VARBIT));
    REQUIRE_FALSE (resolve_bit (encoding, 0, DB_TYPE_VARBIT));

    /* every other key is as long as its radix key */
    REQUIRE (resolve_bit (encoding, 72));
    REQUIRE (encoding.min_key_length == 9);
    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_BIGINT)));
    REQUIRE (encoding.min_key_length == OR_BIGINT_SIZE);
  }

  SECTION ("a monetary key is its amount")
  {
    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_MONETARY)));
    REQUIRE (encoding.word_width == OR_DOUBLE_SIZE);
    REQUIRE (encoding.key_length == OR_DOUBLE_SIZE);
    REQUIRE (encoding.format == RADIX_KEY_MONETARY);
  }

  SECTION ("an OID key takes the OID format")
  {
    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_OBJECT)));
    REQUIRE (encoding.word_width == OR_OID_SIZE);
    REQUIRE (encoding.format == RADIX_KEY_OID);
  }

  SECTION ("a descending domain is taken at the ascending width")
  {
    TP_DOMAIN asc = domain_of (DB_TYPE_INTEGER, false);
    TP_DOMAIN desc = domain_of (DB_TYPE_INTEGER, true);

    REQUIRE (radix_sort_resolve_key (&encoding, &asc));
    REQUIRE_FALSE (encoding.is_desc);
    REQUIRE (radix_sort_resolve_key (&encoding, &desc));
    REQUIRE (encoding.is_desc);
    REQUIRE (encoding.word_width == OR_INT_SIZE);
  }

  SECTION ("a declined key leaves no width behind")
  {
    REQUIRE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_INTEGER)));
    REQUIRE (encoding.word_width == OR_INT_SIZE);
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_JSON)));
    REQUIRE (encoding.word_width == 0);
    REQUIRE (encoding.key_length == 0);
    REQUIRE (encoding.min_key_length == 0);
    REQUIRE (encoding.format == RADIX_KEY_UNSIGNED);
    REQUIRE_FALSE (encoding.is_desc);
  }
}

namespace
{
  /* the columns a MIDXKEY is tried with */
  enum column_kind
  {
    COL_SHORT,
    COL_INT,
    COL_BIGINT,
    COL_FLOAT,
    COL_DOUBLE,
    COL_DATE,
    COL_TIME,
    COL_TIMESTAMP,
    COL_TIMESTAMPLTZ,
    COL_TIMESTAMPTZ,
    COL_DATETIME,
    COL_DATETIMELTZ,
    COL_DATETIMETZ,
    COL_ENUM,
    COL_MONETARY,
    COL_OID,
    COL_CHAR6,
    COL_VARCHAR12,
    COL_VARCHAR16_EN_CS,
    COL_VARCHAR16_EN_CI,
    COL_VARCHAR40,
    COL_LONG_VARCHAR200,	/* close to 200 characters, so the offsets of the columns after reach past 255 */
    COL_VARCHAR300,		/* a domain the key table declines, its values short */
    COL_BIT12,
    COL_VARBIT20,
    COL_VARBIT300,		/* a length of 255 bits or more takes the long form */
    COL_NUMERIC,
    COL_FLOAT_NUMERIC
  };

  /* a column: its kind and direction, its values' shape, or rows alike in groups of `group` when that is set, and a
   * NULL in about one row of `null_every`, 0 for none */
  struct column_spec
  {
    column_kind kind;
    bool desc;
    int shape;
    int group;
    int null_every;
  };

  TP_DOMAIN
  column_domain (const column_spec &spec)
  {
    switch (spec.kind)
      {
      case COL_SHORT:
	return domain_of (DB_TYPE_SHORT, spec.desc);
      case COL_INT:
	return domain_of (DB_TYPE_INTEGER, spec.desc);
      case COL_BIGINT:
	return domain_of (DB_TYPE_BIGINT, spec.desc);
      case COL_FLOAT:
	return domain_of (DB_TYPE_FLOAT, spec.desc);
      case COL_DOUBLE:
	return domain_of (DB_TYPE_DOUBLE, spec.desc);
      case COL_DATE:
	return domain_of (DB_TYPE_DATE, spec.desc);
      case COL_TIME:
	return domain_of (DB_TYPE_TIME, spec.desc);
      case COL_TIMESTAMP:
	return domain_of (DB_TYPE_TIMESTAMP, spec.desc);
      case COL_TIMESTAMPLTZ:
	return domain_of (DB_TYPE_TIMESTAMPLTZ, spec.desc);
      case COL_TIMESTAMPTZ:
	return domain_of (DB_TYPE_TIMESTAMPTZ, spec.desc);
      case COL_DATETIME:
	return domain_of (DB_TYPE_DATETIME, spec.desc);
      case COL_DATETIMELTZ:
	return domain_of (DB_TYPE_DATETIMELTZ, spec.desc);
      case COL_DATETIMETZ:
	return domain_of (DB_TYPE_DATETIMETZ, spec.desc);
      case COL_ENUM:
	return domain_of (DB_TYPE_ENUMERATION, spec.desc);
      case COL_MONETARY:
	return domain_of (DB_TYPE_MONETARY, spec.desc);
      case COL_OID:
	return domain_of (DB_TYPE_OID, spec.desc);
      case COL_CHAR6:
	return string_domain (DB_TYPE_CHAR, 6, LANG_COLL_ISO_BINARY, spec.desc);
      case COL_VARCHAR12:
	return string_domain (DB_TYPE_VARCHAR, 12, LANG_COLL_UTF8_BINARY, spec.desc);
      case COL_VARCHAR16_EN_CS:
	return string_domain (DB_TYPE_VARCHAR, 16, LANG_COLL_ISO_EN_CS, spec.desc);
      case COL_VARCHAR16_EN_CI:
	return string_domain (DB_TYPE_VARCHAR, 16, LANG_COLL_UTF8_EN_CI, spec.desc);
      case COL_VARCHAR40:
	return string_domain (DB_TYPE_VARCHAR, 40, LANG_COLL_ISO_BINARY, spec.desc);
      case COL_LONG_VARCHAR200:
	return string_domain (DB_TYPE_VARCHAR, 200, LANG_COLL_ISO_BINARY, spec.desc);
      case COL_VARCHAR300:
	return string_domain (DB_TYPE_VARCHAR, 300, LANG_COLL_ISO_BINARY, spec.desc);
      case COL_BIT12:
	return bit_domain (12, spec.desc);
      case COL_VARBIT20:
	return bit_domain (20, spec.desc, DB_TYPE_VARBIT);
      case COL_VARBIT300:
	return bit_domain (300, spec.desc, DB_TYPE_VARBIT);
      case COL_NUMERIC:
	return numeric_domain (10, 2, spec.desc);
      default:
	return domain_of (DB_TYPE_NUMERIC, spec.desc);	/* without a precision */
      }
  }

  /* the MIDXKEY domain of the columns, each a domain of its own direction */
  TP_DOMAIN
  midxkey_domain (std::vector<TP_DOMAIN> &columns)
  {
    TP_DOMAIN domain = *tp_domain_resolve_default (DB_TYPE_MIDXKEY);

    for (size_t at = 0; at < columns.size (); at++)
      {
	columns[at].next = (at + 1 < columns.size ()) ? &columns[at + 1] : NULL;
      }
    domain.precision = (int) columns.size ();
    domain.setdomain = columns.empty () ? NULL : columns.data ();
    domain.is_desc = 0;
    return domain;
  }

  /* a column's part of a MIDXKEY image: its bytes as the index writer writes them, none for a NULL */
  struct column_part
  {
    std::vector<unsigned char> bytes;
    bool is_null = false;
  };

  column_part
  written_part (const TP_DOMAIN &domain, DB_VALUE &value)
  {
    char image[64];
    OR_BUF buf;
    column_part part;

    or_init (&buf, image, (int) sizeof (image));
    if (domain.type->index_writeval (&buf, &value) != NO_ERROR)
      {
	FAIL ("index_writeval of a " << domain.type->name);
      }
    part.bytes.assign (image, buf.ptr);
    return part;
  }

  /* a draw's run of bytes, the same for the same draw */
  unsigned long long
  next_of (unsigned long long &state)
  {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state >> 33;
  }

  /* the image the string writers store, spelt from the draw: a length byte, then chars characters */
  column_part
  string_part (int chars, const std::vector<std::string> &letters, unsigned long long drawn)
  {
    unsigned long long state = drawn;
    std::string value;
    column_part part;

    for (int at = 0; at < chars; at++)
      {
	value += letters[ (size_t) (next_of (state) % letters.size ())];
      }
    REQUIRE (value.size () < (size_t) OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION);

    part.bytes.assign (1, (unsigned char) value.size ());
    part.bytes.insert (part.bytes.end (), value.begin (), value.end ());
    return part;
  }

  /* the bytes of a bit string, the last byte's spare bits zero as the writer leaves them */
  std::vector<unsigned char>
  bit_bytes (int bit_length, unsigned long long drawn)
  {
    std::vector<unsigned char> bytes ((size_t) BITS_TO_BYTES (bit_length), 0);
    unsigned long long state = drawn;

    for (size_t at = 0; at < bytes.size (); at++)
      {
	bytes[at] = (unsigned char) next_of (state);
      }
    if (bit_length % 8 != 0)
      {
	bytes.back () &= (unsigned char) (0xff << (8 - bit_length % 8));
      }
    return bytes;
  }

  DB_DATETIME
  datetime_of (unsigned long long drawn)
  {
    DB_DATETIME datetime;

    datetime.date = (unsigned int) (drawn % 3652059);
    datetime.time = (unsigned int) ((drawn >> 20) % 86400000);
    return datetime;
  }

  /* the value of the column in row i: the shape's draw, or the number of the row's group */
  column_part
  column_value (const TP_DOMAIN &domain, const column_spec &spec, int i, int n, std::mt19937_64 &rng)
  {
    const unsigned long long drawn = (spec.group > 0) ? (unsigned long long) (i / spec.group)
				     : shape_value (spec.shape, i, n, rng);
    DB_VALUE value;

    if (spec.null_every > 0 && rng () % (unsigned long long) spec.null_every == 0)
      {
	column_part null_part;

	null_part.is_null = true;
	return null_part;
      }

    switch (spec.kind)
      {
      case COL_SHORT:
	db_make_short (&value, (short) drawn);
	break;
      case COL_INT:
	db_make_int (&value, (int) drawn);
	break;
      case COL_BIGINT:
	db_make_bigint (&value, (DB_BIGINT) drawn);
	break;
      case COL_FLOAT:
	db_make_float (&value, (float) (long long) drawn / 3.0f);
	break;
      case COL_DOUBLE:
	db_make_double (&value, (double) (long long) drawn / 3.0);
	break;
      case COL_DATE:
      {
	DB_DATE date = (DB_DATE) (drawn % 3652059);

	db_value_put_encoded_date (&value, &date);
      }
      break;
      case COL_TIME:
      {
	DB_TIME time = (DB_TIME) (drawn % 86400);

	db_value_put_encoded_time (&value, &time);
      }
      break;
      case COL_TIMESTAMP:
	/* any 32 bits, so a value past 0x7fffffff shows the word is unsigned */
	db_make_timestamp (&value, (DB_TIMESTAMP) drawn);
	break;
      case COL_TIMESTAMPLTZ:
	db_make_timestampltz (&value, (DB_TIMESTAMP) drawn);
	break;
      case COL_TIMESTAMPTZ:
      {
	DB_TIMESTAMPTZ ts_tz;

	/* a zone of its own, which the comparator never reads */
	ts_tz.timestamp = (DB_TIMESTAMP) (drawn % 0x7fffffff);
	ts_tz.tz_id = (TZ_ID) rng ();
	db_make_timestamptz (&value, &ts_tz);
      }
      break;
      case COL_DATETIME:
      {
	DB_DATETIME datetime = datetime_of (drawn);

	db_make_datetime (&value, &datetime);
      }
      break;
      case COL_DATETIMELTZ:
      {
	DB_DATETIME datetime = datetime_of (drawn);

	db_make_datetimeltz (&value, &datetime);
      }
      break;
      case COL_DATETIMETZ:
      {
	DB_DATETIMETZ dt_tz;

	dt_tz.datetime = datetime_of (drawn);
	dt_tz.tz_id = (TZ_ID) rng ();
	db_make_datetimetz (&value, &dt_tz);
      }
      break;
      case COL_ENUM:
	/* the index alone, which the comparator reads as an unsigned short */
	db_make_enumeration (&value, (unsigned short) drawn, NULL, 0, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);
	break;
      case COL_MONETARY:
	db_make_monetary (&value, DB_CURRENCY_DOLLAR, (double) (long long) drawn / 100.0);
	break;
      case COL_OID:
      {
	OID oid;

	/* no field of a key OID is negative, and each goes past a byte, so a field in the wrong byte order shows */
	oid.pageid = (INT32) (drawn % 1000003);
	oid.slotid = (INT16) ((drawn >> 20) % 30011);
	oid.volid = (INT16) ((drawn >> 40) % 30011);
	db_make_oid (&value, &oid);
      }
      break;
      case COL_CHAR6:
	return string_part (6, ISO_LETTERS, drawn);
      case COL_VARCHAR12:
	return string_part ((int) (drawn % 13), UTF8_LETTERS, drawn);
      case COL_VARCHAR16_EN_CS:
	return string_part ((int) (drawn % 17), ISO_LETTERS, drawn);
      case COL_VARCHAR16_EN_CI:
	return string_part ((int) (drawn % 17), UTF8_LETTERS, drawn);
      case COL_VARCHAR40:
	return string_part ((int) (drawn % 41), ISO_LETTERS, drawn);
      case COL_LONG_VARCHAR200:
	return string_part (200 - (int) (drawn % 16), ISO_LETTERS, drawn);
      case COL_VARCHAR300:
	return string_part ((int) (drawn % 41), ISO_LETTERS, drawn);
      case COL_BIT12:
      {
	column_part part;

	part.bytes = bit_bytes (12, drawn);
	return part;
      }
      case COL_VARBIT20:
      {
	const int bit_length = (int) (drawn % 21);
	column_part part;

	part.bytes = varbit_image (bit_length, bit_bytes (bit_length, drawn));
	return part;
      }
      case COL_VARBIT300:
      {
	const int bit_length = (int) (drawn % 301);
	column_part part;

	part.bytes = varbit_image (bit_length, bit_bytes (bit_length, drawn));
	return part;
      }
      case COL_NUMERIC:
      case COL_FLOAT_NUMERIC:
      {
	const bool is_float = (spec.kind == COL_FLOAT_NUMERIC);
	const std::string digits = std::to_string (drawn % 10000000000ULL);
	const bool negative = (drawn & 1) != 0 && digits != "0";
	column_part part;

	part.bytes = numeric_sample (digits, is_float ? (int) digits.size () : 10, is_float ? (int) (drawn % 5) : 2,
				     is_float, negative, 0).key_bytes;
	return part;
      }
      }
    return written_part (domain, value);
  }

  /* the image heap_midxkey_key_generate () stores: a bitmap of the columns that have a value, an offset table, which
   * holds 255 for an offset it cannot, then each value but a NULL, which takes no bytes */
  sample
  midxkey_image (const std::vector<column_part> &parts, unsigned int oid)
  {
    const int count = (int) parts.size ();
    std::vector<unsigned char> image ((size_t) or_multi_header_size (count), 0);
    sample one;

    for (int at = 0; at < count; at++)
      {
	or_multi_put_element_offset ((char *) image.data (), count, (int) image.size (), at);
	if (parts[ (size_t) at].is_null)
	  {
	    one.has_null = true;
	    continue;
	  }
	or_multi_set_not_null ((char *) image.data (), at);
	image.insert (image.end (), parts[ (size_t) at].bytes.begin (), parts[ (size_t) at].bytes.end ());
      }
    or_multi_put_size_offset ((char *) image.data (), count, (int) image.size ());
    one.key_bytes = image;
    one.oid_rank = oid;
    return one;
  }

  sample
  midxkey_sample (const std::vector<TP_DOMAIN> &columns, const std::vector<column_spec> &specs, int i, int n,
		  std::mt19937_64 &rng, unsigned int oid)
  {
    std::vector<column_part> parts;

    for (size_t at = 0; at < columns.size (); at++)
      {
	parts.push_back (column_value (columns[at], specs[at], i, n, rng));
      }
    return midxkey_image (parts, oid);
  }

  /* the MIDXKEY branch of btsort_compare_records (): a NULL first, then the index comparator of each column, in the
   * direction of the column the keys part in */
  key_compare
  cmp_midxkey (const TP_DOMAIN &domain)
  {
    return [domain] (const unsigned char *a, const unsigned char *b)
    {
      const int header_size = or_multi_header_size (domain.precision);
      char *mem1 = (char *) a + header_size;
      char *mem2 = (char *) b + header_size;
      TP_DOMAIN *dom = domain.setdomain;

      for (int i = 0; i < domain.precision; i++, dom = dom->next)
	{
	  const bool null1 = or_multi_is_null ((char *) a, i), null2 = or_multi_is_null ((char *) b, i);
	  int c;

	  if (null1 && null2)
	    {
	      continue;
	    }
	  if (null1 || null2)
	    {
	      c = null1 ? -1 : 1;
	    }
	  else
	    {
	      DB_VALUE_COMPARE_RESULT r = dom->type->index_cmpdisk (mem1, mem2, dom, 0, 1, NULL);

	      assert (r != DB_UNK);
	      c = (r == DB_LT) ? -1 : (r == DB_GT) ? 1 : 0;
	      mem1 += pr_midxkey_element_disk_size (mem1, dom);
	      mem2 += pr_midxkey_element_disk_size (mem2, dom);
	    }
	  if (c != 0)
	    {
	      return dom->is_desc ? -c : c;
	    }
	}
      return 0;
    };
  }

  /* the run's comparator, which the sort takes the order of the columns past the radix columns from: the key by the
   * oracle in cmp_arg, then the instance OID */
  int
  compare_records (const void *first, const void *second, void *cmp_arg)
  {
    const key_compare *oracle = (const key_compare *) cmp_arg;
    const unsigned char *a = (const unsigned char *) first;
    const unsigned char *b = (const unsigned char *) second;
    int c = (*oracle) (key_of (a), key_of (b));

    return (c != 0) ? c : cmp_oid (a, b);
  }

  /* the MIDXKEY of the columns, which the caller keeps alive with the domain */
  RADIX_MIDXKEY
  resolved_midxkey (const std::vector<column_spec> &specs, std::vector<TP_DOMAIN> &columns, TP_DOMAIN &domain,
		    RADIX_KEY_ENCODING &encoding)
  {
    RADIX_MIDXKEY key;

    columns.clear ();
    for (const column_spec &spec : specs)
      {
	columns.push_back (column_domain (spec));
      }
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (key.column_count == (int) specs.size ());
    return key;
  }

  /* build, sort by the MIDXKEY in work memory sized for more records, check against the oracle */
  void
  check_midxkey_run (const RADIX_MIDXKEY &key, run_work &work, bool unique, const std::vector<sample> &samples,
		     const key_compare &oracle, const std::string &what)
  {
    int n = (int) samples.size ();
    run_image run = build_run (unique, samples);
    std::vector<size_t> expected ((size_t) n);

    std::iota (expected.begin (), expected.end (), (size_t) 0);
    std::sort (expected.begin (), expected.end (), [&] (size_t x, size_t y)
    {
      int c = oracle (samples[x].key_bytes.data (), samples[y].key_bytes.data ());

      return (c != 0) ? c < 0 : samples[x].oid_rank < samples[y].oid_rank;
    });

    INFO (what);
    work.sort (&key, compare_records, (void *) &oracle, run.rec_base (), run.slots.data (), n);

    for (size_t i = 0; i < (size_t) n; i++)
      {
	BTSORT_RUN_SLOT want = run.slot_of (expected[i]);

	if (run.slots[i] != want)
	  {
	    INFO (what << ": wrong record at " << i << " of " << n);
	    REQUIRE (run.slots[i] == want);
	  }
      }
  }
}

/* a MIDXKEY: the run by the first word of its first column, then the records that tie on every word so far by
 * the next, down to the OID. The ties come in groups either side of the cutoff and as the whole run, the NULLs of a
 * column as a group of their own, and the columns past the radix columns from the comparator */
TEST_CASE ("radix sorts MIDXKEYs as btsort_compare_records () does", "[radix_sort]")
{
  struct midxkey_case
  {
    const char *name;
    std::vector<column_spec> columns;
    int radix_columns;		/* 0 for every column */
  };
  const std::vector<midxkey_case> cases =
  {
    { "int, int", { { COL_INT, false, SHAPE_RANDOM, 0, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 } }, 0 },
    {
      "int few, varchar",
      { { COL_INT, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_VARCHAR12, false, SHAPE_RANDOM, 0, 0 } }, 0
    },
    {
      "char few, smallint desc dup",
      { { COL_CHAR6, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_SHORT, true, SHAPE_ASC_DUP, 0, 0 } }, 0
    },
    {
      "bigint desc dup, double few, datetime",
      {
	{ COL_BIGINT, true, SHAPE_ASC_DUP, 0, 0 }, { COL_DOUBLE, false, SHAPE_FEW_DISTINCT, 0, 0 },
	{ COL_DATETIME, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    { "numeric dup, int", { { COL_NUMERIC, false, SHAPE_ASC_DUP, 0, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 } }, 0 },
    {
      "monetary few, float desc dup",
      { { COL_MONETARY, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_FLOAT, true, SHAPE_DESC_DUP, 0, 0 } }, 0
    },
    {
      "date few, timestamptz dup, datetimetz desc",
      {
	{ COL_DATE, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_TIMESTAMPTZ, false, SHAPE_ASC_DUP, 0, 0 },
	{ COL_DATETIMETZ, true, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    {
      "bit few, varbit dup, numeric without a precision",
      {
	{ COL_BIT12, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_VARBIT20, false, SHAPE_ASC_DUP, 0, 0 },
	{ COL_FLOAT_NUMERIC, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    { "oid few, int", { { COL_OID, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 } }, 0 },
    { "int dup, oid desc", { { COL_INT, false, SHAPE_ASC_DUP, 0, 0 }, { COL_OID, true, SHAPE_RANDOM, 0, 0 } }, 0 },
    /* a value of 2 or 4 bytes of one field, the extremes of it unsigned */
    {
      "time few, timestamp desc extremes, timestampltz",
      {
	{ COL_TIME, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_TIMESTAMP, true, SHAPE_EXTREMES, 0, 0 },
	{ COL_TIMESTAMPLTZ, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    {
      "enum extremes, datetimeltz desc",
      { { COL_ENUM, false, SHAPE_EXTREMES, 0, 0 }, { COL_DATETIMELTZ, true, SHAPE_RANDOM, 0, 0 } }, 0
    },
    /* a collation's own order, and a CHAR turned round */
    {
      "char desc few, varchar en_ci",
      { { COL_CHAR6, true, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_VARCHAR16_EN_CI, false, SHAPE_RANDOM, 0, 0 } }, 0
    },
    {
      "varchar en_cs few, varchar en_ci desc nulls, int",
      {
	{ COL_VARCHAR16_EN_CS, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_VARCHAR16_EN_CI, true, SHAPE_RANDOM, 0, 3 },
	{ COL_INT, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    /* a VARBIT whose length takes the long form */
    { "varbit long dup, int", { { COL_VARBIT300, false, SHAPE_ASC_DUP, 0, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 } }, 0 },
    {
      "varbit long desc nulls, short",
      { { COL_VARBIT300, true, SHAPE_RANDOM, 0, 4 }, { COL_SHORT, false, SHAPE_RANDOM, 0, 0 } }, 0
    },
    /* the run handed over whole to the next word, and ties of every size on the way down */
    { "int alike, int dup", { { COL_INT, false, SHAPE_ALL_EQUAL, 0, 0 }, { COL_INT, false, SHAPE_ASC_DUP, 0, 0 } }, 0 },
    {
      "int alike, int alike, int",
      {
	{ COL_INT, false, SHAPE_ALL_EQUAL, 0, 0 }, { COL_INT, false, SHAPE_ALL_EQUAL, 0, 0 },
	{ COL_INT, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    { "groups of 64, bigint", { { COL_INT, false, 0, 64, 0 }, { COL_BIGINT, false, SHAPE_RANDOM, 0, 0 } }, 0 },
    { "groups of 65, varchar wide", { { COL_INT, false, 0, 65, 0 }, { COL_VARCHAR40, false, SHAPE_RANDOM, 0, 0 } }, 0 },
    { "groups of 2, groups of 4", { { COL_INT, false, 0, 2, 0 }, { COL_SHORT, false, 0, 4, 0 } }, 0 },
    /* NULLs and a descending column inside a group the insertion sort orders */
    { "groups of 5 nulls, groups of 3", { { COL_INT, false, 0, 5, 2 }, { COL_SHORT, false, 0, 3, 0 } }, 0 },
    {
      "groups of 6 desc, varchar nulls",
      { { COL_INT, true, 0, 6, 0 }, { COL_VARCHAR12, false, SHAPE_RANDOM, 0, 3 } }, 0
    },
    {
      "groups of 130 desc, groups of 65, short",
      {
	{ COL_INT, true, 0, 130, 0 }, { COL_INT, false, 0, 65, 0 }, { COL_SHORT, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    /* a wide first column whose ties take its words one by one before the next column */
    {
      "varchar wide few, int",
      { { COL_VARCHAR40, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 } }, 0
    },
    {
      "three few",
      {
	{ COL_INT, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_SHORT, true, SHAPE_FEW_DISTINCT, 0, 0 },
	{ COL_DOUBLE, false, SHAPE_FEW_DISTINCT, 0, 0 }
      }, 0
    },
    {
      "sixty-four short few",
      std::vector<column_spec> ((size_t) RADIX_MAX_KEY_WORDS, { COL_SHORT, false, SHAPE_FEW_DISTINCT, 0, 0 }), 0
    },
    /* the NULLs of a column, first or, in a descending column, last, then ordered by the columns after */
    {
      "int few nulls, int",
      { { COL_INT, false, SHAPE_FEW_DISTINCT, 0, 4 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 } }, 0
    },
    {
      "int desc few nulls, varchar nulls",
      { { COL_INT, true, SHAPE_FEW_DISTINCT, 0, 3 }, { COL_VARCHAR12, false, SHAPE_RANDOM, 0, 5 } }, 0
    },
    {
      "int all null, bigint dup",
      { { COL_INT, false, SHAPE_RANDOM, 0, 1 }, { COL_BIGINT, false, SHAPE_ASC_DUP, 0, 0 } }, 0
    },
    {
      "datetime few, double desc nulls",
      { { COL_DATETIME, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_DOUBLE, true, SHAPE_FEW_DISTINCT, 0, 2 } }, 0
    },
    {
      "varchar wide few nulls, numeric nulls, short",
      {
	{ COL_VARCHAR40, false, SHAPE_FEW_DISTINCT, 0, 3 }, { COL_NUMERIC, false, SHAPE_FEW_DISTINCT, 0, 3 },
	{ COL_SHORT, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    { "groups of 65, int nulls", { { COL_INT, false, 0, 65, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 2 } }, 0 },
    /* the offset table holds 255 for a column past its reach, which the walk finds */
    {
      "long varchar few, long varchar few, int",
      {
	{ COL_LONG_VARCHAR200, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_LONG_VARCHAR200, true, SHAPE_FEW_DISTINCT, 0, 0 },
	{ COL_INT, false, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    {
      "long varchar few nulls, long varchar few, int desc",
      {
	{ COL_LONG_VARCHAR200, false, SHAPE_FEW_DISTINCT, 0, 3 }, { COL_LONG_VARCHAR200, false, SHAPE_FEW_DISTINCT, 0, 0 },
	{ COL_INT, true, SHAPE_RANDOM, 0, 0 }
      }, 0
    },
    /* the columns past the radix columns from the comparator */
    {
      "int few, varchar(300), int",
      {
	{ COL_INT, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_VARCHAR300, false, SHAPE_FEW_DISTINCT, 0, 0 },
	{ COL_INT, false, SHAPE_RANDOM, 0, 0 }
      }, 1
    },
    {
      "int few nulls, varchar(300) desc nulls",
      { { COL_INT, false, SHAPE_FEW_DISTINCT, 0, 3 }, { COL_VARCHAR300, true, SHAPE_RANDOM, 0, 3 } }, 1
    },
    {
      "sixty-five short few",
      std::vector<column_spec> ((size_t) RADIX_MAX_KEY_WORDS + 1, { COL_SHORT, false, SHAPE_FEW_DISTINCT, 0, 0 }),
      RADIX_MAX_KEY_WORDS
    },
    /* 26 words each, so a third would pass RADIX_MAX_KEY_WORDS */
    {
      "long varchar few, long varchar few, long varchar few, int",
      {
	{ COL_LONG_VARCHAR200, false, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_LONG_VARCHAR200, false, SHAPE_FEW_DISTINCT, 0, 0 },
	{ COL_LONG_VARCHAR200, true, SHAPE_FEW_DISTINCT, 0, 0 }, { COL_INT, false, SHAPE_RANDOM, 0, 0 }
      }, 2
    },
  };
  const int SIZES[] = { 0, 1, 2, 3, 63, 64, 65, 129, 255, 256, 257, 1000, 4096 };
  std::mt19937_64 rng (20261013);

  REQUIRE (lang_init () == NO_ERROR);

  for (const midxkey_case &c : cases)
    {
      std::vector<TP_DOMAIN> columns;
      TP_DOMAIN domain;
      RADIX_KEY_ENCODING encoding;
      const RADIX_MIDXKEY key = resolved_midxkey (c.columns, columns, domain, encoding);
      const key_compare oracle = cmp_midxkey (domain);
      /* one sizing serves every run of the sort */
      run_work work (encoding, 4096);

      INFO (c.name);
      REQUIRE (key.radix_columns == ((c.radix_columns > 0) ? c.radix_columns : (int) c.columns.size ()));
      for (bool unique : UNIQUE_MODES)
	{
	  for (int n : SIZES)
	    {
	      std::vector<unsigned int> oids = make_oids (n, true, rng);
	      std::vector<sample> samples;

	      for (int i = 0; i < n; i++)
		{
		  samples.push_back (midxkey_sample (columns, c.columns, i, n, rng, oids[ (size_t) i]));
		}
	      check_midxkey_run (key, work, unique, samples, oracle,
				 std::string (c.name) + " n=" + std::to_string (n) + (unique ? " unique" : ""));
	    }
	}
    }
}

/* a MIDXKEY is its leading columns with a radix key, each a key of its own domain */
TEST_CASE ("radix MIDXKEY table", "[radix_sort]")
{
  RADIX_MIDXKEY key;
  RADIX_KEY_ENCODING encoding;
  std::vector<TP_DOMAIN> columns;
  TP_DOMAIN domain;

  REQUIRE (lang_init () == NO_ERROR);

  SECTION ("each column is the key of its domain, and the engine's key one word of the widest")
  {
    RADIX_KEY_ENCODING alone;

    columns = { domain_of (DB_TYPE_INTEGER, false), string_domain (DB_TYPE_VARCHAR, 20, LANG_COLL_UTF8_BINARY, true),
		domain_of (DB_TYPE_DATETIME, false)
	      };
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_supports_key (&domain));
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (key.column_count == 3);
    REQUIRE (key.radix_columns == 3);
    for (int at = 0; at < 3; at++)
      {
	REQUIRE (radix_sort_resolve_key (&alone, &columns[ (size_t) at]));
	REQUIRE (memcmp (&key.columns[at].encoding, &alone, sizeof (alone)) == 0);
	REQUIRE (key.columns[at].domain == &columns[ (size_t) at]);
      }
    REQUIRE (key.columns[1].encoding.is_desc);
    REQUIRE (encoding.word_width == OR_BIGINT_SIZE);
    REQUIRE (encoding.key_length == key.columns[0].encoding.key_length + key.columns[1].encoding.key_length
	     + key.columns[2].encoding.key_length);
    REQUIRE (encoding.min_key_length == or_multi_header_size (3));
    REQUIRE (encoding.format == RADIX_KEY_MIDXKEY);
    /* the engine's key is one word of the widest column, the DATETIME's */
    REQUIRE (radix_sort_stack_bytes (&encoding, 1000000) == radix_sort_stack_bytes (&key.columns[2].encoding, 1000000));

    /* the key table of one column takes no MIDXKEY */
    REQUIRE_FALSE (radix_sort_resolve_key (&alone, &domain));
  }

  SECTION ("the engine's word is as narrow as the widest column")
  {
    columns = { domain_of (DB_TYPE_SHORT, false), domain_of (DB_TYPE_SHORT, true) };
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (encoding.word_width == OR_SHORT_SIZE);
    REQUIRE (encoding.key_length == 2 * OR_SHORT_SIZE);

    columns = { domain_of (DB_TYPE_SHORT, false), domain_of (DB_TYPE_DATE, false) };
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (encoding.word_width == OR_INT_SIZE);
  }

  SECTION ("the radix columns end at a column without a radix key, or past the most key words")
  {
    /* 33 and 31 words: the words at the limit, and one more past it */
    columns = { string_domain (DB_TYPE_VARCHAR, 250, LANG_COLL_ISO_BINARY, false),
		string_domain (DB_TYPE_VARCHAR, 240, LANG_COLL_ISO_BINARY, false), domain_of (DB_TYPE_SHORT, false)
	      };
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (key.columns[0].encoding.key_length + key.columns[1].encoding.key_length
	     == RADIX_MAX_KEY_WORDS * OR_BIGINT_SIZE);
    REQUIRE (key.radix_columns == 2);

    columns = { domain_of (DB_TYPE_INTEGER, false), string_domain (DB_TYPE_VARCHAR, 300, LANG_COLL_ISO_BINARY, false),
		domain_of (DB_TYPE_INTEGER, false)
	      };
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_supports_key (&domain));
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (key.column_count == 3);
    REQUIRE (key.radix_columns == 1);

    columns.assign ((size_t) RADIX_MAX_KEY_WORDS + 3, domain_of (DB_TYPE_INTEGER, false));
    domain = midxkey_domain (columns);
    REQUIRE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (key.column_count == RADIX_MAX_KEY_WORDS + 3);
    REQUIRE (key.radix_columns == RADIX_MAX_KEY_WORDS);
  }

  SECTION ("a first column without a radix key keeps the compare sort, and leaves nothing behind")
  {
    columns = { string_domain (DB_TYPE_VARCHAR, 300, LANG_COLL_ISO_BINARY, false), domain_of (DB_TYPE_INTEGER, false) };
    domain = midxkey_domain (columns);
    REQUIRE_FALSE (radix_sort_supports_key (&domain));
    REQUIRE_FALSE (radix_sort_resolve_midxkey (&key, &encoding, &domain));
    REQUIRE (key.column_count == 0);
    REQUIRE (key.radix_columns == 0);
    REQUIRE (encoding.word_width == 0);
    REQUIRE (encoding.key_length == 0);

    REQUIRE_FALSE (radix_sort_resolve_midxkey (&key, &encoding, tp_domain_resolve_default (DB_TYPE_INTEGER)));
    REQUIRE_FALSE (radix_sort_resolve_midxkey (&key, &encoding, NULL));
  }
}
