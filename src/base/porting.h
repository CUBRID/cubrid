/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * porting.h - Functions supporting platform porting
 */

#ifndef _PORTING_H_
#define _PORTING_H_

#ident "$Id$"

#include "config.h"

#if defined (AIX)
#include <sys/socket.h>
#endif

#if !defined (__GNUC__)
#define __attribute__(X)
#endif

#if defined (__GNUC__) && defined (__GNUC_MINOR__) && defined (__GNUC_PATCHLEVEL__)
#define CUB_GCC_VERSION (__GNUC__ * 10000 \
			 + __GNUC_MINOR__ * 100 \
			 + __GNUC_PATCHLEVEL__)
#endif

#if defined (WINDOWS)
#define IMPORT_VAR 	__declspec(dllimport)
#define EXPORT_VAR 	__declspec(dllexport)
#include <WinBase.h>
#else
#define IMPORT_VAR 	extern
#define EXPORT_VAR
#endif

#if defined (WINDOWS)
#define L_cuserid 9
#else /* WINDOWS */
#ifndef L_cuserid
#define L_cuserid 9
#endif /* !L_cuserid */
#endif /* WINDOWS */

#define ONE_K		1024
#define ONE_M		1048576
#define ONE_G		1073741824
#define ONE_T		1099511627776LL
#define ONE_P		1125899906842624LL

#define ONE_SEC		1000
#define ONE_MIN		60000
#define ONE_HOUR	3600000

#define CTIME_MAX 64

#ifndef LLONG_MAX
#define LLONG_MAX	9223372036854775807LL
#endif
#ifndef LLONG_MIN
#define LLONG_MIN	(-LLONG_MAX - 1LL)
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX	18446744073709551615ULL
#endif


#define MEM_SIZE_IS_VALID(size) \
  (((long long unsigned) (size) <= ULONG_MAX) \
   || (sizeof (long long unsigned) <= sizeof (size_t)))

#if defined (__cplusplus)
#include <type_traits>
#include <utility>
#endif // C++

#if defined (WINDOWS)
#include <fcntl.h>
#include <direct.h>
#include <process.h>
#include <sys/timeb.h>
#include <time.h>
#include <sys/locking.h>
#include <windows.h>
#include <winbase.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

#if !defined (ENOMSG)
/* not defined errno on Windows */
#define ENOMSG      100
#endif

#if !defined PATH_MAX
#define PATH_MAX	256
#endif
#if !defined NAME_MAX
#define NAME_MAX	256
#endif

#if !defined (_MSC_VER) || _MSC_VER < 1700 || (defined __cplusplus && _MSC_VER == 1700)
#define log2(x)                 (log ((double) x) / log ((double) 2))
#endif /* !_MSC_VER or c before _MSC_VER 1700 or c++ at 1700 */
extern char *realpath (const char *path, char *resolved_path);
#define sleep(sec) Sleep(1000*(sec))
#define usleep(usec) Sleep((usec)/1000)

#define mkdir(dir, mode)        _mkdir(dir)
#define getpid()                _getpid()
#define snprintf                    _sprintf_p
#define strcasecmp(str1, str2)      _stricmp(str1, str2)
#define strncasecmp(str1, str2, size)     _strnicmp(str1, str2, size)
#define lseek(fd, offset, origin)   _lseeki64(fd, offset, origin)
#define fseek(fd, offset, origin)   _fseeki64(fd, offset, origin)
#define ftruncate(fd, size)     _chsize_s(fd, size)
#define strdup(src)                 _strdup(src)
#define getcwd(buffer, length) _getcwd(buffer, length)
#define popen _popen
#define pclose _pclose
#define strtok_r            strtok_s
#define strtoll             _strtoi64
#define strtoull            _strtoui64
// todo - remove define stat; name is too common
#define stat		    _stati64
#define fstat		    _fstati64
#define ftime		    _ftime_s
#define timeb		    _timeb
#define fileno		_fileno
#define vsnprintf	cub_vsnprintf
#define tempnam         _tempnam
#define printf          _printf_p
#define fprintf         _fprintf_p
#define vfprintf        _vfprintf_p
#define vprintf         _vprintf_p
#define strtof		strtof_win
#if defined (_WIN32)
#define mktime         mktime_for_win32
#endif
#if (_WIN32_WINNT < 0x0600)
#define POLLRDNORM  0x0100
#define POLLRDBAND  0x0200
#define POLLIN      (POLLRDNORM | POLLRDBAND)
#define POLLPRI     0x0400

#define POLLWRNORM  0x0010
#define POLLOUT     (POLLWRNORM)
#define POLLWRBAND  0x0020

#define POLLERR     0x0001
#define POLLHUP     0x0002
#define POLLNVAL    0x0004

struct pollfd
{
  SOCKET fd;
  SHORT events;
  SHORT revents;
};
#endif /* (_WIN32_WINNT < 0x0600) */

