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
 * log_compress.c - log compression functions
 *
 * Note: Using lzo2 library
 */

#ident "$Id$"

#include <string.h>
#include <assert.h>

#include "log_compress.h"
#include "error_manager.h"

/*
 * log_zip - compress(zip) log data into LOG_ZIP
 *   return: true on success, false on failure
 *   log_zip(in/out): LOG_ZIP structure allocated by log_zip_alloc
 *   length(in): length of given data
 *   data(in): log data to be compressed
 */
bool
log_zip (LOG_ZIP * log_zip, LOG_ZIP_SIZE_T length, const void *data)
{
  lzo_uint zip_len = 0;
  LOG_ZIP_SIZE_T buf_size;
  int rc;

  assert (length > 0 && data != NULL);
  assert (log_zip != NULL);

  log_zip->data_length = 0;

  buf_size = LOG_ZIP_BUF_SIZE (length);
  if (buf_size > log_zip->buf_size)
    {
      if (log_zip->log_data)
	{
	  free_and_init (log_zip->log_data);
	}

      log_zip->log_data = (unsigned char *) malloc (buf_size);
      if (log_zip->log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, buf_size);
	}
      log_zip->buf_size = buf_size;
    }

  if (log_zip->log_data == NULL)
    {
      log_zip->data_length = 0;
      log_zip->buf_size = 0;
      return false;
    }

  /* save original data length */
  memcpy (log_zip->log_data, &length, sizeof (LOG_ZIP_SIZE_T));

  rc = lzo1x_1_compress ((lzo_bytep) data,
			 (lzo_uint) length,
			 log_zip->log_data + sizeof (LOG_ZIP_SIZE_T),
			 &zip_len, log_zip->wrkmem);
  if (rc == LZO_E_OK)
    {
      log_zip->data_length = zip_len + sizeof (LOG_ZIP_SIZE_T);
      /* if the compressed data length >= orginal length,
       * then it means that compression is failed */
      if (log_zip->data_length < length)
	{
	  return true;
	}
    }
  return false;
}

/*
 * log_unzip - decompress(unzip) log data into LOG_ZIP
 *   return: true on success, false on failure
 *   log_unzip(out): LOG_ZIP structure allocated by log_zip_alloc
 *   length(in): length of given data
 *   data(out): compressed log data
 */
bool
log_unzip (LOG_ZIP * log_unzip, LOG_ZIP_SIZE_T length, void *data)
{
  lzo_uint unzip_len;
  LOG_ZIP_SIZE_T org_len;
  LOG_ZIP_SIZE_T buf_size;
  int rc;

  assert (length > 0 && data != NULL);
  assert (log_unzip != NULL);

  /* get original legnth from the compressed data */
  memcpy (&org_len, data, sizeof (LOG_ZIP_SIZE_T));

  if (org_len <= 0)
    return false;
  unzip_len = (lzo_uint) org_len;
  buf_size = LOG_ZIP_BUF_SIZE (org_len);
  length -= sizeof (LOG_ZIP_SIZE_T);

  if (buf_size > log_unzip->buf_size)
    {
      if (log_unzip->log_data)
	{
	  free_and_init (log_unzip->log_data);
	}

      log_unzip->log_data = (unsigned char *) malloc (buf_size);
      if (log_unzip->log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, buf_size);
	}
      log_unzip->buf_size = buf_size;
    }

  if (log_unzip->log_data == NULL)
    {
      log_unzip->data_length = 0;
      log_unzip->buf_size = 0;
      return false;
    }

  rc = lzo1x_decompress_safe ((lzo_bytep) data + sizeof (LOG_ZIP_SIZE_T),
			      (lzo_uint) length, log_unzip->log_data,
			      &unzip_len, NULL);

  if (rc == LZO_E_OK)
    {
      log_unzip->data_length = unzip_len;
      /* if the uncompressed data length != original length,
       * then it means that uncompression is failed */
      if (unzip_len == (lzo_uint) org_len)
	{
	  return true;
	}
    }
  return false;
}

/*
 * log_diff - make log diff - redo data XORed with undo data
 *   return: true
 *   undo_length(in): length of undo data
 *   undo_data(in): undo log data
 *   redo_length(in): length of redo data
 *   redo_data(in/out) redo log data; set as side effect
 */
bool
log_diff (LOG_ZIP_SIZE_T undo_length, const void *undo_data,
	  LOG_ZIP_SIZE_T redo_length, void *redo_data)
{
  LOG_ZIP_SIZE_T i, size;
  unsigned char *p, *q;

  assert (undo_length > 0 && undo_data != NULL);
  assert (redo_length > 0 && redo_data != NULL);

  size = MIN (undo_length, redo_length);

  /* redo = redo xor undo */
  p = (unsigned char *) redo_data;
  q = (unsigned char *) undo_data;
  for (i = 0; i < size; i++)
    {
      *(p++) ^= *(q++);
    }

  return true;
}

/*
 * log_zip_alloc - allocate LOG_ZIP structure
 *   return: LOG_ZIP structure or NULL if error
 *   length(in): log_zip data buffer to be allocated
 *   is_zip(in): to be used zip or not
 *
 * Note:
 */
LOG_ZIP *
log_zip_alloc (LOG_ZIP_SIZE_T size, bool is_zip)
{
  LOG_ZIP *log_zip = NULL;
  LOG_ZIP_SIZE_T buf_size = 0;

  buf_size = LOG_ZIP_BUF_SIZE (size);
  log_zip = (LOG_ZIP *) malloc (sizeof (LOG_ZIP));
  if (log_zip == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (LOG_ZIP));

      return NULL;
    }
  log_zip->data_length = 0;

  log_zip->log_data = (lzo_bytep) malloc ((size_t) buf_size);
  if (log_zip->log_data == NULL)
    {
      free_and_init (log_zip);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, buf_size);
      return NULL;
    }
  log_zip->buf_size = buf_size;

  if (is_zip)
    {
      /* lzo method is best speed : LZO1X_1_MEM_COMPRESS */
      log_zip->wrkmem = (lzo_bytep) malloc (LZO1X_1_MEM_COMPRESS);
      if (log_zip->wrkmem == NULL)
	{
	  free_and_init (log_zip->log_data);
	  free_and_init (log_zip);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, LZO1X_1_MEM_COMPRESS);
	  return NULL;
	}
    }
  else
    {
      log_zip->wrkmem = NULL;
    }

  return log_zip;
}

/*
 * log_zip_free - free LOG_ZIP structure
 *   return: none
 *   log_zip(in): LOG_ZIP structure to be freed
 */
void
log_zip_free (LOG_ZIP * log_zip)
{
  assert (log_zip != NULL);
  if (log_zip->log_data)
    {
      free_and_init (log_zip->log_data);
    }
  if (log_zip->wrkmem)
    {
      free_and_init (log_zip->wrkmem);
    }
  free_and_init (log_zip);
}
