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
 * writeset_key_hash.cpp - hash a constraint key the way its index compares it
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <cmath>

#include "writeset_key_hash.hpp"

#include "error_manager.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "language_support.h"
#include "numeric_opfunc.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

/* FNV-1a 64-bit constants */
#define LOG_WSET_FNV_OFFSET_BASIS ((UINT64) 0xcbf29ce484222325ULL)
#define LOG_WSET_FNV_PRIME        ((UINT64) 0x00000100000001b3ULL)

static UINT64 wset_fnv1a (const VFID *index_vfid, const char *packed, int len);
static int wset_pack_string_key (const LANG_COLLATION *lc, DB_TYPE type, const unsigned char *s, int size,
				 unsigned char pbytes[5]);
static int wset_string_hash (const VFID *index_vfid, DB_VALUE *v, UINT64 *hash_out);
static DB_VALUE *wset_strip_zone (DB_VALUE *v, DB_VALUE *scratch);
static DB_VALUE *wset_unsign_zero (DB_VALUE *v, DB_VALUE *scratch);
static DB_VALUE *wset_strip_currency (DB_VALUE *v, DB_VALUE *scratch);
static DB_VALUE *wset_round_bit_length (DB_VALUE *v, DB_VALUE *scratch);
static DB_VALUE *wset_canonical_numeric (DB_VALUE *v, DB_VALUE *scratch, char *text);
static bool wset_value_is_nan (const DB_VALUE *v);
static int wset_midxkey_hash (const VFID *index_vfid, DB_VALUE *key, UINT64 *hash_out, bool *has_nan);

/*
 * wset_fnv1a - hash a constraint key as index VFID and packed key
 *
 * return: writeset key hash
 *
 * Note: Two transactions whose keys hash alike get a dependency and are applied in commit order on the
 *       slave, so the same key of the same B-tree must hash alike. The index VFID and the key are hashed,
 *       not the row's class OID: a class hierarchy shares one B-tree for an inherited PRIMARY KEY or UNIQUE.
 *       For example, T1 deletes k = 1 from sup and T2 inserts k = 1 into sub (sub UNDER sup). With the class
 *       OID the hashes differ, T2 gets no dependency, and the slave can insert first and fail on the unique
 *       key. An FK REF to sup whose parent row is in sub is split the same way. Different keys hashing
 *       alike only add a dependency, which is safe.
 */
static UINT64
wset_fnv1a (const VFID *index_vfid, const char *packed, int len)
{
  UINT64 hash = LOG_WSET_FNV_OFFSET_BASIS;
  unsigned char vfid_bytes[6];
  int i;

  /* index VFID: 6 bytes = fileid (4) + volid (2) */
  memcpy (&vfid_bytes[0], &index_vfid->fileid, sizeof (index_vfid->fileid));
  memcpy (&vfid_bytes[4], &index_vfid->volid, sizeof (index_vfid->volid));

  for (i = 0; i < 6; i++)
    {
      hash ^= (UINT64) vfid_bytes[i];
      hash *= LOG_WSET_FNV_PRIME;
    }

  for (i = 0; i < len; i++)
    {
      hash ^= (UINT64) (unsigned char) packed[i];
      hash *= LOG_WSET_FNV_PRIME;
    }

  return hash;
}

/*
 * wset_string_hash - hash a character key according to its index collation
 *
 *   index_vfid(in): index VFID used by the writeset key
 *   v(in): non-NULL string value carrying the target index collation
 *   hash_out(out): writeset key hash
 *
 * return: NO_ERROR, or an error code on failure
 */
static int
wset_string_hash (const VFID *index_vfid, DB_VALUE *v, UINT64 *hash_out)
{
  LANG_COLLATION *lc;
  const unsigned char *s;
  unsigned char pbytes[5];
  int error;

  lc = lang_get_collation (db_get_string_collation (v));
  s = (const unsigned char *) db_get_string (v);
  assert (lc != NULL);
  assert (s != NULL);

  error = wset_pack_string_key (lc, DB_VALUE_DOMAIN_TYPE (v), s, db_get_string_size (v), pbytes);
  if (error != NO_ERROR)
    {
      return error;
    }

  *hash_out = wset_fnv1a (index_vfid, (const char *) pbytes, 5);

  return NO_ERROR;
}

