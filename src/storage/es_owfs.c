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
 * es_owfs.c - OwFS API for external storage supports (at client and server)
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"
#include "error_code.h"
#include "error_manager.h"
#include "es_owfs.h"

#if defined(CUBRID_OWFS) && !defined(WINDOWS)
#include <pthread.h>
#include <owfs/owfs.h>
#include <owfs/owfs_errno.h>
#include "thread_compat.hpp"
#endif // CUBRID_OWFS
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(CUBRID_OWFS) && !defined(WINDOWS)
#define ES_OWFS_HASH		(786433)
#define ES_OWFS_MAX_APPEND_SIZE	(128 * 1024)

typedef struct
{
  es_list_head_t list;
  char mds_ip[CUB_MAXHOSTNAMELEN];
  char svc_code[MAXSVCCODELEN];
  fs_handle fsh;
} ES_OWFS_FSH;

static char es_base_mds_ip[CUB_MAXHOSTNAMELEN];
static char es_base_svc_code[MAXSVCCODELEN];

pthread_mutex_t es_lock = PTHREAD_MUTEX_INITIALIZER;
static es_list_head_t es_fslist = { &es_fslist, &es_fslist };

static bool es_owfs_initialized = false;

static const char *es_get_token (const char *base_path, char *token, size_t maxlen);
static int es_parse_owfs_path (const char *base_path, char *mds_ip, char *svc_code, char *owner_name, char *file_name);
static ES_OWFS_FSH *es_new_fsh (const char *mds_ip, const char *svc_code);
static void es_make_unique_name (char *owner_name, const char *metaname, char *file_name);
static ES_OWFS_FSH *es_open_owfs (const char *mds_ip, const char *svc_code);


/*
 * es_get_token - get characters as a token which is in between separator '/'
 *
 * return: pointer to the next position to the found token
 *         if the token is last one, return value will point to '\0' character
 *         return NULL if error
 * path(in): path string
 * token(out): buffer for token string
 * maxlen(in): length of the buffer
 *
 * Note: <path> ::= /<token>/<token>/<token> ...
 *       <path> must start with '/'
 */
const char *
es_get_token (const char *base_path, char *token, size_t maxlen)
{
  const char *s;
  size_t len;

  /* must start with '/' */
  if (*base_path != '/')
    {
      return NULL;
    }
  base_path++;

  /* find next separator */
  s = strchr (base_path, '/');
  if (s)
    {
      len = s - base_path;
    }
  else
    {
      len = strlen (base_path);
      s = base_path + len;
    }
  if (len <= 0)
    {
      /* e.g., '///abc//def' */
      return NULL;
    }

  if (len >= maxlen)
    {
      len = maxlen - 1;
    }
  if (token)
    {
      strlcpy (token, base_path, len + 1);
    }
  return s;
}

/*
 * es_parse_owfs_path - splits the given owfs path into several parts,
 * 			MDS IP, service code, owner, and filename
 *
 * return: NO_ERROR or ER_FAILED
 * path(in): path part of OwFS URI
 * mds_ip(out):
 * svc_code(out):
 * owner_name(out):
 * file_name(out):
 *
 * Note: <owfs_path> ::= //mds_ip/svc_code/owner/filename
 * 	 <owfs_uri> ::= owfs:<owfs_path>
 */
static int
es_parse_owfs_path (const char *base_path, char *mds_ip, char *svc_code, char *owner_name, char *file_name)
{
  /* must start with '//' */
  if (!(base_path[0] == '/' && base_path[1] == '/'))
    {
      return ER_FAILED;
    }
  base_path++;
  /* get MDS IP part */
  base_path = es_get_token (base_path, mds_ip, CUB_MAXHOSTNAMELEN);
  if (base_path == NULL)
    {
      return ER_FAILED;
    }
  /* get service code part */
  base_path = es_get_token (base_path, svc_code, MAXSVCCODELEN);
  if (base_path == NULL)
    {
      return ER_FAILED;
    }
  /* get owner part */
  base_path = es_get_token (base_path, owner_name, NAME_MAX);
  if (base_path == NULL)
    {
      return ER_FAILED;
    }
  /* get filename part */
  base_path = es_get_token (base_path, file_name, NAME_MAX);
  if (base_path == NULL)
    {
      return ER_FAILED;
    }
  /* ignore trailing characters if get all parts which are necessary */
  return NO_ERROR;
}

/*
 * es_make_unique_name -
 */
