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
 * nameval.c -
 */

#ident "$Id$"

#include "cm_dep.h"
#include "cm_utils.h"

#include <stdio.h>
#include <string.h>		/* memset() */
#include <stdlib.h>		/* malloc() */
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#if !defined(WINDOWS)
#include <unistd.h>
#else
#include <io.h>
#endif

#define REALLOC(PTR, SIZE)  \
    (PTR == NULL) ? malloc(SIZE) : realloc(PTR, SIZE)



void uRemoveCRLF (char *str);
static int _nv_make_room (nvplist * ref);
static nvpair *_nv_search (nvplist * ref, const char *name);

void
uRemoveCRLF (char *str)
{
  int i;
  if (str == NULL)
    return;
  for (i = (int) strlen (str) - 1; (i >= 0) && (str[i] == 10 || str[i] == 13); i--)
    {
      str[i] = '\0';
    }
}


nvplist *
nv_create (int defsize, const char *lom, const char *lcm, const char *dm, const char *em)
{
  nvplist *nvpl;

  if (defsize < 1)
    return NULL;

  nvpl = (nvplist *) malloc (sizeof (nvplist));
  nv_init (nvpl, defsize, lom, lcm, dm, em);
  return nvpl;
}

int
nv_locate (nvplist * ref, const char *marker, int *index, int *ilen)
{
  int i;
  char *nbuf, *vbuf;

  *index = -1;
  *ilen = -1;
  for (i = 0; i < ref->nvplist_leng; ++i)
    {
      nbuf = dst_buffer (ref->nvpairs[i]->name);
      if (nbuf != NULL)
	{
	  if (strcmp ("open", nbuf) == 0)
	    {
	      vbuf = dst_buffer (ref->nvpairs[i]->value);
	      if (vbuf != NULL && strcmp (vbuf, marker) == 0)
		{
		  *index = i + 1;
		}
	    }
	  else if ((*index != -1) && (strcmp ("close", nbuf) == 0))
	    {
	      vbuf = dst_buffer (ref->nvpairs[i]->value);
	      if (vbuf != NULL && strcmp (vbuf, marker) == 0)
		break;
	    }
	}
      if (*index != -1)
	(*ilen)++;
    }
  return 1;
}

int
nv_lookup (nvplist * ref, int index, char **name, char **value)
{
  if (ref->nvpairs[index] != NULL)
    {
      if (name != NULL)
	{
	  (*name) = dst_buffer (ref->nvpairs[index]->name);
	}
      if (value != NULL)
	{
	  (*value) = dst_buffer (ref->nvpairs[index]->value);
	}
    }
  else
    {
      if (name != NULL)
	{
	  (*name) = NULL;
	}
      if (value != NULL)
	{
	  (*value) = NULL;
	}
      return -1;
    }
  return 1;
}

/*
 * description : insert one name-value pair to the nvp list
 * input : ref=ptr to nvpl, name=field name, value=field value
 *  return : # of nvpairs the list if successful, -1 if error
 *  note : name, value should be null-terminated
 */
int
nv_add_nvp (nvplist * ref, const char *name, const char *value)
{
  nvpair *nv;

  /* check parameters */
  if ((ref == NULL) || (name == NULL))
    {
      return -1;
    }

  /* build name-value pair */
  nv = (nvpair *) malloc (sizeof (nvpair));
  if (nv == NULL)
    {
      return -1;
    }
  nv->name = dst_create ();
  nv->value = dst_create ();
  dst_append (nv->name, name, (name == NULL) ? 0 : (int) strlen (name));
  dst_append (nv->value, value, (value == NULL) ? 0 : (int) strlen (value));

  /* if nvplist is full, double the ptr list */
  if (ref->nvplist_size == ref->nvplist_leng)
    {
      _nv_make_room (ref);
    }

  ref->nvpairs[ref->nvplist_leng] = nv;
  ++(ref->nvplist_leng);

  return ref->nvplist_leng;
}

int
nv_add_nvp_int (nvplist * ref, const char *name, int value)
{
  char strbuf[32];
  sprintf (strbuf, "%d", value);
  return (nv_add_nvp (ref, name, strbuf));
}


