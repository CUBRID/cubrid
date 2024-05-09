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
 * message_catalog.c - Message catalog functions with NLS support
 */

#ident "$Id$"

#include "config.h"
#include "message_catalog.h"

/*
 * Note: stems from FreeBSD nl_type.h and msgcat.c.
 */
#if defined(WINDOWS)
#include <windows.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#if !defined(WINDOWS)
#include <sys/mman.h>
#include <netinet/in.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#include "porting.h"

#include "environment_variable.h"
#include "error_code.h"
#include "error_manager.h"
#include "language_support.h"

#if defined(WINDOWS)
#include "intl_support.h"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * MESSAGE CATALOG FILE FORMAT.
 *
 * The NetBSD/FreeBSD message catalog format is similar to the format used by
 * Svr4 systems.  The differences are:
 *   * fixed byte order (big endian)
 *   * fixed data field sizes
 *
 * A message catalog contains four data types: a catalog header, one
 * or more set headers, one or more message headers, and one or more
 * text strings.
 *
 * NOTE: some changes were done to the initial code. we can no longer use the ntypes.h. and if nltypes.h is included
 *       there will be conflicts... so just rename everything.
 */

#define NLS_MAGIC      0xff88ff89

struct nls_cat_hdr
{
  INT32 _magic;
  INT32 _nsets;
  INT32 _mem;
  INT32 _msg_hdr_offset;
  INT32 _msg_txt_offset;
};

struct nls_set_hdr
{
  INT32 _setno;			/* set number: 0 < x <= NL_SETMAX */
  INT32 _nmsgs;			/* number of messages in the set */
  INT32 _index;			/* index of first msg_hdr in msg_hdr table */
};

struct nls_msg_hdr
{
  INT32 _msgno;			/* msg number: 0 < x <= NL_MSGMAX */
  INT32 _msglen;
  INT32 _offset;
};

#ifndef ENCODING_LEN
#define ENCODING_LEN    40
#endif

#define CUB_NL_SETD         0
#define CUB_NL_CAT_LOCALE   1

typedef struct _nl_cat_d
{
  void *_data;
  int _size;
#if defined(WINDOWS)
  HANDLE map_handle;
#endif
} *cub_nl_catd;

static cub_nl_catd cub_catopen (const char *, int);
static char *cub_catgets (cub_nl_catd, int, int, const char *);
static int cub_catclose (cub_nl_catd);

/*
 * Note: stems from FreeBSD nl_type.h and msgcat.c.
 */
#define DEFAULT_NLS_PATH "/usr/share/nls/%L/%N.cat:/usr/share/nls/%N/%L:/usr/local/share/nls/%L/%N.cat:/usr/local/share/nls/%N/%L"

static cub_nl_catd load_msgcat (const char *);

