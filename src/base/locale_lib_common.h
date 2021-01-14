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
  /* WARNING: Changing the order of the elements breaks locale library backwards compatibility : -
   * 'save_contraction_to_C_file' function needs to be updated - checksum of collation with contraction changes */

  /* next sequence (codepoint or contraction which sorts after this contraction) */
  unsigned int next;

  /* weight value for contraction (collation without expansions) */
  unsigned int wv;

  /* weight values for contraction (collation with expansions) */
  UCA_L13_W uca_w_l13[MAX_UCA_EXP_CE];
  UCA_L4_W uca_w_l4[MAX_UCA_EXP_CE];

  /* buffer of contraction contraction, nul-terminated */
  char c_buf[LOC_MAX_UCA_CHARS_SEQ * INTL_UTF8_MAX_CHAR_SIZE];

  /* number of codepoints in contraction */
  unsigned char cp_count;
  /* size in bytes of c_buf */
  unsigned char size;

  /* UCA weight count (collation with expansions) */
  unsigned char uca_num;
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