int
nv_add_nvp_int64 (nvplist * ref, const char *name, int64_t value)
{
  char strbuf[32];
  sprintf (strbuf, "%lld", (long long int) value);
  return (nv_add_nvp (ref, name, strbuf));
}

int
nv_add_nvp_float (nvplist * ref, const char *name, float value, const char *fmt)
{
  char strbuf[32];
  sprintf (strbuf, fmt, value);
  return (nv_add_nvp (ref, name, strbuf));
}

int
nv_add_nvp_time (nvplist * ref, const char *name, time_t t, const char *fmt, int type)
{
  char strbuf[64];
  if (t == 0)
    {
      return (nv_add_nvp (ref, name, ""));
    }
  else
    {
      time_to_str (t, fmt, strbuf, type);
      return (nv_add_nvp (ref, name, strbuf));
    }
}

/*
 *  description : get the value of designated name
 *  input : ref=ptr to nvpl, name=search val
 *  return : char* if successful, NULL if error
 */
char *
nv_get_val (nvplist * ref, const char *name)
{
  nvpair *nvp;

  if ((ref == NULL) || (name == NULL))
    return NULL;

  nvp = _nv_search (ref, name);

  if (nvp == NULL)		/* if not found */
    return NULL;
  else
    return dst_buffer (nvp->value);
}

/*
 *  description : replace the value referenced by given name
 *  input : ref=ptr to nvpl, name=search val, value=replacement value
 *  return : 1 if successful, -1 if error
 */
int
nv_update_val (nvplist * ref, const char *name, const char *value)
{
  nvpair *nvp;

  if ((ref == NULL) || (name == NULL))
    return -1;

  nvp = _nv_search (ref, name);
  if (nvp == NULL)		/* if not found */
    return -1;

  dst_reset (nvp->value);
  if (value != NULL)
    dst_append (nvp->value, value, (int) strlen (value));
  return 1;
}

int
nv_update_val_int (nvplist * ref, const char *name, int value)
{
  char strbuf[32];
  sprintf (strbuf, "%d", value);
  return (nv_update_val (ref, name, strbuf));
}

/* description : clear all the name-value pairs from the list */
void
nv_reset_nvp (nvplist * ref)
{
  int i;

  for (i = 0; i < (ref->nvplist_size); ++i)
    {
      if (ref->nvpairs[i] != NULL)
	{
	  dst_destroy (ref->nvpairs[i]->name);
	  dst_destroy (ref->nvpairs[i]->value);
	  free (ref->nvpairs[i]);
	  ref->nvpairs[i] = NULL;
	}
    }
  ref->nvplist_leng = 0;
}

/*
 * description : free all memory used by nvpl
 */
void
nv_destroy (nvplist * ref)
{
  int i;

  if (ref == NULL)
    {
      return;
    }

  for (i = 0; i < ref->nvplist_size; ++i)
    {
      if (ref->nvpairs[i] != NULL)
	{
	  dst_destroy (ref->nvpairs[i]->name);
	  dst_destroy (ref->nvpairs[i]->value);
	  free (ref->nvpairs[i]);
	}
    }
  free (ref->nvpairs);
  dst_destroy (ref->listopener);
  dst_destroy (ref->listcloser);
  dst_destroy (ref->delimiter);
  dst_destroy (ref->endmarker);
  free (ref);
}

/* write name value pair list to file */
int
nv_writeto (nvplist * ref, char *filename)
{
  int i;
  FILE *nvfile;
  nvfile = fopen (filename, "w");
  if (nvfile == NULL)
    return -1;

  for (i = 0; i < ref->nvplist_size; ++i)
    {
      if (ref->nvpairs[i] == NULL || dst_buffer (ref->nvpairs[i]->name) == NULL)
	continue;

      fprintf (nvfile, "%s", dst_buffer (ref->nvpairs[i]->name));
      fprintf (nvfile, "%s", ref->delimiter->dbuf);
      if (dst_buffer (ref->nvpairs[i]->value) != NULL)
	{
	  fprintf (nvfile, "%s", dst_buffer (ref->nvpairs[i]->value));
	}

      fprintf (nvfile, ref->listcloser->dbuf);
    }
  fprintf (nvfile, ref->endmarker->dbuf);
  fflush (nvfile);
  fclose (nvfile);
  return 1;
}

