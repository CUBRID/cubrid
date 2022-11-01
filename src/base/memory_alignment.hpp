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
 * memory_alignment.hpp - Definition of memory alignments
 */

#ifndef _MEMORY_ALGINMENT_H_
#define _MEMORY_ALGINMENT_H_

/* Ceiling of positive division */
#define CEIL_PTVDIV(dividend, divisor) \
        (((dividend) == 0) ? 0 : (((dividend) - 1) / (divisor)) + 1)

/* Make sure that sizeof returns and integer, so I can use in the operations */
#define DB_SIZEOF(val)          (sizeof(val))

/*
 * Macros related to alignments
 */
#define CHAR_ALIGNMENT          sizeof(char)
#define SHORT_ALIGNMENT         sizeof(short)
#define INT_ALIGNMENT           sizeof(int)
#define LONG_ALIGNMENT          sizeof(long)
#define FLOAT_ALIGNMENT         sizeof(float)
#define DOUBLE_ALIGNMENT        sizeof(double)
#if __WORDSIZE == 32
#define PTR_ALIGNMENT		4
#else
#define PTR_ALIGNMENT		8
#endif
#define MAX_ALIGNMENT           DOUBLE_ALIGNMENT

#if defined(NDEBUG)
#define PTR_ALIGN(addr, boundary) \
        ((char *)((((UINTPTR)(addr) + ((UINTPTR)((boundary)-1)))) \
                  & ~((UINTPTR)((boundary)-1))))
#else
#define PTR_ALIGN(addr, boundary) \
        (memset((void*)(addr), 0,\
	   DB_WASTED_ALIGN((UINTPTR)(addr), (UINTPTR)(boundary))),\
        (char *)((((UINTPTR)(addr) + ((UINTPTR)((boundary)-1)))) \
                  & ~((UINTPTR)((boundary)-1))))
#endif

#define DB_ALIGN(offset, align) \
        (((offset) + (align) - 1) & ~((align) - 1))

#define DB_ALIGN_BELOW(offset, align) \
        ((offset) & ~((align) - 1))

#define DB_WASTED_ALIGN(offset, align) \
        (DB_ALIGN((offset), (align)) - (offset))

#define DB_ATT_ALIGN(offset) \
        (((offset) + (INT_ALIGNMENT) - 1) & ~((INT_ALIGNMENT) - 1))

/*
 * Return the assumed minimum alignment requirement for the requested
 * size.  Multiples of sizeof(double) are assumed to need double
 * alignment, etc.
 */
inline int
db_alignment (int n)
{
  if (n >= (int) sizeof (double))
    {
      return (int) sizeof (double);
    }
  else if (n >= (int) sizeof (void *))
    {
      return (int) sizeof (void *);
    }
  else if (n >= (int) sizeof (int))
    {
      return (int) sizeof (int);
    }
  else if (n >= (int) sizeof (short))
    {
      return (int) sizeof (short);
    }
  else
    {
      return 1;
    }
}

/*
 * Return the value of "n" to the next "alignment" boundary.  "alignment"
 * must be a power of 2.
 */
#define db_align_to(n, alignment) ((n + alignment - 1) & ~(alignment - 1))

#endif /* _MEMORY_ALGINMENT_H_ */
