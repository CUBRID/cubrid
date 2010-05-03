/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version. 
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
 *
 */


/*
 * tea.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "cm_porting.h"
#include "cm_text_encryption.h"

#if (defined(SOLARIS) && !defined(SOLARIS_X86)) || defined(HPUX) || defined(AIX)
#define BYTE_ORDER_BIG_ENDIAN
#elif defined(WINDOWS) || defined(LINUX) || defined(OSF1) || defined(ALPHA_LINUX) || defined(UNIXWARE7) || defined(SOLARIS_X86)
#ifdef BYTE_ORDER_BIG_ENDIAN
#error BYTE_ORDER_BIG_ENDIAN defined
#endif
#else
#error PLATFORM NOT DEFINED
#endif

static void tea_encrypt (unsigned int key[], int len, char *buf);
static void tea_decrypt (unsigned int key[], int len, char *buf);
static void _encrypt (unsigned int k[], unsigned int text[]);
static void _decrypt (unsigned int k[], unsigned int text[]);
static char ut_get_hexval (char c);
static void array_init_random_value (char *arr, int arrsize);
static int big_endian_int (int from);

static unsigned int key[4] = { 1, 2, 3, 4 };	/* key for tea encryption algorithm */

/*
 *  DESCRIPTION
 *    The uEncrypt() function encrypts the src string and converts
 *    the binary result to the string of hexadecimal representation.
 *    Since one byte is represented as two hexadecimal digit, 'trg' must
 *    have twice the space of 'src'.
 *  NOTE
 *    - 'len' must be multiple of 8.
 *    - size of 'trg' must be at lease twice the 'src'
 *    - 'src' must have the length equal to 'len'
 */
void
uEncrypt (int len, const char *src, char *trg)
{
  char encstr[1024];
  char strbuf[10];
  int i;

  if (src == NULL)
    src = "";

  array_init_random_value (encstr, sizeof (encstr));
  strcpy (encstr, src);

  tea_encrypt (key, len, encstr);
  for (i = 0; i < len; ++i)
    {
      sprintf (strbuf, "%08x", encstr[i]);
      trg[i * 2] = strbuf[6];
      trg[i * 2 + 1] = strbuf[7];
    }
  trg[i * 2] = '\0';
}

/*
 *  DESCRIPTION
 *    The uDecrypt() function decrypts the src string and converts
 *    the string of hexadecimal string into its original values.
 *  NOTE
 *    - size of 'src' must be at least twice the 'trg'
 *    - size of 'trg' must be equal to 'len'
 */
void
uDecrypt (int len, const char *src, char *trg)
{
  int i;
  char v1, v2;
  char hexacode[1024];

  if (src == NULL || src[0] == '\0')
    {
      trg[0] = '\0';
      return;
    }
  memset (hexacode, 0, sizeof (hexacode));
  strcpy (hexacode, src);

  for (i = 0; i < len; ++i)
    {
      v1 = ut_get_hexval (hexacode[i * 2]);
      v2 = ut_get_hexval (hexacode[i * 2 + 1]);
      v1 = v1 << 4;
      trg[i] = (unsigned char) v1 | (unsigned char) v2;
    }
  tea_decrypt (key, len, trg);
  trg[len] = '\0';
}

/* This function assumes that unsigned_int is 4 bytes int.
   En/Decryption processes 8 bytes at a time.
   Although this function encrypts any length of text,
   parameter text must have space size in multiple of 8 because this
   function returns encryption in multiple of 8 bytes       */
static void
tea_encrypt (unsigned int key[], int len, char *text)
{
  unsigned int ulbuf[2];

  while (len > 0)
    {
      if ((size_t) len < sizeof (ulbuf))
	{
	  memset (ulbuf, ' ', sizeof (ulbuf));
	  memcpy (ulbuf, text, len);
	  _encrypt (key, ulbuf);
	  memcpy (text, ulbuf, len);
	  break;
	}
      else
	{
	  memcpy (ulbuf, text, sizeof (ulbuf));
	  _encrypt (key, ulbuf);
	  memcpy (text, ulbuf, sizeof (ulbuf));
	}
      len -= sizeof (ulbuf);
      text += sizeof (ulbuf);
    }
}

/* text parameter must hold encryption of 8-byte multiple size which was
   returned from previous tea_encrypt() call */
static void
tea_decrypt (unsigned int key[], int len, char *text)
{
  unsigned int ulbuf[2];

  while (len > 0)
    {
      if ((size_t) len < sizeof (ulbuf))
	{
	  memset (ulbuf, ' ', sizeof (ulbuf));
	  /*memcpy(ulbuf,text,len); */
	  memcpy (ulbuf, text, sizeof (ulbuf));
	  _decrypt (key, ulbuf);
	  memcpy (text, ulbuf, len);
	  break;
	}
      else
	{
	  memcpy (ulbuf, text, sizeof (ulbuf));
	  _decrypt (key, ulbuf);
	  memcpy (text, ulbuf, sizeof (ulbuf));
	}
      len -= sizeof (ulbuf);
      text += sizeof (ulbuf);
    }
}

static void
_encrypt (unsigned int k[], unsigned int text[])
{
  unsigned int y = big_endian_int (text[0]);
  unsigned int z = big_endian_int (text[1]);
  unsigned int delta = 0x9e3779b9;
  unsigned int sum = 0;
  int n;

  for (n = 0; n < 32; n++)
    {
      sum += delta;
      y += ((z << 4) + k[0]) ^ (z + sum) ^ ((z >> 5) + k[1]);
      z += ((y << 4) + k[2]) ^ (y + sum) ^ ((y >> 5) + k[3]);
    }
  text[0] = big_endian_int (y);
  text[1] = big_endian_int (z);
}

static void
_decrypt (unsigned int k[], unsigned int text[])
{
  unsigned int y = big_endian_int (text[0]);
  unsigned int z = big_endian_int (text[1]);
  unsigned int delta = 0x9e3779b9;
  unsigned int sum = delta << 5;
  int n;

  for (n = 0; n < 32; n++)
    {
      z -= ((y << 4) + k[2]) ^ (y + sum) ^ ((y >> 5) + k[3]);
      y -= ((z << 4) + k[0]) ^ (z + sum) ^ ((z >> 5) + k[1]);
      sum -= delta;
    }
  text[0] = big_endian_int (y);
  text[1] = big_endian_int (z);
}

static char
ut_get_hexval (char c)
{
  if (c >= '0' && c <= '9')
    return (c - '0');

  if (c >= 'a' && c <= 'f')
    return (c - 'a' + 10);

  return (c - 'A' + 10);	/* c >= 'A' && c <= 'F' */
}

static void
array_init_random_value (char *arr, int arrsize)
{
  int i;
  static unsigned int seed = 0;

  if (seed == 0)
    {
      seed = (unsigned int) time (NULL);
      srand (seed);
    }

  for (i = 0; i < arrsize; i++)
    arr[i] = rand () % 100;
}

static int
big_endian_int (int from)
{
#ifdef BYTE_ORDER_BIG_ENDIAN
  return from;
#else
  int to;
  char *p, *q;

  p = (char *) &from;
  q = (char *) &to;

  q[0] = p[3];
  q[1] = p[2];
  q[2] = p[1];
  q[3] = p[0];

  return to;
#endif
}
