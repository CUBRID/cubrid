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
 * log_applier_worker_context.cpp - implementation of worker-local apply resources
 */

#include "config.h"

#include "log_applier_worker_context.hpp"
#include "error_manager.h"
#include "file_io.h"		/* IO_MIN_PAGE_SIZE, IO_MAX_PAGE_SIZE */
#include "memory_alloc.h"
#include "oid.h"

#include <assert.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int la_init_recdes_pool (LA_APPLY_WORKER_CONTEXT *context, int page_size, int num_recdes);
static void la_clear_recdes_pool (LA_APPLY_WORKER_CONTEXT *context);
static RECDES *la_assign_overflow_recdes (LA_RECDES_POOL *pool);
static void la_release_overflow_recdes (LA_RECDES_POOL *pool);

/*
 * la_worker_context_init - allocate one worker's unzip buffers and record pool
 *
 * return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *
 *   context(out): context to set up
 *   db_page_size(in): database page size
 *   recdes_pool_size(in): records in the pool
 */
int
la_worker_context_init (LA_APPLY_WORKER_CONTEXT *context, int db_page_size, int recdes_pool_size)
{
  int error = NO_ERROR;

  assert (context != NULL);

  memset (context, 0, sizeof (*context));
  context->worker_idx = -1;
  OID_SET_NULL (&context->marker_class_oid);
  LSA_SET_NULL (&context->batch_upto_lsa);
  LSA_SET_NULL (&context->batch_run_last);

  context->undo_unzip_ptr = log_zip_alloc (db_page_size);
  context->redo_unzip_ptr = log_zip_alloc (db_page_size);
  if (context->undo_unzip_ptr == NULL || context->redo_unzip_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, db_page_size);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_return;
    }

  error = la_init_recdes_pool (context, db_page_size, recdes_pool_size);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  return NO_ERROR;

error_return:
  la_worker_context_final (context);

  return error;
}

/*
 * la_worker_context_final - release a fully or partly initialized worker context
 *
 * return: none
 *
 *   context(in/out): context to tear down
 */
void
la_worker_context_final (LA_APPLY_WORKER_CONTEXT *context)
{
  assert (context != NULL);

  la_clear_recdes_pool (context);

  if (context->batch_ranges != NULL)
    {
      free_and_init (context->batch_ranges);
    }
  context->batch_range_count = 0;
  context->batch_range_capacity = 0;

  if (context->undo_unzip_ptr != NULL)
    {
      log_zip_free (context->undo_unzip_ptr);
      context->undo_unzip_ptr = NULL;
    }

  if (context->redo_unzip_ptr != NULL)
    {
      log_zip_free (context->redo_unzip_ptr);
      context->redo_unzip_ptr = NULL;
    }
}

/*
 * la_clear_recdes_pool - release every allocation owned by a record pool
 *
 * return: none
 *
 *   context(in/out): context whose record pool is cleared
 */
static void
la_clear_recdes_pool (LA_APPLY_WORKER_CONTEXT *context)
{
  LA_RECDES_POOL *pool = &context->recdes_pool;
  int i;

  if (!pool->is_initialized)
    {
      return;
    }

  if (pool->recdes_arr != NULL)
    {
      for (i = 0; i < pool->num_recdes; i++)
	{
	  RECDES *recdes = &pool->recdes_arr[i];

	  if (recdes->area_size > pool->db_page_size)
	    {
	      free_and_init (recdes->data);
	    }
	}
      free_and_init (pool->recdes_arr);
    }

  if (pool->area != NULL)
    {
      free_and_init (pool->area);
    }

  la_release_overflow_recdes (pool);
  if (pool->overflow != NULL)
    {
      free_and_init (pool->overflow);
    }
  pool->overflow_capacity = 0;
  pool->in_use = 0;

  pool->db_page_size = 0;
  pool->next_idx = 0;
  pool->num_recdes = 0;
  pool->is_initialized = false;
}

