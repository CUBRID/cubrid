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
 *  base64.c -  Base64 encoding and decoding
 *
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#include "porting.h"
#include "error_code.h"
#include "memory_alloc.h"
#include "base64.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define  MAX_BASE64_LINE_LENGTH      76
#define  CH_INVALID                  -1
#define  CH_SPACE                    -2

/* core structure for decoding */
typedef struct base64_chunk BASE64_CHUNK;
struct base64_chunk
{
  char base64_bytes[4];		/* position in base64 table for encoded bytes */
  int padding_len;		/* number of padding */
};

/*
 *   Helper table for encoding
 */
const char *base64_map = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz" "0123456789+/";

/*
 *  Helper table for decoding.
 *  -2 means  space character
 *  -1 means  invalid character
 *   positive values mean valid character
 */
const char from_base64_table[] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2, -1, -1, -1, -1,
  -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* !"#$%&'()*+,-./ */
  -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
  /* 0123456789:;<=>? */
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
  /* @ABCDEFGHIJKLMNO */
  -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  /* PQRSTUVWXYZ[\]^_ */
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
  /* `abcdefghijklmno */
  -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  /* pqrstuvwxyz{|}~ */
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static bool char_is_invalid (unsigned char ch);
static bool char_is_padding (unsigned char ch);
static int get_base64_encode_len (int src_len);
static int base64_encode_local (const unsigned char *src, int src_len, unsigned char *dest);
static int base64_remove_space (const unsigned char *src, int src_len, unsigned char *dest, int *dst_len);
static int base64_partition_into_chunk (const unsigned char *src, int src_len, int *chunk_num, int *dst_len,
					BASE64_CHUNK *** pppchunk);
static int base64_decode_chunk (unsigned char *dest, int chunk_num, BASE64_CHUNK ** ppchunk);
static void free_base64_chunk (BASE64_CHUNK ** ppchunk, int chunk_num);

/*
 * char_is_invalid () -
 *   return: true if input char is invalid character, otherwise false
 *   ch(in): input character
 */
static bool
char_is_invalid (unsigned char ch)
{
  return (ch != '=' && from_base64_table[ch] == CH_INVALID);
}

/*
 * char_is_padding () -
 *   return: true if input char is padding('='), otherwise false
 *   ch(in): input character
 */
static bool
char_is_padding (unsigned char ch)
{
  return (ch == '=');
}

/*
 * find_base64 () -
 *   return: offset in base64 table for input character
 *   ch(in): input character
 */
static int
find_base64 (unsigned char ch)
{
  int offset;
  const char *pos = NULL;

  pos = strchr (base64_map, ch);
  assert (pos != NULL);

  offset = CAST_STRLEN (pos - base64_map);
  return offset;
}

/*
 * base64_partition_into_chunk () - Partition base64 encoded string into
 *                                  multiple chunks. Partition stops when
 *                                  meeting the first chunk ending in
 *                                  padding(s).
 *
 *   return:  int(NO_ERROR if successful, otherwise other base64 error code)
 *   src(in): source string(base64 encoded)
 *   src_len(in):   source string length
 *   chunk_num_out(in/out): actual number of chunks calculated
 *   dst_len_out(in/out):   actual length of buffer to store decoded bytes
 *   pppchunk(in/out):      array pointer of chunks to store encoded bytes
 */
