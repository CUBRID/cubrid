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
 * memory_hash.c - memory hash table
 */

#ident "$Id$"

/*
 *  The structure of the hash table is approximately as follows;
 *
 *   Hash Table header
 *   -----------------
 *   |hash_fun       |
 *   |cmp_fun        |
 *   |table_name     |
 *   |Ptr to entries----->     The entries
 *   |size           |     -------------------
 *   |rehash_at      |     | key, data, next |--> ... -> key, data, next
 *   |num_entries    |     |       .         |
 *   |num_collisions |     |   .             |
 *   -----------------     |       .         |
 *                         -------------------
 */

#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "memory_hash.h"
#include "chartype.h"
#include "misc_string.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "message_catalog.h"
#include "environment_variable.h"
#include "set_object.h"
#include "language_support.h"
#include "intl_support.h"
#include "object_primitive.h"
#include "dbtype.h"

#if __WORDSIZE == 32
#define GET_PTR_FOR_HASH(key) ((unsigned int)(key))
#else
#define GET_PTR_FOR_HASH(key) (((UINT64)(key)) & 0xFFFFFFFFUL)
#endif

/* constants for rehash */
static const float MHT_REHASH_TRESHOLD = 0.7f;
static const float MHT_REHASH_FACTOR = 1.3f;

/* options for mht_put() */
enum mht_put_opt
{
  MHT_OPT_DEFAULT = 0,
  MHT_OPT_KEEP_KEY = 1,
  MHT_OPT_INSERT_ONLY = 2,
  MHT_OPT_INSERT_IF_NOT_EXISTS = 4
};
typedef enum mht_put_opt MHT_PUT_OPT;

/*
 * A table of prime numbers.
 *
 * Some prime numbers which were picked randomly.
 * This table contains more smaller prime numbers.
 * Some of the prime numbers included are:
 * between  1000 and  2000, prime numbers that are farther than  50 units.
 * between  2000 and  7000, prime numbers that are farther than 100 units.
 * between  7000 and 12000, prime numbers that are farther than 200 units.
 * between 12000 and 17000, prime numbers that are farther than 400 units.
 * between 17000 and 22000, prime numbers that are farther than 800 units.
 * above 22000, prime numbers that are farther than 1000 units.
 *
 * Note: if x is a prime number, the n is prime if X**(n-1) mod n == 1
 */

static unsigned int mht_1str_pseudo_key (const void *key, int key_size);
static unsigned int mht_3str_pseudo_key (const void *key, int key_size, const unsigned int max_value);
static unsigned int mht_4str_pseudo_key (const void *key, int key_size);
static unsigned int mht_5str_pseudo_key (const void *key, int key_size);

static int mht_rehash (MHT_TABLE * ht);

static const void *mht_put_internal (MHT_TABLE * ht, const void *key, void *data, MHT_PUT_OPT opt);
static const void *mht_put2_internal (MHT_TABLE * ht, const void *key, void *data, MHT_PUT_OPT opt);
static const void *mht_put_hls_internal (MHT_HLS_TABLE * ht, const void *key, void *data, MHT_PUT_OPT opt);

static unsigned int mht_get_shiftmult32 (unsigned int key, const unsigned int ht_size);
#if defined (ENABLE_UNUSED_FUNCTION)
static unsigned int mht_get32_next_power_of_2 (unsigned int const ht_size);
static unsigned int mht_get_linear_hash32 (const unsigned int key, const unsigned int ht_size);
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * Several hasing functions for different data types
 */

/*
 * mht_1str_pseudo_key() - hash string key into pseudo integer key
 *   return: pseudo integer key
 *   key(in): string key to hash
 *   key_size(in): size of key or -1 when unknown
 *
 * Note: It generates a pseudo integer key based on Gosling's emacs hash
 *       function.
 */
static unsigned int
mht_1str_pseudo_key (const void *key, int key_size)
{
  unsigned const char *byte_p = (unsigned char *) key;
  unsigned int pseudo_key;

  assert (key != NULL);

  for (pseudo_key = 0;; byte_p++)
    {
      if (key_size == -1)
	{
	  if (!(*byte_p))
	    {
	      break;
	    }
	}
      else
	{
	  if (key_size <= 0)
	    {
	      break;
	    }
	}

      pseudo_key = (pseudo_key << 5) - pseudo_key + *byte_p;

      if (key_size > 0)
	{
	  key_size--;
	}
    }

  return pseudo_key;
}

/*
 * mht_2str_pseudo_key() - hash string key into pseudo integer key
 *   return: pseudo integer key
 *   key(in): string key to hash
 *   key_size(in): size of key or -1 when unknown
 *
 * Note: It generates a pseudo integer key based on hash function from
 *       Aho, Sethi, and Ullman's dragon book; pp. 436.
 *	 This function can handles strings when they are binary different.
 *	 For collation dependent function use 'mht2str'.
 */
unsigned int
mht_2str_pseudo_key (const void *key, int key_size)
{
  unsigned const char *byte_p = (unsigned char *) key;
  unsigned int pseudo_key;
  unsigned int i;

  assert (key != NULL);

  for (pseudo_key = 0;; byte_p++)
    {
      if (key_size == -1)
	{
	  if (!(*byte_p))
	    {
	      break;
	    }
	}
      else
	{
	  if (key_size <= 0)
	    {
	      break;
	    }
	}

      pseudo_key = (pseudo_key << 4) + *byte_p;
      i = pseudo_key & 0xf0000000;
      if (i != 0)
	{
	  pseudo_key ^= i >> 24;
	  pseudo_key ^= i;
	}

      if (key_size > 0)
	{
	  key_size--;
	}
    }

  return pseudo_key;
}

/*
 * mht_3str_pseudo_key() - hash string key into pseudo integer key
 *   return: pseudo integer key
 *   key(in): string key to hash
 *   key_size(in): size of key or -1 when unknown
 *   max_value(in) : generally a prime number which represents the size of hash
 *                   table
 *
 * Note: It generates a pseudo integer key based on hash function from
 *       Sedgewick's Algorithm book. The function needs a prime number for
 *       the range. This prime number is usually the hash table size.
 */
static unsigned int
mht_3str_pseudo_key (const void *key, int key_size, const unsigned int max_value)
{
  unsigned const char *byte_p = (unsigned char *) key;
  unsigned int pseudo_key = 0;

  assert (key != NULL);

  for (pseudo_key = 0;; byte_p++)
    {
      if (key_size == -1)
	{
	  if (!(*byte_p))
	    {
	      break;
	    }
	}
      else
	{
	  if (key_size <= 0)
	    {
	      break;
	    }
	}

      pseudo_key = (pseudo_key * 32 + *byte_p++) % max_value;

      if (key_size > 0)
	{
	  key_size--;
	}
    }

  return pseudo_key;
}

/*
 * mht_4str_pseudo_key() - hash string key into pseudo integer key
 *   return: pseudo integer key
 *   key(in): string key to hash
 *   key_size(in): size of key or -1 when unknown
 *
 * Note: It generates four values between 0 and 255, concatenate them,
 *       and returns the result.
 *       Based on Fast Hashing of Variable-Length Text Strings
 *       by Peter K. Pearson Communications of the ACM, June 1990.
 */
static unsigned int
mht_4str_pseudo_key (const void *key, int key_size)
{
  /* a permutation of values 0 to 255 */
  unsigned char tbl[] = {
    166, 231, 148, 061, 050, 131, 000, 057, 126, 223,
    044, 245, 138, 251, 24, 113, 86, 215, 196, 173,
    226, 115, 48, 169, 46, 207, 92, 101, 58, 235,
    72, 225, 6, 199, 244, 29, 146, 99, 96, 25,
    222, 191, 140, 213, 234, 219, 120, 81, 182, 183,
    36, 141, 66, 83, 144, 137, 142, 175, 188, 69,
    154, 203, 168, 193, 102, 167, 84, 253, 242, 67,
    192, 249, 62, 159, 236, 181, 74, 187, 216, 49,
    22, 151, 132, 109, 162, 51, 240, 105, 238, 143,
    28, 37, 250, 171, 8, 161, 198, 135, 180, 221,
    82, 35, 32, 217, 158, 127, 76, 149, 170, 155,
    56, 17, 118, 119, 228, 77, 2, 19, 80, 73, 78,
    111, 124, 5, 90, 139, 104, 129, 38, 103, 20,
    189, 178, 3, 128, 185, 254, 95, 172, 117, 10,
    123, 152, 241, 214, 87, 68, 45, 98, 243, 176,
    41, 174, 79, 220, 229, 186, 107, 200, 97, 134,
    71, 116, 157, 18, 227, 224, 153, 94, 63, 12,
    85, 106, 91, 248, 209, 54, 55, 164, 13, 194,
    211, 16, 9, 14, 47, 60, 197, 26, 75, 40,
    65, 230, 39, 212, 125, 114, 195, 64, 121, 190,
    31, 108, 53, 202, 59, 88, 177, 150, 23, 4,
    237, 34, 179, 112, 233, 110, 15, 156, 165, 122,
    43, 136, 33, 70, 7, 52, 93, 210, 163, 160,
    89, 30, 255, 204, 21, 42, 27, 184, 145, 246,
    247, 100, 205, 130, 147, 208, 201, 206, 239, 252,
    133, 218, 11, 232, 1, 0
  };
  unsigned int byte1, byte2, byte3, byte4;
  unsigned const char *byte_p = (unsigned char *) key;

  assert (key != NULL);

  byte1 = tbl[*byte_p];
  byte2 = tbl[(unsigned int) (*byte_p + 1) % 256];
  byte3 = tbl[(unsigned int) (*byte_p + 2) % 256];
  byte4 = tbl[(unsigned int) (*byte_p + 3) % 256];

  if (key_size == -1)
    {
      if (*byte_p)
	{
	  byte_p++;
	}
    }
  else if (key_size > 0)
    {
      byte_p++;
      key_size--;
    }

  for (;; byte_p++)
    {
      if (key_size == -1)
	{
	  if (!(*byte_p))
	    {
	      break;
	    }
	}
      else
	{
	  if (key_size <= 0)
	    {
	      break;
	    }
	}

      /*
       * Each of the following hash values,
       * generates a value between 0 and 255
       */
      byte1 = tbl[byte1 ^ *byte_p];
      byte2 = tbl[byte2 ^ *byte_p];
      byte3 = tbl[byte3 ^ *byte_p];
      byte4 = tbl[byte4 ^ *byte_p];

      if (key_size > 0)
	{
	  key_size--;
	}
    }

  /* Concatenate all the values */
  return (byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24));
}

