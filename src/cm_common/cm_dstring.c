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
 * cm_dstring.c - 
 */

#include "cm_dep.h"
#include "cm_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* allcate memory and return it */
dstring *
dst_create (void)
{
  dstring *dstr;
  dstr = (dstring *) malloc (sizeof (dstring));
  if (dstr == NULL)
    return NULL;
  dstr->dsize = 0;
  dstr->dlen = 0;
  dstr->dbuf = NULL;
  return dstr;
}

/* destroy dynamic string */
void
dst_destroy (dstring * dstr)
{
  if (dstr != NULL)
    {
      if ((dstr->dsize > 0) && (dstr->dbuf))
	FREE_MEM (dstr->dbuf);
      FREE_MEM (dstr);
    }
}

/* clear contents of the dynamic string */
void
dst_reset (dstring * dstr)
{
  if (dstr == NULL)
    return;

  if (dstr->dsize > 0)
    {
      memset (dstr->dbuf, 0, dstr->dsize);
      dstr->dlen = 0;
    }
  else
    {
      dstr->dbuf = NULL;
      dstr->dsize = 0;
      dstr->dlen = 0;
    }
}

/* DESCRIPTION : append string to dynamic string
RETURN : new string length on success, -1 on error */
int
dst_append (dstring * dstr, const char *str, int slen)
{
  if (dstr == NULL)
    return -1;

  if ((str == NULL) || (slen <= 0))
    return dstr->dlen;

  if ((dstr->dsize - 1) < (dstr->dlen + slen))
    {
      dstr->dsize = dstr->dlen + slen + 1;
      dstr->dsize = ((dstr->dsize + 1023) >> 10) << 10;
      dstr->dbuf = (char *) (REALLOC (dstr->dbuf, dstr->dsize));
    }
  if (dstr->dbuf == NULL)
    {
      dstr->dsize = 0;
      dstr->dbuf = NULL;
      dstr->dlen = 0;
      return -1;
    }

  memcpy ((dstr->dbuf) + (dstr->dlen), str, slen);
  dstr->dlen = dstr->dlen + slen;
  dstr->dbuf[dstr->dlen] = '\0';
  return dstr->dlen;
}

int
dst_length (dstring * dstr)
{
  if (dstr == NULL)
    return -1;
  return dstr->dlen;
}

int
dst_size (dstring * dstr)
{
  if (dstr == NULL)
    return -1;
  return dstr->dsize;
}

char *
dst_buffer (dstring * dstr)
{
  if (dstr == NULL)
    return NULL;

  return dstr->dbuf;
}
