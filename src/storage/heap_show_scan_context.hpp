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
 * heap_show_scan_context.hpp - Shared heap SHOW scan context
 */

#ifndef _HEAP_SHOW_SCAN_CONTEXT_HPP_
#define _HEAP_SHOW_SCAN_CONTEXT_HPP_

#include "storage_common.h"

typedef struct heap_show_scan_ctx HEAP_SHOW_SCAN_CTX;
struct heap_show_scan_ctx
{
  HFID *hfids;			/* Array of class HFID */
  int hfids_count;		/* Count of above hfids array */
};

#endif /* _HEAP_SHOW_SCAN_CONTEXT_HPP_ */
