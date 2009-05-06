// -*- C++ -*-

#ifndef _CUSTOMHEAPS_H_
#define _CUSTOMHEAPS_H_

#include "config.h"

extern UINTPTR hl_register_fixed_heap (int chunk_size);
extern void hl_unregister_fixed_heap (UINTPTR heap_id);
extern void *hl_fixed_alloc (UINTPTR heap_id, size_t sz);
extern void hl_fixed_free (UINTPTR heap_id, void *ptr);

extern UINTPTR hl_register_ostk_heap (int chunk_size);
extern void hl_clear_ostk_heap (UINTPTR heap_id);
extern void hl_unregister_ostk_heap (UINTPTR heap_id);
extern void *hl_ostk_alloc (UINTPTR heap_id, size_t sz);
extern void *hl_ostk_realloc (UINTPTR heap_id, void *ptr, size_t sz);
extern void hl_ostk_free (UINTPTR heap_id, void *ptr);

extern UINTPTR hl_register_kingsley_heap (void);
extern void hl_unregister_kingsley_heap (UINTPTR heap_id);
extern void *hl_kingsley_alloc (UINTPTR heap_id, size_t sz);
extern void *hl_kingsley_realloc (UINTPTR heap_id, void *ptr, size_t sz);
extern void hl_kingsley_free (UINTPTR heap_id, void *ptr);

extern UINTPTR hl_register_lea_heap (void);
extern void hl_unregister_lea_heap (UINTPTR heap_id);
extern void *hl_lea_alloc (UINTPTR heap_id, size_t sz);
extern void *hl_lea_realloc (UINTPTR heap_id, void *ptr, size_t sz);
extern void hl_lea_free (UINTPTR heap_id, void *ptr);
extern void hl_clear_lea_heap (UINTPTR heap_id);

#endif /* _CUSTOMHEAPS_H_ */