typedef unsigned long int nfds_t;
extern int poll (struct pollfd *fds, nfds_t nfds, int timeout);

#if 0
#define O_RDONLY                _O_RDONLY
#endif
#define O_SYNC                  0



#undef O_CREAT
#undef O_RDWR
#undef O_RDONLY
#undef O_TRUNC
#undef O_EXCL

#define O_CREAT _O_CREAT|_O_BINARY
#define O_RDWR _O_RDWR|_O_BINARY
#define O_RDONLY _O_RDONLY|_O_BINARY
#define O_TRUNC _O_TRUNC|_O_BINARY
#define O_EXCL _O_EXCL|_O_BINARY

/* Fake up approxomate DOS definitions see sys/stat.h */
/* for umask() stub */
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IROTH 0
#define S_IWOTH 0

/* read, write, execute for owner */
#define S_IRWXU _S_IREAD | _S_IWRITE | _S_IEXEC
/* rwx for group, same as owner since there are no groups in DOS */
#define S_IRWXG S_IRWXU
/* rwx for other, same as owner since there are no groups in DOS */
#define S_IRWXO S_IRWXU

/* access() mode flags */
#define F_OK    0		/* Test for existence.  */
#define W_OK    2		/* Test for write permission.  */
#define R_OK    4		/* Test for read permission.  */

/* definitions for the WINDOWS implementation of lockf() */
#define F_ULOCK     _LK_UNLCK
#define F_LOCK      _LK_LOCK
#define F_TLOCK     _LK_NBLCK
#define F_TEST      -1

/* definitions for the WINDOWS implmentation of pathconf() */
#define _PC_NAME_MAX 4
#define _PC_PATH_MAX 5
#define _PC_NO_TRUNC 8

typedef char *caddr_t;

typedef SSIZE_T ssize_t;

#if 0
struct stat
{
  _dev_t st_dev;
  _ino_t st_ino;
  unsigned short st_mode;
  short st_nlink;
  short st_uid;
  short st_gid;
  _dev_t st_rdev;
  _off_t st_size;
  time_t st_atime;
  time_t st_mtime;
  time_t st_ctime;
};
extern int stat (const char *path, struct stat *buf);
#endif

extern int gettimeofday (struct timeval *tp, void *tzp);

extern int lockf (int fd, int cmd, long size);

extern char *cuserid (char *string);

extern int getlogin_r (char *buf, size_t bufsize);

extern struct tm *localtime_r (const time_t * time, struct tm *tm_val);

extern char *ctime_r (const time_t * time, char *time_buf);

#if 0
extern int umask (int mask);
#endif
int fsync (int filedes);

long pathconf (char *path, int name);

/*
 * Used by the sigfillset() etc. function in pcio.c
 */
typedef struct sigsettype
{
  unsigned int mask;
  void (*abrt_state) (int);
  void (*fpe_state) (int);
  void (*ill_state) (int);
  void (*int_state) (int);
  void (*term_state) (int);
  void (*sev_state) (int);
} sigset_t;

int sigfillset (sigset_t * set);

int sigprocmask (int how, sigset_t * set, sigset_t * oldset);

/*
 * MS Windows specific operations
 */
extern void pc_init (void);
extern void pc_final (void);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int lock_region (int fd, int cmd, long offset, long size);
#endif
extern int free_space (const char *, int);

#define _longjmp                longjmp
/*
#define _setjmp                 setjmp
*/
#else /* WINDOWS */

#if !defined (HAVE_CTIME_R)
#error "HAVE_CTIME_R"
#endif

#if !defined (HAVE_LOCALTIME_R)
#error "HAVE_LOCALTIME_R"
#endif

#if !defined (HAVE_DRAND48_R)
#error "HAVE_DRAND48_R"
#endif


#endif /* WINDOWS */

#define snprintf_dots_truncate(dest, max_len, ...) \
  if (snprintf (dest, max_len, __VA_ARGS__) < 0) \
    snprintf (dest + max_len - 4, 4, "...")
#define strncpy_size(buf, str, size) \
  strncpy (buf, str, size); buf[(size) - 1] = '\0'
#if defined (__cplusplus)
// *INDENT-OFF*
template<typename T>
inline void
check_is_array (const T & a)
{
  static_assert (std::is_array<T>::value == 1, "expected array");
}
#define strncpy_bufsize(buf, str) \
  strncpy_size (buf, str, sizeof (buf)); check_is_array (buf)
// *INDENT-ON*
#else // not C++
#define strncpy_bufsize(buf, str) \
  strncpy_size (buf, str, sizeof (buf))
#endif // not C++

#if defined (WINDOWS)
#define PATH_SEPARATOR  '\\'
#else /* WINDOWS */
#define PATH_SEPARATOR  '/'
#endif /* WINDOWS */
#define PATH_CURRENT    '.'