static int
base64_partition_into_chunk (const unsigned char *src, int src_len, int *chunk_num_out, int *dst_len_out,
			     BASE64_CHUNK *** pppchunk)
{
  int err = NO_ERROR;
  int i, j, chunk_count, chunk_num, dst_len;
  unsigned char d1, d2, d3, d4;
  bool tail_chunk_flag;
  BASE64_CHUNK *chk = NULL;
  BASE64_CHUNK **ppchunk = NULL;

  assert (src != NULL && src_len > 0 && chunk_num_out != NULL && dst_len_out != NULL && pppchunk != NULL);

  *chunk_num_out = 0;
  *dst_len_out = 0;
  *pppchunk = NULL;

  /* require 4-byte alignment for chunk */
  if ((src_len & 0x3) != 0)
    {
      err = BASE64_INVALID_INPUT;
      goto end;
    }

  /* chunk array pointer to accommodate maximum chunk_num base64 chunks */
  chunk_num = src_len / 4;
  ppchunk = (BASE64_CHUNK **) db_private_alloc (NULL, chunk_num * sizeof (BASE64_CHUNK *));

  if (ppchunk == NULL)
    {
      err = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  memset (ppchunk, 0, chunk_num * sizeof (BASE64_CHUNK *));

  tail_chunk_flag = false;
  i = chunk_count = dst_len = 0;
  while (chunk_count < chunk_num)
    {
      d1 = src[i];
      d2 = src[i + 1];
      d3 = src[i + 2];
      d4 = src[i + 3];

      /* check all invalid conditions */
      if (char_is_invalid (d1) || char_is_invalid (d2) || char_is_invalid (d3) || char_is_invalid (d4)
	  || char_is_padding (d1) || char_is_padding (d2))
	{
	  err = BASE64_INVALID_INPUT;
	  goto end;
	}

      if (char_is_padding (d3) && !char_is_padding (d4))
	{
	  err = BASE64_INVALID_INPUT;
	  goto end;
	}

      chk = (BASE64_CHUNK *) db_private_alloc (NULL, sizeof (BASE64_CHUNK));
      if (chk == NULL)
	{
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto end;
	}

      memset (chk, 0, sizeof (BASE64_CHUNK));

      chk->padding_len = char_is_padding (d4) ? 1 : 0;
      chk->padding_len += char_is_padding (d3) ? 1 : 0;

      for (j = 0; j <= (3 - chk->padding_len); j++)
	{
	  chk->base64_bytes[j] = (char) find_base64 (src[i + j]);
	}

      ppchunk[chunk_count] = chk;

      if (char_is_padding (d4) == true)
	{
	  /* middle chunk that has padding is invalid */
	  if ((chunk_count + 1) != chunk_num)
	    {
	      err = BASE64_INVALID_INPUT;
	      goto end;
	    }
	  else			/* tail chunk that has padding */
	    {
	      /* how many decoded bytes to add in this chunk, e.g, from_base64(YWE=)='aa',from_base64(YW==)='a' */
	      dst_len += char_is_padding (d3) ? 1 : 2;
	      tail_chunk_flag = true;
	    }
	}

      chunk_count++;

      if (tail_chunk_flag == true)
	{
	  break;
	}

      dst_len += 3;
      i += 4;
    }

end:

  if (err == NO_ERROR)
    {
      *chunk_num_out = chunk_count;
      *dst_len_out = dst_len;
      *pppchunk = ppchunk;
    }
  else
    {
      /* free resource if error */
      if (ppchunk != NULL)
	{
	  free_base64_chunk (ppchunk, chunk_count);
	}

      *chunk_num_out = 0;
      *dst_len_out = 0;
      *pppchunk = NULL;
    }

  return err;
}

/*
 * base64_remove_space () - remove space characters to facilitate base64
 *                           chunk partition
 *   return: NO_ERROR
 *   src(in): input string
 *   size(in):length of input string
 *   dest(in/out): buffer to store the string that has no space characters
 *   dst_len(in/out): length of dest
 *
 *   Note: dest is allocated in this function but freed in caller. dst_len may
 *         differ from size in case of space characters.
 */
static int
base64_remove_space (const unsigned char *src, int size, unsigned char *dest, int *dst_len)
{
  int i;
  unsigned char *q;

  assert (src != NULL && dest != NULL && dst_len != NULL);

  *dst_len = 0;
  q = dest;

  i = 0;
  while (i < size)
    {
      /* search for the next char that is non-space character */
      while (from_base64_table[src[i]] == CH_SPACE && i < size)
	{
	  i++;
	}

      if (i >= size)
	{
	  break;
	}

      /* be careful of memory bounds! */
      *q++ = src[i++];
      (*dst_len)++;
    }

  return NO_ERROR;
}

/*
 *  base64_decode_chunk () - convert encoded bytes stored in chunks to
 *                           plain-text bytes
 *   return:        NO_ERROR
 *   dest(in/out):  buffer to store decoded bytes
 *   chunk_num(in): number of chunks
 *   ppchunk(in): buffer to store chunks
 *
 *  Note:  each chunk has four encoded bytes and can be converted to [1,2,3]
 *         decoded bytes depending on padding.
 */
static int
base64_decode_chunk (unsigned char *dest, int chunk_num, BASE64_CHUNK ** ppchunk)
{
  int i, d, copy_num;
  char decode_bytes[3];
  BASE64_CHUNK *chk;

  assert (dest != NULL && ppchunk != NULL);

  i = 0;
  d = 0;
  while (i < chunk_num)
    {
      chk = ppchunk[i];
      d += (chk->base64_bytes[0]) << 18;
      d += (chk->base64_bytes[1]) << 12;
      d += (chk->base64_bytes[2]) << 6;
      d += (chk->base64_bytes[3]) << 0;

      decode_bytes[0] = (d >> 16) & 0xff;
      decode_bytes[1] = (d >> 8) & 0xff;
      decode_bytes[2] = (d >> 0) & 0xff;

      /* only allow for 0,1,2 */
      assert (chk->padding_len < 3);
      copy_num = 3 - chk->padding_len;

      memcpy (dest, decode_bytes, copy_num);

      dest += copy_num;
      d = 0;
      i++;
    }

  return NO_ERROR;
}

/*
 *  base64_decode() -
 *   return:        int(NO_ERROR if successful,otherwise failure error code)
 *   src(in):       source plain-text string
 *   src_len(in):   length of source string
 *   dest(in/out):  buffer to store decoded bytes, allocated in this func
 *   dest_len(in/out): required length to store decoded buffer
 *
 *  Note:  there are some error handlings to handle invalid characters.
 *         some buffers are allocated in  this function or its callees,
 *         and freed in the end.
 *
 */
int
base64_decode (const unsigned char *src, int src_len, unsigned char **dest, int *dest_len)
{
  int error_status = NO_ERROR;
  int len_no_space, real_dest_len, chunk_num = 0;
  unsigned char *str_no_space = NULL;
  unsigned char *real_dest = NULL;
  BASE64_CHUNK **ppchunk = NULL;

  assert (src != NULL && dest != NULL && dest_len != NULL);

  *dest = NULL;
  *dest_len = 0;

  len_no_space = 0;

  /* assume there are no space characters in source string */
  str_no_space = (unsigned char *) db_private_alloc (NULL, src_len + 1);
  if (str_no_space == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto buf_clean;
    }

  /* remove space characters */
  base64_remove_space (src, src_len, str_no_space, &len_no_space);

  /* ' ' has spaces. after space removal, it is empty string */
  if (len_no_space == 0)
    {
      error_status = BASE64_EMPTY_INPUT;
      goto buf_clean;
    }

  /* Partition encoded bytes with no space characters into multiple chunks for decoding, stop when there is a middle
   * chunk ending in padding(s), which is considered as invalid input */
  error_status = base64_partition_into_chunk (str_no_space, len_no_space, &chunk_num, &real_dest_len, &ppchunk);

  if (error_status != NO_ERROR)
    {
      goto buf_clean;
    }

  real_dest = (unsigned char *) db_private_alloc (NULL, real_dest_len + 1);
  if (real_dest == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto buf_clean;
    }

  real_dest[real_dest_len] = '\0';

  assert (chunk_num > 0);
  base64_decode_chunk (real_dest, chunk_num, ppchunk);

  *dest = real_dest;
  *dest_len = real_dest_len;

  error_status = NO_ERROR;

buf_clean:

  if (str_no_space != NULL)
    {
      db_private_free_and_init (NULL, str_no_space);
    }

  if (error_status != NO_ERROR && real_dest != NULL)
    {
      db_private_free_and_init (NULL, real_dest);
    }

  if (ppchunk != NULL)
    {
      free_base64_chunk (ppchunk, chunk_num);
    }

  return error_status;
}

/*
 * get_base64_encode_len () -
 *   return:  the string length with base64 encoding
 *   src_len(in):   source string length
 */
static int
get_base64_encode_len (int src_len)
{
  int nbytes, backspace_bytes_need, total_bytes;

  assert (src_len >= 0);

  nbytes = (src_len + 2) / 3 * 4;

  /* need to account for backspaces in the encoded string */
  backspace_bytes_need = nbytes / MAX_BASE64_LINE_LENGTH;
  if (nbytes % MAX_BASE64_LINE_LENGTH == 0)
    {
      --backspace_bytes_need;
    }

  total_bytes = nbytes + backspace_bytes_need;

  return total_bytes;
}

/*
 *  base64_encode () -
 *   return:  NO_ERROR(NO_ERROR if successful,otherwise failure error code)
 *   src(in):     source plain-text string
 *   src_len(in): length of source string
 *   dest(in/out):  base64 encoded string
 *   dest_len(in/out): required length for encoded string
 *
 */
int
base64_encode (const unsigned char *src, int src_len, unsigned char **dest, int *dest_len)
{
  int encode_len;
  int error_status;
  unsigned char *dest_p = NULL;

  assert (src != NULL && src_len >= 0 && dest != NULL && dest_len != NULL);

  *dest = NULL;
  *dest_len = 0;

  encode_len = get_base64_encode_len (src_len);

  dest_p = (unsigned char *) db_private_alloc (NULL, encode_len + 1);
  if (dest_p == NULL)
    {
      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  error_status = base64_encode_local (src, src_len, dest_p);

  if (error_status == NO_ERROR)
    {
      dest_p[encode_len] = '\0';

      *dest = dest_p;
      *dest_len = encode_len;
    }
  else
    {
      if (dest_p != NULL)
	{
	  db_private_free_and_init (NULL, dest_p);
	}
    }

end:

  return error_status;
}

/*
 *  base64_encode_local () - convert a plain-text string with base64,
 *                            invoked by base64_encode
 *   return:        NO_ERROR
 *   src(in):       source plain-text string
 *   src_len(in):   length of source string
 *   dest(in):      base64 encoded string
 *
 *  Note: it's the caller's responsibility to claim and reclaim(if needed)
 *        memory space for dest
 */
static int
base64_encode_local (const unsigned char *src, int src_len, unsigned char *dest)
{
  const unsigned char *p;
  unsigned int d;
  int i, encoded_len, fill, line_break_count;

  assert (src != NULL && src_len >= 0 && dest != NULL);

  encoded_len = 0;
  line_break_count = 0;
  p = src;

  i = 0;
  while (i < src_len)
    {
      /* require maximum char length per line to be 76 */
      if (line_break_count == MAX_BASE64_LINE_LENGTH)
	{
	  dest[encoded_len++] = '\n';
	  line_break_count = 0;
	}

      /* move forward the source string every three bytes and translate it into four 6-bit bytes */
      d = (p[i++] << 16);	/* the most significant(3rd) bytes */
      fill = 2;			/* assuming 2 paddings needed */

      if (src_len >= (i + 1))
	{
	  d += p[i++] << 8;	/* the second byte, one padding needed */
	  fill = 1;
	}

      if (src_len >= (i + 1))	/* least significant byte, no padding needed */
	{
	  d += p[i++] << 0;
	  fill = 0;
	}

      /* convert 24bit stream into four 6-bit bytes */
      dest[encoded_len++] = base64_map[(d >> 18) & 0x3f];
      dest[encoded_len++] = base64_map[(d >> 12) & 0x3f];
      dest[encoded_len++] = (fill > 1) ? '=' : base64_map[(d >> 6) & 0x3f];
      dest[encoded_len++] = (fill > 0) ? '=' : base64_map[(d >> 0) & 0x3f];

      line_break_count += 4;
    }

  return NO_ERROR;
}

/*
 *  free_base64_chunk () - free allocated base64_chunk
 *   return:
 *   pchunk(in):       source plain-text string
 *
 */
static void
free_base64_chunk (BASE64_CHUNK ** ppchunk, int chunk_num)
{
  int i = 0;

  if (ppchunk != NULL)
    {
      for (i = 0; i < chunk_num; ++i)
	{
	  if (ppchunk[i] != NULL)
	    {
	      db_private_free (NULL, ppchunk[i]);
	    }
	}

      db_private_free (NULL, ppchunk);
    }

  return;
}
