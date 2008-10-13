// -*- C++ -*-

#ifndef _CUSTOMHEAPS_H_ 
#define _CUSTOMHEAPS_H_

extern unsigned int hl_register_fixed_heap (int chunk_size);
extern void hl_unregister_fixed_heap (unsigned int heap_id);
extern void * hl_fixed_alloc (unsigned int heap_id, size_t sz);
extern void hl_fixed_free (unsigned int heap_id, void * ptr);

extern unsigned int hl_register_ostk_heap (int chunk_size);
extern void hl_clear_ostk_heap (unsigned int heap_id);
extern void hl_unregister_ostk_heap (unsigned int heap_id);
extern void * hl_ostk_alloc (unsigned int heap_id, size_t sz);
extern void * hl_ostk_realloc (unsigned int heap_id, void *ptr, size_t sz);
extern void hl_ostk_free (unsigned int heap_id, void * ptr);

extern unsigned int hl_register_kingsley_heap ();
extern void hl_unregister_kingsley_heap (unsigned int heap_id);
extern void * hl_kingsley_alloc (unsigned int heap_id, size_t sz);
extern void * hl_kingsley_realloc (unsigned int heap_id, void *ptr, size_t sz);
extern void hl_kingsley_free (unsigned int heap_id, void * ptr);

#endif /* _CUSTOMHEAPS_H_ */