/*
 * la_realloc_recdes_data - grow one record buffer when the row exceeds its current area
 *
 * return: NO_ERROR, ER_FAILED for an uninitialized pool, or ER_OUT_OF_VIRTUAL_MEMORY
 *
 *   context(in): context that owns the record buffer
 *   recdes(in/out): record buffer to resize
 *   data_size(in): required buffer size
 */
int
la_realloc_recdes_data (LA_APPLY_WORKER_CONTEXT *context, RECDES *recdes, int data_size)
{
  LA_RECDES_POOL *pool;

  assert (context != NULL);
  assert (recdes != NULL);

  pool = &context->recdes_pool;
  if (!pool->is_initialized)
    {
      return ER_FAILED;
    }

  if (recdes->area_size < data_size)
    {
      /* allocated first, so a failure leaves the buffer as it was instead of a NULL area with the old size */
      char *new_data = (char *) malloc (data_size);

      if (new_data == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, data_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (recdes->area_size > pool->db_page_size)
	{
	  free_and_init (recdes->data);
	}

      recdes->data = new_data;
      recdes->area_size = data_size;
    }
  recdes->length = 0;

  return NO_ERROR;
}

/*
 * la_assign_overflow_recdes - allocate a record buffer outside the ring
 *
 * return: a reset record buffer, or NULL with an error set when memory runs out
 *
 *   pool(in/out): pool whose ring buffers are all in use
 *
 * Note: the buffer and its page-sized record area are one allocation, so la_realloc_recdes_data and
 *       la_release_oversized_recdes treat it as they treat a ring buffer.
 */
static RECDES *
la_assign_overflow_recdes (LA_RECDES_POOL *pool)
{
  RECDES *recdes;
  size_t header_size = DB_ALIGN (sizeof (RECDES), MAX_ALIGNMENT);
  char *block;

  if (pool->overflow_count >= pool->overflow_capacity)
    {
      int new_capacity = (pool->overflow_capacity == 0) ? pool->num_recdes : pool->overflow_capacity * 2;
      RECDES **new_overflow = (RECDES **) realloc (pool->overflow, sizeof (RECDES *) * new_capacity);

      if (new_overflow == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (RECDES *) * new_capacity);
	  return NULL;
	}
      pool->overflow = new_overflow;
      pool->overflow_capacity = new_capacity;
    }

  block = (char *) malloc (header_size + pool->db_page_size);
  if (block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, header_size + pool->db_page_size);
      return NULL;
    }

  recdes = (RECDES *) block;
  recdes->data = block + header_size;
  recdes->area_size = pool->db_page_size;
  recdes->length = 0;
  recdes->type = 0;
  pool->overflow[pool->overflow_count++] = recdes;

  if (!pool->overflow_noted)
    {
      char message[256];

      pool->overflow_noted = true;
      /* once per pool: shows that a flush unit needed more buffers than the ring holds */
      snprintf (message, sizeof (message), "apply worker record pool: %d buffers in use before a flush and the ring "
		"has %d; the rest are allocated on their own until the next flush", pool->in_use, pool->num_recdes);
      er_stack_push ();
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1, message);
      er_stack_pop ();
    }

  return recdes;
}

/*
 * la_release_overflow_recdes - free the record buffers allocated outside the ring
 *
 * return: none
 *
 *   pool(in/out): pool whose buffers no replication object refers to any more
 */
static void
la_release_overflow_recdes (LA_RECDES_POOL *pool)
{
  size_t header_size = DB_ALIGN (sizeof (RECDES), MAX_ALIGNMENT);
  int i;

  for (i = 0; i < pool->overflow_count; i++)
    {
      RECDES *recdes = pool->overflow[i];

      if (recdes->data != (char *) recdes + header_size)
	{
	  /* grown by la_realloc_recdes_data */
	  free_and_init (recdes->data);
	}
      free_and_init (pool->overflow[i]);
    }
  pool->overflow_count = 0;
}

