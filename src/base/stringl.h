#ifndef _STRINGL_H_
#define _STRINGL_H_

#include <stddef.h>

#ifndef HAVE_STRCAT
#if defined(ENABLE_UNUSED_FUNCTION)
extern size_t strlcat (char *, const char *, size_t);
#endif
#endif

#ifndef HAVE_STRLCPY
extern size_t strlcpy (char *, const char *, size_t);
#endif

#endif /* _STRINGL_H_ */