cub_nl_catd
cub_catopen (const char *name, int type)
{
  int spcleft, saverr;
  char path[PATH_MAX];
  char *nlspath, *lang, *base, *cptr, *pathP, *tmpptr;
  char *cptr1, *plang, *pter, *pcode;
  struct stat sbuf;

  if (name == NULL || *name == '\0')
    {
      errno = EINVAL;
      return NULL;
    }

  /* is it absolute path ? if yes, load immediately */
  if (strchr (name, '/') != NULL)
    {
      return load_msgcat (name);
    }

  if (type == CUB_NL_CAT_LOCALE)
    {
      lang = setlocale (LC_MESSAGES, NULL);
    }
  else
    {
      lang = getenv ("CHARSET");
    }

  if (lang == NULL || *lang == '\0' || strlen (lang) > ENCODING_LEN
      || (lang[0] == '.' && (lang[1] == '\0' || (lang[1] == '.' && lang[2] == '\0'))) || strchr (lang, '/') != NULL)
    {
      lang = (char *) "C";
    }

  plang = cptr1 = strdup (lang);
  if (cptr1 == NULL)
    {
      return NULL;
    }

  cptr = strchr (cptr1, '@');
  if (cptr != NULL)
    {
      *cptr = '\0';
    }

  pter = pcode = (char *) "";
  cptr = strchr (cptr1, '_');
  if (cptr != NULL)
    {
      *cptr++ = '\0';
      pter = cptr1 = cptr;
    }

  cptr = strchr (cptr1, '.');
  if (cptr != NULL)
    {
      *cptr++ = '\0';
      pcode = cptr;
    }

  nlspath = getenv ("NLSPATH");
  if (nlspath == NULL)
    {
      nlspath = (char *) DEFAULT_NLS_PATH;
    }

  base = cptr = strdup (nlspath);
  if (cptr == NULL)
    {
      saverr = errno;
      free (plang);
      errno = saverr;
      return NULL;
    }

  while ((nlspath = strsep (&cptr, ":")) != NULL)
    {
      pathP = path;
      if (*nlspath)
	{
	  for (; *nlspath; ++nlspath)
	    {
	      if (*nlspath == '%')
		{
		  switch (*(nlspath + 1))
		    {
		    case 'l':
		      tmpptr = plang;
		      break;
		    case 't':
		      tmpptr = pter;
		      break;
		    case 'c':
		      tmpptr = pcode;
		      break;
		    case 'L':
		      tmpptr = lang;
		      break;
		    case 'N':
		      tmpptr = (char *) name;
		      break;
		    case '%':
		      ++nlspath;
		      /* fallthrough */
		    default:
		      if (pathP - path >= PATH_MAX - 1)
			{
			  goto too_long;
			}
		      *(pathP++) = *nlspath;
		      continue;
		    }
		  ++nlspath;

		put_tmpptr:
		  spcleft = PATH_MAX - (CAST_STRLEN (pathP - path)) - 1;
		  if (strlcpy (pathP, tmpptr, spcleft) >= (size_t) spcleft)
		    {
		    too_long:
		      free (plang);
		      free (base);
		      errno = ENAMETOOLONG;
		      return NULL;
		    }
		  pathP += strlen (tmpptr);
		}
	      else
		{
		  if (pathP - path >= PATH_MAX - 1)
		    {
		      goto too_long;
		    }
		  *(pathP++) = *nlspath;
		}
	    }
	  *pathP = '\0';
	  if (stat (path, &sbuf) == 0)
	    {
	      free (plang);
	      free (base);
	      return load_msgcat (path);
	    }
	}
      else
	{
	  tmpptr = (char *) name;
	  --nlspath;
	  goto put_tmpptr;
	}
    }

  free (plang);
  free (base);
  errno = ENOENT;

  return NULL;
}

char *
cub_catgets (cub_nl_catd catd, int set_id, int msg_id, const char *s)
{
  struct nls_cat_hdr *cat_hdr;
  struct nls_set_hdr *set_hdr;
  struct nls_msg_hdr *msg_hdr;
  int l, u, i, r;

  if (catd == NULL)
    {
      errno = EBADF;
      /* LINTED interface problem */
      return (char *) s;
    }

  cat_hdr = (struct nls_cat_hdr *) catd->_data;
  set_hdr = (struct nls_set_hdr *) (void *) ((char *) catd->_data + sizeof (struct nls_cat_hdr));

  /* binary search, see knuth algorithm b */
  l = 0;
  u = ntohl ((UINT32) cat_hdr->_nsets) - 1;
  while (l <= u)
    {
      i = (l + u) / 2;
      r = set_id - ntohl ((UINT32) set_hdr[i]._setno);

      if (r == 0)
	{
	  msg_hdr =
	    (struct nls_msg_hdr *) (void *) ((char *) catd->_data + sizeof (struct nls_cat_hdr) +
					     ntohl ((UINT32) cat_hdr->_msg_hdr_offset));

	  l = ntohl ((UINT32) set_hdr[i]._index);
	  u = l + ntohl ((UINT32) set_hdr[i]._nmsgs) - 1;
	  while (l <= u)
	    {
	      i = (l + u) / 2;
	      r = msg_id - ntohl ((UINT32) msg_hdr[i]._msgno);
	      if (r == 0)
		{
		  return ((char *) catd->_data + sizeof (struct nls_cat_hdr) +
			  ntohl ((UINT32) cat_hdr->_msg_txt_offset) + ntohl ((UINT32) msg_hdr[i]._offset));
		}
	      else if (r < 0)
		{
		  u = i - 1;
		}
	      else
		{
		  l = i + 1;
		}
	    }

	  /* not found */
	  goto notfound;

	}
      else if (r < 0)
	{
	  u = i - 1;
	}
      else
	{
	  l = i + 1;
	}
    }

notfound:
  /* not found */
  errno = ENOMSG;
  /* LINTED interface problem */
  return (char *) s;
}