#define IS_PATH_SEPARATOR(c) ((c) == PATH_SEPARATOR)

#if defined (WINDOWS)
#define IS_ABS_PATH(p) IS_PATH_SEPARATOR((p)[0]) \
	|| (isalpha((p)[0]) && (p)[1] == ':' && IS_PATH_SEPARATOR((p)[2]))
#else /* WINDOWS */
#define IS_ABS_PATH(p) IS_PATH_SEPARATOR((p)[0])
#endif /* WINDOWS */

/*
 * Some platforms (e.g., Solaris) evidently don't define _longjmp.  If
 * it's not available, just use regular old longjmp.
 */
#if defined (SOLARIS) || defined (WINDOWS)
#define LONGJMP longjmp
#define SETJMP setjmp
#else
#define LONGJMP _longjmp
#define SETJMP _setjmp
#endif

/**
 * RFC1123 - Section 2.1
 * https://tools.ietf.org/html/rfc1123
 *
 * Host software MUST handle host names of up to 63 characters and
 * SHOULD handle host names of up to 255 characters.
 */
#define CUB_MAXHOSTNAMELEN 256	/* 255 + 1(for NULL terminator) */

#define GETHOSTNAME(p, l) css_gethostname(p, l)

#if defined (WINDOWS)
#define FINITE(x) _finite(x)
#elif defined (HPUX)
#define FINITE(x) isfinite(x)
#else /* ! WINDOWS && ! HPUX */
#define FINITE(x) finite(x)
#endif

#if defined (WINDOWS)
#define difftime64(time1, time2) _difftime64(time1, time2)
#else /* !WINDOWS */
#define difftime64(time1, time2) difftime(time1, time2)
#endif /* !WINDOWS */

#if defined (WINDOWS)
#ifndef wcswcs
#define wcswcs(ws1, ws2)     wcsstr((ws1), (ws2))
#endif
#define wcsspn(ws1, ws2)     ((int) wcsspn((ws1), (ws2)))
#endif /* WINDOWS */

#if defined (SOLARIS)
#define wcslen(ws)           wslen((ws))
#define wcschr(ws, wc)       wschr((ws), (wc))
#define wcsrchr(ws, wc)      wsrchr((ws), (wc))
#define wcstok(ws1, ws2)     wstok((ws1), (ws2))
#define wcscoll(ws1, ws2)    wscoll((ws1), (ws2))
#define wcsspn(ws1, ws2)     wsspn((ws1), (ws2))
#define wcscspn(ws1, ws2)    wscspn((ws1), (ws2))
#define wcscmp(ws1, ws2)     wscmp((ws1), (ws2))
#define wcsncmp(ws1, ws2, n) wsncmp((ws1), (ws2), (n))
#define wcscpy(ws1, ws2)     wscpy((ws1), (ws2))
#define wcsncpy(ws1, ws2, n) wsncpy((ws1), (ws2), (n))
#endif /* SOLARIS */

#if !defined (HAVE_STRDUP)
extern char *strdup (const char *str);
#endif /* HAVE_STRDUP */

#if !defined (HAVE_VASPRINTF)
extern int vasprintf (char **ptr, const char *format, va_list ap);
#endif /* HAVE_VASPRINTF */
#if !defined (HAVE_ASPRINTF)
extern int asprintf (char **ptr, const char *format, ...);
#endif /* HAVE_ASPRINTF */
#if defined (HAVE_ERR_H)
#include <err.h>
#else
#define err(fd, ...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)
#define errx(fd, ...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)
#endif
extern int cub_dirname_r (const char *path, char *pathbuf, size_t buflen);
#if defined (AIX)
double aix_ceil (double x);
#define ceil(x) aix_ceil(x)
#endif

#if !defined (HAVE_DIRNAME)
char *dirname (const char *path);
#endif /* HAVE_DIRNAME */
extern int basename_r (const char *path, char *pathbuf, size_t buflen);
#if !defined (HAVE_BASENAME)
extern char *basename (const char *path);
#endif /* HAVE_BASENAME */
#if defined (WINDOWS)
#if !defined (HAVE_STRSEP)
extern char *strsep (char **stringp, const char *delim);
#endif
extern char *getpass (const char *prompt);
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
extern int utona (unsigned int u, char *s, size_t n);
extern int itona (int i, char *s, size_t n);
#endif

extern char *stristr (const char *s, const char *find);

#if 1
#define SUPPRESS_STRLEN_WARNING
#else /* !1 */
/* TODO: this causes a compile error on windows, since it uses in its headers std::strlen. anyway, this is an ugly hack
 *       too.
 *       now, we have hundreds of annoying warnings of casts from size_t to int. so either rename this define and all
 *       its usages, or replaces all occurrences of strlen with (int) strlen. either way, all project is changed. */