/*
 * mht_5str_pseudo_key() - hash string key into pseudo integer key
 * return: pseudo integer key
 * key(in): string key to hash
 * key_size(in): size of key or -1 when unknown
 *
 * Note: Based on hash method reported by Diniel J. Bernstein.
 */
static unsigned int
mht_5str_pseudo_key (const void *key, int key_size)
{
  unsigned int hash = 5381;
  int i = 0;
  char *k;

  assert (key != NULL);
  k = (char *) key;

  if (key_size == -1)
    {
      int c;
      while ((c = *(k + i++)) != 0)
	{
	  hash = ((hash << 5) + hash) + c;	/* hash * 33 + c */
	}
    }
  else
    {
      for (; i < key_size; i++)
	{
	  hash = ((hash << 5) + hash) + *(k + i);
	}
    }

  hash += ~(hash << 9);
  hash ^= ((hash >> 14) | (i << 18));	/* >>> */
  hash += (hash << 4);
  hash ^= ((hash >> 10) | (i << 22));	/* >>> */

  return hash;
}

/*
 * mht_1strlowerhash - hash a string key (in lowercase)
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Taken from Gosling's emacs
 *	 This handles only ASCII characters; for charset-independent version
 *	 use 'intl_identifier_mht_1strlowerhash'
 */
unsigned int
mht_1strlowerhash (const void *key, const unsigned int ht_size)
{
  unsigned int hash;
  unsigned const char *byte_p = (unsigned char *) key;
  unsigned int ch;

  assert (key != NULL);

  for (hash = 0; *byte_p; byte_p++)
    {
      /* TODO: Original comment originally this way but due to compiler problems on the PC, it doesn't always work
       * consistently: hash = (hash << 5) - hash + tolower(*key++); */
      ch = char_tolower (*byte_p);
      hash = (hash << 5) - hash + ch;
    }
  return hash % ht_size;
}

/*
 * mht_1strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Taken from Gosling's emacs
 */
unsigned int
mht_1strhash (const void *key, const unsigned int ht_size)
{
  assert (key != NULL);

  return mht_1str_pseudo_key (key, -1) % ht_size;
}

/*
 * mht_2strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Form to hash strings.
 *       Taken from Aho, Sethi, and Ullman's dragon book; pp. 436.
 *	 This function uses 'mht_2str_pseudo_key'.
 *	 For collation dependent function use 'mht2str'.
 */
unsigned int
mht_2strhash (const void *key, const unsigned int ht_size)
{
  assert (key != NULL);

  return mht_2str_pseudo_key (key, -1) % ht_size;
}

/*
 * mht_3strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Form to hash strings.
 *       Taken from Sedgewick's Algorithm book
 */
unsigned int
mht_3strhash (const void *key, const unsigned int ht_size)
{
  assert (key != NULL);

  return mht_3str_pseudo_key (key, -1, ht_size);
}

/*
 * mht_4strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Form to hash strings.
 *       It generates four values between 0 and 255, concatenate them
 *       and applies the mod.
 *
 *       Based on Fast Hashing of Variable-Length Text Strings
 *       by Peter K. Pearson
 *       Communications of the ACM, June 1990.
 */
unsigned int
mht_4strhash (const void *key, const unsigned int ht_size)
{
  assert (key != NULL);

  return mht_4str_pseudo_key (key, -1) % ht_size;
}

/*
 * mht_5strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Based on hash method reported by Diniel J. Bernstein.
 */
unsigned int
mht_5strhash (const void *key, const unsigned int ht_size)
{
  return mht_5str_pseudo_key (key, -1) % ht_size;
}

/*
 * mht_numash - hash an integer key
 *   return: hash value
 *   key(in): void pointer to integer key to hash
 *   ht_size(in): size of hash table
 */
unsigned int
mht_numhash (const void *key, const unsigned int ht_size)
{
  assert (key != NULL);

  return (*(const unsigned int *) key) % ht_size;
}

/*
 * mht_ptrhash - hash a pointer key (hash memory pointers)
 *   return: hash value
 *   key(in): pointer value key to hash
 *   ht_size(in): size of hash table
 */
unsigned int
mht_ptrhash (const void *key, const unsigned int ht_size)
{
  assert (key != NULL);

  return GET_PTR_FOR_HASH (key) % ht_size;
}

/*
 * mht_valhash - hash a DB_VALUE key
 *   return: hash value
 *   key(in): pointer to DB_VALUE key to hash
 *   ht_size(in): size of hash table
 */
unsigned int
mht_valhash (const void *key, const unsigned int ht_size)
{
  unsigned int hash = 0;
  const DB_VALUE *val = (const DB_VALUE *) key;
  int t_n;
  DB_VALUE t_val;

  if (key != NULL)
    {
      switch (db_value_type (val))
	{
	case DB_TYPE_NULL:
	  hash = 0;
	  break;
	case DB_TYPE_INTEGER:
	  hash = (unsigned int) db_get_int (val);
	  break;
	case DB_TYPE_SHORT:
	  hash = (unsigned int) db_get_short (val);
	  break;
	case DB_TYPE_BIGINT:
	  {
	    DB_BIGINT bigint;
	    unsigned int x, y;

	    bigint = db_get_bigint (val);
	    x = bigint >> 32;
	    y = (unsigned int) bigint;
	    hash = x ^ y;
	    break;
	  }
	case DB_TYPE_FLOAT:
	  hash = (unsigned int) db_get_float (val);
	  break;
	case DB_TYPE_DOUBLE:
	  hash = (unsigned int) db_get_double (val);
	  break;
	case DB_TYPE_NUMERIC:
	  hash = mht_1str_pseudo_key (db_get_numeric (val), -1);
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  hash = mht_1str_pseudo_key (db_get_string (val), db_get_string_size (val));
	  break;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  hash = mht_1str_pseudo_key (db_get_bit (val, &t_n), -1);
	  break;
	case DB_TYPE_TIME:
	  {
	    unsigned int *time = db_get_time (val);
	    hash = (unsigned int) (*time);
	    break;
	  }
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  {
	    DB_TIMESTAMP *time_st = db_get_timestamp (val);
	    hash = (unsigned int) (*time_st);
	    break;
	  }
	case DB_TYPE_TIMESTAMPTZ:
	  {
	    DB_TIMESTAMPTZ *ts_tz = db_get_timestamptz (val);
	    hash = (unsigned int) (ts_tz->timestamp);
	  }
	  break;
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMELTZ:
	  {
	    DB_DATETIME *datetime;
	    datetime = db_get_datetime (val);
	    hash = (unsigned int) (datetime->date ^ datetime->time);
	  }
	  break;
	case DB_TYPE_DATETIMETZ:
	  {
	    DB_DATETIMETZ *dt_tz;
	    dt_tz = db_get_datetimetz (val);
	    hash = (unsigned int) (dt_tz->datetime.date ^ dt_tz->datetime.time);
	  }
	  break;
	case DB_TYPE_DATE:
	  {
	    DB_DATE *date = db_get_date (val);
	    hash = (unsigned int) (*date);
	    break;
	  }
	case DB_TYPE_MONETARY:
	  {
	    DB_MONETARY *mon = db_get_monetary (val);
	    hash = (unsigned int) mon->amount;
	    break;
	  }
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  db_make_null (&t_val);
	  {
	    DB_SET *set;
	    set = db_get_set (val);
	    if (set_get_element (set, 0, &t_val) == NO_ERROR)
	      {
		hash = mht_valhash (&t_val, ht_size);
		(void) pr_clear_value (&t_val);
		t_n = set_size (set);
		if ((t_n > 0) && set_get_element (set, t_n - 1, &t_val) == NO_ERROR)
		  {
		    hash += mht_valhash (&t_val, ht_size);
		    (void) pr_clear_value (&t_val);
		  }
	      }
	  }
	  break;
	case DB_TYPE_OBJECT:
	  hash = GET_PTR_FOR_HASH (db_get_object (val));
	  break;
	case DB_TYPE_OID:
	  hash = (unsigned int) OID_PSEUDO_KEY (db_get_oid (val));
	  break;
	case DB_TYPE_MIDXKEY:
	  db_make_null (&t_val);
	  {
	    DB_MIDXKEY *midxkey;
	    midxkey = db_get_midxkey (val);
	    if (pr_midxkey_get_element_nocopy (midxkey, 0, &t_val, NULL, NULL) == NO_ERROR)
	      {
		hash = mht_valhash (&t_val, ht_size);
		t_n = midxkey->size;
		if (t_n > 0 && pr_midxkey_get_element_nocopy (midxkey, t_n - 1, &t_val, NULL, NULL) == NO_ERROR)
		  {
		    hash += mht_valhash (&t_val, ht_size);
		  }
	      }
	  }
	  break;
	case DB_TYPE_POINTER:
	  hash = GET_PTR_FOR_HASH (db_get_pointer (val));
	  break;
	case DB_TYPE_BLOB:
	case DB_TYPE_CLOB:
	case DB_TYPE_SUB:
	case DB_TYPE_ERROR:
	case DB_TYPE_VOBJ:
	case DB_TYPE_DB_VALUE:
	case DB_TYPE_RESULTSET:
	case DB_TYPE_TABLE:
	default:
	  hash = GET_PTR_FOR_HASH (val);
	  break;
	}
    }

  return hash % ht_size;
}


