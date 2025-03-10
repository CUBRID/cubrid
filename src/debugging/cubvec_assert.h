#ifndef CUBVEC_ASSERT_H
#define CUBVEC_ASSERT_H

#include <assert.h>

#ifdef CUBVEC_TEAM
#define ASSERT_CUBVEC(expr) assert(expr)
#else
#define ASSERT_CUBVEC(expr) ((void)0)
#endif

#ifdef VIMKIM
#define ASSERT_VIMKIM(expr) assert(expr)
#else
#define ASSERT_VIMKIM(expr) ((void)0)
#endif

#endif // CUBVEC_ASSERT_H