#define strlen(s1)  ((int) strlen(s1))
#endif /* !1 */
#define CAST_STRLEN (int)
#define CAST_BUFLEN (int)
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 32
#define OFF_T_MAX  INT_MAX
#else
#define OFF_T_MAX  LLONG_MAX
#endif

#if defined (WINDOWS)
#define IS_INVALID_SOCKET(socket) ((socket) == INVALID_SOCKET)
typedef int socklen_t;
#else
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define IS_INVALID_SOCKET(socket) ((socket) < 0)
#endif

/*
 * wrapper for cuserid()
 */
extern char *getuserid (char *string, int size);
/*
 * wrapper for OS dependent operations
 */
extern int os_rename_file (const char *src_path, const char *dest_path);

/* os_send_kill() - send the KILL signal to ourselves */
#if defined (WINDOWS)
#define os_send_kill() os_send_signal(SIGABRT)
#else
#define os_send_kill() os_send_signal(SIGKILL)
#endif
typedef void (*SIGNAL_HANDLER_FUNCTION) (int sig_no);
extern SIGNAL_HANDLER_FUNCTION os_set_signal_handler (const int sig_no, SIGNAL_HANDLER_FUNCTION sig_handler);
extern void os_send_signal (const int sig_no);

#if defined (WINDOWS)
#define atoll(a)	_atoi64((a))
#if !defined(_MSC_VER) || _MSC_VER < 1800
/* ref: https://msdn.microsoft.com/en-us/library/a206stx2.aspx */
#define llabs(a)	_abs64((a))
#endif /* _MSC_VER && _MSC_VER < 1800 */
#endif

#if defined (AIX) && !defined (NAME_MAX)
#define NAME_MAX pathconf("/",_PC_NAME_MAX)
#endif

#if defined (AIX) && !defined (DONT_HOOK_MALLOC)
void *aix_malloc (size_t size);
#define malloc(a) aix_malloc(a)
#endif

#if defined (AIX) && !defined (SOL_TCP)
#define SOL_TCP IPPROTO_TCP
#endif

#if defined (WINDOWS)
int setenv (const char *name, const char *value, int overwrite);
int cub_vsnprintf (char *buffer, size_t count, const char *format, va_list argptr);
#endif

#if defined (WINDOWS)
/* The following structure is used to generate uniformly distributed
 * pseudo-random numbers reentrantly.
 */
struct drand48_data
{
  unsigned short _rand48_seed[3];
};

/* These functions are implemented in rand.c. And rand.c will be included
 * on Windows build.
 */
extern long lrand48 (void);
extern void srand48 (long seed);
extern double drand48 (void);
extern int srand48_r (long int seedval, struct drand48_data *buffer);
extern int lrand48_r (struct drand48_data *buffer, long int *result);
extern int drand48_r (struct drand48_data *buffer, double *result);
extern int rand_r (unsigned int *seedp);

#if !defined(_MSC_VER) || _MSC_VER < 1800
  /* Ref: https://msdn.microsoft.com/en-us/library/dn353646(v=vs.140).aspx */
extern double round (double d);
#endif /* !_MSC_VER || _MSC_VER < 1800 */

/* Maybe replace this with std::mutex */
typedef struct
{
  CRITICAL_SECTION cs;
  CRITICAL_SECTION *csp;
  UINT32 watermark;
} pthread_mutex_t;

typedef HANDLE pthread_mutexattr_t;

/* Use a large prime as watermark */
#define WATERMARK_MUTEX_INITIALIZED 0x96438AF7

#define PTHREAD_MUTEX_INITIALIZER	{{ NULL, 0, 0, NULL, NULL, 0 }, NULL, 0}

typedef enum
{
  COND_SIGNAL = 0,
  COND_BROADCAST = 1,
  MAX_EVENTS = 2
} EVENTS;

typedef union
{
  CONDITION_VARIABLE native_cond;

  struct
  {
    bool initialized;
    unsigned int waiting;
    CRITICAL_SECTION lock_waiting;
    HANDLE events[MAX_EVENTS];
    HANDLE broadcast_block_event;
  };
} pthread_cond_t;


typedef HANDLE pthread_condattr_t;

#if !defined (ETIMEDOUT)
#define ETIMEDOUT WAIT_TIMEOUT
#endif
#define PTHREAD_COND_INITIALIZER	{ NULL }

#if defined(_MSC_VER) && _MSC_VER >= 1900 && !defined(_CRT_NO_TIME_T)
#define _TIMESPEC_DEFINED
#endif /* _MSC_VER && _MSC_VER >= 1900 && !_CRT_NO_TIME_T */
#if !defined(_TIMESPEC_DEFINED)
#define _TIMESPEC_DEFINED
struct timespec
{
  int tv_sec;
  int tv_nsec;
};
#endif /* !_TIMESPEC_DEFINED */