/*
 * Compare functions for datatypes
 */

/*
 * mht_compare_ints_are_equal - compare two integer keys
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to integer key1
 *   key2(in): pointer to integer key2
 */
int
mht_compare_ints_are_equal (const void *key1, const void *key2)
{
  return ((*(const int *) key1 == *(const int *) key2));
}

/*
 * mht_logpageid_compare_equal - compare two LOG_PAGEID keys
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to LOG_PAGEID key1
 *   key2(in): pointer to LOG_PAGEID key2
 */
int
mht_compare_logpageids_are_equal (const void *key1, const void *key2)
{
  return ((*(const LOG_PAGEID *) key1 == *(const LOG_PAGEID *) key2));
}

/*
 * mht_compare_identifiers_equal - compare two identifiers keys (ignoring case)
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to string key1
 *   key2(in): pointer to string key2
 */
int
mht_compare_identifiers_equal (const void *key1, const void *key2)
{
  return ((intl_identifier_casecmp ((const char *) key1, (const char *) key2)) == 0);
}

/*
 * mht_compare_strings_are_equal - compare two string keys (case sensitive)
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to string key1
 *   key2(in): pointer to string key2
 */
int
mht_compare_strings_are_equal (const void *key1, const void *key2)
{
  return ((strcmp ((const char *) key1, (const char *) key2)) == 0);
}

/*
 * mht_compare_ptrs_are_equal - compare two pointer keys
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer key1
 *   key2(in): pointer key2
 */
int
mht_compare_ptrs_are_equal (const void *key1, const void *key2)
{
  return (key1 == key2);
}

/*
 * mht_compare_dbvalues_are_equal - compare two DB_VALUEs
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to DB_VALUE key1
 *   key2(in): pointer to DB_VALUE key2
 */
int
mht_compare_dbvalues_are_equal (const void *key1, const void *key2)
{
  return ((key1 == key2) || (tp_value_compare ((DB_VALUE *) key1, (DB_VALUE *) key2, 0, 1) == DB_EQ));
}

/*
 * mht_calculate_htsize - find a good hash table size
 *   return: adjusted hash table size
 *   ht_size(in): given hash table size
 *
 * Note: Find a good size for a hash table. A prime number is the best
 *       size for a hash table, unfortunately prime numbers are
 *       difficult to found. If the given htsize, falls among the
 *       prime numbers known by this module, a close prime number to
 *       the given htsize value is returned, otherwise, a power of two
 *       is returned.
 */
#define NPRIMES 170
static const unsigned int mht_Primes[NPRIMES] = {
  11, 23, 37, 53, 67, 79, 97, 109, 127, 149,
  167, 191, 211, 227, 251, 269, 293, 311, 331, 349,
  367, 389, 409, 431, 449, 467, 487, 509, 541, 563,
  587, 607, 631, 653, 673, 521, 541, 557, 569, 587,
  599, 613, 641, 673, 701, 727, 751, 787, 821, 853,
  881, 907, 941, 977, 1039, 1087, 1129, 1171, 1213, 1259,
  1301, 1361, 1409, 1471, 1523, 1579, 1637, 1693, 1747, 1777,
  1823, 1867, 1913, 1973, 2017, 2129, 2237, 2339, 2441, 2543,
  2647, 2749, 2851, 2953, 3061, 3163, 3271, 3373, 3491, 3593,
  3697, 3803, 3907, 4013, 4177, 4337, 4493, 4649, 4801, 4957,
  5059, 5167, 5273, 5381, 5483, 5591, 5693, 5801, 5903, 6007,
  6113, 6217, 6317, 6421, 6521, 6637, 6737, 6841, 6847, 7057,
  7283, 7487, 7687, 7901, 8101, 8311, 8513, 8713, 8923, 9127,
  9337, 9539, 9739, 9941, 10141, 10343, 10559, 10771, 10973, 11177,
  11383, 11587, 11789, 12007, 12409, 12809, 13217, 13619, 14029, 14431,
  14831, 15233, 15641, 16057, 16477, 16879, 17291, 18097, 18899, 19699,
  20507, 21313, 22123, 23131, 24133, 25147, 26153, 27179, 28181, 29123
};

unsigned int
mht_calculate_htsize (unsigned int ht_size)
{
  int left, right, middle;	/* indices for binary search */

  if (ht_size > mht_Primes[NPRIMES - 1])
    {
      /* get a power of two */
      if (!((ht_size & (ht_size - 1)) == 0))
	{
	  /* Turn off some bits but the left most one */
	  while (!(ht_size & (ht_size - 1)))
	    {
	      ht_size &= (ht_size - 1);
	    }
	  ht_size <<= 1;
	}
    }
  else
    {
      /* we can assign a primary number; binary search */
      for (middle = 0, left = 0, right = NPRIMES - 1; left <= right;)
	{
	  middle = CEIL_PTVDIV ((left + right), 2);
	  if (ht_size == mht_Primes[middle])
	    {
	      break;
	    }
	  else if (ht_size > mht_Primes[middle])
	    {
	      left = middle + 1;
	    }
	  else
	    {
	      right = middle - 1;
	    }
	}
      /* If we didn't find the size, get the larger size and not the small one */
      if (ht_size > mht_Primes[middle] && middle < (NPRIMES - 1))
	{
	  middle++;
	}
      ht_size = mht_Primes[middle];
    }

  return ht_size;
}

/*
 * mht_create - create a hash table
 *   return: hash table
 *   name(in): name of hash table
 *   est_size(in): estimated number of entries
 *   hash_func(in): hash function
 *   cmp_func(in): key compare function
 *
 * Note: Create a new hash table. The estimated number of entries for
 *       the hash table may be adjusted (to a prime number) in order to
 *       obtain certain favorable circumstances when the table is
 *       created, when the table is almost full and there are at least
 *       5% of collisions. 'hash_func' must return an integer between 0 and
 *       table_size - 1. 'cmp_func' must return TRUE if key1 = key2,
 *       otherwise, FALSE.
 */
MHT_TABLE *
mht_create (const char *name, int est_size, unsigned int (*hash_func) (const void *key, unsigned int ht_size),
	    int (*cmp_func) (const void *key1, const void *key2))
{
  MHT_TABLE *ht;
  HENTRY_PTR *hvector;		/* Entries of hash table */
  unsigned int ht_estsize;
  size_t size;

  assert (hash_func != NULL && cmp_func != NULL);

  /* Get a good number of entries for hash table */
  if (est_size <= 0)
    {
      est_size = 2;
    }

  ht_estsize = mht_calculate_htsize ((unsigned int) est_size);

  /* Allocate the header information for hash table */
  ht = (MHT_TABLE *) malloc (DB_SIZEOF (MHT_TABLE));
  if (ht == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (MHT_TABLE));

      return NULL;
    }

  /* Initialize the chunky memory manager */
  ht->heap_id = db_create_fixed_heap (DB_SIZEOF (HENTRY), MAX (2, ht_estsize / 2 + 1));
  if (ht->heap_id == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (HENTRY));

      free_and_init (ht);
      return NULL;
    }

  /* Allocate the hash table entry pointers */
  size = ht_estsize * DB_SIZEOF (*hvector);
  hvector = (HENTRY_PTR *) malloc (size);
  if (hvector == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);

      db_destroy_fixed_heap (ht->heap_id);
      free_and_init (ht);
      return NULL;
    }

  ht->hash_func = hash_func;
  ht->cmp_func = cmp_func;
  ht->name = name;
  ht->table = hvector;
  ht->act_head = NULL;
  ht->act_tail = NULL;
  ht->lru_head = NULL;
  ht->lru_tail = NULL;
  ht->prealloc_entries = NULL;
  ht->size = ht_estsize;
  ht->rehash_at = (unsigned int) (ht_estsize * MHT_REHASH_TRESHOLD);
  ht->nentries = 0;
  ht->nprealloc_entries = 0;
  ht->ncollisions = 0;
  ht->build_lru_list = false;

  /* Initialize each of the hash entries */
  for (; ht_estsize > 0; ht_estsize--)
    {
      *hvector++ = NULL;
    }

  return ht;
}