int
nv_readfrom (nvplist * ref, char *filename)
{
  char *buf;
  char *p, *q, *next;
  struct stat statbuf;
  int remain_read, read_len;
  int end = 0;
  int fd;

  if (stat (filename, &statbuf) != 0 || statbuf.st_size <= 0)
    {
      return ERR_STAT;
    }

  buf = (char *) malloc (statbuf.st_size + 1);
  if (buf == NULL)
    {
      return ERR_MEM_ALLOC;
    }
  memset (buf, 0, statbuf.st_size + 1);

  fd = open (filename, O_RDONLY);
  if (fd < 0)
    {
      free (buf);
      return ERR_TMPFILE_OPEN_FAIL;
    }

  remain_read = statbuf.st_size;
  p = buf;
  while (remain_read > 0)
    {
      read_len = read (fd, p, remain_read);
      if (read_len == 0)
	{
	  *p = '\0';
	  break;
	}
      if (read_len < 0)
	{
	  free (buf);
	  close (fd);
	  return ERR_TMPFILE_OPEN_FAIL;
	}
      remain_read -= read_len;
      p += read_len;
    }
  close (fd);

  p = buf;
  while (!end)
    {
      next = strchr (p, '\n');
      if (next == NULL)
	end = 1;
      else
	*next = '\0';
      uRemoveCRLF (p);

      if (*p != '\0')
	{
	  q = strchr (p, ':');
	  if (q != NULL)
	    {
	      *q = '\0';
	      q++;
	    }
	  nv_add_nvp (ref, p, q);
	}

      p = next + 1;
    }
  free (buf);

  return ERR_NO_ERROR;
}

/* description : double the nvp list size */
static int
_nv_make_room (nvplist * ref)
{
  ref->nvpairs = (nvpair **) (REALLOC (ref->nvpairs, sizeof (nvpair *) * (ref->nvplist_size) * 2));
  if (ref->nvpairs == NULL)
    return -1;
  memset (ref->nvpairs + ref->nvplist_size, 0, sizeof (nvpair *) * (ref->nvplist_size));

  ref->nvplist_size = ref->nvplist_size * 2;

  return ref->nvplist_size;
}

/* description : find ptr of nvp from given name string */
static nvpair *
_nv_search (nvplist * ref, const char *name)
{
  int i;
  for (i = 0; i < ref->nvplist_size; ++i)
    {
      if (ref->nvpairs[i] != NULL)
	{
	  char *dstbuf = dst_buffer (ref->nvpairs[i]->name);

	  if (((int) strlen (name) == dst_length (ref->nvpairs[i]->name)) && (dstbuf != NULL)
	      && (strncmp (name, dstbuf, strlen (name)) == 0))
	    {
	      return ref->nvpairs[i];
	    }
	}
    }

  return NULL;
}

/*
 * description : initialize name-value pair list structure
 * input : ref=ptr to name-value pair list, defsize=default size of the list
 * return : # of nvpair slots created if successful, -1 if error
 */
int
nv_init (nvplist * ref, int defsize, const char *lom, const char *lcm, const char *dm, const char *em)
{
  if ((ref == NULL) || (defsize < 1))
    {
      return -1;
    }

  ref->nvpairs = (nvpair **) malloc (sizeof (nvpair) * defsize);
  if (ref->nvpairs == NULL)
    {
      return -1;
    }

  memset (ref->nvpairs, 0, sizeof (nvpair *) * defsize);
  ref->nvplist_size = defsize;
  ref->nvplist_leng = 0;

  ref->listopener = dst_create ();
  ref->listcloser = dst_create ();
  ref->delimiter = dst_create ();
  ref->endmarker = dst_create ();

  if (lom != NULL)
    dst_append (ref->listopener, lom, (int) strlen (lom));
  if (lcm != NULL)
    dst_append (ref->listcloser, lcm, (int) strlen (lcm));
  if (dm != NULL)
    dst_append (ref->delimiter, dm, (int) strlen (dm));
  if (em != NULL)
    dst_append (ref->endmarker, em, (int) strlen (em));

  return 1;
}