extern pthread_mutex_t css_Internal_mutex_for_mutex_initialize;

int pthread_mutex_init (pthread_mutex_t * mutex, pthread_mutexattr_t * attr);
int pthread_mutex_destroy (pthread_mutex_t * mutex);

void port_win_mutex_init_and_lock (pthread_mutex_t * mutex);
int port_win_mutex_init_and_trylock (pthread_mutex_t * mutex);

__inline int
pthread_mutex_lock (pthread_mutex_t * mutex)
{
  if (mutex->csp == &mutex->cs && mutex->watermark == WATERMARK_MUTEX_INITIALIZED)
    {
      EnterCriticalSection (mutex->csp);
    }
  else
    {
      port_win_mutex_init_and_lock (mutex);
    }

  return 0;
}

__inline int
pthread_mutex_unlock (pthread_mutex_t * mutex)
{
  if (mutex->csp->LockCount == -1)
    {
      /* this means unlock mutex which isn't locked */
      assert (0);
      return 0;
    }

  LeaveCriticalSection (mutex->csp);
  return 0;
}

__inline int
pthread_mutex_trylock (pthread_mutex_t * mutex)
{
  if (mutex->csp == &mutex->cs && mutex->watermark == WATERMARK_MUTEX_INITIALIZED)
    {
      if (TryEnterCriticalSection (mutex->csp))
	{
	  if (mutex->csp->RecursionCount > 1)
	    {
	      LeaveCriticalSection (mutex->csp);
	      return EBUSY;
	    }

	  return 0;
	}

      return EBUSY;
    }
  else
    {
      return port_win_mutex_init_and_trylock (mutex);
    }

  return 0;
}

int pthread_mutexattr_init (pthread_mutexattr_t * attr);
int pthread_mutexattr_settype (pthread_mutexattr_t * attr, int type);
int pthread_mutexattr_destroy (pthread_mutexattr_t * attr);

int pthread_cond_init (pthread_cond_t * cond, const pthread_condattr_t * attr);
int pthread_cond_wait (pthread_cond_t * cond, pthread_mutex_t * mutex);
int pthread_cond_timedwait (pthread_cond_t * cond, pthread_mutex_t * mutex, struct timespec *ts);
int pthread_cond_destroy (pthread_cond_t * cond);
int pthread_cond_signal (pthread_cond_t * cond);
int pthread_cond_broadcast (pthread_cond_t * cond);



/* Data Types */
typedef HANDLE pthread_t;
typedef int pthread_attr_t;
typedef int pthread_key_t;

#define THREAD_RET_T unsigned int
#define THREAD_CALLING_CONVENTION __stdcall

int pthread_create (pthread_t * thread, const pthread_attr_t * attr,
		    THREAD_RET_T (THREAD_CALLING_CONVENTION * start_routine) (void *), void *arg);
void pthread_exit (THREAD_RET_T ptr);
pthread_t pthread_self (void);
int pthread_join (pthread_t thread, void **value_ptr);

#define pthread_attr_init(dummy1)	0
#define pthread_attr_destroy(dummy1)	0

int pthread_key_create (pthread_key_t * key, void (*destructor) (void *));
int pthread_key_delete (pthread_key_t key);
int pthread_setspecific (pthread_key_t key, const void *value);
void *pthread_getspecific (pthread_key_t key);

#else /* WINDOWS */

#define THREAD_RET_T void*
#define THREAD_CALLING_CONVENTION

#endif /* WINDOWS */

#if (defined (WINDOWS) || defined (X86))
#define COPYMEM(type,dst,src)   do {		\
  *((type *) (dst)) = *((type *) (src));  	\
}while(0)
#else /* WINDOWS || X86 */
#define COPYMEM(type,dst,src)   do {		\
  memcpy((dst), (src), sizeof(type)); 		\
}while(0)
#endif /* WINDOWS || X86 */

/*
 * Interfaces for atomic operations
 *
 * Developers should check HAVE_ATOMIC_BUILTINS before using atomic builtins
 * as follows.
 *  #if defined (HAVE_ATOMIC_BUILTINS)
 *   ... write codes with atomic builtins ...
 *  #else
 *   ... leave legacy codes or write codes without atomic builtins ...
 *  #endif
 *
 * ATOMIC_TAS_xx (atomic test-and-set) writes new_val into *ptr, and returns
 * the previous contents of *ptr. ATOMIC_CAS_xx (atomic compare-and-swap) returns
 * true if the swap is done. It is only done if *ptr equals to cmp_val.
 * ATOMIC_INC_xx (atomic increment) returns the result of *ptr + amount.
 *
 * Regarding Windows, there are two types of APIs to provide atomic operations.
 * While InterlockedXXX functions handles 32bit values, InterlockedXXX64 handles
 * 64bit values. That is why we define two types of macros.
 */
