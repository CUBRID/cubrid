#include <stdlib.h>
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* DL MALLOC ADAPTATION AND MODIFICATION LAYER */
/* -------------------------------------------------------------------------- */
/* 
 * use malloc/free instead of mmap/munmap 
 */
#define USE_MALLOC_INSTEAD 1
#define system_malloc my_malloc
#define system_free my_free

static void *
my_malloc (size_t sz)
{
  return malloc (sz);
}

static int
my_free (void *p)
{
  free (p);
  return 0;
}

/* 
 * override  default DEFAULT_GRANULARITY 
 */
#define DEFAULT_GRANULARITY (32U*1024U)

/* 
 * prevent mremap use 
 */
#if !defined(WINDOWS)
#if defined(linux)
#undef linux
#endif
#endif

/* 
 * use mspace only 
 */
#define ONLY_MSPACES 1

/* 
 * Fix the problem that mmaped (malloced when USE_MALLOC_INSTEAD is 1) 
 * memory region (which is returned by mmap_alloc function) is not 
 * automatically freed when destroy_mspace is called. 
 */
typedef struct mmap_trace_h_s MMAP_TRACE_H;
struct mmap_trace_h_s
{
  MMAP_TRACE_H *next;
  MMAP_TRACE_H *prev;
  void *ptr;
};

#define MMAP_TRACE_H_SIZE sizeof(MMAP_TRACE_H)

#define MMAP_TRACE_H_INIT(h, p) do {    \
  MMAP_TRACE_H *__h = (h);              \
  (__h)->next = (__h)->prev = (__h);    \
  (__h)->ptr = (p);                     \
} while (0)

#define MMAP_TRACE_H_REMOVE(h) do {     \
  MMAP_TRACE_H *__h = (h);              \
  (__h)->next->prev = (__h)->prev;      \
  (__h)->prev->next = (__h)->next;      \
} while (0)

#define MMAP_TRACE_H_ADD(p, h) do {     \
  MMAP_TRACE_H *__ih = (p);             \
  MMAP_TRACE_H *__bh = (h);             \
  (__ih)->prev = (__bh);                \
  (__ih)->next = (__bh)->next;          \
  (__bh)->next->prev = (__ih);          \
  (__bh)->next = (__ih);                \
} while (0)

static void mmap_called (void *m, void *ptr, MMAP_TRACE_H * h);
static void munmap_is_to_be_called (void *m, void *ptr, MMAP_TRACE_H * h);

#define ENABLE_SEPARATE_MMAP_EVENT_TRACE
#include "malloc_2_8_3.c"

/* ------------------------------------------------------------------------- */
/* HL_LEA_HEAP IMPLEMENTATION */
/* ------------------------------------------------------------------------- */

typedef struct hl_mspace_s HL_MSPACE;
struct hl_mspace_s
{
  mstate ms;
  void *base;
  size_t base_size;
  MMAP_TRACE_H header;
};

/* Memory layout of HL_MSPACE
   +-----------+
   | HL_MSPACE |
   +-----------+ <-- 8 byte aligned. msp
   |           |     mstate (or mspace) m = chunk2mem(msp)
   +-----------+ <-- m
   |  mstate   |
   | ......... |
                 
 */
#define mspace2hlmspace(m) \
  (HL_MSPACE *)((char *)mem2chunk(m) - ((sizeof (HL_MSPACE) + 7U) & ~7U))

static void
mmap_called (void *m, void *ptr, MMAP_TRACE_H * h)
{
  HL_MSPACE *hms = mspace2hlmspace (m);
  MMAP_TRACE_H_INIT (h, ptr);
  MMAP_TRACE_H_ADD (h, &hms->header);
}

static void
munmap_is_to_be_called (void *m, void *ptr, MMAP_TRACE_H * h)
{
  HL_MSPACE *hms = mspace2hlmspace (m);
  assert (h->ptr == ptr);
  MMAP_TRACE_H_REMOVE (h);
}

/* EXPORTED FUNCTIONS */
#define LEA_HEAP_BASE_SIZE (64U*1024U)

unsigned int
hl_register_lea_heap ()
{
  HL_MSPACE *hms;

  hms = malloc (LEA_HEAP_BASE_SIZE);
  if (hms == NULL)
    {
      return 0;
    }

  hms->base = (char *) hms + ((sizeof (*hms) + 7U) & ~7U);
  hms->base_size =
    LEA_HEAP_BASE_SIZE - (size_t) ((char *) hms->base - (char *) hms);
  MMAP_TRACE_H_INIT (&hms->header, NULL);

  hms->ms = create_mspace_with_base (hms->base, hms->base_size, 0);
  if (hms->ms == NULL)
    {
      free (hms);
      return 0;
    }

  /* This has LP64 porting issue... but go along with heap layer anyway */
  return (unsigned int) hms;
}

static void
destroy_mspace_internal (HL_MSPACE * hms)
{
  destroy_mspace (hms->ms);
  hms->ms = NULL;
  /* remove unmapped segments */
  while (hms->header.next != &hms->header)
    {
      MMAP_TRACE_H *h = hms->header.next;
      MMAP_TRACE_H_REMOVE (h);	/* unlink from doubley linked list */
      my_free (h->ptr);		/* no need to free h. h is embedded */
    }
}


void
hl_clear_lea_heap (unsigned int heap_id)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      destroy_mspace_internal (hms);
      hms->ms = create_mspace_with_base (hms->base, hms->base_size, 0);
      assert (hms->ms != NULL);
    }
}

void
hl_unregister_lea_heap (unsigned int heap_id)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      destroy_mspace_internal (hms);
      free (hms);
    }
}

void *
hl_lea_alloc (unsigned int heap_id, size_t sz)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      return mspace_malloc (hms->ms, sz);
    }
  return NULL;
}

void *
hl_lea_realloc (unsigned int heap_id, void *ptr, size_t sz)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      return mspace_realloc (hms->ms, ptr, sz);
    }
  return NULL;
}

void
hl_lea_free (unsigned int heap_id, void *ptr)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      mspace_free (hms->ms, ptr);
    }
}