/*
 * mht_create_hls - create a hash table
 *   return: hash table
 *   name(in): name of hash table
 *   est_size(in): estimated number of entries
 *   hash_func(in): hash function
 *   cmp_func(in): key compare function
 *
 * Note: Create a new hash table for HASH LIST SCAN.
 *       The estimated number of entries for the hash table is fixed in HASH LIST SCAN.
 *       rehashing hash table is not needed becaues of that.
 *       key comparison is performed in executor.
 */
MHT_HLS_TABLE *
mht_create_hls (const char *name, int est_size, unsigned int (*hash_func) (const void *key, unsigned int ht_size),
		int (*cmp_func) (const void *key1, const void *key2))
{
  MHT_HLS_TABLE *ht;
  HENTRY_HLS_PTR *hvector;	/* Entries of hash table */
  unsigned int ht_estsize;
  size_t size;

  /* Get a good number of entries for hash table */
  if (est_size <= 0)
    {
      est_size = 2;
    }

  ht_estsize = mht_calculate_htsize ((unsigned int) est_size);

  /* Allocate the header information for hash table */
  ht = (MHT_HLS_TABLE *) malloc (DB_SIZEOF (MHT_HLS_TABLE));
  if (ht == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (MHT_HLS_TABLE));

      return NULL;
    }

  /* Initialize the chunky memory manager */
  ht->heap_id = db_create_fixed_heap (DB_SIZEOF (HENTRY_HLS), MAX (2, ht_estsize / 2 + 1));
  if (ht->heap_id == 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, DB_SIZEOF (HENTRY_HLS));

      free_and_init (ht);
      return NULL;
    }

  /* Allocate the hash table entry pointers */
  size = ht_estsize * DB_SIZEOF (*hvector);
  hvector = (HENTRY_HLS_PTR *) malloc (size);
  if (hvector == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);

      db_destroy_fixed_heap (ht->heap_id);
      free_and_init (ht);
      return NULL;
    }

  ht->hash_func = hash_func;
  ht->cmp_func = cmp_func;
  ht->name = name;
  ht->table = hvector;
  ht->prealloc_entries = NULL;
  ht->size = ht_estsize;
  ht->nentries = 0;
  ht->nprealloc_entries = 0;
  ht->ncollisions = 0;
  ht->build_lru_list = false;

  /* Initialize each of the hash entries */
  for (; ht_estsize > 0; ht_estsize--)
    {
      *hvector++ = NULL;
    }

  return ht;
}

/*
 * mht_rehash - rehash all entires of a hash table
 *   return: error code
 *   ht(in/out): hash table to rehash
 *
 * Note: Expand a hash table. All entries are rehashed onto a larger
 *       hash table of entries.
 */
static int
mht_rehash (MHT_TABLE * ht)
{
  HENTRY_PTR *new_hvector;	/* New entries of hash table */
  HENTRY_PTR *hvector;		/* Entries of hash table */
  HENTRY_PTR hentry;		/* A hash table entry. linked list */
  HENTRY_PTR next_hentry = NULL;	/* Next element in linked list */
  float rehash_factor;
  unsigned int hash;
  unsigned int est_size;
  size_t size;
  unsigned int i;

  /* Find an estimated size for hash table entries */

  rehash_factor = (float) (1.0 + ((float) ht->ncollisions / (float) ht->nentries));
  if (MHT_REHASH_FACTOR > rehash_factor)
    {
      est_size = (unsigned int) (ht->size * MHT_REHASH_FACTOR);
    }
  else
    {
      est_size = (unsigned int) (ht->size * rehash_factor);
    }

  est_size = mht_calculate_htsize (est_size);

  /* Allocate a new vector to keep the estimated hash entries */
  size = est_size * DB_SIZEOF (*hvector);
  new_hvector = (HENTRY_PTR *) malloc (size);
  if (new_hvector == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Initialize all entries */
  memset (new_hvector, 0x00, size);

  /* Now rehash the current entries onto the vector of hash entries table */
  for (ht->ncollisions = 0, hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
    {
      /* Go over each linked list */
      for (hentry = *hvector; hentry != NULL; hentry = next_hentry)
	{
	  next_hentry = hentry->next;

	  hash = (*ht->hash_func) (hentry->key, est_size);
	  if (hash >= est_size)
	    {
	      hash %= est_size;
	    }

	  /* Link the new entry with any previous elements */
	  hentry->next = new_hvector[hash];
	  if (hentry->next != NULL)
	    {
	      ht->ncollisions++;
	    }
	  new_hvector[hash] = hentry;
	}
    }

  /* Now move to new vector of entries */
  free_and_init (ht->table);

  ht->table = new_hvector;
  ht->size = est_size;
  ht->rehash_at = (int) (est_size * MHT_REHASH_TRESHOLD);

  return NO_ERROR;
}

/*
 * mht_destroy - destroy a hash table
 *   return: void
 *   ht(in/out): hash table
 *
 * Note: ht is set as a side effect
 */
void
mht_destroy (MHT_TABLE * ht)
{
  assert (ht != NULL);

  free_and_init (ht->table);

  /* release hash table entry storage */
  db_destroy_fixed_heap (ht->heap_id);

  free_and_init (ht);
}

/*
 * mht_destroy_hls - destroy a hash table
 *   return: void
 *   ht(in/out): hash table
 *
 * Note: ht is set as a side effect
 */
void
mht_destroy_hls (MHT_HLS_TABLE * ht)
{
  assert (ht != NULL);

  free_and_init (ht->table);

  /* release hash table entry storage */
  db_destroy_fixed_heap (ht->heap_id);

  free_and_init (ht);
}

/*
 * mht_clear - remove and free all entries of hash table
 *   return: error code
 *   ht(in/out): hash table
 *   rem_func(in): removal function
 *   func_args(in): removal function arguments
 */
int
mht_clear (MHT_TABLE * ht, int (*rem_func) (const void *key, void *data, void *args), void *func_args)
{
  HENTRY_PTR *hvector;		/* Entries of hash table */
  HENTRY_PTR hentry;		/* A hash table entry. linked list */
  HENTRY_PTR next_hentry = NULL;	/* Next element in linked list */
  unsigned int i, error_code;

  assert (ht != NULL);

  /*
   * Go over the hash table, removing all entries and setting the vector
   * entries to NULL.
   */
  for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
    {
      /* Go over the linked list for this hash table entry */
      for (hentry = *hvector; hentry != NULL; hentry = next_hentry)
	{
	  /* free */
	  if (rem_func)
	    {
	      error_code = (*rem_func) (hentry->key, hentry->data, func_args);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}

	      hentry->key = NULL;
	      hentry->data = NULL;
	    }

	  next_hentry = hentry->next;
	  /* Save the entries for future insertions */
	  ht->nprealloc_entries++;
	  hentry->next = ht->prealloc_entries;
	  ht->prealloc_entries = hentry;
	}
      *hvector = NULL;
    }

  ht->act_head = NULL;
  ht->act_tail = NULL;
  ht->lru_head = NULL;
  ht->lru_tail = NULL;
  ht->ncollisions = 0;
  ht->nentries = 0;

  return NO_ERROR;
}

/*
 * mht_hls_clear - remove and free all entries of hash table
 *   return: error code
 *   ht(in/out): hash table
 *   rem_func(in): removal function
 *   func_args(in): removal function arguments
 */
int
mht_clear_hls (MHT_HLS_TABLE * ht, int (*rem_func) (const void *key, void *data, void *args), void *func_args)
{
  HENTRY_HLS_PTR *hvector;	/* Entries of hash table */
  HENTRY_HLS_PTR hentry;	/* A hash table entry. linked list */
  HENTRY_HLS_PTR next_hentry = NULL;	/* Next element in linked list */
  unsigned int i, error_code;

  assert (ht != NULL);

  /*
   * Go over the hash table, removing all entries and setting the vector
   * entries to NULL.
   */
  for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
    {
      /* Go over the linked list for this hash table entry */
      for (hentry = *hvector; hentry != NULL; hentry = next_hentry)
	{
	  /* free */
	  if (rem_func)
	    {
	      error_code = (*rem_func) (NULL, hentry->data, func_args);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}

	      hentry->data = NULL;
	    }

	  next_hentry = hentry->next;
	  /* Save the entries for future insertions */
	  ht->nprealloc_entries++;
	  hentry->next = ht->prealloc_entries;
	  ht->prealloc_entries = hentry;
	}
      *hvector = NULL;
    }

  ht->ncollisions = 0;
  ht->nentries = 0;

  return NO_ERROR;
}

