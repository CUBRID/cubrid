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
 * log_compress.c - log compression functions
 *
 * Note: Using lz4 library
 */

#ident "$Id$"

#include <string.h>
#include <assert.h>

#include "log_compress.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "perf_monitor.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
  int zip_len = 0;
  LOG_ZIP_SIZE_T buf_size;
  bool compressed;
#if defined (SERVER_MODE) || defined (SA_MODE)
  PERF_UTIME_TRACKER time_track;
#endif

  assert (length > 0 && data != NULL);
  assert (log_zip != NULL);

  if (length > LZ4_MAX_INPUT_SIZE)
    {
      /* Can't compress beyonds max LZ4 max input size. */
      return false;
    }

  log_zip->data_length = 0;

  buf_size = LOG_ZIP_BUF_SIZE (length);
  if (buf_size > log_zip->buf_size)
    {
      if (log_zip->log_data)
	{
	  free_and_init (log_zip->log_data);
	}

      log_zip->log_data = (char *) malloc (buf_size);
      if (log_zip->log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_size);
	}
      log_zip->buf_size = buf_size;
    }

  if (log_zip->log_data == NULL)
    {
      log_zip->data_length = 0;
      log_zip->buf_size = 0;
      return false;
    }

#if defined (SERVER_MODE) || defined (SA_MODE)
  PERF_UTIME_TRACKER_START (NULL, &time_track);
#endif

  compressed = false;

  /* save original data length */
  memcpy (log_zip->log_data, &length, sizeof (LOG_ZIP_SIZE_T));

  zip_len =
    LZ4_compress_default ((const char *) data, log_zip->log_data + sizeof (LOG_ZIP_SIZE_T), length,
			  buf_size - sizeof (LOG_ZIP_SIZE_T));
  if (zip_len > 0)
    {
      log_zip->data_length = (LOG_ZIP_SIZE_T) zip_len + sizeof (LOG_ZIP_SIZE_T);
      /* if the compressed data length >= orginal length, then it means that compression failed */
      if (log_zip->data_length < length)
	{
	  compressed = true;
	}
    }

#if defined (SERVER_MODE) || defined (SA_MODE)
  PERF_UTIME_TRACKER_TIME (NULL, &time_track, PSTAT_LOG_LZ4_COMPRESS_TIME_COUNTERS);
#endif

  return compressed;
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
  int unzip_len;
  LOG_ZIP_SIZE_T buf_size;
  bool decompressed;
#if defined (SERVER_MODE) || defined (SA_MODE)
  PERF_UTIME_TRACKER time_track;
#endif

  assert (length > 0 && data != NULL);
  assert (log_unzip != NULL);

  /* get original legnth from the compressed data */
  memcpy (&buf_size, data, sizeof (LOG_ZIP_SIZE_T));

  if (buf_size <= 0)
    {
      return false;
    }

  length -= sizeof (LOG_ZIP_SIZE_T);

  if (buf_size > log_unzip->buf_size)
    {
      if (log_unzip->log_data)
	{
	  free_and_init (log_unzip->log_data);
	}

      log_unzip->log_data = (char *) malloc (buf_size);
      if (log_unzip->log_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_size);
	}
      log_unzip->buf_size = buf_size;
    }

  if (log_unzip->log_data == NULL)
    {
      log_unzip->data_length = 0;
      log_unzip->buf_size = 0;
      return false;
    }

#if defined (SERVER_MODE) || defined (SA_MODE)
  PERF_UTIME_TRACKER_START (NULL, &time_track);
#endif

  decompressed = false;

  unzip_len =
    LZ4_decompress_safe ((const char *) data + sizeof (LOG_ZIP_SIZE_T), (char *) log_unzip->log_data, length, buf_size);
  if (unzip_len >= 0)
    {
      log_unzip->data_length = (LOG_ZIP_SIZE_T) unzip_len;
      /* if the uncompressed data length != original length, then it means that uncompression failed */
      if (unzip_len == buf_size)
	{
	  decompressed = true;
	}
    }

#if defined (SERVER_MODE) || defined (SA_MODE)
  PERF_UTIME_TRACKER_TIME (NULL, &time_track, PSTAT_LOG_LZ4_DECOMPRESS_TIME_COUNTERS);
#endif

  return decompressed;
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
log_diff (LOG_ZIP_SIZE_T undo_length, const void *undo_data, LOG_ZIP_SIZE_T redo_length, void *redo_data)
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
 *
 * Note:
 */
LOG_ZIP *
log_zip_alloc (LOG_ZIP_SIZE_T size)
{
  LOG_ZIP *log_zip = NULL;
  LOG_ZIP_SIZE_T buf_size = 0;

  assert (size <= LZ4_MAX_INPUT_SIZE);

  buf_size = LOG_ZIP_BUF_SIZE (size);
  log_zip = (LOG_ZIP *) malloc (sizeof (LOG_ZIP));
  if (log_zip == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LOG_ZIP));

      return NULL;
    }
  log_zip->data_length = 0;

  log_zip->log_data = (char *) malloc ((size_t) buf_size);
  if (log_zip->log_data == NULL)
    {
      free_and_init (log_zip);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_size);
      return NULL;
    }
  log_zip->buf_size = buf_size;

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

  free_and_init (log_zip);
}
