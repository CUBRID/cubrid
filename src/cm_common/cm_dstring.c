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