#ifdef __cplusplus

#if defined (WINDOWS)
#define MEMORY_BARRIER() \
	MemoryBarrier()
#define HAVE_ATOMIC_BUILTINS
#else
#if defined (HAVE_GCC_ATOMIC_BUILTINS)
#define HAVE_ATOMIC_BUILTINS
#if defined (X86) && defined (CUB_GCC_VERSION) && (CUB_GCC_VERSION < 40400)
#define MEMORY_BARRIER() \
        do { \
          asm volatile("mfence" ::: "memory"); \
          __sync_synchronize(); \
        } while (0)
#else
#define MEMORY_BARRIER() \
              __sync_synchronize()
#endif
#endif
#endif /* defined (WINDOWS) */

#endif /* __cplusplus */

#if defined(HAVE_ATOMIC_BUILTINS)

#if defined (WINDOWS)

#ifndef _WIN64
/*
 * These functions are used on Windows 32bit OS.
 * InterlockedXXX64 functions are provided by Windows Vista (client)/Windows
 * 2003 (server) or later versions. So, Windows XP 32bit does not have them.
 * We provide the following functions to support atomic operations on all
 * Windows versions.
 */
extern UINT64 win32_compare_exchange64 (UINT64 volatile *val_ptr, UINT64 swap_val, UINT64 cmp_val);
extern UINT64 win32_exchange_add64 (UINT64 volatile *ptr, UINT64 amount);
extern UINT64 win32_exchange64 (UINT64 volatile *ptr, UINT64 new_val);

#endif /* !_WIN64 */
#endif /* defined (WINDOWS) */

#ifndef __cplusplus
#define static_assert(a, b)
#endif /* not __cplusplus */

/* *INDENT-OFF* */
template <typename T, typename V> inline T ATOMIC_INC_32 (volatile T *ptr, V amount)
{
  static_assert (sizeof (T) == sizeof (UINT32), "Not 32bit");
  static_assert (sizeof (V) == sizeof (UINT32), "Not 32bit");
#if defined (WINDOWS)
#if _MSC_VER <= 1500
  return InterlockedExchangeAdd (reinterpret_cast <volatile LONG *>(ptr), amount) + amount;
#else
  return InterlockedExchangeAdd (reinterpret_cast <volatile UINT32 *>(ptr), amount) + amount;
#endif
#else
  return __sync_add_and_fetch (ptr, amount);
#endif
}

template <typename T, typename V1, typename V2> inline bool ATOMIC_CAS_32 (volatile T *ptr, V1 cmp_val, V2 swap_val)
{
  static_assert (sizeof (T) == sizeof (UINT32), "Not 32bit");
  static_assert (sizeof (V1) == sizeof (UINT32), "Not 32bit");
  static_assert (sizeof (V2) == sizeof (UINT32), "Not 32bit");
#if defined (WINDOWS)
#if _MSC_VER <= 1500
  return InterlockedCompareExchange (reinterpret_cast <volatile LONG *>(ptr), swap_val, cmp_val) == cmp_val;
#else
  return InterlockedCompareExchange (reinterpret_cast <volatile UINT32 *>(ptr), swap_val, cmp_val) == cmp_val;
#endif
#else
  return __sync_bool_compare_and_swap (ptr, cmp_val, swap_val);
#endif
}

template <typename T, typename V> inline T ATOMIC_TAS_32 (volatile T *ptr, V amount)
{
  static_assert (sizeof (T) == sizeof (UINT32), "Not 32bit");
  static_assert (sizeof (V) <= sizeof (UINT32), "Not 32bit");
#if defined (WINDOWS)
#if _MSC_VER <= 1500
  return InterlockedExchange (reinterpret_cast <volatile LONG *>(ptr), amount);
#else
  return InterlockedExchange (reinterpret_cast <volatile UINT32 *>(ptr), amount);
#endif
#else
  return __sync_lock_test_and_set (ptr, amount);
#endif
}

template <typename T, typename V> inline T ATOMIC_INC_64 (volatile T *ptr, V amount)
{
  static_assert (sizeof (T) == sizeof (UINT64), "Not 64bit");
#if defined (_WIN64)
  return (T) InterlockedExchangeAdd64 (reinterpret_cast <volatile INT64 *>(ptr), amount) + amount;
#elif defined(WINDOWS)
  return win32_exchange_add64 (reinterpret_cast <volatile UINT64 *>(ptr), amount) + amount;
#else
  return __sync_add_and_fetch (ptr, amount);
#endif
}

