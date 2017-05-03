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
#include "error_manager.h"
#include "storage_common.h"
#include "object_representation.h"
#include "object_primitive.h"

#include "es.h"
#include "boot.h"

#if defined(CS_MODE)
#include "network_interface_cl.h"
#else
#include "xserver_interface.h"
#endif

static const DB_ELO elo_Initializer = { -1LL, NULL, NULL, ELO_NULL, ES_NONE };

#define ELO_NEEDS_TRANSACTION(e) \
        ((e)->es_type == ES_OWFS || (e)->es_type == ES_POSIX)

#if defined (ENABLE_UNUSED_FUNCTION)
/* Meta data */
typedef struct elo_meta ELO_META;
typedef struct elo_meta_item ELO_META_ITEM;

struct elo_meta
{
  ELO_META_ITEM *items;
};

struct elo_meta_item
{
  ELO_META_ITEM *next;
  char *key;
  char *value;
};

/* Meta data */
static ELO_META *meta_create (const char *s);
static void meta_destroy_internal (ELO_META * meta);
static int meta_destroy (ELO_META * meta);
static ELO_META_ITEM *meta_item_get (ELO_META * meta, const char *key);
static int meta_check_value (const char *val, bool permit_zero_len);
static ELO_META_ITEM *meta_item_create (const char *key, int key_len, const char *value, int value_len);
static const char *meta_get (ELO_META * meta, const char *key);
static int meta_set (ELO_META * meta, const char *key, const char *val);
static char *meta_to_string (ELO_META * meta);
#endif /* ENABLE_UNUSED_FUNCTION */

static LOB_LOCATOR_STATE find_lob_locator (const char *locator, char *real_locator);
static int add_lob_locator (const char *locator, LOB_LOCATOR_STATE state);
static int change_state_of_locator (const char *locator, const char *new_locator, LOB_LOCATOR_STATE state);
static int drop_lob_locator (const char *locator);

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * meta_create () - create ELO_META structure
 * return: ELO_META instance if successful, NULL otherwise
 * s(in): meta string
 */
static ELO_META *
meta_create (const char *s)
{
  ELO_META *meta;

  meta = db_private_alloc (NULL, sizeof (ELO_META));
  if (meta == NULL)
    {
      return NULL;
    }
  meta->items = NULL;

  if (s != NULL)
    {
      char *end_p;
      char *pp;
      char *key_sp, *key_ep;	/* key_ep = position of the last key char + 1 */
      char *val_sp, *val_ep;

      pp = (char *) s;
      end_p = (char *) s + strlen (s);
      while (pp < end_p)
	{
	  key_sp = pp;
	  key_ep = strchr (key_sp, '=');
	  if (key_ep == NULL)
	    {
	      goto error;
	    }

	  val_sp = key_ep + 1;
	  val_ep = strchr (val_sp, ',');
	  if (val_ep == NULL)
	    {
	      val_ep = end_p;
	    }
	  /* make a new item. key should have positive length */
	  if (key_ep - key_sp > 0 && val_ep - val_sp >= 0)
	    {
	      ELO_META_ITEM *item = meta_item_create (key_sp, key_ep - key_sp, val_sp,
						      val_ep - val_sp);
	      if (item == NULL)
		{
		  goto error;
		}

	      /* add to meta data items */
	      item->next = meta->items;
	      meta->items = item;
	    }
	  else
	    {
	      goto error;
	    }

	  /* advance to the position of the next key */
	  pp = val_ep + 1;
	}
    }

  return meta;

error:
  if (meta != NULL)
    {
      meta_destroy_internal (meta);
    }

  return NULL;
}

/*
 * meta_destroy_internal () - destroy ELO_META structure
 * return:
 * meta(in): ELO_META instance
 */
static void
meta_destroy_internal (ELO_META * meta)
{
  ELO_META_ITEM *item;

  while (meta->items != NULL)
    {
      item = meta->items;
      meta->items = item->next;

      db_private_free_and_init (NULL, item->key);
      db_private_free_and_init (NULL, item->value);
      db_private_free_and_init (NULL, item);
    }

  db_private_free_and_init (NULL, meta);
}