int
cub_catclose (cub_nl_catd catd)
{
  if (catd == NULL)
    {
      errno = EBADF;
      return (-1);
    }

#if defined(WINDOWS)
  UnmapViewOfFile (catd->_data);
  CloseHandle (catd->map_handle);
#else
  munmap (catd->_data, (size_t) catd->_size);
#endif
  free (catd);
  return (0);
}

/*
 * Internal support functions
 */

static cub_nl_catd
load_msgcat (const char *path)
{
  struct stat st;
  cub_nl_catd catd;
  void *data = NULL;
#if defined(WINDOWS)
  HANDLE map_handle;
  HANDLE file_handle;
#else
  int fd;
#endif

  /* XXX: path != NULL? */

#if defined(WINDOWS)
  file_handle = CreateFile (path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file_handle == NULL)
    {
      return NULL;
    }
  if (stat (path, &st) != 0)
    {
      CloseHandle (file_handle);
      return NULL;
    }

  map_handle = CreateFileMapping ((HANDLE) file_handle, NULL, PAGE_READONLY, 0, (DWORD) st.st_size, NULL);
  if (map_handle != NULL)
    {
      data = MapViewOfFile (map_handle, FILE_MAP_READ, 0, 0, 0);
    }

  if (data == NULL)
    {
      return NULL;
    }
#else
  fd = open (path, O_RDONLY);
  if (fd == -1)
    {
      return NULL;
    }

  if (fstat (fd, &st) != 0)
    {
      close (fd);
      return NULL;
    }

  data = mmap (0, (size_t) st.st_size, PROT_READ, MAP_SHARED, fd, (off_t) 0);
  close (fd);

  if (data == MAP_FAILED)
    {
      return NULL;
    }
#endif

  if (ntohl ((UINT32) ((struct nls_cat_hdr *) data)->_magic) != NLS_MAGIC)
    {
#if defined(WINDOWS)
      UnmapViewOfFile (data);
      CloseHandle (map_handle);
#else
      munmap (data, (size_t) st.st_size);
#endif
      errno = EINVAL;
      return NULL;
    }

  catd = (cub_nl_catd) malloc (sizeof (*catd));
  if (catd == NULL)
    {
#if defined(WINDOWS)
      UnmapViewOfFile (data);
      CloseHandle (map_handle);
#else
      munmap (data, (size_t) st.st_size);
#endif
      return NULL;
    }

  catd->_data = data;
  catd->_size = (int) st.st_size;
#if defined(WINDOWS)
  catd->map_handle = map_handle;
#endif
  return catd;
}

/* system message catalog definition */
struct msgcat_def
{
  int cat_id;
  const char *name;
  MSG_CATD msg_catd;
};
struct msgcat_def msgcat_System[] = {
  {MSGCAT_CATALOG_CUBRID /* 0 */ , "cubrid.cat", NULL},
  {MSGCAT_CATALOG_CSQL /* 1 */ , "csql.cat", NULL},
  {MSGCAT_CATALOG_UTILS /* 2 */ , "utils.cat", NULL}
};

#define MSGCAT_SYSTEM_DIM \
        (sizeof(msgcat_System) / sizeof(struct msgcat_def))

/*
 * msgcat_init - initialize message catalog module
 *   return: NO_ERROR or ER_FAILED
 */
