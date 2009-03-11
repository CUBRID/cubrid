#include <stdlib.h>
#include <stddef.h>

/* -------------------------- */
/* DL MALLOC ADAPTATION LAYER */
/* -------------------------- */
/* use malloc/free instead of mmap/munmap */
#define USE_MALLOC_INSTEAD 1
#define system_malloc malloc
#define system_free my_free

/* disable default DEFAULT_GRANULARITY */
#define DEFAULT_GRANULARITY (32U*1024U)

/* prevent mremap use */
#if !defined(WINDOWS)
#if defined(linux)
#undef linux
#endif
#endif

#define ONLY_MSPACES 1

static int
my_free (void *p)
{
  free (p);
  return 0;
}

#include "malloc_2_8_3.c"

/* -------------------------- */
/* HL_LEA_HEAP IMPLEMENTATION */
/* -------------------------- */

#define LEA_HEAP_BASE_SIZE (64U*1024U)

typedef struct hl_mspace_s HL_MSPACE;
struct hl_mspace_s
{
  mstate ms;
  void *base;
  size_t base_size;
};

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

  hms->ms = create_mspace_with_base (hms->base, hms->base_size, 0);
  if (hms->ms == NULL)
    {
      free (hms);
      return 0;
    }

  /* This has LP64 porting issue... but go along with heap layer anyway */
  return (unsigned int) hms;
}

void
hl_clear_lea_heap (unsigned int heap_id)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      destroy_mspace (hms->ms);
      hms->ms = create_mspace_with_base (hms->base, hms->base_size, 0);
      assert(hms->ms != NULL);
    }
}

void
hl_unregister_lea_heap (unsigned int heap_id)
{
  HL_MSPACE *hms = (HL_MSPACE *) heap_id;

  if (hms != NULL)
    {
      destroy_mspace (hms->ms);
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