template <typename T, typename V1, typename V2> inline bool ATOMIC_CAS_64 (volatile T *ptr, V1 cmp_val, V2 swap_val)
{
  static_assert (sizeof (T) == sizeof (UINT64), "Not 64bit");
#if defined (_WIN64)
  return InterlockedCompareExchange64 (reinterpret_cast <volatile INT64 *>(ptr), swap_val, cmp_val) == cmp_val;
#elif defined(WINDOWS)
  return win32_compare_exchange64 (reinterpret_cast <volatile UINT64 *>(ptr), swap_val, cmp_val) == cmp_val;
#else
  return __sync_bool_compare_and_swap (ptr, cmp_val, swap_val);
#endif
}

template <typename T, typename V> inline T ATOMIC_TAS_64 (volatile T *ptr, V amount)
{
  static_assert (sizeof (T) == sizeof (UINT64), "Not 64bit");
#if defined (_WIN64)
  return (T) InterlockedExchange64 (reinterpret_cast <volatile INT64 *>(ptr), (__int64) amount);
#elif defined(WINDOWS)
  return win32_exchange64 (reinterpret_cast <volatile UINT64 *>(ptr), amount);
#else
  return __sync_lock_test_and_set (ptr, amount);
#endif
}

namespace dispatch
{
  template <bool B> struct Bool2Type
    {
      enum { value = B };
    };

  template <typename T, typename V1, typename V2> inline bool atomic_cas (volatile T *ptr, V1 cmp_val, V2 swap_val,
									  Bool2Type <true> /*_is_64_bit*/ )
    {
      return ATOMIC_CAS_64 (ptr, cmp_val, swap_val);
    }

  template <typename T, typename V1, typename V2> inline bool atomic_cas (volatile T *ptr, V1 cmp_val, V2 swap_val,
									  Bool2Type <false> /*_is_64_bit*/ )
    {
      return ATOMIC_CAS_32 (ptr, cmp_val, swap_val);
    }

  template <typename T, typename V> inline T atomic_tas (volatile T *ptr, V amount, Bool2Type <true> /*_is_64_bit*/ )
    {
      return ATOMIC_TAS_64 (ptr, amount);
    }

  template <typename T, typename V> inline T atomic_tas (volatile T * ptr, V amount, Bool2Type <false> /*_is_64_bit*/ )
    {
      return ATOMIC_TAS_32 (ptr, amount);
    }

  template <typename T, typename V> inline T atomic_inc (volatile T *ptr, V amount, Bool2Type <true> /*_is_64_bit*/ )
    {
      return ATOMIC_INC_64 (ptr, amount);
    }

  template <typename T, typename V> inline T atomic_inc (volatile T * ptr, V amount, Bool2Type <false> /*_is_64_bit*/ )
    {
      return ATOMIC_INC_32 (ptr, amount);
    }
}				/* namespace dispatch */

template <typename T, typename V> inline T ATOMIC_TAS (volatile T *ptr, V amount)
{
  return dispatch::atomic_tas (ptr, amount, dispatch::Bool2Type <sizeof (T) == sizeof (UINT64)> ());
}

template <typename T, typename V1, typename V2> inline bool ATOMIC_CAS (volatile T *ptr, V1 cmp_val, V2 swap_val)
{
  return dispatch::atomic_cas (ptr, cmp_val, swap_val, dispatch::Bool2Type <sizeof (T) == sizeof (UINT64)> ());
}

template <typename T> inline T *ATOMIC_TAS_ADDR (T * volatile *ptr, T *new_val)
{
#if defined (WINDOWS)
  return static_cast <T *>(InterlockedExchangePointer (reinterpret_cast <volatile PVOID *>(ptr), new_val));
#else
  return __sync_lock_test_and_set (ptr, new_val);
#endif
}

template <typename T> inline bool ATOMIC_CAS_ADDR (T * volatile *ptr, T *cmp_val, T *swap_val)
{
#if defined (WINDOWS)
  return InterlockedCompareExchangePointer (reinterpret_cast <volatile PVOID *>(ptr), swap_val, cmp_val) == cmp_val;
#else
  return __sync_bool_compare_and_swap (ptr, cmp_val, swap_val);
#endif
}

template <typename T, typename V> inline T ATOMIC_INC (volatile T *ptr, V amount)
{
  return dispatch::atomic_inc (ptr, amount, dispatch::Bool2Type <sizeof (T) == sizeof (UINT64)> ());
}

template <typename T> inline T ATOMIC_LOAD (volatile T *ptr)
{
  return ATOMIC_INC (ptr, 0);
}

template <typename T, typename V> inline void ATOMIC_STORE (volatile T *ptr, V amount)
{
  (void) ATOMIC_TAS (ptr, amount);
}

/* *INDENT-ON* */

#define ATOMIC_LOAD_64(ptr) ATOMIC_INC_64(ptr, 0)
#define ATOMIC_STORE_64(ptr, val) ATOMIC_TAS(ptr, val)

#endif /* defined(HAVE_ATOMIC_BUILTINS) */

