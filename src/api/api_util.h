#ifndef _API_UTIL_H_
#define _API_UTIL_H_
#include <pthread.h>
#include <stdio.h>

/* doubly linked list header */
typedef struct dlisth_s dlisth;
struct dlisth_s
{
  dlisth *next;
  dlisth *prev;
};

/* dlisth_map() hook function type definition */
typedef int (*dlist_map_func) (dlisth * h, void *arg, int *cont);

extern int dlisth_map (dlisth * h, dlist_map_func func, void *arg);

#define dlisth_init(h)         \
do {                           \
  dlisth *__h = (h);           \
  (__h)->next = (__h)->prev = (__h); \
} while (0)

#define dlisth_is_empty(h) ((h)->next == (h) && (h)->prev == (h))

#define dlisth_delete(h_)              \
do {                                   \
  dlisth *__h = (h_);                  \
  (__h)->next->prev = (__h)->prev;     \
  (__h)->prev->next = (__h)->next;     \
  (__h)->next = (__h)->prev = (__h);   \
} while(0)

#define dlisth_insert_before(ih, bh)   \
do {                                   \
  dlisth *__ih = (ih);                 \
  dlisth *__bh = (bh);                 \
  (__ih)->next = (__bh);               \
  (__ih)->prev = (__bh)->prev;         \
  (__bh)->prev->next = (__ih);         \
  (__bh)->prev = (__ih);               \
} while (0)

#define dlisth_insert_after(ih, bh)    \
do {                                   \
  dlisth *__ih = (ih);                 \
  dlisth *__bh = (bh);                 \
  (__ih)->prev = (__bh);               \
  (__ih)->next = (__bh)->next;         \
  (__bh)->next->prev = (__ih);         \
  (__bh)->next = (__ih);               \
} while (0)


/* generic static hash table */
typedef struct hash_table_s hash_table;
/* key compare function */
typedef int (*ht_comparef) (void *key1, void *key2, int *r);
/* key hash function */
typedef int (*ht_hashf) (void *key, unsigned int *rv);
/* key function : get pointer to the key from element */
typedef int (*ht_keyf) (void *elem, void **rk);
/* element destroy function */
typedef void (*ht_destroyf) (void *elem);

extern int hash_new (int bucket_sz,
		     ht_hashf hashf,
		     ht_keyf keyf, ht_comparef comparef, hash_table ** ht);
extern void hash_destroy (hash_table * ht, ht_destroyf dtor);
extern int hash_lookup (hash_table * ht, void *key, void **elem);
extern int hash_insert (hash_table * ht, void *elem);
extern int hash_delete (hash_table * ht, void *key, void **elem);

/* misc. */
#define API_MUTEX pthread_mutex_t
#define API_MUTEX_INIT(m) pthread_mutex_init(m,NULL)
#define API_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define API_LOCK(m) pthread_mutex_lock(m)
#define API_UNLOCK(m) pthread_mutex_unlock(m)
#define API_TRYLOCK(m) pthread_mutex_trylock(m)
#define API_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#define API_ONCE_TYPE pthread_once_t
#define API_ONCE_INIT PTHREAD_ONCE_INIT
#define API_ONCE_FUNC(t,r) pthread_once(t,r)

/* malloc/calloc/free hook functions for memory check */
#if 1 
#define API_CALLOC(n,s) api_calloc((n),(s),__FILE__,__LINE__)
#define API_MALLOC(s) api_malloc((s),__FILE__,__LINE__)
#define API_FREE(p) api_free((p), __FILE__, __LINE__)
extern void * api_calloc (size_t nmemb, size_t size, const char *file, int line);
extern void * api_malloc (size_t size, const char *file, int line);
extern void api_free (void *ptr, const char *file, int line);
extern int api_check_memory (FILE *fp);

/* api_malloc_dhook_flag_set is global flag used to enable malloc debug */
extern int api_malloc_dhook_flag_set;
#else
#define API_CALLOC(n,s) calloc(n,s)
#define API_MALLOC(s) malloc(s)
#define API_FREE(p) free(p)
#endif

#endif /* _API_UTIL_H_*/