/*
 * wset_pack_string_key - pack a character string into the bytes hashed for it
 *
 *   lc(in): collation of the index column the string belongs to
 *   type(in): DB_TYPE_CHAR or DB_TYPE_VARCHAR, the type of the index column
 *   s(in): string bytes
 *   size(in): string size in bytes
 *   pbytes(out): 0xC5 tag followed by the 4-byte collation pseudo key
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: Strings the index compares as equal pack to the same bytes. The collation pseudo key (mht2str) gives
 *       collation-equal strings one value, except under a collation with expansions, whose compare skips zero
 *       weights the pseudo key folds in: there the weights the compare looks at are hashed into the value
 *       (lang_uca_weight_key). The 0xC5 tag keeps it apart from a packed value of another type.
 */
static int
wset_pack_string_key (const LANG_COLLATION *lc, DB_TYPE type, const unsigned char *s, int size,
		      unsigned char pbytes[5])
{
  bool strip_space = (type == DB_TYPE_CHAR || prm_get_bool_value (PRM_ID_IGNORE_TRAILING_SPACE));
  unsigned char *copy = NULL;
  unsigned char *weights = NULL;
  int weights_size = 0;
  unsigned int pseudo;
  int error;
  int i;

  /* a collation with expansions compares by its full UCA weights, which its pseudo key does not follow
   * (lang_uca_weight_key); the weights the compare looks at are hashed instead. The compare sees the bytes as
   * they are: a NUL inside the string is a zero weight it skips, so the NUL-to-space rewrite below is not
   * for this path; the trailing spaces the index ignores are cut here as for the pseudo key. */
  {
    int uca_size = size;

    while (strip_space && uca_size > 0 && s[uca_size - 1] == ' ')
      {
	uca_size--;
      }
    error = lang_uca_weight_key (lc, s, uca_size, strip_space, &weights, &weights_size);
    if (error != NO_ERROR)
      {
	return error;
      }
    if (weights != NULL)
      {
	/* FNV-1a over the weight sequence, in the 4 bytes the pseudo key takes */
	pseudo = 2166136261u;
	for (i = 0; i < weights_size; i++)
	  {
	    pseudo = (pseudo ^ weights[i]) * 16777619u;
	  }
	free_and_init (weights);

	pbytes[0] = 0xC5;
	pbytes[1] = (unsigned char) (pseudo & 0xff);
	pbytes[2] = (unsigned char) ((pseudo >> 8) & 0xff);
	pbytes[3] = (unsigned char) ((pseudo >> 16) & 0xff);
	pbytes[4] = (unsigned char) ((pseudo >> 24) & 0xff);

	return NO_ERROR;
      }
  }

  if (lc->codeset == INTL_CODESET_BINARY)
    {
      /* the binary collation compares raw bytes and ignores only trailing NULs (lang_fastcmp_binary) */
      while (size > 0 && s[size - 1] == '\0')
	{
	  size--;
	}
    }
  /* the byte, UTF-8 and EUC-KR compares see a NUL as a space anywhere (lang_fastcmp_byte, lang_strmatch_utf8,
   * lang_strmatch_ko); under another collation this only merges keys */
  else if (size > 0 && memchr (s, '\0', (size_t) size) != NULL)
    {
      copy = (unsigned char *) malloc ((size_t) size);
      if (copy == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      for (i = 0; i < size; i++)
	{
	  copy[i] = (s[i] == '\0') ? ' ' : s[i];
	}
      s = copy;
    }

  /* cut the trailing spaces the index ignores: CHAR, or VARCHAR with ignore_trailing_space
   * (mr_data_cmpdisk_char, mr_data_cmpdisk_string) */
  while (strip_space && size > 0 && s[size - 1] == ' ')
    {
      size--;
    }

  pseudo = lc->mht2str (lc, s, size);

  if (copy != NULL)
    {
      free_and_init (copy);
    }

  pbytes[0] = 0xC5;
  pbytes[1] = (unsigned char) (pseudo & 0xff);
  pbytes[2] = (unsigned char) ((pseudo >> 8) & 0xff);
  pbytes[3] = (unsigned char) ((pseudo >> 16) & 0xff);
  pbytes[4] = (unsigned char) ((pseudo >> 24) & 0xff);

  return NO_ERROR;
}

/*
 * wset_strip_zone - the value hashed for a key value whose zone is not part of the key
 *
 *   v(in): key value
 *   scratch(out): holds the value returned when v carries a zone
 *
 * return: scratch holding the UTC part of a TIMESTAMPTZ or DATETIMETZ value, or v itself for any other value
 *
 * Note: The index compares TIMESTAMPTZ and DATETIMETZ values by their UTC time only (mr_cmpval_timestamptz,
 *       mr_cmpval_datetimetz), so the same instant written in two zones is one key and must hash alike. The UTC
 *       part is hashed, as a TIMESTAMP or DATETIME value, without the zone id packed after it.
 */
static DB_VALUE *
wset_strip_zone (DB_VALUE *v, DB_VALUE *scratch)
{
  if (DB_IS_NULL (v))
    {
      return v;
    }

  switch (DB_VALUE_DOMAIN_TYPE (v))
    {
    case DB_TYPE_TIMESTAMPTZ:
      db_make_timestamp (scratch, db_get_timestamptz (v)->timestamp);
      return scratch;

    case DB_TYPE_DATETIMETZ:
      db_make_datetime (scratch, &db_get_datetimetz (v)->datetime);
      return scratch;

    default:
      return v;
    }
}

/*
 * wset_unsign_zero - the value hashed for a FLOAT or DOUBLE key value, without the sign of zero
 *
 *   v(in): key value
 *   scratch(out): holds the value returned when v is a FLOAT or DOUBLE zero
 *
 * return: scratch holding +0.0 for a FLOAT or DOUBLE zero, or v itself for any other value
 *
 * Note: The index compares FLOAT and DOUBLE values as numbers (mr_cmpval_float, mr_cmpval_double), so -0.0 and
 *       +0.0 are one key and must hash alike, though -0.0 packs a sign bit. -0.0 is hashed as +0.0.
 */
static DB_VALUE *
wset_unsign_zero (DB_VALUE *v, DB_VALUE *scratch)
{
  if (DB_IS_NULL (v))
    {
      return v;
    }

  switch (DB_VALUE_DOMAIN_TYPE (v))
    {
    case DB_TYPE_FLOAT:
      if (db_get_float (v) == 0.0f)
	{
	  db_make_float (scratch, 0.0f);
	  return scratch;
	}
      return v;

    case DB_TYPE_DOUBLE:
      if (db_get_double (v) == 0.0)
	{
	  db_make_double (scratch, 0.0);
	  return scratch;
	}
      return v;

    default:
      return v;
    }
}

/*
 * wset_strip_currency - the value hashed for a MONETARY key value, without its currency
 *
 *   v(in): key value
 *   scratch(out): holds the value returned when v is a MONETARY value
 *
 * return: scratch holding the amount in a fixed currency for a MONETARY value, or v itself for any other value
 *
 * Note: The index compares MONETARY values by amount only (mr_cmpval_money, mr_index_cmpdisk_money), so $100 and
 *       \100 are one key and must hash alike. The amount is hashed in one fixed currency, and -0.0 as +0.0.
 */
static DB_VALUE *
wset_strip_currency (DB_VALUE *v, DB_VALUE *scratch)
{
  double amount;

  if (DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) != DB_TYPE_MONETARY)
    {
      return v;
    }

  amount = db_get_monetary (v)->amount;
  db_make_monetary (scratch, DB_CURRENCY_DOLLAR, (amount == 0.0) ? 0.0 : amount);

  return scratch;
}