#if defined (WINDOWS)
extern double strtod_win (const char *str, char **end_ptr);
#define string_to_double(str, end_ptr) strtod_win((str), (end_ptr));
#else
#define string_to_double(str, end_ptr) strtod((str), (end_ptr))
#endif

extern INT64 timeval_diff_in_msec (const struct timeval *end_time, const struct timeval *start_time);
extern int timeval_add_msec (struct timeval *added_time, const struct timeval *start_time, int msec);
extern int timeval_to_timespec (struct timespec *to, const struct timeval *from);

extern FILE *port_open_memstream (char **ptr, size_t * sizeloc);

extern void port_close_memstream (FILE * fp, char **ptr, size_t * sizeloc);

extern char *trim (char *str);

extern int parse_bigint (INT64 * ret_p, const char *str_p, int base);

extern int str_to_int32 (int *ret_p, char **end_p, const char *str_p, int base);
extern int str_to_uint32 (unsigned int *ret_p, char **end_p, const char *str_p, int base);
extern int str_to_int64 (INT64 * ret_p, char **end_p, const char *str_p, int base);
extern int str_to_uint64 (UINT64 * ret_p, char **end_p, const char *str_p, int base);
extern int str_to_double (double *ret_p, char **end_p, const char *str_p);
extern int str_to_float (float *ret_p, char **end_p, const char *str_p);

#if defined (WINDOWS)
extern float strtof_win (const char *nptr, char **endptr);
#endif

#ifndef HAVE_STRLCPY
extern size_t strlcpy (char *, const char *, size_t);
#endif

#if (defined (WINDOWS) && defined (_WIN32))
extern time_t mktime_for_win32 (struct tm *tm);
#endif

#if (defined (WINDOWS) && !defined (PRId64))
#define PRId64 "lld"
#define PRIx64 "llx"
#endif

extern int msleep (const long msec);

#ifdef __cplusplus
extern "C"
{
#endif
  extern int parse_int (int *ret_p, const char *str_p, int base);
#ifdef __cplusplus
}
#endif

#if defined (_MSC_VER) || defined (__GNUC__)
#define PORTABLE_FUNC_NAME __func__
#else
#define PORTABLE_FUNC_NAME "(unknown)"
#endif

#ifdef __GNUC__
#define UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#define UNUSED(x) x
#endif

#ifdef __GNUC__
#define UNUSED_FUNCTION(x) __attribute__((__unused__)) UNUSED_ ## x
#else
#define UNUSED_FUNCTION(x) x
#endif

/* casts for C++ and C to be used in *.c files; cpp files should use C++ specific syntax. */
/* todo: we should split porting.h into several files to avoid recompiling whole project every time we change
 *       something. when we do that, we can avoid including dangerous macro's like these in header files.
 *       so, try not to include this in header files. */
#ifdef STATIC_CAST
#error "STATIC_CAST definition conflict"
#else				/* !STATIC_CAST */
#ifdef __cplusplus
#define STATIC_CAST(dest_type, expr) static_cast<dest_type>(expr)
#else				/* !__cplusplus */
#define STATIC_CAST(dest_type, expr) ((dest_type) (expr))
#endif				/* !__cplusplus */
#endif				/* !STATIC_CAST */

#ifdef CONST_CAST
#error "CONST_CAST definition conflict"
#else				/* !CONST_CAST */
#ifdef __cplusplus
#define CONST_CAST(dest_type, expr) const_cast<dest_type>(expr)
#else				/* !__cplusplus */
#define CONST_CAST(dest_type, expr) ((dest_type) (expr))
#endif				/* !__cplusplus */
#endif				/* !CONST_CAST */

#ifdef DYNAMIC_CAST
#error "DYNAMIC_CAST definition conflict"
#else				/* !DYNAMIC_CAST */
#ifdef __cplusplus
#define DYNAMIC_CAST(dest_type, expr) dynamic_cast<dest_type>(expr)
#else				/* !__cplusplus */
#define DYNAMIC_CAST(dest_type, expr) ((dest_type) (expr))
#endif				/* !__cplusplus */
#endif				/* !DYNAMIC_CAST */

#ifdef REINTERPRET_CAST
#error "REINTERPRET_CAST definition conflict"
#else				/* !REINTERPRET_CAST */
#ifdef __cplusplus
#define REINTERPRET_CAST(dest_type, expr) reinterpret_cast<dest_type>(expr)
#else				/* !__cplusplus */
#define REINTERPRET_CAST(dest_type, expr) ((dest_type) (expr))
#endif				/* !__cplusplus */
#endif				/* !REINTERPRET_CAST */


/* Use REFP to declare a reference to pointer in *.c files without indent complaining. Do not use it in .cpp files. */
#ifdef REFPTR
#error "REFP definition conflict"
#else				/* !REFPTR */
#define REFPTR(T, name) T *& name
#endif				/* !REFPTR */

#endif				/* _PORTING_H_ */
