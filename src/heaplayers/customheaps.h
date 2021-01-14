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
// -*- C++ -*-

#ifndef _CUSTOMHEAPS_H_
#define _CUSTOMHEAPS_H_

#include "config.h"

extern UINTPTR hl_register_fixed_heap (int chunk_size);
extern void hl_unregister_fixed_heap (UINTPTR heap_id);
extern void *hl_fixed_alloc (UINTPTR heap_id, size_t sz);
extern void hl_fixed_free (UINTPTR heap_id, void *ptr);

extern UINTPTR hl_register_ostk_heap (int chunk_size);
extern void hl_unregister_ostk_heap (UINTPTR heap_id);
extern void *hl_ostk_alloc (UINTPTR heap_id, size_t sz);
extern void hl_ostk_free (UINTPTR heap_id, void *ptr);

extern UINTPTR hl_register_lea_heap (void);
extern void hl_unregister_lea_heap (UINTPTR heap_id);
extern void *hl_lea_alloc (UINTPTR heap_id, size_t sz);
extern void *hl_lea_realloc (UINTPTR heap_id, void *ptr, size_t sz);
extern void hl_lea_free (UINTPTR heap_id, void *ptr);
extern void hl_clear_lea_heap (UINTPTR heap_id);

#endif /* _CUSTOMHEAPS_H_ */