/*
 * wset_round_bit_length - the value hashed for a VARBIT key value, with its bit length in whole bytes
 *
 *   v(in): key value
 *   scratch(out): holds the value returned when v is a VARBIT value
 *
 * return: scratch holding the same bytes with a bit length of their byte count times 8 for a VARBIT value, or v
 *         itself for any other value
 *
 * Note: The index compares VARBIT values by their bytes only (mr_data_cmpdisk_varbit, mr_cmpval_varbit), so B'1'
 *       and B'10' (both the byte 0x80) are one key and must hash alike. The bytes are hashed with their bit length
 *       rounded up to whole bytes.
 */
static DB_VALUE *
wset_round_bit_length (DB_VALUE *v, DB_VALUE *scratch)
{
  const char *bits;
  int bit_length = 0;

  if (DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) != DB_TYPE_VARBIT)
    {
      return v;
    }

  bits = db_get_bit (v, &bit_length);
  db_make_varbit (scratch, DB_MAX_VARBIT_PRECISION, bits, BITS_TO_BYTES (bit_length) * 8);

  return scratch;
}

/*
 * wset_canonical_numeric - the value hashed for a NUMERIC key value, as the number it stands for
 *
 *   v(in): key value
 *   scratch(out): holds the value returned when v is a NUMERIC value
 *   text(out): NUMERIC_MAX_STRING_SIZE bytes that scratch refers to
 *
 * return: scratch holding the decimal text of a NUMERIC value, or v itself for any other value
 *
 * Note: The index compares NUMERIC values as numbers (mr_cmpval_numeric, numeric_db_value_compare), so 1.00
 *       stored as NUMERIC(10,2) and 1.000 stored as NUMERIC(12,3) are one key and must hash alike. A column
 *       declared without a precision keeps each value with the precision and scale it arrived with, and a
 *       packed NUMERIC carries both, so the value is hashed as its decimal text instead: the sign, the integer
 *       digits without leading zeros and the fraction digits without trailing zeros (1.000 -> "1",
 *       1.50 -> "1.5", -0.0 -> "0").
 */