/*
 * mht_dump - display all entries of hash table
 *   return: TRUE/FALSE
 *   out_fp(in): FILE stream where to dump; if NULL, stdout
 *   ht(in): hash table to print
 *   print_id_opt(in): option for printing hash index vector id
 *   print_func(in): supplied printing function
 *   func_args(in): arguments to be passed to print_func
 *
 * Note: Dump the header of hash table, and for each entry in hash table,
 *       call function "print_func" on three arguments: the key of the entry,
 *       the data associated with the key, and args, in order to print the entry
 *       print_id_opt - Print hash index ? Will run faster if we do not need to
 *       print this information
 */
int
mht_dump (THREAD_ENTRY * thread_p, FILE * out_fp, const MHT_TABLE * ht, const int print_id_opt,
	  int (*print_func) (THREAD_ENTRY * thread_p, FILE * fp, const void *key, void *data, void *args),
	  void *func_args)
{
  HENTRY_PTR *hvector;		/* Entries of hash table */
  HENTRY_PTR hentry;		/* A hash table entry. linked list */
  unsigned int i;
  int cont = TRUE;

  assert (ht != NULL);

  if (out_fp == NULL)
    {
      out_fp = stdout;
    }

  fprintf (out_fp,
	   "HTABLE NAME = %s, SIZE = %d, REHASH_AT = %d,\n" "NENTRIES = %d, NPREALLOC = %d, NCOLLISIONS = %d\n\n",
	   ht->name, ht->size, ht->rehash_at, ht->nentries, ht->nprealloc_entries, ht->ncollisions);

  if (print_id_opt == -1)
    {
      /* noting to do */
    }
  else if (print_id_opt)
    {
      /* Need to print the index vector id. Therefore, scan the whole table */
      for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
	{
	  if (*hvector != NULL)
	    {
	      fprintf (out_fp, "HASH AT %d\n", i);
	      /* Go over the linked list */
	      for (hentry = *hvector; cont == TRUE && hentry != NULL; hentry = hentry->next)
		{
		  cont = (*print_func) (thread_p, out_fp, hentry->key, hentry->data, func_args);
		}
	    }
	}
    }
  else
    {
      /* Quick scan by following only the active entries */
      for (hentry = ht->act_head; cont == TRUE && hentry != NULL; hentry = hentry->act_next)
	{
	  cont = (*print_func) (thread_p, out_fp, hentry->key, hentry->data, func_args);
	}
    }

  fprintf (out_fp, "\n");

  return (cont);
}

/*
 * mht_dump_hls - display all entries of hash table for HASH LIST SCAN
 *   return: TRUE/FALSE
 *   out_fp(in): FILE stream where to dump; if NULL, stdout
 *   ht(in): hash table to print
 *   print_id_opt(in): option for printing hash index vector id
 *   print_func(in): supplied printing function
 *   func_args(in): arguments to be passed to print_func
 *
 * Note: Dump the header of hash table, and for each entry in hash table,
 *       call function "print_func" on three arguments: the key of the entry,
 *       the data associated with the key, and args, in order to print the entry
 *       print_id_opt - Print hash index ? Will run faster if we do not need to
 *       print this information
 */
int
mht_dump_hls (THREAD_ENTRY * thread_p, FILE * out_fp, const MHT_HLS_TABLE * ht, const int print_id_opt,
	      int (*print_func) (THREAD_ENTRY * thread_p, FILE * fp, const void *data, void *args), void *func_args)
{
  HENTRY_HLS_PTR *hvector;	/* Entries of hash table */
  HENTRY_HLS_PTR hentry;	/* A hash table entry. linked list */
  unsigned int i;
  int cont = TRUE;

  assert (ht != NULL);

  if (out_fp == NULL)
    {
      out_fp = stdout;
    }

  fprintf (out_fp,
	   "HTABLE NAME = %s, SIZE = %d,\n" "NENTRIES = %d, NPREALLOC = %d, NCOLLISIONS = %d\n\n",
	   ht->name, ht->size, ht->nentries, ht->nprealloc_entries, ht->ncollisions);

  if (print_id_opt)
    {
      /* Need to print the index vector id. Therefore, scan the whole table */
      for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
	{
	  if (*hvector != NULL)
	    {
	      fprintf (out_fp, "HASH AT %d\n", i);
	      /* Go over the linked list */
	      for (hentry = *hvector; cont == TRUE && hentry != NULL; hentry = hentry->next)
		{
		  cont = (*print_func) (thread_p, out_fp, hentry->data, func_args);
		}
	    }
	}
    }
  fprintf (out_fp, "\n");

  return (cont);
}

/*
 * mht_get - find data associated with key
 *   return: the data associated with the key, or NULL if not found
 *   ht(in): hash table
 *   key(in): hashing key
 *
 * Note: Find the entry in hash table whose key is the value of the given key
 */
void *
mht_get (MHT_TABLE * ht, const void *key)
{
  unsigned int hash;
  HENTRY_PTR hentry;

  assert (ht != NULL);
  assert (key != NULL);

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* now search the linked list */
  for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
    {
      if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	{
	  mht_adjust_lru_list (ht, hentry);

	  /* return value */
	  return hentry->data;
	}
    }
  return NULL;
}

/*
 * mht_adjust_lru_list -
 *   ht(in): hash table
 *   hentry(in): hash entry
 */
int
mht_adjust_lru_list (MHT_TABLE * ht, HENTRY_PTR hentry)
{
  assert (ht && hentry);

  if (ht && hentry && ht->build_lru_list && ht->lru_tail != hentry)
    {
      if (ht->lru_head == hentry)
	{
	  ht->lru_head = hentry->lru_next;
	}

      /* unlink */
      hentry->lru_next->lru_prev = hentry->lru_prev;
      if (hentry->lru_prev)
	{
	  hentry->lru_prev->lru_next = hentry->lru_next;
	}

      /* add at end */
      ht->lru_tail->lru_next = hentry;
      hentry->lru_prev = ht->lru_tail;
      hentry->lru_next = NULL;
      ht->lru_tail = hentry;
    }

  return NO_ERROR;
}

/*
 * mht_get2 - Find the next data associated with the key; Search the entry next
 *            to the last result
 *   return: the data associated with the key, or NULL if not found
 *   ht(in):
 *   key(in):
 *   last(in/out):
 *
 * NOTE: This call does not affect the LRU list.
 */
void *
mht_get2 (const MHT_TABLE * ht, const void *key, void **last)
{
  unsigned int hash;
  HENTRY_PTR hentry;

  assert (ht != NULL && key != NULL);

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* now search the linked list */
  for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
    {
      if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	{
	  if (last == NULL)
	    {
	      return hentry->data;
	    }
	  else if (*((HENTRY_PTR *) last) == NULL)
	    {
	      *((HENTRY_PTR *) last) = hentry;
	      return hentry->data;
	    }
	  else if (*((HENTRY_PTR *) last) == hentry)
	    {
	      /* found the last result; go forward one more step to get next the above 'if' will be true when the next
	       * one is found */
	      *((HENTRY_PTR *) last) = NULL;
	    }
	}
    }

  return NULL;
}

/*
 * mht_get_hls - Find the data associated with the key;
 *   return: the data associated with the key, or NULL if not found
 *   ht(in):
 *   key(in):
 *   last(in/out):
 *
 * NOTE: This call does not affect the LRU list.
 */
void *
mht_get_hls (const MHT_HLS_TABLE * ht, const void *key, void **last)
{
  unsigned int hash, hash_idx;
  HENTRY_HLS_PTR hentry;

  assert (ht != NULL && key != NULL);

  /*
   * Hash the key and make sure that the return value is between 0 and size of hash table
   */
  hash = *((unsigned int *) key);
  if (hash >= ht->size)
    {
      hash_idx = hash % ht->size;
    }
  else
    {
      hash_idx = hash;
    }

  /* In HASH LIST SCAN, only hash key comparison is performed. */
  for (hentry = ht->table[hash_idx]; hentry != NULL; hentry = hentry->next)
    {
      if (hentry->key == hash)
	{
	  *((HENTRY_HLS_PTR *) last) = hentry;
	  return hentry->data;
	}
    }
  return NULL;
}

/*
 * mht_get_next_hls - Search the entry next to the last result
 *   return: the data associated with the key, or NULL if not found
 *   ht(in):
 *   key(in):
 *   last(in/out):
 *
 * NOTE: This call does not affect the LRU list.
 */
void *
mht_get_next_hls (const MHT_HLS_TABLE * ht, const void *key, void **last)
{
  unsigned int hash;
  HENTRY_HLS_PTR hentry;

  assert (ht != NULL && key != NULL && last != NULL);

  if ((*(HENTRY_HLS_PTR *) last)->next == NULL)
    {
      return NULL;
    }
  /* Hash the key and make sure that the return value is between 0 and size of hash table */
  hash = *((unsigned int *) key);

  for (hentry = (*(HENTRY_HLS_PTR *) last)->next; hentry != NULL; hentry = hentry->next)
    {
      if (hentry->key == hash)
	{
	  *((HENTRY_HLS_PTR *) last) = hentry;
	  return hentry->data;
	}
    }
  return NULL;
}

