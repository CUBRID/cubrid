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

#if !defined(__GNUC__)
#define __attribute__(X)
#endif

#if defined(WINDOWS)
#define IMPORT_VAR 	__declspec(dllimport)
#define EXPORT_VAR 	__declspec(dllexport)
#else
#define IMPORT_VAR 	extern
#define EXPORT_VAR
#endif

#if defined(WINDOWS)
#define L_cuserid 9
#else /* WINDOWS */
#ifndef L_cuserid
#define L_cuserid 9
#endif /* !L_cuserid */
#endif /* WINDOWS */

#if defined(WINDOWS)
#include <direct.h>
#include <process.h>
#include <sys/timeb.h>
#include <time.h>
#include <sys/locking.h>
#include <windows.h>
#include <winbase.h>

#define MAP_FAILED  NULL

/* not defined errno on Windows */
#define ENOMSG      100

#define PATH_MAX	256
#define NAME_MAX	256

#define log2(x)                 (log ((double) x) / log ((double) 2))
#define realpath(path, resolved_path) \
        _fullpath(resolved_path, path, _MAX_PATH)

#define sleep(sec) Sleep(1000*(sec))
#define usleep(usec) Sleep((usec)/1000)

#define mkdir(dir, mode)        _mkdir(dir)
#define getpid()                _getpid()
#define snprintf                    _snprintf
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
#define ftime		    _ftime_s
#define timeb		    _timeb
#define fileno		_fileno
#define localtime_r(time, tm)   localtime_s(tm, time)
#define vsnprintf	cub_vsnprintf

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

/*
 * MAXHOSTNAMELEN definition
 * This is defined in sys/param.h on the linux.
 */
#define MAXHOSTNAMELEN 64

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
#if defined(ENABLE_UNUSED_FUNCTION)
extern int lock_region (int fd, int cmd, long offset, long size);
#endif
extern int free_space (const char *);

#define _longjmp                longjmp
/*
#define _setjmp                 setjmp
*/
#endif /* WINDOWS */

/*
 * Some platforms (e.g., Solaris) evidently don't define _longjmp.  If
 * it's not available, just use regular old longjmp.
 */
#if defined(SOLARIS) || defined(WINDOWS)
#define LONGJMP longjmp
#define SETJMP setjmp
#else
#define LONGJMP _longjmp
#define SETJMP _setjmp
#endif

#if defined(WINDOWS)
#define srand48(seed)    srand(seed)
#define lrand48()        rand()
#define drand48()        ( (double) rand() / (double) RAND_MAX )
#endif /* WINDOWS */

#if defined(WINDOWS)
#define GETHOSTNAME(p, l) css_gethostname(p, l)
#else /* ! WINDOWS */
#define GETHOSTNAME(p, l) gethostname(p, l)
#endif /* ! WINDOWS */

#if defined(WINDOWS)
#define FINITE(x) _finite(x)
#elif defined(HPUX)
#define FINITE(x) isfinite(x)
#else /* ! WINDOWS && ! HPUX */
#define FINITE(x) finite(x)
#endif

#if defined(WINDOWS)
#define difftime64(time1, time2) _difftime64(time1, time2)
#else /* !WINDOWS */
#define difftime64(time1, time2) difftime(time1, time2)
#endif /* !WINDOWS */

#if defined(WINDOWS)
#ifndef wcswcs
#define wcswcs(ws1, ws2)     wcsstr((ws1), (ws2))
#endif
#define wcsspn(ws1, ws2)     ((int) wcsspn((ws1), (ws2)))
#endif /* WINDOWS */

#if defined(SOLARIS)
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

#if !defined(HAVE_STRDUP)
extern char *strdup (const char *str);
#endif /* HAVE_STRDUP */

#if !defined(HAVE_VASPRINTF)
extern int vasprintf (char **ptr, const char *format, va_list ap);
#endif /* HAVE_VASPRINTF */
#if !defined(HAVE_ASPRINTF)
extern int asprintf (char **ptr, const char *format, ...);
#endif /* HAVE_ASPRINTF */

extern int dirname_r (const char *path, char *pathbuf, size_t buflen);
#if !defined(HAVE_DIRNAME)
char *dirname (const char *path);
#endif /* HAVE_DIRNAME */
extern int basename_r (const char *path, char *pathbuf, size_t buflen);
#if !defined(HAVE_BASENAME)
extern char *basename (const char *path);
#endif /* HAVE_BASENAME */
#if defined(WINDOWS)
#if !defined(HAVE_STRSEP)
extern char *strsep (char **stringp, const char *delim);
#endif
extern char *getpass (const char *prompt);
#endif

#if defined(ENABLE_UNUSED_FUNCTION)
extern int utona (unsigned int u, char *s, size_t n);
extern int itona (int i, char *s, size_t n);
#endif

extern char *stristr (const char *s, const char *find);

#define strlen(s1)  ((int) strlen(s1))
#define CAST_STRLEN (int)
#define CAST_BUFLEN (int)
#if _FILE_OFFSET_BITS == 32
#define OFF_T_MAX  INT_MAX
#else
#define OFF_T_MAX  LLONG_MAX
#endif

#if defined(WINDOWS)
#define IS_INVALID_SOCKET(socket) ((socket) == INVALID_SOCKET)
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
#if defined(WINDOWS)
#define os_send_kill() os_send_signal(SIGABRT)
#else
#define os_send_kill() os_send_signal(SIGKILL)
#endif
typedef void (*SIGNAL_HANDLER_FUNCTION) (int sig_no);
extern SIGNAL_HANDLER_FUNCTION os_set_signal_handler (const int sig_no,
						      SIGNAL_HANDLER_FUNCTION
						      sig_handler);
extern void os_send_signal (const int sig_no);

#if defined(WINDOWS)
#define atoll(a)	_atoi64((a))
#define llabs(a)	_abs64((a))
#endif

#if defined(WINDOWS)
int setenv (const char *name, const char *value, int overwrite);
int cub_vsnprintf (char *buffer, size_t count, const char *format,
		   va_list argptr);
#endif

#endif /* _PORTING_H_ */
