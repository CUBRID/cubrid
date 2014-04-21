/*
 * routines to set the default memory allocator/deallocator used in the library
 */
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>

#include "include/regex38a.h"
#include "regmem.ih"

CUB_REG_MALLOC cub_malloc = NULL;
CUB_REG_REALLOC cub_realloc = NULL;
CUB_REG_FREE cub_free = NULL;

/*
 = typedef void * (*CUB_REG_MALLOC)(void *, size_t);
 = typedef void * (*CUB_REG_REALLOC)(void *, void *, size_t);
 = typedef void (*CUB_REG_FREE)(void *, void *);
 == #include "regex38a.h"
 == extern CUB_REG_MALLOC cub_malloc;
 == extern CUB_REG_REALLOC cub_realloc;
 == extern CUB_REG_FREE cub_free;
 */

/*
 - cub_regset_malloc - set the memory allocator
 = extern void cub_regset_malloc(CUB_REG_MALLOC);
 */
void
cub_regset_malloc (CUB_REG_MALLOC malloc_p)
{
  cub_malloc = malloc_p;
}

/*
 - cub_regset_realloc - set the memory reallocator
 = extern void cub_regset_realloc(CUB_REG_REALLOC);
 */
void
cub_regset_realloc (CUB_REG_REALLOC realloc_p)
{
  cub_realloc = realloc_p;
}

/*
 - cub_regset_free - set the memory deallocator
 = extern void cub_regset_free(CUB_REG_FREE);
 */
void
cub_regset_free (CUB_REG_FREE free_p)
{
  cub_free = free_p;
}

/*
 - cub_malloc_ok - returns true if mem allocator/deallocator/reallocator are set, false otherwise
 == int cub_malloc_ok(void);
 */
int
cub_malloc_ok (void)
{
  return (cub_malloc != NULL && cub_realloc != NULL
	  && cub_free != NULL ? 1 : 0);
}