static DB_VALUE *
wset_canonical_numeric (DB_VALUE *v, DB_VALUE *scratch, char *text)
{
  char digits[NUMERIC_MAX_STRING_SIZE];
  const char *p;
  const char *int_end;
  const char *frac_end;
  int scale;
  int len;
  bool negative;
  bool is_float_numeric = false;
  char *out = text;

  if (DB_IS_NULL (v) || DB_VALUE_DOMAIN_TYPE (v) != DB_TYPE_NUMERIC)
    {
      return v;
    }

  /* an optional '-' and then the coefficient digits, zero padded on the left */
  numeric_coerce_num_to_dec_str (v, digits);
  p = digits;
  negative = (*p == '-');
  if (negative)
    {
      p++;
    }
  len = (int) strlen (p);
  /* a value a column without a declared precision keeps carries its own scale in the value itself */
  scale = db_get_numeric_scale (v, &is_float_numeric);
  if (scale < 0)
    {
      scale = 0;
    }
  if (scale > len)
    {
      scale = len;
    }
  int_end = p + len - scale;
  frac_end = p + len;

  while (p < int_end - 1 && *p == '0')
    {
      p++;
    }
  while (frac_end > int_end && frac_end[-1] == '0')
    {
      frac_end--;
    }

  /* zero has no sign and no fraction */
  if (negative && ! (int_end - p == 1 && *p == '0' && frac_end == int_end))
    {
      *out++ = '-';
    }
  if (int_end == p)
    {
      *out++ = '0';
    }
  else
    {
      memcpy (out, p, (size_t) (int_end - p));
      out += int_end - p;
    }
  if (frac_end > int_end)
    {
      *out++ = '.';
      memcpy (out, int_end, (size_t) (frac_end - int_end));
      out += frac_end - int_end;
    }
  *out = '\0';

  db_make_string (scratch, text);

  return scratch;
}

/*
 * wset_value_is_nan - whether a key value is a FLOAT, DOUBLE or MONETARY NaN
 *
 *   v(in): key value
 *
 * Note: The index compares these types with MR_CMP, which finds a NaN neither less nor greater than any value,
 *       so a NaN key is equal to every key of its index (inserting 1.0 next to a NaN fails on a unique index).
 *       No hash can stand for that, so a transaction with a NaN key falls back to commit order
 *       (wset_switch_to_commit_order): it waits for everything before it and everything after it waits for it.
 */
static bool
wset_value_is_nan (const DB_VALUE *v)
{
  if (DB_IS_NULL (v))
    {
      return false;
    }

  switch (DB_VALUE_DOMAIN_TYPE (v))
    {
    case DB_TYPE_FLOAT:
      return std::isnan (db_get_float (v));
    case DB_TYPE_DOUBLE:
      return std::isnan (db_get_double (v));
    case DB_TYPE_MONETARY:
      return std::isnan (db_get_monetary (v)->amount);
    default:
      return false;
    }
}

/*
 * wset_midxkey_hash - hash a composite key element by element
 *
 *   index_vfid(in): index VFID used by the writeset key
 *   key(in): composite key whose domain lists the element domains
 *   hash_out(out): writeset key hash
 *   has_nan(out): true when an element is a NaN (wset_value_is_nan); the hash is then not meaningful
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: The packed image of a composite key keeps each character element as its stored bytes, so two keys
 *       that the index treats as equal under a case- or accent-insensitive collation would hash apart, and
 *       a parent write and the child reference spelled differently would not find each other. Each element
 *       is therefore reduced the way a single-column key is: a character element to the pseudo key of its
 *       column's collation, any other element to its packed bytes, and a NULL element to a fixed tag. The
 *       pieces are concatenated in element order and hashed once.
 */
