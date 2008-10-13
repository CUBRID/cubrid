#ifdef XCLIENT_MT

#ifndef PUBLIC_TLS
#define PUBLIC_TLS    __thread
#endif

#ifndef PRIVATE_TLS
#define PRIVATE_TLS   static __thread
#endif

#ifndef EXTERN_TLS
#define EXTERN_TLS    extern __thread
#endif

#else /* XCLIENT_MT */

#ifndef PUBLIC_TLS
#define PUBLIC_TLS
#endif

#ifndef PRIVATE_TLS
#define PRIVATE_TLS   static
#endif

#ifndef EXTERN_TLS
#define EXTERN_TLS    extern
#endif

#endif /* XCLIENT_MT */