/*
 * meta_destroy () - destroy ELO_META instance
 * return: NO_ERROR if successful, ER_FAILED otherwise
 * meta(in): ELO_META instance
 */
static int
meta_destroy (ELO_META * meta)
{
  if (meta != NULL)
    {
      meta_destroy_internal (meta);
      return NO_ERROR;
    }

  return ER_FAILED;
}

/*
 * meta_item_get () - get ELO_META_ITEM instance from ELO_META
 * return: ELO_META_ITEM found, NULL otherwise
 * meta(in): ELO_META instance
 * key(in): key for value
 */
static ELO_META_ITEM *
meta_item_get (ELO_META * meta, const char *key)
{
  ELO_META_ITEM *item;

  if (meta != NULL)
    {
      item = meta->items;
      while (item != NULL)
	{
	  if (strcmp (item->key, key) == 0)
	    {
	      return item;
	    }

	  item = item->next;
	}
    }

  return NULL;
}

/*
 * meta_check_value () - check value for meta data
 * return: NO_ERROR if check passed, ER_FAILED otherwise
 * val(in): value
 * permit_zero_len(in): if non zero value, permits zero length value
 */
static int
meta_check_value (const char *val, bool permit_zero_len)
{
  char *p;

  p = (char *) val;
  if (p != NULL)
    {
      while (*p)
	{
	  if (*p == ',' || *p == '=')
	    {
	      return ER_FAILED;
	    }
	  p++;
	}
    }

  if (!permit_zero_len)
    {
      return (p - val) > 0 ? NO_ERROR : ER_FAILED;
    }
  else
    {
      return NO_ERROR;
    }

  return ER_FAILED;
}

/*
 * meta_item_create () - create ELO_META_ITEM with key and value
 * return: ELO_META_ITEM instance if successful, NULL otherwise
 * key(in): key
 * key_len(in): key length
 * value(in): value
 * value_len(in): value length
 */
static ELO_META_ITEM *
meta_item_create (const char *key, int key_len, const char *value, int value_len)
{
  ELO_META_ITEM *item;
  char *k, *v;

  item = db_private_alloc (NULL, sizeof (ELO_META_ITEM));
  if (item != NULL)
    {
      k = db_private_alloc (NULL, key_len + 1);
      if (k != NULL)
	{
	  memcpy (k, key, key_len);
	  k[key_len] = '\0';

	  v = db_private_alloc (NULL, value_len + 1);
	  if (v != NULL)
	    {
	      memcpy (v, value, value_len);
	      v[value_len] = '\0';

	      item->key = k;
	      item->value = v;

	      return item;
	    }
	  db_private_free_and_init (NULL, k);
	}
      db_private_free_and_init (NULL, item);
    }

  return NULL;
}

/*
 * meta_get () - get value of the key from ELO_META
 * return: value if found, NULL otherwise
 * meta(in): ELO_META instance
 * key(in): key
 */
static const char *
meta_get (ELO_META * meta, const char *key)
{
  ELO_META_ITEM *item;

  item = meta_item_get (meta, key);
  if (item != NULL)
    {
      return item->value;
    }

  return NULL;
}

/*
 * meta_set () - set key/value data into ELO_META structure
 * return: NO_ERROR if successful, ER_FAILED otherwise
 * meta(in): ELO_META instance
 * key(in): key
 * value(in): value
 */
static int
meta_set (ELO_META * meta, const char *key, const char *val)
{
  ELO_META_ITEM *item;
  char *k, *v;

  assert (meta != NULL);
  assert (key != NULL);

  if (meta_check_value (key, false) != NO_ERROR || meta_check_value (val, true) != NO_ERROR)
    {
      return ER_FAILED;
    }

  item = meta_item_get (meta, key);
  if (item != NULL)
    {
      v = db_private_strdup (NULL, val);
      if (v == NULL)
	{
	  return ER_FAILED;
	}

      db_private_free_and_init (NULL, item->value);
      item->value = v;

      return NO_ERROR;
    }

  /* create new item */
  item = meta_item_create (key, strlen (key), val, val == NULL ? 0 : strlen (val));
  if (item == NULL)
    {
      return ER_FAILED;
    }

  /* insert into item */
  item->next = meta->items;
  meta->items = item;

  return NO_ERROR;
}

