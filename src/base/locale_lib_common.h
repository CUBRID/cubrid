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
 * locale_lib_common.h : Locale support using LDML files
 *
 */

#ifndef _LOCALE_SUPPORT_AUX_H_
#define _LOCALE_SUPPORT_AUX_H_

/* maximum characters to be used as a sequence in UCA
  (contraction or expansion) */
#define LOC_MAX_UCA_CHARS_SEQ 3

#define MAX_UCA_EXP_CE 10

/* maximum char size for UTF-8 */
#define INTL_UTF8_MAX_CHAR_SIZE	4

#define INTL_MAX_UNICODE_CP_ALLOWED 0x10ffff

typedef unsigned int UCA_L13_W;
typedef unsigned short int UCA_L4_W;

/* Collation data with optimized weights */
typedef struct coll_contraction COLL_CONTRACTION;
struct coll_contraction
{
  /* number of codepoints in contraction */
  int cp_count;

  /* buffer of contraction contraction, nul-terminated */
  char c_buf[LOC_MAX_UCA_CHARS_SEQ * INTL_UTF8_MAX_CHAR_SIZE];
  int size;

  /* weight value for contraction */
  unsigned int wv;

  /* UCA weights values */
  char uca_num;
  UCA_L13_W uca_w_l13[MAX_UCA_EXP_CE];
  UCA_L4_W uca_w_l4[MAX_UCA_EXP_CE];
  /* next sequence */
  unsigned int next;
};

#define TEXT_CONV_MAX_BYTES 3

/* bytes sequence encoding of a source codepoint :
 * codepoint is index in an array; encoding is the item accessed by the index */
typedef struct conv_cp_to_bytes CONV_CP_TO_BYTES;
struct conv_cp_to_bytes
{
  unsigned char size;		/* size in bytes of converted codepoint */
  unsigned char bytes[TEXT_CONV_MAX_BYTES];	/* bytes of encoded sequence */
};


/* Unicode normalization structures */

/* This is the maximum number of bytes necessary to store any of the unicode
 * decompositions
 * Buffer size is enough to fit maximum 3 characters of a decomposition
 */
#define NORMALIZATION_MAX_BUF_SIZE 10


typedef struct unicode_mapping UNICODE_MAPPING;
struct unicode_mapping
{
  unsigned int cp;		/* The codepoint which the buffer composes to. */
  int size;			/* number of bytes in the buffer */
  /* utf8 buffer, null terminated. */
  unsigned char buffer[NORMALIZATION_MAX_BUF_SIZE];
};

#endif /* _LOCALE_SUPPORT_AUX_H_ */