static void
es_make_unique_name (char *owner_name, const char *metaname, char *file_name)
{
  UINT64 unum;
  unsigned int base;
  int hashval;
  int r;

#if defined(SERVER_MODE)
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  r = rand_r (&thread_p->rand_seed);
#else
  r = rand ();
#endif

  /* get unique numbers */
  unum = es_get_unique_num ();
  /* make a defferent base numer for approximately every different year */
  base = (unsigned int) (unum >> 45);

  /* make a file name & an owner name */
  snprintf (file_name, NAME_MAX, "%s.%020llu_%04d", metaname, unum, r % 10000);
  hashval = es_name_hash_func (ES_OWFS_HASH, file_name);
  snprintf (owner_name, NAME_MAX, "ces_%010u_%06d", base, hashval);
}

/*
 * es_new_fsh -
 */
static ES_OWFS_FSH *
es_new_fsh (const char *mds_ip, const char *svc_code)
{
  int ret;
  ES_OWFS_FSH *es_fsh;

  es_fsh = (ES_OWFS_FSH *) malloc (sizeof (ES_OWFS_FSH));
  if (!es_fsh)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (ES_OWFS_FSH));
      return NULL;
    }

  ret = owfs_open_fs ((char *) mds_ip, (char *) svc_code, &es_fsh->fsh);
  if (ret < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      free (es_fsh);
      return NULL;
    }

  strcpy (es_fsh->mds_ip, mds_ip);
  strcpy (es_fsh->svc_code, svc_code);
  ES_INIT_LIST_HEAD (&es_fsh->list);

  return es_fsh;
}

/*
 * es_open_owfs -
 *
 * return:
 * mds_ip(in):
 * svc_code(in):
 */
static ES_OWFS_FSH *
es_open_owfs (const char *mds_ip, const char *svc_code)
{
  int ret, rv;
  es_list_head_t *lh;
  ES_OWFS_FSH *fsh;

  rv = pthread_mutex_lock (&es_lock);
  /*
   * initialize owfs if it is first time
   */
  if (!es_owfs_initialized)
    {
      owfs_param_t param;

      ES_INIT_LIST_HEAD (&es_fslist);

      owfs_get_param (&param);
      param.use_mdcache = OWFS_TRUE;
      ret = owfs_init (&param);
      if (ret < 0)
	{
	  /* failed to init owfs */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
	  pthread_mutex_unlock (&es_lock);
	  return NULL;
	}
      es_owfs_initialized = true;
    }

  /*
   * find open fs in the cache
   */
  ES_LIST_FOR_EACH (lh, &es_fslist)
  {
    fsh = ES_LIST_ENTRY (lh, ES_OWFS_FSH, list);
    if (!strcmp (fsh->mds_ip, mds_ip) && !strcmp (fsh->svc_code, svc_code))
      {
	pthread_mutex_unlock (&es_lock);
	return fsh;
      }
  }

  /*
   * open new fs
   */
  fsh = es_new_fsh (mds_ip, svc_code);
  if (fsh == NULL)
    {
      pthread_mutex_unlock (&es_lock);
      return NULL;
    }
  es_list_add (&fsh->list, &es_fslist);
  pthread_mutex_unlock (&es_lock);

  return fsh;
}

/*
 * es_owfs_init - initialize owfs module
 *
 * return: error code, ER_ES_GENERAL or NO_ERROR
 * base_path(in): in the form of "//mds_ip/svc_code"
 */
int
es_owfs_init (const char *base_path)
{
  ES_OWFS_FSH *fsh;

  assert (base_path != NULL);

  /*
   * get MDS IP and SVC CODE
   */
  /* must start with '//' */
  if (!(base_path[0] == '/' && base_path[1] == '/'))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, base_path);
      return ER_ES_INVALID_PATH;
    }
  base_path++;
  /* get MDS IP part */
  base_path = es_get_token (base_path, es_base_mds_ip, CUB_MAXHOSTNAMELEN);
  if (base_path != NULL)
    {
      /* get service code part */
      base_path = es_get_token (base_path, es_base_svc_code, MAXSVCCODELEN);
    }
  if (base_path == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, base_path);
      return ER_ES_INVALID_PATH;
    }

  return NO_ERROR;
}

/*
 * es_owfs_final - finalize owfs module
 *
 * return: none
 */
