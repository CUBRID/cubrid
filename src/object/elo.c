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
 * elo.c - ELO interface
 *
 * MEMOERY MANAGEMENT
 *   db_private_alloc/db_private_free

 * ERROR HANDLING
 *   Every exported function sets error code if error occurred
 *   Assume argument checking is already done.
 *
 * LOCATOR FORMAT: <scheme name>:<scheme specific data>
 *   <scheme name>
 *     inline : data is encoded in locator
 *     <other>: for server side BLOB/CLOB storage extension.
 *
 *   <scheme specific data>
 *     inline : 2byte hexa decimal code encoding
 *     <other>: scheme_name specific part (EXTERNAL STORAGE)
 *
 * META DATA
 * - simple key, value pair
 * - format: <key>=<value>[,<key>=<value>[...]]
 *           key, value should not contain '=', or ',' character
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "elo.h"

#include "boot.h"
#include "dbtype.h"
#include "error_manager.h"
#include "es.h"
#include "lob_locator.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "storage_common.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static const DB_ELO elo_Initializer = { -1LL, NULL, NULL, ELO_NULL, ES_NONE };

#define ELO_NEEDS_TRANSACTION(e) \
        ((e)->es_type == ES_OWFS || (e)->es_type == ES_POSIX)

/* ========================================================================== */
/* EXPORTED FUNCTIONS */
/* ========================================================================== */

/*
 * elo_create () - create a new file
 * return: NO_ERROR if successful, error code otherwise
 * DB_ELO(in): DB_ELO
 * type(in): DB_ELO_TYPE
 */