/*
 * meta_to_string () - create string from ELO_META
 * return: meta data string if successful, NULL otherwise
 * meta(in): ELO_META instance
 */
static char *
meta_to_string (ELO_META * meta)
{
  ELO_META_ITEM *item;
  size_t sz;
  char *buf, *buf_p;
  const char *sep;

  assert (meta != NULL);

  if (meta->items == NULL)
    {
      /* do not return "" */
      return NULL;
    }

  /* calculate total size needed */
  sz = 0;
  item = meta->items;
  while (item != NULL)
    {
      sz += strlen (item->key);
      sz += strlen (item->value);
      sz += 2;			/* first('=' and '\0'), second~ (',' and '=') */
      item = item->next;
    }

  /* allocate buffer and copy items */
  buf_p = buf = db_private_alloc (NULL, sz);
  if (buf == NULL)
    {
      return NULL;
    }

  item = meta->items;
  sep = "";
  while (item != NULL)
    {
      int r;

      r = snprintf (buf_p, sz, "%s%s=%s", sep, item->key, item->value);
      buf_p = buf_p + r;
      sz = sz - r;

      item = item->next;
      if (sep[0] == 0)
	{
	  sep = ",";
	}
    }

  assert (sz > 0);
  *buf_p = '\0';

  return buf;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * find_lob_locator () - wrapper function
 * return: LOB_LOCATOR_STATE
 * locator(in):
 * real_locator(out):
 */
static LOB_LOCATOR_STATE
find_lob_locator (const char *locator, char *real_locator)
{
#if defined(CS_MODE)
  return log_find_lob_locator (locator, real_locator);
#else /* CS_MODE */
  return xlog_find_lob_locator (NULL, locator, real_locator);
#endif /* CS_MODE */
}

/*
 * add_lob_locator () - wrapper function
 * return: error status
 * locator(in):
 * state(in):
 */
static int
add_lob_locator (const char *locator, LOB_LOCATOR_STATE state)
{
#if defined(CS_MODE)
  return log_add_lob_locator (locator, state);
#else /* CS_MODE */
  return xlog_add_lob_locator (NULL, locator, state);
#endif /* CS_MODE */
}

/*
 * change_state_of_locator () - wrapper function
 * return: error status
 * locator(in):
 * new_locator(in):
 * state(in):
 */
static int
change_state_of_locator (const char *locator, const char *new_locator, LOB_LOCATOR_STATE state)
{
#if defined(CS_MODE)
  return log_change_state_of_locator (locator, new_locator, state);
#else /* CS_MODE */
  return xlog_change_state_of_locator (NULL, locator, new_locator, state);
#endif /* CS_MODE */
}

/*
 * drop_lob_locator () - wrapper function
 * return: error status
 * locator(in):
 */
static int
drop_lob_locator (const char *locator)
{
#if defined(CS_MODE)
  return log_drop_lob_locator (locator);
#else /* CS_MODE */
  return xlog_drop_lob_locator (NULL, locator);
#endif /* CS_MODE */
}

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
      ret = add_lob_locator (elo->locator, LOB_TRANSIENT_CREATED);
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

      state = find_lob_locator (elo->locator, real_locator);
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
	    ret = change_state_of_locator (elo->locator, locator, LOB_PERMANENT_CREATED);
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
	    ret = drop_lob_locator (elo->locator);
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
	    ret = add_lob_locator (locator, LOB_PERMANENT_CREATED);
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

      state = find_lob_locator (elo->locator, real_locator);
      switch (state)
	{
	case LOB_TRANSIENT_CREATED:
	  ret = es_delete_file (real_locator);
	  if (ret != NO_ERROR)
	    {
	      return ret;
	    }

	  ret = drop_lob_locator (elo->locator);
	  break;

	case LOB_PERMANENT_CREATED:
	case LOB_PERMANENT_DELETED:
	  ret = change_state_of_locator (elo->locator, NULL, LOB_PERMANENT_DELETED);
	  break;

	case LOB_TRANSIENT_DELETED:
	  break;

	case LOB_NOT_FOUND:
	  ret = add_lob_locator (elo->locator, LOB_TRANSIENT_DELETED);
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