/*
 * mht_put_internal - internal function for mht_put(), mht_put_new(), and
 *                    mht_put_data();
 *                    insert an entry associating key with data
 *   return:
 *       For option MHT_OPT_DEFAULT, MHT_OPT_KEEP_KEY, MHT_OPT_INSERT_ONLY,
 *           returns key if insertion was OK, otherwise, it returns NULL.
 *       For option MHT_OPT_INSERT_IF_NOT_EXISTS,
 *           returns existing data if duplicated key was found, or return
 *           inserted data if insertion was OK, otherwise, it returns NULL.
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *   opt(in): options;
 *            MHT_OPT_DEFAULT - change data and the key as given of the hash
 *                              entry; replace the old entry with the new one
 *                              if there is an entry with the same key
 *            MHT_OPT_KEEP_KEY - change data but the key of the hash entry
 *            MHT_OPT_INSERT_ONLY - do not replace the existing hash entry
 *                                  even if there is an etnry with the same key
 *            MHT_OPT_INSERT_IF_NOT_EXISTS - only insert if the key not exists,
 *                                           do nothing if the same key exists.
 */
static const void *
mht_put_internal (MHT_TABLE * ht, const void *key, void *data, MHT_PUT_OPT opt)
{
  unsigned int hash;
  HENTRY_PTR hentry;

  assert (ht != NULL && key != NULL);

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  if (!(opt & MHT_OPT_INSERT_ONLY))
    {
      /* Now search the linked list.. Is there any entry with the given key ? */
      for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
	{
	  if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	    {
	      if (opt & MHT_OPT_INSERT_IF_NOT_EXISTS)
		{
		  /* Return data for this option */
		  return hentry->data;
		}

	      /* Replace the old data with the new one */
	      if (!(opt & MHT_OPT_KEEP_KEY))
		{
		  hentry->key = key;
		}
	      hentry->data = data;
	      return key;
	    }
	}
    }

  /* This is a new entry */
  if (ht->nprealloc_entries > 0)
    {
      ht->nprealloc_entries--;
      hentry = ht->prealloc_entries;
      ht->prealloc_entries = ht->prealloc_entries->next;
    }
  else
    {
      hentry = (HENTRY_PTR) db_fixed_alloc (ht->heap_id, DB_SIZEOF (HENTRY));
      if (hentry == NULL)
	{
	  return NULL;
	}
    }

  if (ht->build_lru_list)
    {
      /* link new entry to LRU list */
      hentry->lru_next = NULL;
      hentry->lru_prev = ht->lru_tail;
      if (ht->lru_tail)
	{
	  ht->lru_tail->lru_next = hentry;
	}
      ht->lru_tail = hentry;
      if (ht->lru_head == NULL)
	{
	  ht->lru_head = hentry;
	}
    }

  /*
   * Link the new entry to the double link list of active entries and the
   * hash itself. The previous entry should point to new one.
   */

  hentry->key = key;
  hentry->data = data;
  hentry->act_next = NULL;
  hentry->act_prev = ht->act_tail;
  /* Double link tail entry should point to newly created entry */
  if (ht->act_tail != NULL)
    {
      ht->act_tail->act_next = hentry;
    }

  ht->act_tail = hentry;
  if (ht->act_head == NULL)
    {
      ht->act_head = hentry;
    }

  hentry->next = ht->table[hash];
  if (hentry->next != NULL)
    {
      ht->ncollisions++;
    }

  ht->table[hash] = hentry;
  ht->nentries++;

  /*
   * Rehash if almost all entries of hash table are used and there are at least
   * 5% of collisions
   */
  if (ht->nentries > ht->rehash_at && ht->ncollisions > (ht->nentries * 0.05))
    {
      if (mht_rehash (ht) < 0)
	{
	  return NULL;
	}
    }

  return (opt & MHT_OPT_INSERT_IF_NOT_EXISTS) ? data : key;
}

const void *
mht_put_new (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_internal (ht, key, data, MHT_OPT_INSERT_ONLY);
}

const void *
mht_put_hls (MHT_HLS_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_hls_internal (ht, key, data, MHT_OPT_INSERT_ONLY);
}

/*
 * mht_put_if_not_exists - insert only if the same key not exists.
 *   return: Return existing data if duplicated key found,
 *           or return new insertion data if insertion successful,
 *           otherwise return NULL.
 *   ht(in/out): hash table
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *
 *   Insert an entry into a hash table only same key not exists.
 *   Note that this function different with other put functions, do not return key.
 */
const void *
mht_put_if_not_exists (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_internal (ht, key, data, MHT_OPT_INSERT_IF_NOT_EXISTS);
}

const void *
mht_put_data (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_internal (ht, key, data, MHT_OPT_KEEP_KEY);
}

/*
 * mht_put - insert an entry associating key with data
 *   return: Returns key if insertion was OK, otherwise, it returns NULL
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *
 * Note: Insert an entry into a hash table, associating the given key
 *       with the given data. If the entry is duplicated, that is, if
 *       the key already exist the new entry replaces the old one.
 *
 *       The key and data are NOT COPIED, only its pointers are copied. The
 *       user must be aware that changes to the key and value are reflected
 *       into the hash table
 */
const void *
mht_put (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_internal (ht, key, data, MHT_OPT_DEFAULT);
}

/*
 * mht_put2_internal - internal function for mht_put2(), mht_put_new2(), and
 *                     mht_put_data2();
 *                     Insert an entry associating key with data;
 *                     Allow mutiple entries with the same key value
 *   return: Returns key if insertion was OK, otherwise, it returns NULL
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *   opt(in): options;
 *            MHT_OPT_DEFAULT - change data and the key as given of the hash
 *                              entry; replace the old entry with the new one
 *                              if there is an entry with the same key
 *            MHT_OPT_KEEP_KEY - change data but the key of the hash entry
 *            MHT_OPT_INSERT_ONLY - do not replace the existing hash entry
 *                                  even if there is an etnry with the same key
 */
static const void *
mht_put2_internal (MHT_TABLE * ht, const void *key, void *data, MHT_PUT_OPT opt)
{
  unsigned int hash;
  HENTRY_PTR hentry;

  assert (ht != NULL && key != NULL);

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  if (!(opt & MHT_OPT_INSERT_ONLY))
    {
      /* now search the linked list */
      for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
	{
	  if ((hentry->key == key || (*ht->cmp_func) (hentry->key, key)) && hentry->data == data)
	    {
	      /* We found the existing entry. Replace the old data with the new one. */
	      if (!(opt & MHT_OPT_KEEP_KEY))
		{
		  hentry->key = key;
		}
	      hentry->data = data;
	      return key;
	    }
	}
    }

  /* get new entry */
  if (ht->nprealloc_entries > 0)
    {
      ht->nprealloc_entries--;
      hentry = ht->prealloc_entries;
      ht->prealloc_entries = ht->prealloc_entries->next;
    }
  else
    {
      hentry = (HENTRY_PTR) db_fixed_alloc (ht->heap_id, DB_SIZEOF (HENTRY));
      if (hentry == NULL)
	{
	  return NULL;
	}
    }

  if (ht->build_lru_list)
    {
      /* link new entry to LRU list */
      hentry->lru_next = NULL;
      hentry->lru_prev = ht->lru_tail;
      if (ht->lru_tail)
	{
	  ht->lru_tail->lru_next = hentry;
	}
      ht->lru_tail = hentry;
      if (ht->lru_head == NULL)
	{
	  ht->lru_head = hentry;
	}
    }

  /* link the new entry to the double link list of active entries */
  hentry->key = key;
  hentry->data = data;
  hentry->act_next = NULL;
  hentry->act_prev = ht->act_tail;
  if (ht->act_tail != NULL)
    {
      ht->act_tail->act_next = hentry;
    }
  ht->act_tail = hentry;
  if (ht->act_head == NULL)
    {
      ht->act_head = hentry;
    }
  /* and link to the hash itself */
  if ((hentry->next = ht->table[hash]) != NULL)
    {
      ht->ncollisions++;
    }
  ht->table[hash] = hentry;
  ht->nentries++;

  /* rehash if almost all entries of hash table are used and there are at least 5% of collisions */
  if (ht->nentries > ht->rehash_at && ht->ncollisions > (ht->nentries * 0.05))
    {
      mht_rehash (ht);
    }

  return key;
}

const void *
mht_put2_new (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put2_internal (ht, key, data, MHT_OPT_INSERT_ONLY);
}

#if defined (ENABLE_UNUSED_FUNCTION)
const void *
mht_put2_data (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put2_internal (ht, key, data, MHT_OPT_KEEP_KEY);
}

/*
 * mht_put2 - Insert an entry associating key with data;
 *            Allow mutiple entries with the same key value
 *   return: Returns key if insertion was OK, otherwise, it returns NULL
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *
 * Note: Insert an entry into a hash table, associating the given key
 *       with the given data. If the entry is duplicated, that is, if
 *       the key already exist the new entry replaces the old one.
 *
 *       The key and data are NOT COPIED, only its pointers are copied. The
 *       user must be aware that changes to the key and value are reflected
 *       into the hash table
 */
const void *
mht_put2 (MHT_TABLE * ht, const void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put2_internal (ht, key, data, MHT_OPT_DEFAULT);
}
#endif