static int
wset_midxkey_hash (const VFID *index_vfid, DB_VALUE *key, UINT64 *hash_out, bool *has_nan)
{
  DB_MIDXKEY *midxkey;
  TP_DOMAIN *element_domain;
  DB_VALUE element;
  LANG_COLLATION *lc;
  char *buf = NULL;
  int buf_len = 0;
  int buf_cap = 0;
  int packed_len;
  int error = NO_ERROR;
  int pass;
  int i;

  assert (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY);
  assert (has_nan != NULL);

  *has_nan = false;
  midxkey = db_get_midxkey (key);
  if (midxkey == NULL || midxkey->domain == NULL)
    {
      return wset_ensure_key_error (NO_ERROR, "composite key without a domain");
    }

  /* the first pass only measures the reduced form, the second fills the buffer sized by the first */
  for (pass = 0; pass < 2 && error == NO_ERROR; pass++)
    {
      if (pass == 1)
	{
	  buf = (char *) malloc ((size_t) buf_cap);
	  if (buf == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_cap);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	  /* the alignment padding skipped by or_pack_mem_value is hashed too, so it must not hold garbage */
	  memset (buf, 0, (size_t) buf_cap);
	}

      element_domain = midxkey->domain->setdomain;
      for (i = 0; i < midxkey->ncolumns && element_domain != NULL; i++, element_domain = element_domain->next)
	{
	  db_make_null (&element);
	  error = pr_midxkey_get_element_nocopy (midxkey, i, &element, NULL, NULL);
	  if (error != NO_ERROR)
	    {
	      error = wset_ensure_key_error (error, "failed to extract a composite key element");
	      break;
	    }

	  if (DB_IS_NULL (&element))
	    {
	      /* 0xC0 tag: a NULL element, distinct from any packed value and from a string pseudo key */
	      if (pass == 1)
		{
		  buf[buf_len] = (char) 0xC0;
		}
	      buf_len += 1;
	    }
	  else if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&element)))
	    {
	      lc = lang_get_collation (TP_DOMAIN_COLLATION (element_domain));
	      if (lc == NULL)
		{
		  pr_clear_value (&element);
		  error = wset_ensure_key_error (NO_ERROR, "unknown collation of a composite key element");
		  break;
		}

	      if (pass == 1)
		{
		  error = wset_pack_string_key (lc, TP_DOMAIN_TYPE (element_domain),
						(const unsigned char *) db_get_string (&element),
						db_get_string_size (&element), (unsigned char *) buf + buf_len);
		  if (error != NO_ERROR)
		    {
		      pr_clear_value (&element);
		      break;
		    }
		}
	      buf_len += 5;
	    }
	  else
	    {
	      DB_VALUE zone_free;
	      DB_VALUE sign_free;
	      DB_VALUE currency_free;
	      DB_VALUE bytes_free;
	      DB_VALUE numeric_free;
	      char numeric_text[NUMERIC_MAX_STRING_SIZE];
	      DB_VALUE *hashed;

	      if (wset_value_is_nan (&element))
		{
		  *has_nan = true;
		}

	      hashed = wset_strip_zone (&element, &zone_free);
	      hashed = wset_unsign_zero (hashed, &sign_free);
	      hashed = wset_strip_currency (hashed, &currency_free);
	      hashed = wset_round_bit_length (hashed, &bytes_free);
	      hashed = wset_canonical_numeric (hashed, &numeric_free, numeric_text);

	      /* 0xC1 tag followed by the packed element in its aligned size, so the layout does not depend on
	       * how much of that size the packer used. or_pack_mem_value aligns its start and its value to
	       * MAX_ALIGNMENT, and OR_VALUE_ALIGNED_SIZE leaves room for that only from an aligned start, so
	       * the element starts at the next aligned offset, as or_pack_value aligns its buffer first. The
	       * buffer comes from malloc, so an aligned offset is an aligned address, and the skipped bytes
	       * are zero. */
	      if (pass == 1)
		{
		  buf[buf_len] = (char) 0xC1;
		}
	      buf_len = DB_ALIGN (buf_len + 1, MAX_ALIGNMENT);
	      if (pass == 1)
		{
		  int used_len;

		  (void) or_pack_mem_value (buf + buf_len, hashed, &used_len);
		}
	      packed_len = OR_VALUE_ALIGNED_SIZE (hashed);
	      buf_len += packed_len;
	    }

	  pr_clear_value (&element);
	}

      if (pass == 0)
	{
	  buf_cap = buf_len;
	  buf_len = 0;
	}
    }

  if (error == NO_ERROR)
    {
      assert (buf_len == buf_cap);
      *hash_out = wset_fnv1a (index_vfid, buf, buf_len);
    }

  if (buf != NULL)
    {
      free_and_init (buf);
    }

  return error;
}

