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
	header->has_null = 0;
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
}

/* the key table itself: what it accepts, what it refuses, and the widths it hands out */
TEST_CASE ("radix key table", "[radix_sort]")
{
  RADIX_KEY_ENCODING encoding;

  SECTION ("types without a fixed-width key fall back to the compare sort")
  {
    const DB_TYPE unsupported[] =
    {
      DB_TYPE_VARCHAR, DB_TYPE_CHAR, DB_TYPE_VARNCHAR_DEPRECATED, DB_TYPE_NCHAR_DEPRECATED,
      DB_TYPE_MIDXKEY, DB_TYPE_SET, DB_TYPE_BLOB, DB_TYPE_CLOB, DB_TYPE_JSON, DB_TYPE_NULL
    };

    for (DB_TYPE t : unsupported)
      {
	INFO ("type " << (int) t);
	REQUIRE_FALSE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (t)));
      }
  }

  /* a numeric: 3-byte header plus a magnitude sized by the value, so no fixed offset or width, bytes out of order */
  SECTION ("a numeric is declined")
  {
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_NUMERIC)));
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
    REQUIRE_FALSE (radix_sort_resolve_key (&encoding, tp_domain_resolve_default (DB_TYPE_NUMERIC)));
    REQUIRE (encoding.word_width == 0);
    REQUIRE (encoding.key_length == 0);
    REQUIRE (encoding.min_key_length == 0);
    REQUIRE (encoding.format == RADIX_KEY_UNSIGNED);
    REQUIRE_FALSE (encoding.is_desc);
  }
}