int
msgcat_init (void)
{
  size_t i;

  for (i = 0; i < MSGCAT_SYSTEM_DIM; i++)
    {
      if (msgcat_System[i].msg_catd == NULL)
	{
	  msgcat_System[i].msg_catd = msgcat_open (msgcat_System[i].name);
	}
    }

  for (i = 0; i < MSGCAT_SYSTEM_DIM; i++)
    {
      if (msgcat_System[i].msg_catd == NULL)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * msgcat_final - finalize message catalog module
 *   return: NO_ERROR or ER_FAILED
 */
int
msgcat_final (void)
{
  size_t i;
  int rc;

  rc = NO_ERROR;
  for (i = 0; i < MSGCAT_SYSTEM_DIM; i++)
    {
      if (msgcat_System[i].msg_catd != NULL)
	{
	  if (msgcat_close (msgcat_System[i].msg_catd) != NO_ERROR)
	    {
	      rc = ER_FAILED;
	    }
	  msgcat_System[i].msg_catd = NULL;
	}
    }

  return rc;
}

/*
 * msgcat_message -
 *   return: a message string or NULL
 *   cat_id(in):
 *   set_id(in):
 *   msg_id(in):
 *
 * Note:
 */
char *
msgcat_message (int cat_id, int set_id, int msg_id)
{
  char *msg;
  static char *empty = (char *) "";

  if (cat_id < 0 || ((size_t) cat_id) >= MSGCAT_SYSTEM_DIM)
    {
      return NULL;
    }

  if (msgcat_System[cat_id].msg_catd == NULL)
    {
      msgcat_System[cat_id].msg_catd = msgcat_open (msgcat_System[cat_id].name);
      if (msgcat_System[cat_id].msg_catd == NULL)
	{
	  return NULL;
	}
    }

  msg = msgcat_gets (msgcat_System[cat_id].msg_catd, set_id, msg_id, NULL);
  if (msg == NULL)
    {
      fprintf (stderr, "Cannot find message id %d in set id %d from the file %s(%s).", msg_id, set_id,
	       msgcat_System[cat_id].name, msgcat_System[cat_id].msg_catd->file);
      /* to protect the error of copying NULL pointer, return empty string ("") rather than NULL */
      return empty;
    }

  return msg;
}

/*
 * msgcat_open - open a message catalog
 *   return: message catalog descriptor MSG_CATD or NULL
 *   name(in): message catalog file name
 *
 * Note: File name will be converted to a full path name with the the root
 *       directory prefix.
 *       The returned MSG_CATD is allocated with malloc(). It will be freed in
 *       msgcat_close().
 */
MSG_CATD
msgcat_open (const char *name)
{
  cub_nl_catd catd;
  MSG_CATD msg_catd;
  char path[PATH_MAX];

  /* $CUBRID/msg/$CUBRID_MSG_LANG/'name' */
  envvar_localedir_file (path, PATH_MAX, lang_get_msg_Loc_name (), name);
  catd = cub_catopen (path, 0);
  if (catd == NULL)
    {
      /* try once more as default language */
      envvar_localedir_file (path, PATH_MAX, LANG_NAME_DEFAULT, name);
      catd = cub_catopen (path, 0);
      if (catd == NULL)
	{
	  return NULL;
	}
    }

  msg_catd = (MSG_CATD) malloc (sizeof (*msg_catd));
  if (msg_catd == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*msg_catd));
      cub_catclose (catd);
      return NULL;
    }

  msg_catd->file = strdup (path);
  msg_catd->catd = (void *) catd;

  return msg_catd;
}

/*
 * msgcat_get_descriptor - get a message catalog descriptor
 *   return: message catalog descriptor MSG_CATD
 *   cat_id(in): message id (number)
 *
 * Note:
 */
MSG_CATD
msgcat_get_descriptor (int cat_id)
{
  return (msgcat_System[cat_id].msg_catd);
}

/*
 * msgcat_gets - read a message string from the message catalog
 *   return:
 *   msg_catd(in): message catalog descriptor
 *   set_id(in): set id (number)
 *   msg_id(in): message id (number)
 *   s(in): default message string which shall be returned if the identified
 *          message is not available
 *
 * Note:
 */
char *
msgcat_gets (MSG_CATD msg_catd, int set_id, int msg_id, const char *s)
{
  cub_nl_catd catd;

  catd = (cub_nl_catd) msg_catd->catd;
  return cub_catgets (catd, set_id, msg_id, s);
}

/*
 * msgcat_close - close a message catalog
 *   return: NO_ERROR or ER_FAILED
 *   msg_catd(in): message catalog descriptor MSG_CATD
 *
 * Note:
 */
int
msgcat_close (MSG_CATD msg_catd)
{
  cub_nl_catd catd;

  catd = (cub_nl_catd) msg_catd->catd;
  free ((void *) msg_catd->file);
  free (msg_catd);
  if (cub_catclose (catd) < 0)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * msgcat_open_file - open a text file in the directory of message catalogs
 *   return: FILE pointer or NULL
 *   name(in):
 */
FILE *
msgcat_open_file (const char *name)
{
  FILE *fp;
  char path[PATH_MAX];

  /* $CUBRID/msg/$CUBRID_MSG_LANG/'name' */
  envvar_localedir_file (path, PATH_MAX, lang_get_msg_Loc_name (), name);
  fp = fopen (path, "r");
  if (fp == NULL)
    {
      /* try once more as default language */
      envvar_localedir_file (path, PATH_MAX, LANG_NAME_DEFAULT, name);
      fp = fopen (path, "r");
    }

  return fp;
}
