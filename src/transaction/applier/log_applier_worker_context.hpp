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
 * log_applier_worker_context.hpp - interface for worker-local apply resources
 *
 * Each worker owns one context for log decompression, reconstructed records and marker execution.
 * The context is created after the worker opens its server connection and released before that
 * connection is closed.
 */

#ifndef _LOG_APPLIER_WORKER_CONTEXT_HPP_
#define _LOG_APPLIER_WORKER_CONTEXT_HPP_

#include "storage_common.h"	/* RECDES */
#include "log_compress.h"	/* LOG_ZIP */
#include "log_lsa.hpp"		/* LOG_LSA */

/* Record buffers of one worker. Replication items collect until one flush, so their records must remain
 * allocated together. A record larger than one page temporarily replaces its pool slot allocation. */
typedef struct la_recdes_pool LA_RECDES_POOL;
struct la_recdes_pool
{
  RECDES *recdes_arr;
  char *area;
  int next_idx;
  int db_page_size;
  int num_recdes;
  bool is_initialized;
};

/* State used by one worker while it reconstructs and applies records. Each worker owns one instance. */
typedef struct la_apply_worker_context LA_APPLY_WORKER_CONTEXT;
struct la_apply_worker_context
{
  int worker_idx;
  INT16 rec_type;
  LOG_ZIP *undo_unzip_ptr;
  LOG_ZIP *redo_unzip_ptr;
  LA_RECDES_POOL recdes_pool;
  OID marker_class_oid;	/* identifies fatal marker errors in a mixed row batch */
  /* markers of the open slave transaction, written just before it commits (la_add_batch_markers) */
  LOG_LSA batch_upto_lsa;	/* single worker: commit LSA of the last task that applied a row */
  bool batch_has_rows;		/* single worker: a task in the transaction applied a row */
  LOG_LSA *batch_lsas;		/* commit LSAs to list, one per task that applied a row; used with several workers */
  int batch_lsa_count;
  int batch_lsa_capacity;
  /* tasks that applied at least one item into the open slave transaction, the current one included; reset when
   * that transaction commits or rolls back. When the server rolls the transaction back on its own, these are the
   * tasks whose changes were lost with it. */
  int tran_applied_tasks;
};

extern int la_worker_context_init (LA_APPLY_WORKER_CONTEXT *context, int db_page_size, int recdes_pool_size);
extern void la_worker_context_final (LA_APPLY_WORKER_CONTEXT *context);

extern RECDES *la_assign_recdes_from_pool (LA_APPLY_WORKER_CONTEXT *context);
extern int la_realloc_recdes_data (LA_APPLY_WORKER_CONTEXT *context, RECDES *recdes, int data_size);
extern void la_release_oversized_recdes (LA_APPLY_WORKER_CONTEXT *context);

#endif /* _LOG_APPLIER_WORKER_CONTEXT_HPP_ */
