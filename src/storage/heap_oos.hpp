/*
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
 * heap_oos.hpp - Heap-level OOS (Out-of-row Overflow Storage) expansion and eager cleanup
 */

#ifndef _HEAP_OOS_HPP_
#define _HEAP_OOS_HPP_

#include "heap_file.h"
#include "storage_common.h"

extern SCAN_CODE heap_record_replace_oos_oids (THREAD_ENTRY *thread_p, HEAP_GET_CONTEXT *context);

/* Eager OOS cleanup for the non-MVCC (!is_mvcc_op) heap delete/update paths. */
extern int heap_delete_home_delete_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
extern int heap_delete_relocation_delete_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
    const RECDES *forward_recdes);
extern int heap_update_home_delete_replaced_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context);
extern int heap_update_relocation_delete_replaced_oos (THREAD_ENTRY *thread_p, HEAP_OPERATION_CONTEXT *context,
    const RECDES *old_forward_recdes);

#endif /* _HEAP_OOS_HPP_ */