/*
 * wset_hash_key - hash a constraint key the way its index compares it
 *
 *   index_vfid(in): VFID of the index the key belongs to
 *   key(in): key value; a composite key carries its domain
 *   hash_out(out): writeset key hash, set unless the key is a NaN
 *   is_nan_out(out): true when the key or one of its elements is a NaN, which no hash can stand for
 *
 * return: NO_ERROR, or an error code on failure
 *
 * Note: Keys the index compares as equal get one hash. A character key is hashed through its collation pseudo
 *       key (wset_string_hash), a composite key element by element (wset_midxkey_hash), and any other value over
 *       its packed bytes after wset_strip_zone, wset_unsign_zero, wset_strip_currency, wset_round_bit_length and
 *       wset_canonical_numeric.
 */
int
wset_hash_key (const VFID *index_vfid, DB_VALUE *key, UINT64 *hash_out, bool *is_nan_out)
{
  DB_VALUE zone_free;
  DB_VALUE sign_free;
  DB_VALUE currency_free;
  DB_VALUE bytes_free;
  DB_VALUE numeric_free;
  char numeric_text[NUMERIC_MAX_STRING_SIZE];
  DB_VALUE *hashed;
  char *buf;
  int buf_len;
  int packed_len = 0;

  assert (index_vfid != NULL);
  assert (key != NULL);
  assert (hash_out != NULL);
  assert (is_nan_out != NULL);

  *is_nan_out = false;

  /* A NULL unique key has no character data to collation-hash; it falls through and is
   * hashed below as its packed NULL representation (conservative design policy). */
  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (key)) && !DB_IS_NULL (key))
    {
      return wset_string_hash (index_vfid, key, hash_out);
    }

  /* without its domain a composite key packs as a bare NULL-domain header, so every key of the index would
   * hash alike and miss the hash of the same row collected with the domain set */
  if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY && key->data.midxkey.domain == NULL)
    {
      assert (false);
      return wset_ensure_key_error (NO_ERROR, "composite key without a domain");
    }

  /* a composite key is reduced element by element so that its character elements follow their collation */
  if (DB_VALUE_DOMAIN_TYPE (key) == DB_TYPE_MIDXKEY)
    {
      return wset_midxkey_hash (index_vfid, key, hash_out, is_nan_out);
    }

  if (wset_value_is_nan (key))
    {
      *is_nan_out = true;
      return NO_ERROR;
    }

  hashed = wset_strip_zone (key, &zone_free);
  hashed = wset_unsign_zero (hashed, &sign_free);
  hashed = wset_strip_currency (hashed, &currency_free);
  hashed = wset_round_bit_length (hashed, &bytes_free);
  hashed = wset_canonical_numeric (hashed, &numeric_free, numeric_text);

  buf_len = OR_VALUE_ALIGNED_SIZE (hashed);
  buf = (char *) malloc ((size_t) buf_len);
  if (buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* the alignment padding skipped by or_pack_mem_value is hashed too, so it must not hold garbage */
  memset (buf, 0, (size_t) buf_len);

  (void) or_pack_mem_value (buf, hashed, &packed_len);

  *hash_out = wset_fnv1a (index_vfid, buf, packed_len);

  free_and_init (buf);

  return NO_ERROR;
}

/*
 * wset_ensure_key_error - preserve a lower-layer error or set a writeset key-build error
 *
 * return: error code
 */
int
wset_ensure_key_error (int error, const char *detail)
{
  if (error != NO_ERROR && error != ER_FAILED)
    {
      return error;
    }
  if (error == ER_FAILED && er_errid () != NO_ERROR)
    {
      return er_errid ();
    }
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_WRITESET_KEY_BUILD_FAILED, 1, detail);

  return ER_HA_WRITESET_KEY_BUILD_FAILED;
}

#endif /* SERVER_MODE || SA_MODE */