/*
 * mht_rem - remove a hash entry
 *   return: error code
 *   ht(in): hash table (set as a side effect)
 *   key(in): hashing key
 *   rem_func(in): supplied function to delete the data and key
 *   func_args(in): arguments to be passed to rem_func
 *
 * Note: For each entry in hash table, call function 'rem_func' on three
 *       arguments: the key of the entry, the data associated with the key,
 *       and the given args, in order to delete the data and key
 */
int
mht_rem (MHT_TABLE * ht, const void *key, int (*rem_func) (const void *key, void *data, void *args), void *func_args)
{
  unsigned int hash;
  HENTRY_PTR prev_hentry;
  HENTRY_PTR hentry;
  int error_code = NO_ERROR;

  assert (ht != NULL && key != NULL);

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* Now search the linked list.. Is there any entry with the given key ? */
  for (hentry = ht->table[hash], prev_hentry = NULL; hentry != NULL; prev_hentry = hentry, hentry = hentry->next)
    {
      if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	{
	  /*
	   * We found the entry
	   * Call "rem_func" (if any) to delete the data and key
	   * Delete the node from the double link list of active entries.
	   * Then delete the node from the hash table.
	   */

	  if (rem_func)
	    {
	      error_code = (*rem_func) (hentry->key, hentry->data, func_args);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }

	  if (ht->build_lru_list)
	    {
	      /* remove from LRU list */
	      if (ht->lru_head == ht->lru_tail)
		{
		  ht->lru_head = ht->lru_tail = NULL;
		}
	      else if (ht->lru_head == hentry)
		{
		  ht->lru_head = hentry->lru_next;
		  hentry->lru_next->lru_prev = NULL;
		}
	      else if (ht->lru_tail == hentry)
		{
		  ht->lru_tail = hentry->lru_prev;
		  hentry->lru_prev->lru_next = NULL;
		}
	      else
		{
		  hentry->lru_prev->lru_next = hentry->lru_next;
		  hentry->lru_next->lru_prev = hentry->lru_prev;
		}
	    }

	  /* Remove from double link list of active entries */
	  if (ht->act_head == ht->act_tail)
	    {
	      /* Single active element */
	      ht->act_head = ht->act_tail = NULL;
	    }
	  else if (ht->act_head == hentry)
	    {
	      /* Deleting from the head */
	      ht->act_head = hentry->act_next;
	      hentry->act_next->act_prev = NULL;
	    }
	  else if (ht->act_tail == hentry)
	    {
	      /* Deleting from the tail */
	      ht->act_tail = hentry->act_prev;
	      hentry->act_prev->act_next = NULL;
	    }
	  else
	    {
	      /* Deleting from the middle */
	      hentry->act_prev->act_next = hentry->act_next;
	      hentry->act_next->act_prev = hentry->act_prev;
	    }

	  /* Remove from the hash */
	  if (prev_hentry != NULL)
	    {
	      prev_hentry->next = hentry->next;
	      ht->ncollisions--;
	    }
	  else if ((ht->table[hash] = hentry->next) != NULL)
	    {
	      ht->ncollisions--;
	    }
	  ht->nentries--;
	  /* Save the entry for future insertions */
	  ht->nprealloc_entries++;
	  hentry->next = ht->prealloc_entries;
	  ht->prealloc_entries = hentry;

	  return NO_ERROR;
	}
    }

  return ER_FAILED;
}

/*
 * mht_rem2 - Remove an hash entry having the specified data
 *   return: error code
 *   ht(in): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in):
 *   rem_func(in): supplied function to delete the data and key
 *   func_args(in): arguments to be passed to rem_func
 *
 * Note: For each entry in hash table, call function 'rem_func' on three
 *       arguments: the key of the entry, the data associated with the key,
 *       and the given args, in order to delete the data and key
 */
int
mht_rem2 (MHT_TABLE * ht, const void *key, const void *data, int (*rem_func) (const void *key, void *data, void *args),
	  void *func_args)
{
  unsigned int hash;
  HENTRY_PTR prev_hentry;
  HENTRY_PTR hentry;
  int error_code = NO_ERROR;

  assert (ht != NULL && key != NULL);

  /* hash the key and make sure that the return value is between 0 and size of hash table */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* now search the linked list */
  for (hentry = ht->table[hash], prev_hentry = NULL; hentry != NULL; prev_hentry = hentry, hentry = hentry->next)
    {
      if ((hentry->key == key || (*ht->cmp_func) (hentry->key, key)) && hentry->data == data)
	{
	  /*
	   * We found the entry.
	   * Call "fun" (if any) to delete the data and key.
	   * Delete the node from the double link list of active entries.
	   * Then delete the node from the hash table.
	   */
	  if (rem_func)
	    {
	      error_code = (*rem_func) (hentry->key, hentry->data, func_args);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }

	  if (ht->build_lru_list)
	    {
	      /* remove from LRU list */
	      if (ht->lru_head == ht->lru_tail)
		{
		  ht->lru_head = ht->lru_tail = NULL;
		}
	      else if (ht->lru_head == hentry)
		{
		  ht->lru_head = hentry->lru_next;
		  hentry->lru_next->lru_prev = NULL;
		}
	      else if (ht->lru_tail == hentry)
		{
		  ht->lru_tail = hentry->lru_prev;
		  hentry->lru_prev->lru_next = NULL;
		}
	      else
		{
		  hentry->lru_prev->lru_next = hentry->lru_next;
		  hentry->lru_next->lru_prev = hentry->lru_prev;
		}
	    }

	  /* remove from double link list of active entries */
	  if (ht->act_head == ht->act_tail)
	    {
	      ht->act_head = ht->act_tail = NULL;
	    }
	  else if (ht->act_head == hentry)
	    {
	      ht->act_head = hentry->act_next;
	      hentry->act_next->act_prev = NULL;
	    }
	  else if (ht->act_tail == hentry)
	    {
	      ht->act_tail = hentry->act_prev;
	      hentry->act_prev->act_next = NULL;
	    }
	  else
	    {
	      hentry->act_prev->act_next = hentry->act_next;
	      hentry->act_next->act_prev = hentry->act_prev;
	    }
	  /* remove from the hash */
	  if (prev_hentry != NULL)
	    {
	      prev_hentry->next = hentry->next;
	      ht->ncollisions--;
	    }
	  else if ((ht->table[hash] = hentry->next) != NULL)
	    {
	      ht->ncollisions--;
	    }
	  ht->nentries--;
	  /* save the entry for future insertions */
	  ht->nprealloc_entries++;
	  hentry->next = ht->prealloc_entries;
	  ht->prealloc_entries = hentry;

	  return NO_ERROR;
	}
    }

  return ER_FAILED;
}

/*
 * mht_map - map over hash entries
 *   return: NO_ERROR or error code - the result of user supplied "map_func"
 *   ht(in): hash table
 *   map_func(in): user supplied mapping function
 *   func_args(in): arguments to be passed to rem_func
 *
 * Note: For each entry in hash table, call function "map_func" on three
 *       arguments: the key of the entry, the data associated with the key,
 *       and the given args. The function that is called should not remove
 *       (other than the current entry being processed) nor insert entries on
 *       the given hash table, otherwise, the behavior of the _map is
 *       unpredictable (e.g., a newly inserted entry may or may not be
 *       included as part of the map. If "map_func" returns FALSE,
 *       the mapping is stopped.
 */
int
mht_map (const MHT_TABLE * ht, int (*map_func) (const void *key, void *data, void *args), void *func_args)
{
  HENTRY_PTR hentry;
  HENTRY_PTR next;
  int error_code = NO_ERROR;

  assert (ht != NULL);

  for (hentry = ht->act_head; hentry != NULL; hentry = next)
    {
      /* Just in case the hentry is removed using mht_rem save the next entry */
      next = hentry->act_next;
      error_code = (*map_func) (hentry->key, hentry->data, func_args);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }

  return (error_code);
}

/*
 * mht_map_no_key - map over hash entries;
 *                  Same as mht_map, but "map_func" is called on two arguments:
 *                  data and  arguments.
 *   return: NO_ERROR or error code - the result of user supplied "map_func"
 *   ht(in): hash table
 *   map_func(in): user supplied mapping function
 *   func_args(in): arguments to be passed to rem_func
 */
int
mht_map_no_key (THREAD_ENTRY * thread_p, const MHT_TABLE * ht,
		int (*map_func) (THREAD_ENTRY * thread_p, void *data, void *args), void *func_args)
{
  HENTRY_PTR hentry;
  HENTRY_PTR next;
  int error_code = NO_ERROR;

  assert (ht != NULL);

  for (hentry = ht->act_head; hentry != NULL; hentry = next)
    {
      /* Just in case the hentry is removed using mht_rem save the next entry */
      next = hentry->act_next;
      error_code = (*map_func) (thread_p, hentry->data, func_args);
      if (error_code != NO_ERROR)
	{
	  break;
	}
    }

  return (error_code);
}

/*
 * mht_count - number of hash entries
 *   return: number of entries
 *   ht(in): hash table
 */
unsigned int
mht_count (const MHT_TABLE * ht)
{
  assert (ht != NULL);
  return ht->nentries;
}