/*
 * la_assign_recdes_from_pool - return the next record buffer from one worker's ring
 *
 * return: a reset record buffer, or NULL when the pool is not initialized or memory runs out (error set)
 *
 *   context(in/out): context that owns the record pool
 *
 * Note: a buffer stays referred to by its replication object until the object is flushed. Once num_recdes
 *       buffers are in use, the next ring buffer may still be referred to, so a buffer is allocated outside
 *       the ring instead until la_release_oversized_recdes says nothing refers to them.
 */
RECDES *
la_assign_recdes_from_pool (LA_APPLY_WORKER_CONTEXT *context)
{
  LA_RECDES_POOL *pool;
  RECDES *recdes;

  assert (context != NULL);

  pool = &context->recdes_pool;
  if (!pool->is_initialized)
    {
      return NULL;
    }

  if (pool->in_use >= pool->num_recdes)
    {
      recdes = la_assign_overflow_recdes (pool);
      if (recdes != NULL)
	{
	  pool->in_use++;
	}
      return recdes;
    }

  recdes = &pool->recdes_arr[pool->next_idx];
  assert (recdes->data != NULL);

  if (recdes->area_size > pool->db_page_size)
    {
      free_and_init (recdes->data);
      recdes->data = pool->area + pool->db_page_size * pool->next_idx;
      recdes->area_size = pool->db_page_size;
    }

  recdes->length = 0;
  pool->next_idx = (pool->next_idx + 1) % pool->num_recdes;
  pool->in_use++;

  return recdes;
}

/*
 * la_release_oversized_recdes - return the pool's buffers once no replication object refers to them
 *
 * return: none
 *
 *   context(in/out): context whose record buffers are reset
 *
 * Note: call only after no replication object refers to a record in this pool. Oversized ring buffers go back
 *       to their page-sized areas, buffers allocated outside the ring are freed, and the ring may be reused
 *       from any position again.
 */
void
la_release_oversized_recdes (LA_APPLY_WORKER_CONTEXT *context)
{
  LA_RECDES_POOL *pool;
  int i;

  assert (context != NULL);

  pool = &context->recdes_pool;
  if (!pool->is_initialized)
    {
      return;
    }

  for (i = 0; i < pool->num_recdes; i++)
    {
      RECDES *recdes = &pool->recdes_arr[i];

      if (recdes->area_size > pool->db_page_size)
	{
	  free_and_init (recdes->data);
	  recdes->data = pool->area + pool->db_page_size * i;
	  recdes->area_size = pool->db_page_size;
	}
    }

  la_release_overflow_recdes (pool);
  pool->in_use = 0;
}

/*
 * la_init_recdes_pool - allocate or reset one worker's record pool
 *
 * return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *
 *   context(in/out): context that owns the pool
 *   page_size(in): initial size of each record buffer
 *   num_recdes(in): number of record buffers
 */
static int
la_init_recdes_pool (LA_APPLY_WORKER_CONTEXT *context, int page_size, int num_recdes)
{
  LA_RECDES_POOL *pool = &context->recdes_pool;
  int i;

  assert (page_size >= IO_MIN_PAGE_SIZE && page_size <= IO_MAX_PAGE_SIZE);
  assert (num_recdes > 0);

  if (!pool->is_initialized)
    {
      pool->area = (char *) malloc (page_size * num_recdes);
      if (pool->area == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, page_size * num_recdes);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      pool->recdes_arr = (RECDES *) malloc (sizeof (RECDES) * num_recdes);
      if (pool->recdes_arr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (RECDES) * num_recdes);
	  free_and_init (pool->area);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      for (i = 0; i < num_recdes; i++)
	{
	  RECDES *recdes = &pool->recdes_arr[i];

	  recdes->data = pool->area + page_size * i;
	  recdes->area_size = page_size;
	  recdes->length = 0;
	}

      pool->db_page_size = page_size;
      pool->num_recdes = num_recdes;
      pool->is_initialized = true;
    }
  else if (pool->db_page_size != page_size || pool->num_recdes != num_recdes)
    {
      la_clear_recdes_pool (context);
      return la_init_recdes_pool (context, page_size, num_recdes);
    }

  pool->next_idx = 0;
  pool->in_use = 0;

  return NO_ERROR;
}
