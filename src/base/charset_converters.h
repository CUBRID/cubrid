/*
 * Copyright (C) 1999-2002, 2004-2009 Free Software Foundation, Inc.
 * This file is part of the GNU LIBICONV Library.
 *
 * The GNU LIBICONV Library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * The GNU LIBICONV Library is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the GNU LIBICONV Library; see the file COPYING.LIB.
 * If not, write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA 02110-1301, USA.
 */

/* This file defines all the converters. */

/* Return code if invalid. (xxx_mbtowc) */
#define RET_ILSEQ   -1
#define RET_TOOFEW  -2


/* Return code if invalid. (xxx_wctomb) */
#define RET_ILUNI	-1
#define RET_TOOSMALL	-2


/* Our own notion of wide character, as UCS-4, according to ISO-10646-1. */
typedef unsigned int ucs4_t;

typedef struct s_Summary16 Summary16;
struct s_Summary16
{
  unsigned short indx;		/* index into big table */
  unsigned short used;		/* bitmask of used entries */
};

#include "ksc5601.h"
#include "jisx0212.h"