/*
 * mht_get_hash_number - get linear hash number or string hash number
 *   return: the hash value of the hash key
 *   ht_size(out): hash size
 *   val(in): DB_VALUE to hash
 *
 * Note:
 */
unsigned int
mht_get_hash_number (const unsigned int ht_size, const DB_VALUE * val)
{
  unsigned int hashcode = 0;
  int i, len;
  const char *ptr;

  if (!val || DB_IS_NULL (val) || ht_size <= 1)
    {
      hashcode = 0;
    }
  else
    {
      switch (db_value_type (val))
	{
	case DB_TYPE_INTEGER:
	  hashcode = mht_get_shiftmult32 (val->data.i, ht_size);
	  break;
	case DB_TYPE_SMALLINT:
	  hashcode = mht_get_shiftmult32 (val->data.sh, ht_size);
	  break;
	case DB_TYPE_BIGINT:
	  {
	    DB_BIGINT bigint;
	    unsigned int x, y;

	    bigint = db_get_bigint (val);
	    x = bigint >> 32;
	    y = (unsigned int) bigint;
	    hashcode = mht_get_shiftmult32 (x ^ y, ht_size);
	    break;
	  }
	  break;
	case DB_TYPE_FLOAT:
	  {
	    unsigned int *x, y;
	    x = (unsigned int *) &val->data.f;
	    y = (*x) & 0xFFFFFFF0;
	    hashcode = mht_get_shiftmult32 (y, ht_size);
	  }
	  break;
	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	  {
	    unsigned int *x, y, z;
	    x = (unsigned int *) &val->data.d;
	    y = (x[0]) & 0xFFFFFFF0;
	    z = (x[1]) & 0xFFFFFFF0;
	    hashcode = mht_get_shiftmult32 (y ^ z, ht_size);
	  }
	  break;
	case DB_TYPE_NUMERIC:
	  {
	    unsigned int *buf = (unsigned int *) val->data.num.d.buf;
	    hashcode = mht_get_shiftmult32 (buf[0] ^ buf[1] ^ buf[2] ^ buf[3], ht_size);
	  }
	  break;
	case DB_TYPE_DATE:
	  hashcode = mht_get_shiftmult32 (val->data.date, ht_size);
	  break;
	case DB_TYPE_TIME:
	  hashcode = mht_get_shiftmult32 (val->data.time, ht_size);
	  break;
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  hashcode = mht_get_shiftmult32 (val->data.utime, ht_size);
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	  hashcode = mht_get_shiftmult32 (val->data.timestamptz.timestamp, ht_size);
	  break;
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMELTZ:
	  hashcode = mht_get_shiftmult32 (val->data.datetime.date ^ val->data.datetime.time, ht_size);
	  break;
	case DB_TYPE_DATETIMETZ:
	  hashcode =
	    mht_get_shiftmult32 (val->data.datetimetz.datetime.date ^ val->data.datetimetz.datetime.time, ht_size);
	  break;
	case DB_TYPE_OID:
	  {
	    unsigned int x = (val->data.oid.volid << 16) | (val->data.oid.slotid);
	    unsigned int y = val->data.oid.pageid;

	    hashcode = mht_get_shiftmult32 (x ^ y, ht_size);
	  }
	  break;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  ptr = db_get_string (val);
	  if (ptr)
	    {
	      len = db_get_string_size (val);
	      if (len < 0)
		{
		  len = (int) strlen (ptr);
		}

	      i = len;
	      for (i--; 0 <= i && ptr[i]; i--)
		{
		  /* only the trailing ASCII space is ignored; the hashing for other characters depend on collation */
		  if (ptr[i] != 0x20)
		    {
		      break;
		    }
		}
	      i++;
	    }
	  if (!ptr || ptr[0] == 0 || i <= 0)
	    {
	      hashcode = 0;
	    }
	  else
	    {
	      hashcode = MHT2STR_COLL (db_get_string_collation (val), (unsigned char *) ptr, i);
	      hashcode %= ht_size;
	    }
	  break;
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	case DB_TYPE_VOBJ:
	  {
	    int size = val->data.set->disk_size / 4;
	    unsigned int x = 0;
	    int i;

	    for (i = 0; i < size; i++)
	      {
		x ^= (unsigned int) (val->data.set->disk_set[i * 4]);
	      }

	    hashcode = mht_get_shiftmult32 (x, ht_size);
	  }
	  break;
	case DB_TYPE_ENUMERATION:
	  hashcode = mht_get_shiftmult32 (val->data.enumeration.short_val, ht_size);
	  break;
	case DB_TYPE_JSON:
	  {
	    char *json_body = NULL;

	    json_body = db_get_json_raw_body (val);

	    if (json_body == NULL)
	      {
		hashcode = 0;
	      }
	    else
	      {
		len = (int) strlen (json_body);

		hashcode = MHT2STR_COLL (LANG_COLL_BINARY, (unsigned char *) json_body, len);
		hashcode %= ht_size;
		db_private_free (NULL, json_body);
	      }
	  }
	  break;
	default:		/* impossible */
	  /*
	   * TODO this is actually possible. See the QA scenario:
	   * sql/_01_object/_09_partition/_006_prunning/cases/1093.sql
	   * select * from hash_test where test_int = round(11.57);
	   * The value has type DB_TYPE_DOUBLE in this case.
	   */
	  er_log_debug (ARG_FILE_LINE, "mht_get_hash_number: ERROR type = %d" " is Unsupported partition column type.",
			db_value_type (val));
#if defined(NDEBUG)
	  hashcode = 0;
#else /* NDEBUG */
	  /* debugging purpose */
	  abort ();
#endif /* NDEBUG */
	  break;
	}
    }

  return hashcode;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * mht_get32_next_power_of_2 -
 *   return: the next power of 2 greater than ht_size
 *   ht_size(out): hash size
 *
 * Note: find first 1's bit of 32bits integer
 */
static unsigned int
mht_get32_next_power_of_2 (const unsigned int ht_size)
{
  unsigned int map = 0xffff0000, mapsave;
  int mi;

  if (ht_size == 0)
    {
      return 1;
    }

  if ((map & ht_size) == 0)
    {
      map ^= 0xffffffff;
    }

  for (mi = 3; mi >= 0; mi--)
    {
      mapsave = map;
      map = (map << (1 << mi)) & mapsave;
      if ((map & ht_size) == 0)
	{
	  map = (map ^ 0xffffffff) & mapsave;
	}
    }

  if (ht_size == map)
    {
      return map;
    }
  else
    {
      return map << 1;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * mht_get_shiftmult32 -
 *   return: new integer hash value
 *   key(in): hash key value
 *   ht_size(in): hash size
 *
 * Note: Robert Jenkin & Thomas Wang algorithm
 */
static unsigned int
mht_get_shiftmult32 (unsigned int key, const unsigned int ht_size)
{
  unsigned int c2 = 0x27d4eb2d;	/* a prime or an odd constant */

  key = (key ^ 61) ^ (key >> 16);
  key += (key << 3);
  key ^= (key >> 4);
  key *= c2;
  key ^= (key >> 15);
  return (key % ht_size);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * mht_get_linear_hash32 - find the next power of 2 greater than hashsize
 *   return: linear hash value
 *   key: hash key value
 *   ht_size: hash size
 */
static unsigned int
mht_get_linear_hash32 (const unsigned int key, const unsigned int ht_size)
{
  unsigned int np, ret;

  np = mht_get32_next_power_of_2 (ht_size);
  ret = key & (np - 1);

  for (ret = key & (np - 1); ret >= ht_size; ret &= (np - 1))
    {
      if (np % 2)
	{
	  np = np / 2 + 1;
	}
      else
	{
	  np = np / 2;
	}
    }
  return ret;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * mht_put_hls_internal
 *   internal function for mht_put_hls()
 *   put data in the order.
 *   eliminates unnecessary logic to improve performance for hash list scan.
 *
 *   return: key
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *   opt(in): options;
 */
static const void *
mht_put_hls_internal (MHT_HLS_TABLE * ht, const void *key, void *data, MHT_PUT_OPT opt)
{
  unsigned int hash;
  HENTRY_HLS_PTR hentry;

  assert (ht != NULL && key != NULL);

  /* Hash the key and make sure that the return value is between 0 and size of hash table. */
  hash = *((unsigned int *) key);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* This is a new entry */
  if (ht->nprealloc_entries > 0)
    {
      ht->nprealloc_entries--;
      hentry = ht->prealloc_entries;
      ht->prealloc_entries = ht->prealloc_entries->next;
    }
  else
    {
      hentry = (HENTRY_HLS_PTR) db_fixed_alloc (ht->heap_id, DB_SIZEOF (HENTRY_HLS));
      if (hentry == NULL)
	{
	  return NULL;
	}
    }

  hentry->data = data;
  hentry->key = *((unsigned int *) key);

  /* To input in order, use the tail node. */
  if (ht->table[hash] == NULL)
    {
      ht->table[hash] = hentry;
    }
  else
    {
      ht->table[hash]->tail->next = hentry;
      ht->ncollisions++;
    }
  hentry->next = NULL;
  ht->table[hash]->tail = hentry;
  ht->nentries++;

  return key;
}