void
es_owfs_final (void)
{
  ES_OWFS_FSH *es_fsh;
  int rv;

  rv = pthread_mutex_lock (&es_lock);
  while (!es_list_empty (&es_fslist))
    {
      es_fsh = ES_LIST_ENTRY (es_fslist.next, ES_OWFS_FSH, list);
      es_list_del (&es_fsh->list);
      owfs_close_fs (es_fsh->fsh);
      free (es_fsh);
    }
  owfs_finalize ();

  es_owfs_initialized = false;
  pthread_mutex_unlock (&es_lock);
}

/*
 * es_owfs_create_file - create a new external file with auto generated name
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * new_path(out): file path newly created
 */
int
es_owfs_create_file (char *new_path)
{
  char owner_name[NAME_MAX], file_name[NAME_MAX];
  ES_OWFS_FSH *fsh;
  owner_handle oh;
  file_handle fh;
  int ret;

  fsh = es_open_owfs (es_base_mds_ip, es_base_svc_code);
  if (fsh == NULL)
    {
      return ER_ES_GENERAL;
    }

retry:
  /* make a file name & an owner name */
  es_make_unique_name (owner_name, "ces_temp", file_name);

  /* open the owner. if not exist, create it */
  ret = owfs_open_owner (fsh->fsh, owner_name, &oh);
  if (ret == -OWFS_ENOENTOWNER)
    {
      ret = owfs_create_owner (fsh->fsh, owner_name, &oh);
      if (ret == -OWFS_EEXIST)
	{
	  ret = owfs_open_owner (fsh->fsh, owner_name, &oh);
	}
    }
  if (ret < 0)
    {
      /* failed to create an owner */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* create file */
  ret = owfs_open_file (oh, file_name, OWFS_CREAT, &fh);
  if (ret < 0)
    {
      owfs_close_owner (oh);
      if (ret == -OWFS_EEXIST)
	{
	  goto retry;
	}
      /* failed to create a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }
  ret = owfs_close_file (fh);
  owfs_close_owner (oh);
  if (ret < 0)
    {
      /* failed to create a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* make path */
  snprintf (new_path, PATH_MAX, "//%s/%s/%s/%s", fsh->mds_ip, fsh->svc_code, owner_name, file_name);
  return NO_ERROR;
}

/*
 * es_owfs_write_file - write to the external file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
ssize_t
es_owfs_write_file (const char *path, const void *buf, size_t count, off_t offset)
{
  char mds_ip[CUB_MAXHOSTNAMELEN], svc_code[MAXSVCCODELEN], owner_name[NAME_MAX], file_name[NAME_MAX];
  ES_OWFS_FSH *fsh;
  owner_handle oh;
  owfs_file_stat ostat;
  size_t total = 0, append_size;
  int ret;

  if (es_parse_owfs_path (path, mds_ip, svc_code, owner_name, file_name) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, path);
      return ER_ES_INVALID_PATH;
    }
  fsh = es_open_owfs (mds_ip, svc_code);
  if (fsh == NULL)
    {
      return ER_ES_GENERAL;
    }

  /* open owner */
  ret = owfs_open_owner (fsh->fsh, owner_name, &oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* check size of file, it should be the same with offset */
  ret = owfs_stat (oh, file_name, &ostat);
  if (ret < 0)
    {
      owfs_close_owner (oh);
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  if (ostat.s_size != offset)
    {
      owfs_close_owner (oh);
      /* invalid operation */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "offset error");
      return ER_ES_GENERAL;
    }

  /* write data by append-calls */
  while (total < count)
    {
      append_size = MIN (count - total, ES_OWFS_MAX_APPEND_SIZE);
    retry:
      ret = owfs_append_file (oh, file_name, (char *) buf + total, append_size);
      if (ret == -OWFS_ELOCK)
	{
	  goto retry;
	}
      if (ret < 0)
	{
	  owfs_close_owner (oh);
	  /* failed to write data */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
	  return ER_ES_GENERAL;
	}
      total += append_size;
    }

  owfs_close_owner (oh);
  return total;
}

/*
 * es_owfs_read_file - read from the external file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
ssize_t
es_owfs_read_file (const char *path, void *buf, size_t count, off_t offset)
{
  char mds_ip[CUB_MAXHOSTNAMELEN], svc_code[MAXSVCCODELEN], owner_name[NAME_MAX], file_name[NAME_MAX];
  ES_OWFS_FSH *fsh;
  owner_handle oh;
  file_handle fh;
  int ret;

  if (es_parse_owfs_path (path, mds_ip, svc_code, owner_name, file_name) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, path);
      return ER_ES_INVALID_PATH;
    }
  fsh = es_open_owfs (mds_ip, svc_code);
  if (fsh == NULL)
    {
      return ER_ES_GENERAL;
    }

  /* open owner */
  ret = owfs_open_owner (fsh->fsh, owner_name, &oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* open a file */
  ret = owfs_open_file (oh, file_name, OWFS_READ, &fh);
  if (ret < 0)
    {
      owfs_close_owner (oh);
      /* failed to open a file */
      if (ret == -OWFS_ENOENT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_FILE_NOT_FOUND, 1, path);
	  return ER_ES_FILE_NOT_FOUND;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
	  return ER_ES_GENERAL;
	}
    }

  ret = owfs_lseek (fh, offset, OWFS_SEEK_SET);
  if (ret < 0)
    {
      owfs_close_file (fh);
      owfs_close_owner (oh);
      /* failed to seek */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;

    }

  ret = owfs_read_file (fh, (char *) buf, (unsigned int) count);
  owfs_close_file (fh);
  owfs_close_owner (oh);
  if (ret < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;

    }
  return ret;
}

/*
 * es_owfs_delete_file - delete the external file
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * path(in): file path
 */
int
es_owfs_delete_file (const char *path)
{
  char mds_ip[CUB_MAXHOSTNAMELEN], svc_code[MAXSVCCODELEN], owner_name[NAME_MAX], file_name[NAME_MAX];
  ES_OWFS_FSH *fsh;
  owner_handle oh;
  int ret;

  if (es_parse_owfs_path (path, mds_ip, svc_code, owner_name, file_name) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, path);
      return ER_ES_INVALID_PATH;
    }
  fsh = es_open_owfs (mds_ip, svc_code);
  if (fsh == NULL)
    {
      return ER_ES_GENERAL;
    }

  /* open owner */
  ret = owfs_open_owner (fsh->fsh, owner_name, &oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  ret = owfs_delete_file (oh, file_name);
  owfs_close_owner (oh);

  if (ret == -OWFS_ENOENT)
    {
      assert (0);
    }
  else if (ret < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }
  return NO_ERROR;
}

/*
 * es_owfs_copy_file - copy the external file to new one
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * src_path(in): file path to be copied
 * new_path(out): file path newly created
 */
int
es_owfs_copy_file (const char *src_path, const char *metaname, char *new_path)
{
  char src_mds_ip[CUB_MAXHOSTNAMELEN], src_svc_code[MAXSVCCODELEN], src_owner_name[NAME_MAX], src_file_name[NAME_MAX];
  char new_owner_name[NAME_MAX], new_file_name[NAME_MAX];
  ES_OWFS_FSH *src_fsh, *dest_fsh;
  owner_handle src_oh, dest_oh;
  owfs_op_handle oph;
  int ret;

  if (es_parse_owfs_path (src_path, src_mds_ip, src_svc_code, src_owner_name, src_file_name) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, src_path);
      return ER_ES_INVALID_PATH;
    }
  src_fsh = es_open_owfs (src_mds_ip, src_svc_code);
  if (src_fsh == NULL)
    {
      return ER_ES_GENERAL;
    }
  /* open owner */
  ret = owfs_open_owner (src_fsh->fsh, src_owner_name, &src_oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  dest_fsh = es_open_owfs (es_base_mds_ip, es_base_svc_code);
  if (dest_fsh == NULL)
    {
      owfs_close_owner (src_oh);
      return ER_ES_GENERAL;
    }

retry:
  /* make a file name & an owner name */
  es_make_unique_name (new_owner_name, metaname, new_file_name);

  /* open the owner. if not exist, create it */
  ret = owfs_open_owner (dest_fsh->fsh, new_owner_name, &dest_oh);
  if (ret == -OWFS_ENOENTOWNER)
    {
      ret = owfs_create_owner (dest_fsh->fsh, new_owner_name, &dest_oh);
      if (ret == -OWFS_EEXIST)
	{
	  ret = owfs_open_owner (dest_fsh->fsh, new_owner_name, &dest_oh);
	}
    }
  if (ret < 0)
    {
      /* failed to create an owner */
      owfs_close_owner (src_oh);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* open handles for a copy operation */
  ret = owfs_open_copy_operation (src_oh, src_file_name, dest_oh, new_file_name, OWFS_CREAT, &oph);
  if (ret < 0)
    {
      owfs_close_owner (dest_oh);
      if (ret == -OWFS_EEXIST)
	{
	  goto retry;
	}
      /* failed to create a file */
      owfs_close_owner (src_oh);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  ret = owfs_copy_operation (oph, 0);
  if (ret < 0)
    {
      owfs_release_copy_operation (oph);
      owfs_close_owner (dest_oh);
      owfs_close_owner (src_oh);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  ret = owfs_close_copy_operation (oph);
  owfs_close_owner (dest_oh);
  owfs_close_owner (src_oh);
  if (ret < 0)
    {
      /* failed to create a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }


  /* make path */
  snprintf (new_path, PATH_MAX, "//%s/%s/%s/%s", dest_fsh->mds_ip, dest_fsh->svc_code, new_owner_name, new_file_name);
  return NO_ERROR;
}

/*
 * es_owfs_rename_file - convert a locator & file path according to the metaname
 *
 * return: error code, ER_ES_GENERAL or NO_ERRROR
 * src_path(in): file path to rename
 * metapath(in) : meta name combined with src_path
 * new_path(out): new file path
 */
int
es_owfs_rename_file (const char *src_path, const char *metaname, char *new_path)
{
  char src_mds_ip[CUB_MAXHOSTNAMELEN], src_svc_code[MAXSVCCODELEN], src_owner_name[NAME_MAX], src_file_name[NAME_MAX],
    tgt_file_name[NAME_MAX];
  char *s;
  ES_OWFS_FSH *src_fsh;
  owner_handle src_oh;
  int ret;

  if (es_parse_owfs_path (src_path, src_mds_ip, src_svc_code, src_owner_name, src_file_name) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, src_path);
      return ER_ES_INVALID_PATH;
    }
  src_fsh = es_open_owfs (src_mds_ip, src_svc_code);
  if (src_fsh == NULL)
    {
      return ER_ES_GENERAL;
    }
  /* open owner */
  ret = owfs_open_owner (src_fsh->fsh, src_owner_name, &src_oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* rename a file */
  s = strchr (src_file_name, '.');
  assert (s != NULL);
  if (s == NULL)
    {
      strcpy (tgt_file_name, src_file_name);
    }
  sprintf (tgt_file_name, "%s%s", metaname, s);
  ret = owfs_rename (src_oh, src_file_name, tgt_file_name);

  owfs_close_owner (src_oh);
  if (ret < 0)
    {
      /* failed to rename a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* make a path */
  snprintf (new_path, PATH_MAX, "//%s/%s/%s/%s", src_fsh->mds_ip, src_fsh->svc_code, src_owner_name, tgt_file_name);
  return NO_ERROR;
}


/*
 * es_owfs_get_file_size -
 *
 * return: file size or -1 on error
 * uri(in):
 */
off_t
es_owfs_get_file_size (const char *path)
{
  char mds_ip[CUB_MAXHOSTNAMELEN], svc_code[MAXSVCCODELEN], owner_name[NAME_MAX], file_name[NAME_MAX];
  ES_OWFS_FSH *fsh;
  owner_handle oh;
  owfs_file_stat ostat;
  int ret;

  if (es_parse_owfs_path (path, mds_ip, svc_code, owner_name, file_name) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH, 1, path);
      return ER_ES_INVALID_PATH;
    }
  fsh = es_open_owfs (mds_ip, svc_code);
  if (fsh == NULL)
    {
      return ER_ES_GENERAL;
    }

  /* open owner */
  ret = owfs_open_owner (fsh->fsh, owner_name, &oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  /* check size of file, it should be the same with offset */
  ret = owfs_stat (oh, file_name, &ostat);
  owfs_close_owner (oh);
  if (ret < 0)
    {
      /* failed to stat a file */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", owfs_perror (ret));
      return ER_ES_GENERAL;
    }

  return ostat.s_size;
}

#else /* CUBRID_OWFS */

/*
 * es_owfs_xxx() functions will return error if it does not compiled with
 *  --enable-owfs
 */

int
es_owfs_init (const char *base_path)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

void
es_owfs_final (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
}

int
es_owfs_create_file (char *new_path)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

ssize_t
es_owfs_write_file (const char *path, const void *buf, size_t count, off_t offset)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

ssize_t
es_owfs_read_file (const char *path, void *buf, size_t count, off_t offset)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

int
es_owfs_delete_file (const char *path)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

int
es_owfs_copy_file (const char *src_path, const char *metaname, char *new_path)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

int
es_owfs_rename_file (const char *src_path, const char *metaname, char *new_path)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not owfs build");
  return ER_ES_GENERAL;
}

off_t
es_owfs_get_file_size (const char *path)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "OwFS", "not built with OwFS");
  return ER_ES_GENERAL;
}

#endif /* !CUBRID_OWFS */