int
elo_create (DB_ELO * elo)
{
  ES_URI out_uri;
  char *uri = NULL;
  int ret = NO_ERROR;

  assert (elo != NULL);


  ret = es_create_file (out_uri);
  if (ret < NO_ERROR)
    {
      ASSERT_ERROR ();
      return ret;
    }

  uri = db_private_strdup (NULL, out_uri);
  if (uri == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, strlen (out_uri));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  elo_init_structure (elo);
  elo->size = 0;
  elo->locator = uri;
  elo->type = ELO_FBO;
  elo->es_type = es_get_type (uri);

  if (ELO_NEEDS_TRANSACTION (elo))
    {
      ret = lob_locator_add (elo->locator, LOB_TRANSIENT_CREATED);
      if (ret != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
    }
  return ret;
}

/*
 * elo_init_structure () - init. ELO structure
 */
void
elo_init_structure (DB_ELO * elo)
{
  if (elo != NULL)
    {
      *elo = elo_Initializer;
    }
}

/*
 * elo_copy_structure () - Copy elo instance
 * return:
 * elo(in):
 * dest(out):
 */
int
elo_copy_structure (const DB_ELO * elo, DB_ELO * dest)
{
  char *locator = NULL;
  char *meta_data = NULL;

  assert (elo != NULL);
  assert (dest != NULL);

  if (elo->locator != NULL)
    {
      locator = db_private_strdup (NULL, elo->locator);
      if (locator == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  if (elo->meta_data != NULL)
    {
      meta_data = db_private_strdup (NULL, elo->meta_data);
      if (meta_data == NULL)
	{
	  if (locator != NULL)
	    {
	      db_private_free_and_init (NULL, locator);
	    }
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  *dest = *elo;
  dest->locator = locator;
  dest->meta_data = meta_data;

  return NO_ERROR;
}

/*
 * elo_free_structure () - free DB_ELO structure
 *
 * return:
 * elo(in):
 */
void
elo_free_structure (DB_ELO * elo)
{
  if (elo != NULL)
    {
      if (elo->locator != NULL)
	{
	  db_private_free_and_init (NULL, elo->locator);
	}

      if (elo->meta_data != NULL)
	{
	  db_private_free_and_init (NULL, elo->meta_data);
	}

      elo_init_structure (elo);
    }
}

/*
 * elo_copy () - Create new elo instance and copy the file located by elo.
 * return:
 * elo(in):
 * dest(out):
 */
int
elo_copy (DB_ELO * elo, DB_ELO * dest)
{
  int ret;
  ES_URI out_uri;
  char *locator = NULL;
  char *meta_data = NULL;

  assert (elo != NULL);
  assert (dest != NULL);
  assert (elo->type == ELO_FBO);
  assert (elo->locator != NULL);

  /* create elo instance and copy file */
  if (elo->meta_data != NULL)
    {
      meta_data = db_private_strdup (NULL, elo->meta_data);
      if (meta_data == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  /* if it uses external storage, do transaction work */
  elo->es_type = es_get_type (elo->locator);
  if (!ELO_NEEDS_TRANSACTION (elo))
    {
      ret = es_copy_file (elo->locator, elo->meta_data, out_uri);
      if (ret != NO_ERROR)
	{
	  goto error_return;
	}

      locator = db_private_strdup (NULL, out_uri);
      if (locator == NULL)
	{
	  es_delete_file (out_uri);
	  goto error_return;
	}
    }
  else
    {
      LOB_LOCATOR_STATE state;
      ES_URI real_locator;

      state = lob_locator_find (elo->locator, real_locator);
      switch (state)
	{
	case LOB_TRANSIENT_CREATED:
	case LOB_PERMANENT_DELETED:
	  {
	    ret = es_rename_file (real_locator, elo->meta_data, out_uri);
	    if (ret != NO_ERROR)
	      {
		goto error_return;
	      }
	    locator = db_private_strdup (NULL, out_uri);
	    if (locator == NULL)
	      {
		assert (er_errid () != NO_ERROR);
		ret = er_errid ();
		goto error_return;
	      }
	    ret = lob_locator_change_state (elo->locator, locator, LOB_PERMANENT_CREATED);
	    if (ret != NO_ERROR)
	      {
		goto error_return;
	      }
	  }
	  break;

	case LOB_TRANSIENT_DELETED:
	  {
	    locator = db_private_strdup (NULL, elo->locator);
	    if (locator == NULL)
	      {
		assert (er_errid () != NO_ERROR);
		ret = er_errid ();
		goto error_return;
	      }
	    ret = lob_locator_drop (elo->locator);
	    if (ret != NO_ERROR)
	      {
		goto error_return;
	      }
	  }
	  break;

	case LOB_PERMANENT_CREATED:
	case LOB_NOT_FOUND:
	  {
	    ret = es_copy_file (real_locator, elo->meta_data, out_uri);
	    if (ret != NO_ERROR)
	      {
		goto error_return;
	      }
	    locator = db_private_strdup (NULL, out_uri);
	    if (locator == NULL)
	      {
		es_delete_file (out_uri);
		goto error_return;
	      }
	    ret = lob_locator_add (locator, LOB_PERMANENT_CREATED);
	    if (ret != NO_ERROR)
	      {
		goto error_return;
	      }
	  }
	  break;

	case LOB_UNKNOWN:
	  assert (er_errid () != NO_ERROR);
	  ret = er_errid ();
	  goto error_return;

	default:
	  assert (0);
	  return ER_FAILED;
	}
    }

  *dest = *elo;
  dest->locator = locator;
  dest->meta_data = meta_data;

  return NO_ERROR;

error_return:
  if (locator != NULL)
    {
      db_private_free_and_init (NULL, locator);
    }

  if (meta_data != NULL)
    {
      db_private_free_and_init (NULL, meta_data);
    }
  return ret;
}

/*
 * elo_delete () - delete the file located by elo and the structure itself.
 * return: NO_ERROR if successful, error code otherwise
 * elo(in):
 */
int
elo_delete (DB_ELO * elo, bool force_delete)
{
  int ret = NO_ERROR;

  assert (elo != NULL);
  assert (elo->type == ELO_FBO);
  assert (elo->locator != NULL);

  /* if it uses external storage, do transaction work */
  elo->es_type = es_get_type (elo->locator);
  if (!ELO_NEEDS_TRANSACTION (elo) || force_delete)
    {
      ret = es_delete_file (elo->locator);
    }
  else
    {
      LOB_LOCATOR_STATE state;
      ES_URI real_locator;

      state = lob_locator_find (elo->locator, real_locator);
      switch (state)
	{
	case LOB_TRANSIENT_CREATED:
	  ret = es_delete_file (real_locator);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }

	  ret = lob_locator_drop (elo->locator);
	  break;

	case LOB_PERMANENT_CREATED:
	case LOB_PERMANENT_DELETED:
	  ret = lob_locator_change_state (elo->locator, NULL, LOB_PERMANENT_DELETED);
	  break;

	case LOB_TRANSIENT_DELETED:
	  break;

	case LOB_NOT_FOUND:
	  ret = lob_locator_add (elo->locator, LOB_TRANSIENT_DELETED);
	  break;

	case LOB_UNKNOWN:
	  assert (er_errid () != NO_ERROR);
	  ret = er_errid ();
	  break;

	default:
	  assert (0);
	  return ER_FAILED;
	}
    }

  return ret;
}

/*
 * elo_size () - get the size of file located by elo
 * return: non-negative integer if successful, -1 if error.
 * elo(in):
 */
off_t
elo_size (DB_ELO * elo)
{
  off_t ret;

  assert (elo != NULL);
  assert (elo->type == ELO_FBO);
  assert (elo->locator != NULL);

  if (elo->size >= 0)
    {
      return elo->size;
    }

  ret = es_get_file_size (elo->locator);
  if (ret < 0)
    {
      return ret;
    }

  elo->size = ret;
  return ret;
}

/*
 * elo_read () - read data from elo
 * return: bytes read if successful, -1 if error
 * elo(in): ELO instance
 * buf(out): output buffer
 * count(in): bytes to read
 *
 */
ssize_t
elo_read (const DB_ELO * elo, off_t pos, void *buf, size_t count)
{
  ssize_t ret;

  assert (elo != NULL);
  assert (pos >= 0);
  assert (buf != NULL);
  assert (elo->type == ELO_FBO);
  assert (elo->locator != NULL);

  ret = es_read_file (elo->locator, buf, count, pos);
  if (ret < NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  return ret;
}

/*
 * elo_write () - write data to elo
 * return: bytes written if successful, -1 if error
 * elo(in): ELO instance
 * buf(out): data buffer
 * count(in): bytes to write
 */
ssize_t
elo_write (DB_ELO * elo, off_t pos, const void *buf, size_t count)
{
  ssize_t ret;

  assert (elo != NULL);
  assert (pos >= 0);
  assert (buf != NULL);
  assert (count > 0);
  assert (elo->type == ELO_FBO);
  assert (elo->locator != NULL);

  ret = es_write_file (elo->locator, buf, count, pos);
  if (ret < 0)
    {
      ASSERT_ERROR ();
      return ret;
    }

  /* adjust size field */
  if ((INT64) (pos + count) > elo->size)
    {
      elo->size = pos + count;
    }
  return ret;
}
