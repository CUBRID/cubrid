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
 * porting.c - Functions supporting platform porting
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>

#if defined(WINDOWS)
#include <tchar.h>
#include <float.h>
#include <io.h>
#include <conio.h>
#include <math.h>
#else
#if defined(AIX)
#define _BOOL
#include <unistd.h>
#include <curses.h>
#else
#include <unistd.h>
#include <curses.h>
#endif
#endif

#include "porting.h"

#if !defined(HAVE_ASPRINTF)
#include <stdarg.h>
#endif

#ifndef HAVE_STRLCPY
#include <sys/types.h>
#include <string.h>
#endif

#if defined(AIX) && !defined(DONT_HOOK_MALLOC)
#undef malloc
void *
aix_malloc (size_t size)
{
  /* malloc 0 size memory will be failed in AIX */
  if (size == 0)
    {
      size = 1;
    }
  return malloc (size);
}

#define malloc(a) aix_malloc(a)
#endif

#if defined (WINDOWS)
/*
 * realpath() -
 *    return: pointer to resolved_path or NULL if error occurred
 *    path(in): the relative path to be resolved
 *    resolved_path(out): the buffer to output resolved path
 */
char *
realpath (const char *path, char *resolved_path)
{
  struct stat stat_buf;
  char *tmp_str = _fullpath (resolved_path, path, _MAX_PATH);
  char tmp_path[_MAX_PATH] = { 0 };
  int len = 0;

  if (tmp_str != NULL)
    {
      strncpy (tmp_path, tmp_str, _MAX_PATH);

      /* 
       * The output of _fullpath() ends with '\'(Windows format) or without it. 
       * It doesn't end with '/'(Linux format).
       * 
       * Even if the directory path exists, the stat() in Windows fails when
       * the directory path ends with '\'.
       */
      len = strlen (tmp_path);
      if (len > 0 && tmp_path[len - 1] == '\\')
	{
	  tmp_path[len - 1] = '\0';
	}

      if (stat (tmp_path, &stat_buf) == 0)
	{
	  return tmp_str;
	}
    }

  return NULL;
}

/*
 * poll() -
 *    return: return poll result
 *    fds(in): socket descriptors to wait
 *    nfds(in): number of descriptors
 *    timeout(in): timeout in milliseconds
 */
int
poll (struct pollfd *fds, nfds_t nfds, int timeout)
{
  struct timeval to, *tp;
  fd_set rset, wset, eset;
  fd_set *rp, *wp, *ep;
  unsigned long int i;
  int r;
  unsigned int max_fd;

  tp = NULL;
  if (timeout >= 0)
    {
      to.tv_sec = timeout / 1000;
      to.tv_usec = (timeout % 1000) * 1000;
      tp = &to;
    }

  FD_ZERO (&rset);
  FD_ZERO (&wset);
  FD_ZERO (&eset);
  rp = wp = ep = NULL;
  max_fd = 0;

  for (i = 0; i < nfds; i++)
    {
      if (fds[i].events & POLLIN)
	{
	  if (rp == NULL)
	    {
	      rp = &rset;
	    }
	  FD_SET (fds[i].fd, rp);
	  max_fd = MAX (fds[i].fd, max_fd);
	}
      if (fds[i].events & POLLOUT)
	{
	  if (wp == NULL)
	    {
	      wp = &wset;
	    }
	  FD_SET (fds[i].fd, wp);
	  max_fd = MAX (fds[i].fd, max_fd);
	}
      if (fds[i].events & POLLPRI)
	{
	  if (ep == NULL)
	    {
	      ep = &eset;
	    }
	  FD_SET (fds[i].fd, ep);
	  max_fd = MAX (fds[i].fd, max_fd);
	}
    }

  r = select (max_fd + 1, rp, wp, ep, tp);
  for (i = 0; i < nfds; i++)
    {
      fds[i].revents = 0;
      if ((fds[i].events & POLLIN) && FD_ISSET (fds[i].fd, rp))
	{
	  fds[i].revents |= POLLIN;
	}
      if ((fds[i].events & POLLOUT) && FD_ISSET (fds[i].fd, wp))
	{
	  fds[i].revents |= POLLOUT;
	}
      if ((fds[i].events & POLLPRI) && FD_ISSET (fds[i].fd, ep))
	{
	  fds[i].revents |= POLLPRI;
	}
    }

  return r;
}

/* Number of 100 nanosecond units from 1/1/1601 to 1/1/1970 */
#define EPOCH_BIAS_IN_100NANOSECS 116444736000000000LL

/*
 * gettimeofday - Windows port of Unix gettimeofday()
 *   return: none
 *   tp(out): where time is stored
 *   tzp(in): unused
 */
int
gettimeofday (struct timeval *tp, void *tzp)
{
/*
 * Rapid calculation divisor for 10,000,000
 * x/10000000 == x/128/78125 == (x>>7)/78125
 */
#define RAPID_CALC_DIVISOR 78125

  union
  {
    unsigned __int64 nsec100;	/* in 100 nanosecond units */
    FILETIME ft;
  } now;

  GetSystemTimeAsFileTime (&now.ft);

  /* 
   * Optimization for sec = (long) (x / 10000000);
   * where "x" is number of 100 nanoseconds since 1/1/1970.
   */
  tp->tv_sec = (long) (((now.nsec100 - EPOCH_BIAS_IN_100NANOSECS) >> 7) / RAPID_CALC_DIVISOR);

  /* 
   * Optimization for usec = (long) (x % 10000000) / 10;
   * Let c = x / b,
   * An alternative for MOD operation (x % b) is: (x - c * b),
   *   which consumes less time, specially, for a 64 bit "x".
   */
  tp->tv_usec =
    ((long) (now.nsec100 - EPOCH_BIAS_IN_100NANOSECS - (((unsigned __int64) (tp->tv_sec * RAPID_CALC_DIVISOR)) << 7))) /
    10;

  return 0;
}

#define LOCKING_SIZE 2000
/*
 * lockf() - lockf() WINDOWS implementation
 *   return: 0 if success, -1 otherwise
 *   fd(in): file descriptor
 *   cmd(in): locking command to perform
 *   size(in): number of bytes
 */
int
lockf (int fd, int cmd, long size)
{
  switch (cmd)
    {
    case F_ULOCK:
      return (_locking (fd, _LK_UNLCK, (size ? size : LOCKING_SIZE)));

    case F_LOCK:
      return (_locking (fd, _LK_LOCK, (size ? size : LOCKING_SIZE)));

    case F_TLOCK:
      return (_locking (fd, _LK_NBLCK, (size ? size : LOCKING_SIZE)));

    case F_TEST:
      /* not implemented on WINDOWS */
      return (-1);

    default:
      errno = EINVAL;
      return (-1);
    }
}

/*
 * cuserid - returns a pointer to a string containing a user name
 *                    associated with the effective user ID of the process
 *   return: string
 *   string(in):
 *
 * Note: Changed to allow the user name to be specified using an environment
 *       variable.  This is primarily so that abortdb can have something
 *       meaningful when it displays connection info.
 */
char *
cuserid (char *string)
{
  const char *env;

  /* make 'em supply a buffer */
  if (string != NULL)
    {
      env = getenv ("USERNAME");
      if (env == NULL)
	{
	  strcpy (string, "noname");
	}
      else
	{
	  strlcpy (string, env, L_cuserid);
	}

      return string;
    }

  return string;
}

int
getlogin_r (char *buf, size_t bufsize)
{
  return GetUserName (buf, &bufsize);
}

#if 0
/*
 * umask - This is a stub for umask()
 *   return:
 *   mask(in):
 *
 *  Note: It belongs in the os unit and should be moved there.
 */
int
umask (int mask)
{
  return (0);
}
#endif

/*
 * fsync - This is a stub for fsync()
 *   return:
 *   fd(in):
 *
 * Note: It belongs in the os unit and should be moved there.
 */
int
fsync (int filedes)
{
  return 0;
}

/*
 * pathconf -
 *   return:
 *   path(in):
 *   name(in):
 *
 * Note:
 */
long
pathconf (char *path, int name)
{

  long namelen;
  long filesysflags;

  switch (name)
    {
    case _PC_PATH_MAX:
      /* 
       * NT and OS/2 file systems claim to be able to handle 255 char
       * file names.  But none of the system calls seem to be able to
       * handle a path of more than 255 chars + 1 NULL.  Nor does there
       * appear to be a system function to return the real max length.
       * MAX_PATH is defined in stdlib.h on the NT system.
       */
      return ((MAX_PATH - 1));

    case _PC_NAME_MAX:
      if (GetVolumeInformation (NULL, NULL, 0, NULL, (LPDWORD) & namelen, (LPDWORD) & filesysflags, NULL, 0))
	{
	  /* WARNING!, for "old" DOS style file systems, namelen will be 12 right now, totaling the 8 bytes for name
	   * with the 3 bytes for for extension plus a dot.  This ISN'T what the caller wants, It really wants the
	   * maximum size of an unqualified pathname. I'm not sure what this works out to be under the new file system.
	   * We probably need to make a similar adjustment but hopefully we'll have more breathing room. */
	  if (namelen == 12)
	    namelen = 8;

	  return (namelen);
	}
      else
	{
	  return (8);		/* Length of MSDOS file name */
	}

    case _PC_NO_TRUNC:
      return (TRUE);

    default:
      return (-1);
    }
}

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIGABRT_BIT 1
#define SIGFPE_BIT 2
#define SIGILL_BIT 4
#define SIGINT_BIT 8
#define SIGSEGV_BIT 16
#define SIGTERM_BIT 64

/*
 * sigfillset -
 *   return:
 *   set(in/out):
 */
int
sigfillset (sigset_t * set)
{
  if (set)
    {
      set->mask = 0;
      return (0);
    }
  else
    {
      return (-1);
    }
}

/* satic function for sigprocmask */
static int setmask (sigset_t * set, sigset_t * oldset);
static int block_signals (sigset_t * set, sigset_t * oldset);
static int unblock_signals (sigset_t * set, sigset_t * oldset);
static void sync_mask (sigset_t * set);

/*
 * setmask -
 *   return:
 *   set(in/out):
 *   oldset(out):
 */
static int
setmask (sigset_t * set, sigset_t * oldset)
{
  sigset_t tmp;
  unsigned int test;

  if (set)
    {
      test = set->mask;
    }
  else
    {
      test = -1;
    }

  tmp.mask = set->mask;

  tmp.abrt_state = signal (SIGABRT, (tmp.mask |= SIGABRT_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.abrt_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGABRT, tmp.abrt_state);

  tmp.fpe_state = signal (SIGFPE, (tmp.mask |= SIGFPE_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.fpe_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGFPE, tmp.fpe_state);

  tmp.ill_state = signal (SIGILL, (tmp.mask |= SIGILL_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.ill_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGILL, tmp.ill_state);

  tmp.int_state = signal (SIGINT, (tmp.mask |= SIGINT_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.int_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGINT, tmp.int_state);

  tmp.sev_state = signal (SIGSEGV, (tmp.mask |= SIGSEGV_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.sev_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGSEGV, tmp.sev_state);

  tmp.term_state = signal (SIGTERM, (tmp.mask |= SIGTERM_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.term_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGTERM, tmp.term_state);

  if (oldset)
    {
      oldset->term_state = tmp.term_state;
      oldset->sev_state = tmp.sev_state;
      oldset->int_state = tmp.int_state;
      oldset->ill_state = tmp.ill_state;
      oldset->fpe_state = tmp.fpe_state;
      oldset->abrt_state = tmp.abrt_state;
      sync_mask (oldset);
    }

  return (0);

whoops:
  /* 
   * I'm supposed to restore the signals to the original
   * state if something fails, but I'm blowing it off for now.
   */

  return (-1);
}

/*
 * block_signals -
 *   return:
 *   set(in/out):
 *   oldset(out):
 */
static int
block_signals (sigset_t * set, sigset_t * oldset)
{
  sigset_t tmp;
  unsigned int test;

  if (set)
    {
      test = set->mask;
    }
  else
    {
      test = -1;
    }

  tmp.mask = 0;

  if (test & SIGABRT_BIT)
    {
      tmp.mask |= SIGABRT_BIT;
      tmp.abrt_state = signal (SIGABRT, SIG_IGN);
      if (tmp.abrt_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGABRT, tmp.abrt_state);
    }

  if (test & SIGFPE_BIT)
    {
      tmp.mask |= SIGFPE_BIT;
      tmp.fpe_state = signal (SIGFPE, SIG_IGN);
      if (tmp.fpe_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGFPE, tmp.fpe_state);
    }

  if (test & SIGILL_BIT)
    {
      tmp.mask |= SIGILL_BIT;
      tmp.ill_state = signal (SIGILL, SIG_IGN);
      if (tmp.ill_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGILL, tmp.ill_state);
    }

  if (test & SIGINT_BIT)
    {
      tmp.mask |= SIGINT_BIT;
      tmp.int_state = signal (SIGINT, SIG_IGN);
      if (tmp.int_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGINT, tmp.int_state);
    }

  if (test & SIGSEGV_BIT)
    {
      tmp.mask |= SIGSEGV_BIT;
      tmp.sev_state = signal (SIGSEGV, SIG_IGN);
      if (tmp.sev_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGSEGV, tmp.sev_state);
    }

  if (test & SIGTERM_BIT)
    {
      tmp.mask |= SIGTERM_BIT;
      tmp.term_state = signal (SIGTERM, SIG_IGN);
      if (tmp.term_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGTERM, tmp.term_state);
    }

  if (oldset)
    {
      oldset->term_state = tmp.term_state;
      oldset->sev_state = tmp.sev_state;
      oldset->int_state = tmp.int_state;
      oldset->ill_state = tmp.ill_state;
      oldset->fpe_state = tmp.fpe_state;
      oldset->abrt_state = tmp.abrt_state;
      sync_mask (oldset);
    }

  return (0);

whoops:
  /* 
   * I'm supposed to restore the signals to the original
   * state if something fails, but I'm blowing it off for now.
   */

  return (-1);
}

/*
 * unblock_signals -
 *   return:
 *   set(in/out):
 *   oldset(out):
 */
static int
unblock_signals (sigset_t * set, sigset_t * oldset)
{
  sigset_t tmp;
  unsigned int test;

  if (set)
    {
      test = set->mask;
    }
  else
    {
      test = -1;
    }

  tmp.mask = 0;

  if (test & SIGABRT_BIT)
    {
      tmp.mask |= SIGABRT_BIT;
      tmp.abrt_state = signal (SIGABRT, set->abrt_state);
      if (tmp.abrt_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGABRT, tmp.abrt_state);
    }

  if (test & SIGFPE_BIT)
    {
      tmp.mask |= SIGFPE_BIT;
      tmp.fpe_state = signal (SIGFPE, set->fpe_state);
      if (tmp.fpe_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGFPE, tmp.fpe_state);
    }

  if (test & SIGILL_BIT)
    {
      tmp.mask |= SIGILL_BIT;
      tmp.ill_state = signal (SIGILL, set->ill_state);
      if (tmp.ill_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGILL, tmp.ill_state);
    }

  if (test & SIGINT_BIT)
    {
      tmp.mask |= SIGINT_BIT;
      tmp.int_state = signal (SIGINT, set->int_state);
      if (tmp.int_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGINT, tmp.int_state);
    }

  if (test & SIGSEGV_BIT)
    {
      tmp.mask |= SIGSEGV_BIT;
      tmp.sev_state = signal (SIGSEGV, set->sev_state);
      if (tmp.sev_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGSEGV, tmp.sev_state);
    }

  if (test & SIGTERM_BIT)
    {
      tmp.mask |= SIGTERM_BIT;
      tmp.term_state = signal (SIGTERM, set->term_state);
      if (tmp.term_state < 0)
	goto whoops;
      if (!set)
	(void) signal (SIGTERM, tmp.term_state);
    }

  if (oldset)
    {
      oldset->term_state = tmp.term_state;
      oldset->sev_state = tmp.sev_state;
      oldset->int_state = tmp.int_state;
      oldset->ill_state = tmp.ill_state;
      oldset->fpe_state = tmp.fpe_state;
      oldset->abrt_state = tmp.abrt_state;
      sync_mask (oldset);
    }

  return (0);

whoops:
  /* 
   * I'm supposed to restore the signals to the original
   * state if something fails, but I'm blowing it off for now.
   */

  return (-1);
}

/*
 * sync_mask -
 *   return:
 *   set(in/out):
 */
static void
sync_mask (sigset_t * set)
{
  set->mask |= (set->term_state == SIG_IGN) ? SIGTERM_BIT : set->mask;
  set->mask |= (set->sev_state == SIG_IGN) ? SIGSEGV_BIT : set->mask;
  set->mask |= (set->int_state == SIG_IGN) ? SIGINT_BIT : set->mask;
  set->mask |= (set->ill_state == SIG_IGN) ? SIGILL_BIT : set->mask;
  set->mask |= (set->fpe_state == SIG_IGN) ? SIGFPE_BIT : set->mask;
  set->mask |= (set->abrt_state == SIG_IGN) ? SIGABRT_BIT : set->mask;
}

/*
 * sigprocmask -
 *   return:
 *   how(in):
 *   set(in/out):
 *   oldset(out):
 *
 * Note:
 */
int
sigprocmask (int how, sigset_t * set, sigset_t * oldset)
{
  switch (how)
    {
    case SIG_BLOCK:
      return (block_signals (set, oldset));

    case SIG_UNBLOCK:
      return (unblock_signals (set, oldset));

    case SIG_SETMASK:
      return (setmask (set, oldset));
    }

  return (-1);
}

/*
 * getpagesize -
 *   return:
 */
DWORD
getpagesize ()
{
  static DWORD NT_PageSize = 0;
  SYSTEM_INFO sysinfo;

  if (NT_PageSize == 0)
    {
      GetSystemInfo (&sysinfo);
      NT_PageSize = sysinfo.dwPageSize;
    }
  return (NT_PageSize);
}

#if 0
/*
 * stat - Windows port of Unix stat()
 *   return: 0 or -1
 *   path(in): file path
 *   buffer(in): struct _stat
 */
int
stat (const char *path, struct stat *buf)
{
  struct _stat _buf;
  int rc;

  rc = _stat (path, &_buf);
  if (buf)
    *buf = _buf;
  return rc;
}
#endif

/*
 * pc_init()
 *   return: none
 */
void
pc_init (void)
{
  unsigned int fpbits;

  fpbits = _EM_OVERFLOW | _EM_UNDERFLOW | _EM_ZERODIVIDE;
  (void) _control87 (fpbits, fpbits);
}

/*
 * pc_final()
 *   return: none
 */
void
pc_final (void)
{
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * lock_region() - lock/unlock region of a file
 *   return: 0 if success, -1 otherwise
 *   fd(in): file descriptor
 *   cmd(in): locking command to perform
 *   offset(in): start offset
 *   size(in): number of bytes
 */
int
lock_region (int fd, int cmd, long offset, long size)
{
  if (lseek (fd, offset, SEEK_SET) != offset)
    {
      return -1;
    }
  return lockf (fd, cmd, size);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* free_space -
 *   return:
 *   path(in):
 *   page_size(in):
 *
 * Note:
 *   This function is designed to be compatible with both wide character
 *   and single byte character strings.  Hence, the use of tchar.h.
 *   The definition of 'UNICODE' during compilation determines that TCHAR
 *   becomes 'wchar_t' and not 'char'.  If so, we assume that 'path' is
 *   already a wide character type.
 */
int
free_space (const char *path, int page_size)
{
  ULARGE_INTEGER freebytes_user, total_bytes, freebytes_system;
  TCHAR disk[PATH_MAX];
  TCHAR *temp = NULL;

  /* If there is a : then change c:\foo\bar to c:\ */
  _tcsncpy (disk, (TCHAR *) path, PATH_MAX);

  temp = _tcschr (disk, __TEXT (':'));
  if (temp)
    {
      ++temp;			/* move past the colon */
      if (*temp == __TEXT ('\\') || *temp == __TEXT ('/'))
	{
	  ++temp;
	}
      *temp = __TEXT ('\0');	/* terminate the string */
    }

  /* if there's no colon use the root of local dir by passing a NULL */
  if (!GetDiskFreeSpaceEx ((temp) ? disk : NULL, &freebytes_user, &total_bytes, &freebytes_system))
    {
      return (-1);
    }
  else
    {
      return ((int) (freebytes_user.QuadPart / page_size));
    }
}
#endif /* WINDOWS */

#if !defined(HAVE_STRDUP)
/*
 * strdup() - duplicate a string
 *   return: returns a pointer to the duplicated string
 *   str(in): string
 */
char *
strdup (const char *str)
{
  char *sdup;

  assert (str != NULL);

  size_t len = strlen (str) + 1;
  sdup = (char *) malloc (len);
  if (sdup != NULL)
    {
      memcpy (sdup, str, len);
    }

  return sdup;
}
#endif /* !HAVE_STRDUP */

#if !defined(HAVE_VASPRINTF)
#if defined(WINDOWS)
int
vasprintf (char **ptr, const char *format, va_list ap)
{
  int len;

  len = _vscprintf_p (format, ap) + 1;
  *ptr = (char *) malloc (len * sizeof (char));
  if (!*ptr)
    {
      return -1;
    }

  return _vsprintf_p (*ptr, len, format, ap);
}
#else
int
vasprintf (char **ptr, const char *format, va_list ap)
{
  va_list ap_copy;
  char *buffer = NULL;
  int count;

  va_copy (ap_copy, ap);
  count = vsnprintf (NULL, 0, format, ap);
  if (count >= 0)
    {
      buffer = (char *) malloc (count + 1);
      if (buffer != NULL)
	{
	  count = vsnprintf (buffer, count + 1, format, ap_copy);
	  if (count < 0)
	    {
	      free (buffer);
	    }
	  else
	    {
	      *ptr = buffer;
	    }
	}
      else
	{
	  count = -1;
	}
    }
  va_end (ap_copy);

  return count;
}
#endif
#endif /* !HAVE_VASPRINTF */

#if !defined(HAVE_ASPRINTF)
int
asprintf (char **ptr, const char *format, ...)
{
  va_list ap;
  int ret;

  *ptr = NULL;

  va_start (ap, format);
  ret = vasprintf (ptr, format, ap);
  va_end (ap);

  return ret;
}
#endif /* !HAVE_ASPRINTF */

int
cub_dirname_r (const char *path, char *pathbuf, size_t buflen)
{
  const char *endp;
  ptrdiff_t len;

  if (buflen < 2)
    return (errno = ERANGE);

  /* Empty or NULL string gets treated as "." */
  if (path == NULL || *path == '\0')
    {
      pathbuf[0] = PATH_CURRENT;
      pathbuf[1] = '\0';
      return 1;
    }

  /* Strip trailing slashes */
  endp = path + strlen (path) - 1;
  while (endp > path && *endp == PATH_SEPARATOR)
    endp--;

  /* Find the start of the dir */
  while (endp > path && *endp != PATH_SEPARATOR)
    endp--;

  /* Either the dir is "/" or there are no slashes */
  if (endp == path)
    {
      if (*endp == PATH_SEPARATOR)
	pathbuf[0] = PATH_SEPARATOR;
      else
	pathbuf[0] = PATH_CURRENT;
      pathbuf[1] = '\0';
      return 1;
    }
  else
    {
      do
	{
	  endp--;
	}
      while (endp > path && *endp == PATH_SEPARATOR);
    }

  len = (ptrdiff_t) (endp - path) + 1;
  if (len + 1 > PATH_MAX)
    {
      return (errno = ENAMETOOLONG);
    }
  if (len + 1 > (int) buflen)
    {
      return (errno = ERANGE);
    }
  (void) strncpy (pathbuf, path, len);
  pathbuf[len] = '\0';
  return (int) len;
}

#if defined(AIX)
#undef ceil
double
aix_ceil (double x)
{
  double result = ceil (x);
  /* e.g ceil(-0.5) should be -0, in AIX, it is 0 */
  if ((x < 0) && (result == 0))
    {
      result = -result;
    }
  return result;
}

#define ceil(x) aix_ceil(x)
#endif

#if !defined(HAVE_DIRNAME)
char *
dirname (const char *path)
{
  static char *bname = NULL;

  if (bname == NULL)
    {
      bname = (char *) malloc (PATH_MAX);
      if (bname == NULL)
	return (NULL);
    }

  return (cub_dirname_r (path, bname, PATH_MAX) < 0) ? NULL : bname;
}
#endif /* !HAVE_DIRNAME */

int
basename_r (const char *path, char *pathbuf, size_t buflen)
{
  const char *endp, *startp;
  ptrdiff_t len;

  if (buflen < 2)
    return (errno = ERANGE);

  /* Empty or NULL string gets treated as "." */
  if (path == NULL || *path == '\0')
    {
      pathbuf[0] = PATH_CURRENT;
      pathbuf[1] = '\0';
      return 1;
    }

  /* Strip trailing slashes */
  endp = path + strlen (path) - 1;
  while (endp > path && *endp == PATH_SEPARATOR)
    endp--;

  /* All slashes becomes "/" */
  if (endp == path && *endp == PATH_SEPARATOR)
    {
      pathbuf[0] = PATH_SEPARATOR;
      pathbuf[1] = '\0';
      return 1;
    }

  /* Find the start of the base */
  startp = endp;
  while (startp > path && *(startp - 1) != PATH_SEPARATOR)
    startp--;

  len = (ptrdiff_t) (endp - startp) + 1;
  if (len + 1 > PATH_MAX)
    {
      return (errno = ENAMETOOLONG);
    }
  if (len + 1 > (int) buflen)
    {
      return (errno = ERANGE);
    }
  (void) strncpy (pathbuf, startp, len);
  pathbuf[len] = '\0';
  return (int) len;
}

#if !defined(HAVE_BASENAME)
char *
basename (const char *path)
{
  static char *bname = NULL;

  if (bname == NULL)
    {
      bname = (char *) malloc (PATH_MAX);
      if (bname == NULL)
	return (NULL);
    }

  return (basename_r (path, bname, PATH_MAX) < 0) ? NULL : bname;
}
#endif /* !HAVE_BASENAME */

#if defined(WINDOWS)
char *
ctime_r (const time_t * time, char *time_buf)
{
  int err;
  assert (time != NULL && time_buf != NULL);

  err = ctime_s (time_buf, CTIME_MAX, time);
  if (err != 0)
    {
      return NULL;
    }
  return time_buf;
}
#endif /* !WINDOWS */

#if defined(WINDOWS)
struct tm *
localtime_r (const time_t * time, struct tm *tm_val)
{
  int err;
  assert (time != NULL && tm_val != NULL);

  err = localtime_s (tm_val, time);
  if (err != 0)
    {
      return NULL;
    }
  return tm_val;
}
#endif /* WIDNOWS */


#if defined (ENABLE_UNUSED_FUNCTION)
int
utona (unsigned int u, char *s, size_t n)
{
  char nbuf[10], *p, *t;

  if (s == NULL || n == 0)
    {
      return 0;
    }
  if (n == 1)
    {
      *s = '\0';
      return 1;
    }

  p = nbuf;
  do
    {
      *p++ = u % 10 + '0';
    }
  while ((u /= 10) > 0);
  p--;

  t = s;
  do
    {
      *t++ = *p--;
    }
  while (p >= nbuf && --n > 1);
  *t++ = '\0';

  return (t - s);
}

int
itona (int i, char *s, size_t n)
{
  if (s == NULL || n == 0)
    {
      return 0;
    }
  if (n == 1)
    {
      *s = '\0';
      return 1;
    }

  if (i < 0)
    {
      *s++ = '-';
      n--;
      return utona (-i, s, n) + 1;
    }
  else
    {
      return utona (i, s, n);
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

char *
stristr (const char *s, const char *find)
{
  char c, sc;
  size_t len;

  if ((c = *find++) != '0')
    {
      len = strlen (find);
      do
	{
	  do
	    {
	      if ((sc = *s++) == '\0')
		{
		  return NULL;
		}
	    }
	  while (toupper (sc) != toupper (c));
	}
      while (strncasecmp (s, find, len) != 0);
      s--;
    }
  return (char *) s;
}

/*
 * wrapper for cuserid() function
 */
char *
getuserid (char *string, int size)
{
  if (cuserid (string) == NULL)
    {
      return NULL;
    }
  else
    {
      string[size - 1] = '\0';
      return string;
    }
}

/*
 * wrapper for OS dependent operations
 */
/*
 * os_rename_file() - rename a file
 *   return: 0 on success, otherwise -1
 *   src_path(in): source path
 *   dest_path(in): destination path
 */
int
os_rename_file (const char *src_path, const char *dest_path)
{
#if defined(WINDOWS)
  /* NOTE: Windows 95 and 98 do not support MoveFileEx */
  if (MoveFileEx (src_path, dest_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
      return 0;
    }
  else
    {
      return -1;
    }
  /* TODO: Windows 95/98 does not replace the file if it already exists.  (void) _unlink (dest_path); return rename
   * (src_path, dest_path); */
#else
  return rename (src_path, dest_path);
#endif /* WINDOWS */
}

#include <signal.h>
/*
 * os_set_signal_handler() - sets the signal handler
 *   return: Old signal handler which can be used to restore
 *           If it fails, it returns SIG_ERR
 *   signo(in): specifies the signal except SIGKILL and/or SIGSTOP
 *   sig_handler(in): Function to handle the above signal or SIG_DFL, SIG_IGN
 *
 * Note: We would like the signals to work as follow:
 *   - Multiple signals should not get lost; the system should queue them
 *   - Signals must be reliable. The signal handler should not need to
 *     reestablish itself like in the old days of Unix
 *   - The signal hander remains installed after a signal has been delivered
 *   - If a caught signal occurs during certain system calls terminating
 *     the call prematurely, the call is automatically restarted
 *   - If SIG_DFL is given, the default action is reinstaled
 *   - If SIG_IGN is given as sig_handler, the signal is subsequently ignored
 *     and pending instances of the signal are discarded
 */
SIGNAL_HANDLER_FUNCTION
os_set_signal_handler (const int sig_no, SIGNAL_HANDLER_FUNCTION sig_handler)
{
#if defined(WINDOWS)
  return signal (sig_no, sig_handler);
#else /* WINDOWS */
  struct sigaction act;
  struct sigaction oact;

  act.sa_handler = sig_handler;
  act.sa_flags = 0;

  if (sigemptyset (&act.sa_mask) < 0)
    {
      return (SIG_ERR);
    }

  switch (sig_no)
    {
    case SIGALRM:
#if defined(SA_INTERRUPT)
      act.sa_flags |= SA_INTERRUPT;	/* disable other interrupts */
#endif /* SA_INTERRUPT */
      break;
    default:
#if defined(SA_RESTART)
      act.sa_flags |= SA_RESTART;	/* making certain system calls restartable across signals */
#endif /* SA_RESTART */
      break;
    }

  if (sigaction (sig_no, &act, &oact) < 0)
    {
      return (SIG_ERR);
    }

  return (oact.sa_handler);
#endif /* WINDOWS */
}

/*
 * os_send_signal() - send the signal to ourselves
 *   return: none
 *   signo(in): signal number to send
 */
void
os_send_signal (const int sig_no)
{
#if defined(WINDOWS)
  raise (sig_no);
#else /* WINDOWS */
  kill (getpid (), sig_no);
#endif /* WINDOWS */
}

#if defined(WINDOWS)
#if !defined(HAVE_STRSEP)
char *
strsep (char **stringp, const char *delim)
{
  char *p, *token;

  if (*stringp == NULL)
    return NULL;

  token = *stringp;

  p = strstr (*stringp, delim);
  if (p == NULL)
    {
      *stringp = NULL;
    }
  else
    {
      *p = '\0';
      *stringp = p + strlen (delim);
    }

  return token;
}
#endif

/*
 * getpass() - get a password
 *   return: password string
 *   prompt(in): prompt message string
 */
char *
getpass (const char *prompt)
{
  size_t pwlen = 0;
  int c;
  static char password_buffer[80];

  fprintf (stdout, prompt);

  while (1)
    {
      c = getch ();
      if (c == '\r' || c == '\n')
	break;
      if (c == '\b')
	{			/* backspace */
	  if (pwlen > 0)
	    pwlen--;
	  continue;
	}
      if (pwlen < sizeof (password_buffer) - 1)
	password_buffer[pwlen++] = c;
    }
  password_buffer[pwlen] = '\0';
  return password_buffer;
}
#endif /* WINDOWS */


#if defined(WINDOWS)

int
setenv (const char *name, const char *val, int overwrite)
{
  errno_t ret;

  if (!overwrite)
    {
      char *ptr = getenv (name);
      if (ptr != NULL)
	{
	  return -1;
	}
    }

  ret = _putenv_s (name, val);
  if (ret == EINVAL)
    {
      return -1;
    }

  return 0;
}

int
cub_vsnprintf (char *buffer, size_t count, const char *format, va_list argptr)
{
  int len = _vscprintf_p (format, argptr) + 1;

  if (len > (int) count)
    {
      char *cp = malloc (len);
      if (cp == NULL)
	{
	  return -1;
	}

      len = _vsprintf_p (cp, len, format, argptr);
      if (len < 0)
	{
	  free (cp);
	  return len;
	}

      memcpy (buffer, cp, count - 1);
      buffer[count - 1] = 0;

      free (cp);
      return (int) count;
    }

  return _vsprintf_p (buffer, count, format, argptr);
}

#if !defined(_MSC_VER) || _MSC_VER < 1800
double
round (double d)
{
  return d >= 0 ? floor (d + 0.5) : ceil (d - 0.5);
}
#endif

int
pthread_mutex_init (pthread_mutex_t * mutex, pthread_mutexattr_t * attr)
{
  if (mutex->csp == &mutex->cs && mutex->watermark == WATERMARK_MUTEX_INITIALIZED)
    {
      /* already inited */
      assert (0);
      return 0;
    }

  mutex->csp = &mutex->cs;
  mutex->watermark = WATERMARK_MUTEX_INITIALIZED;
  InitializeCriticalSection (mutex->csp);

  return 0;
}

int
pthread_mutex_destroy (pthread_mutex_t * mutex)
{
  if (mutex->csp != &mutex->cs || mutex->watermark != WATERMARK_MUTEX_INITIALIZED)
    {
      if (mutex->csp == NULL)	/* inited by PTHREAD_MUTEX_INITIALIZER */
	{
	  mutex->watermark = 0;
	  return 0;
	}

      /* invalid destroy */
      assert (0);
      mutex->csp = NULL;
      mutex->watermark = 0;
      return 0;
    }

  DeleteCriticalSection (mutex->csp);
  mutex->csp = NULL;
  mutex->watermark = 0;
  return 0;
}

int
pthread_mutexattr_init (pthread_mutexattr_t * attr)
{
  return 0;
}

int
pthread_mutexattr_settype (pthread_mutexattr_t * attr, int type)
{
  return 0;
}

int
pthread_mutexattr_destroy (pthread_mutexattr_t * attr)
{
  return 0;
}


pthread_mutex_t css_Internal_mutex_for_mutex_initialize = PTHREAD_MUTEX_INITIALIZER;

void
port_win_mutex_init_and_lock (pthread_mutex_t * mutex)
{
  if (css_Internal_mutex_for_mutex_initialize.csp != &css_Internal_mutex_for_mutex_initialize.cs
      || css_Internal_mutex_for_mutex_initialize.watermark != WATERMARK_MUTEX_INITIALIZED)
    {
      pthread_mutex_init (&css_Internal_mutex_for_mutex_initialize, NULL);
    }

  EnterCriticalSection (css_Internal_mutex_for_mutex_initialize.csp);
  if (mutex->csp != &mutex->cs || mutex->watermark != WATERMARK_MUTEX_INITIALIZED)
    {
      /* 
       * below assert means that lock without pthread_mutex_init
       * or PTHREAD_MUTEX_INITIALIZER
       */
      assert (mutex->csp == NULL);
      pthread_mutex_init (mutex, NULL);
    }
  LeaveCriticalSection (css_Internal_mutex_for_mutex_initialize.csp);

  EnterCriticalSection (mutex->csp);
}

int
port_win_mutex_init_and_trylock (pthread_mutex_t * mutex)
{
  bool r;

  if (css_Internal_mutex_for_mutex_initialize.csp != &css_Internal_mutex_for_mutex_initialize.cs
      || css_Internal_mutex_for_mutex_initialize.watermark != WATERMARK_MUTEX_INITIALIZED)
    {
      pthread_mutex_init (&css_Internal_mutex_for_mutex_initialize, NULL);
    }

  EnterCriticalSection (css_Internal_mutex_for_mutex_initialize.csp);
  if (mutex->csp != &mutex->cs || mutex->watermark != WATERMARK_MUTEX_INITIALIZED)
    {
      /* 
       * below assert means that trylock without pthread_mutex_init
       * or PTHREAD_MUTEX_INITIALIZER
       */
      assert (mutex->csp == NULL);
      pthread_mutex_init (mutex, NULL);
    }
  LeaveCriticalSection (css_Internal_mutex_for_mutex_initialize.csp);

  r = TryEnterCriticalSection (mutex->csp);
  if (mutex->csp->RecursionCount > 1)
    {
      LeaveCriticalSection (mutex->csp);
      return EBUSY;
    }

  return r ? 0 : EBUSY;
}


typedef void (WINAPI * InitializeConditionVariable_t) (CONDITION_VARIABLE *);
typedef bool (WINAPI * SleepConditionVariableCS_t) (CONDITION_VARIABLE *, CRITICAL_SECTION *, DWORD dwMilliseconds);

typedef void (WINAPI * WakeAllConditionVariable_t) (CONDITION_VARIABLE *);
typedef void (WINAPI * WakeConditionVariable_t) (CONDITION_VARIABLE *);

InitializeConditionVariable_t fp_InitializeConditionVariable;
SleepConditionVariableCS_t fp_SleepConditionVariableCS;
WakeAllConditionVariable_t fp_WakeAllConditionVariable;
WakeConditionVariable_t fp_WakeConditionVariable;

static bool have_CONDITION_VARIABLE = false;


static void
check_CONDITION_VARIABLE (void)
{
  HMODULE kernel32 = GetModuleHandle ("kernel32");

  have_CONDITION_VARIABLE = true;
  fp_InitializeConditionVariable =
    (InitializeConditionVariable_t) GetProcAddress (kernel32, "InitializeConditionVariable");
  if (fp_InitializeConditionVariable == NULL)
    {
      have_CONDITION_VARIABLE = false;
      return;
    }

  fp_SleepConditionVariableCS = (SleepConditionVariableCS_t) GetProcAddress (kernel32, "SleepConditionVariableCS");
  fp_WakeAllConditionVariable = (WakeAllConditionVariable_t) GetProcAddress (kernel32, "WakeAllConditionVariable");
  fp_WakeConditionVariable = (WakeConditionVariable_t) GetProcAddress (kernel32, "WakeConditionVariable");
}

static int
timespec_to_msec (const struct timespec *abstime)
{
  int msec = 0;
  struct timeval tv;

  if (abstime == NULL)
    {
      return INFINITE;
    }

  gettimeofday (&tv, NULL);
  msec = (abstime->tv_sec - tv.tv_sec) * 1000;
  msec += (abstime->tv_nsec / 1000 - tv.tv_usec) / 1000;

  if (msec < 0)
    {
      msec = 0;
    }

  return msec;
}


/*
 * old (pre-vista) windows does not support CONDITION_VARIABLES
 * so, we need below custom pthread_cond modules for them
 */
static int
win_custom_cond_init (pthread_cond_t * cond, const pthread_condattr_t * attr)
{
  cond->initialized = true;
  cond->waiting = 0;
  InitializeCriticalSection (&cond->lock_waiting);

  cond->events[COND_SIGNAL] = CreateEvent (NULL, FALSE, FALSE, NULL);
  cond->events[COND_BROADCAST] = CreateEvent (NULL, TRUE, FALSE, NULL);
  cond->broadcast_block_event = CreateEvent (NULL, TRUE, TRUE, NULL);

  if (cond->events[COND_SIGNAL] == NULL || cond->events[COND_BROADCAST] == NULL || cond->broadcast_block_event == NULL)
    {
      return ENOMEM;
    }

  return 0;
}

static int
win_custom_cond_destroy (pthread_cond_t * cond)
{
  if (!cond->initialized)
    {
      return 0;
    }

  DeleteCriticalSection (&cond->lock_waiting);

  if (CloseHandle (cond->events[COND_SIGNAL]) == 0 || CloseHandle (cond->events[COND_BROADCAST]) == 0
      || CloseHandle (cond->broadcast_block_event) == 0)
    {
      return EINVAL;
    }

  cond->initialized = false;
  return 0;
}

static int
win_custom_cond_timedwait (pthread_cond_t * cond, pthread_mutex_t * mutex, struct timespec *abstime)
{
  int result;
  int msec;

  assert (cond->initialized == true);

  msec = timespec_to_msec (abstime);
  WaitForSingleObject (cond->broadcast_block_event, INFINITE);

  EnterCriticalSection (&cond->lock_waiting);
  cond->waiting++;
  LeaveCriticalSection (&cond->lock_waiting);

  LeaveCriticalSection (mutex->csp);
  result = WaitForMultipleObjects (2, cond->events, FALSE, msec);
  assert (result == WAIT_TIMEOUT || result <= 2);

  /*** THREAD UNSAFE AREA ***/

  EnterCriticalSection (&cond->lock_waiting);
  cond->waiting--;

  if (cond->waiting == 0)
    {
      ResetEvent (cond->events[COND_BROADCAST]);
      SetEvent (cond->broadcast_block_event);

      /* 
       * Remove additional signal if exists
       * (That's received in above THREAD UNSAFE AREA)
       */
      WaitForSingleObject (cond->events[COND_SIGNAL], 0);
    }

  LeaveCriticalSection (&cond->lock_waiting);
  EnterCriticalSection (mutex->csp);

  return result == WAIT_TIMEOUT ? ETIMEDOUT : 0;
}

static int
win_custom_cond_signal (pthread_cond_t * cond)
{
  assert (cond->initialized == true);

  EnterCriticalSection (&cond->lock_waiting);

  if (cond->waiting > 0)
    {
      SetEvent (cond->events[COND_SIGNAL]);
    }

  LeaveCriticalSection (&cond->lock_waiting);

  return 0;
}

static int
win_custom_cond_broadcast (pthread_cond_t * cond)
{
  assert (cond->initialized == true);

  EnterCriticalSection (&cond->lock_waiting);

  if (cond->waiting > 0)
    {
      ResetEvent (cond->broadcast_block_event);
      SetEvent (cond->events[COND_BROADCAST]);
    }

  LeaveCriticalSection (&cond->lock_waiting);

  return 0;
}

int
pthread_cond_init (pthread_cond_t * cond, const pthread_condattr_t * attr)
{
  static bool checked = false;
  if (checked == false)
    {
      check_CONDITION_VARIABLE ();
      checked = true;
    }

  if (have_CONDITION_VARIABLE)
    {
      fp_InitializeConditionVariable (&cond->native_cond);
      return 0;
    }

  return win_custom_cond_init (cond, attr);
}

int
pthread_cond_destroy (pthread_cond_t * cond)
{
  if (have_CONDITION_VARIABLE)
    {
      return 0;
    }

  return win_custom_cond_destroy (cond);
}

int
pthread_cond_broadcast (pthread_cond_t * cond)
{
  if (have_CONDITION_VARIABLE)
    {
      fp_WakeAllConditionVariable (&cond->native_cond);
      return 0;
    }

  return win_custom_cond_broadcast (cond);
}

int
pthread_cond_signal (pthread_cond_t * cond)
{
  if (have_CONDITION_VARIABLE)
    {
      fp_WakeConditionVariable (&cond->native_cond);
      return 0;
    }

  return win_custom_cond_signal (cond);
}

int
pthread_cond_timedwait (pthread_cond_t * cond, pthread_mutex_t * mutex, struct timespec *abstime)
{
  if (have_CONDITION_VARIABLE)
    {
      int msec = timespec_to_msec (abstime);
      if (fp_SleepConditionVariableCS (&cond->native_cond, mutex->csp, msec) == false)
	{
	  return ETIMEDOUT;
	}

      return 0;
    }

  return win_custom_cond_timedwait (cond, mutex, abstime);
}

int
pthread_cond_wait (pthread_cond_t * cond, pthread_mutex_t * mutex)
{
  return pthread_cond_timedwait (cond, mutex, NULL);
}


int
pthread_create (pthread_t * thread, const pthread_attr_t * attr,
		THREAD_RET_T (THREAD_CALLING_CONVENTION * start_routine) (void *), void *arg)
{
  unsigned int tid;
  *thread = (pthread_t) _beginthreadex (NULL, 0, start_routine, arg, 0, &tid);
  return (*thread <= 0) ? -1 : 0;
}

void
pthread_exit (void *ptr)
{
  _endthreadex ((unsigned int) ptr);
}

pthread_t
pthread_self ()
{
  return GetCurrentThread ();
}

int
pthread_join (pthread_t thread, void **value_ptr)
{
  return WaitForSingleObject (thread, INFINITE);
}

int
pthread_key_create (pthread_key_t * key, void (*destructor) (void *))
{
  return (*key = TlsAlloc ()) != 0xFFFFFFFF ? 0 : -1;
}

int
pthread_key_delete (pthread_key_t key)
{
  return TlsFree (key) != 0 ? 0 : -1;
}

int
pthread_setspecific (pthread_key_t key, const void *value)
{
  return TlsSetValue (key, (LPVOID) value) != 0 ? 0 : -1;
}

void *
pthread_getspecific (pthread_key_t key)
{
  return TlsGetValue (key);
}

#if !defined(_WIN64)
/*
 * The following functions are used to provide atomic operations on
 * Windows 32bit OS. See the comment in porting.h for more information.
 */
UINT64
win32_compare_exchange64 (UINT64 volatile *val_ptr, UINT64 swap_val, UINT64 cmp_val)
{
  /* *INDENT-OFF* */
  __asm
  {
      mov esi,[val_ptr]
      mov ebx, dword ptr[swap_val]
      mov ecx, dword ptr[swap_val + 4]
      mov eax, dword ptr[cmp_val]
      mov edx, dword ptr[cmp_val + 4]
      lock cmpxchg8b[esi]
  }
  /* *INDENT-ON* */
}

UINT64
win32_exchange_add64 (UINT64 volatile *ptr, UINT64 amount)
{
  UINT64 old;
  do
    {
      old = *ptr;
    }
  while (win32_compare_exchange64 (ptr, old + amount, old) != old);
  return old;
}

UINT64
win32_exchange64 (UINT64 volatile *ptr, UINT64 new_val)
{
  UINT64 old;
  do
    {
      old = *ptr;
    }
  while (win32_compare_exchange64 (ptr, new_val, old) != old);
  return old;
}
#endif /* _WIN64 */

#endif /* WINDOWS */

#if defined(WINDOWS)
/*
 * strtod_win () convert string to double
 * return : the converted double
 * str (in): string to convert
 * end_ptr (in): see strtod
 */
double
strtod_win (const char *str, char **end_ptr)
{
  bool is_hex = false;
  double result = 0.0, int_d = 0.0, float_d = 0.0;
  double tmp_d = 0.0;
  const char *p = NULL, *dot_p = NULL, *end_p = NULL;
  int sign_flag = 1;

  if (str == NULL || *str == '\0')
    {
      if (end_ptr != NULL)
	{
	  *end_ptr = (char *) str;
	}
      return result;
    }

  /* if the string start with "0x", "0X", "+0x", "+0X", "-0x" or "-0X" then deal with it as hex string */
  p = str;
  if (*p == '+')
    {
      p++;
    }
  else if (*p == '-')
    {
      sign_flag = -1;
      p++;
    }

  if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X'))
    {
      is_hex = true;
      p += 2;
    }

  if (is_hex)
    {
      /* convert integer part */
      while (*p != '\0')
	{
	  if (*p == '.')
	    {
	      break;
	    }

	  if ('0' <= *p && *p <= '9')
	    {
	      tmp_d = (double) (*p - '0');
	    }
	  else if ('A' <= *p && *p <= 'F')
	    {
	      tmp_d = (double) (*p - 'A' + 10);
	    }
	  else if ('a' <= *p && *p <= 'f')
	    {
	      tmp_d = (double) (*p - 'a' + 10);
	    }
	  else
	    {
	      end_p = p;
	      goto end;
	    }

	  int_d = int_d * 16.0 + tmp_d;

	  p++;
	}
      end_p = p;

      /* convert float part */
      if (*p == '.')
	{
	  /* find the end */
	  dot_p = p;
	  while (*++p != '\0')
	    ;
	  end_p = p;
	  p--;

	  while (p != dot_p)
	    {
	      if ('0' <= *p && *p <= '9')
		{
		  tmp_d = (double) (*p - '0');
		}
	      else if ('A' <= *p && *p <= 'F')
		{
		  tmp_d = (double) (*p - 'A' + 10);
		}
	      else if ('a' <= *p && *p <= 'f')
		{
		  tmp_d = (double) (*p - 'a' + 10);
		}
	      else
		{
		  end_p = p;
		  goto end;
		}

	      float_d = (float_d + tmp_d) / 16.0;

	      p--;
	    }
	}

      result = int_d + float_d;
      if (sign_flag == -1)
	{
	  result = -result;
	}

      /* underflow and overflow */
      if (result > DBL_MAX || (-result) > DBL_MAX)
	{
	  errno = ERANGE;
	}
    }
  else
    {
      result = strtod (str, end_ptr);
    }

end:

  if (is_hex && end_ptr != NULL)
    {
      *end_ptr = (char *) end_p;
    }

  return result;
}
#endif

/*
 * timeval_diff_in_msec -
 *
 *   return: msec
 *
 */
INT64
timeval_diff_in_msec (const struct timeval * end_time, const struct timeval * start_time)
{
  INT64 msec;

  msec = (end_time->tv_sec - start_time->tv_sec) * 1000LL;
  msec += (end_time->tv_usec - start_time->tv_usec) / 1000LL;

  return msec;
}

/*
 * timeval_add_msec -
 *   return: 0
 *
 *   addted_time(out):
 *   start_time(in):
 *   msec(in):
 */
int
timeval_add_msec (struct timeval *added_time, const struct timeval *start_time, int msec)
{
  int usec;

  added_time->tv_sec = start_time->tv_sec + msec / 1000LL;
  usec = (msec % 1000LL) * 1000LL;

  added_time->tv_sec += (start_time->tv_usec + usec) / 1000000LL;
  added_time->tv_usec = (start_time->tv_usec + usec) % 1000000LL;

  return 0;
}

/*
 * timeval_to_timespec -
 *   return: 0
 *
 *   to(out):
 *   from(in):
 */
int
timeval_to_timespec (struct timespec *to, const struct timeval *from)
{
  assert (to != NULL);
  assert (from != NULL);

  to->tv_sec = from->tv_sec;
  to->tv_nsec = from->tv_usec * 1000LL;

  return 0;
}


/*
 * port_open_memstream - make memory stream file handle if possible.
 *			 if not, make temporiry file handle.
 *   return: file handle
 *
 *   ptr (out): memory stream (or temp file name)
 *   sizeloc (out): stream size
 *
 *   NOTE: this function use memory allocation in it.
 *         so you should ensure that stream size is not too huge
 *         before you use this.
 */
FILE *
port_open_memstream (char **ptr, size_t * sizeloc)
{
#ifdef HAVE_OPEN_MEMSTREAM
  return open_memstream (ptr, sizeloc);
#else
  *ptr = tempnam (NULL, "cubrid_");
  return fopen (*ptr, "w+");
#endif
}


/*
 * port_close_memstream - flush file handle and close
 *
 *   fp (in): file handle to close
 *   ptr (in/out): memory stream (out) or temp file name (in)
 *   sizeloc (out): stream size
 *
 *   NOTE: you should call this function before refer ptr
 *         this function flush contents to ptr before close handle
 */
void
port_close_memstream (FILE * fp, char **ptr, size_t * sizeloc)
{
  char *buff = NULL;
  struct stat stat_buf;
  size_t n;

  fflush (fp);

  if (fp)
    {
#ifdef HAVE_OPEN_MEMSTREAM
      fclose (fp);
#else
      if (fstat (fileno (fp), &stat_buf) == 0)
	{
	  *sizeloc = stat_buf.st_size;

	  buff = malloc (*sizeloc + 1);
	  if (buff)
	    {
	      fseek (fp, 0, SEEK_SET);
	      n = fread (buff, 1, *sizeloc, fp);
	      buff[n] = '\0';
	      *sizeloc = n;
	    }
	}

      fclose (fp);
      /* tempname from port_open_memstream */
      unlink (*ptr);
      free (*ptr);

      /* set output */
      *ptr = buff;
#endif
    }
}

char *
trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str; *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'); s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    memmove (str, s, strlen (s) + 1);

  return (str);
}

int
parse_int (int *ret_p, const char *str_p, int base)
{
  int error = 0;
  int val;
  char *end_p;

  assert (ret_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;

  error = str_to_int32 (&val, &end_p, str_p, base);
  if (error < 0)
    {
      return -1;
    }

  if (*end_p != '\0')
    {
      return -1;
    }

  *ret_p = val;

  return 0;
}

int
parse_bigint (INT64 * ret_p, const char *str_p, int base)
{
  int error = 0;
  INT64 val;
  char *end_p;

  assert (ret_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;

  error = str_to_int64 (&val, &end_p, str_p, base);
  if (error < 0)
    {
      return -1;
    }

  if (*end_p != '\0')
    {
      return -1;
    }

  *ret_p = val;

  return 0;
}

int
str_to_int32 (int *ret_p, char **end_p, const char *str_p, int base)
{
  long val = 0;

  assert (ret_p != NULL);
  assert (end_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;
  *end_p = NULL;

  errno = 0;
  val = strtol (str_p, end_p, base);

  if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0))
    {
      return -1;
    }

  if (*end_p == str_p)
    {
      return -1;
    }

  /* Long is 8 bytes and int is 4 bytes in Linux 64bit, so the additional check of integer range is necessary. */
  if (val < INT_MIN || val > INT_MAX)
    {
      return -1;
    }

  *ret_p = (int) val;

  return 0;
}

int
str_to_uint32 (unsigned int *ret_p, char **end_p, const char *str_p, int base)
{
  unsigned long val = 0;

  assert (ret_p != NULL);
  assert (end_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;
  *end_p = NULL;

  errno = 0;
  val = strtoul (str_p, end_p, base);

  if ((errno == ERANGE && val == ULONG_MAX) || (errno != 0 && val == 0))
    {
      return -1;
    }

  if (*end_p == str_p)
    {
      return -1;
    }

  /* Long is 8 bytes and int is 4 bytes in Linux 64bit, so the additional check of integer range is necessary. */
  if (val > UINT_MAX)
    {
      return -1;
    }

  *ret_p = (unsigned int) val;

  return 0;
}


int
str_to_int64 (INT64 * ret_p, char **end_p, const char *str_p, int base)
{
  INT64 val;

  assert (ret_p != NULL);
  assert (end_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;
  *end_p = NULL;

  errno = 0;
  val = strtoll (str_p, end_p, base);

  if ((errno == ERANGE && (val == LLONG_MAX || val == LLONG_MIN)) || (errno != 0 && val == 0))
    {
      return -1;
    }

  if (*end_p == str_p)
    {
      return -1;
    }

  *ret_p = val;

  return 0;
}

int
str_to_uint64 (UINT64 * ret_p, char **end_p, const char *str_p, int base)
{
  UINT64 val;

  assert (ret_p != NULL);
  assert (end_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;
  *end_p = NULL;

  errno = 0;
  val = strtoull (str_p, end_p, base);

  if ((errno == ERANGE && val == ULLONG_MAX) || (errno != 0 && val == 0))
    {
      return -1;
    }

  if (*end_p == str_p)
    {
      return -1;
    }

  *ret_p = val;

  return 0;
}

int
str_to_double (double *ret_p, char **end_p, const char *str_p)
{
  double val = 0;

  assert (ret_p != NULL);
  assert (end_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;
  *end_p = NULL;

  errno = 0;
  val = strtod (str_p, end_p);

  if (errno == ERANGE || errno != 0)
    {
      return -1;
    }

  if (*end_p == str_p)
    {
      return -1;
    }

  *ret_p = val;

  return 0;
}

int
str_to_float (float *ret_p, char **end_p, const char *str_p)
{
  float val = 0;

  assert (ret_p != NULL);
  assert (end_p != NULL);
  assert (str_p != NULL);

  *ret_p = 0;
  *end_p = NULL;

  errno = 0;
  val = strtof (str_p, end_p);

  if (errno == ERANGE || errno != 0)
    {
      return -1;
    }

  if (*end_p == str_p)
    {
      return -1;
    }

  *ret_p = val;

  return 0;
}

#if defined(WINDOWS)
float
strtof_win (const char *nptr, char **endptr)
{
  double d_val = 0;
  float f_val = 0;

  errno = 0;

  d_val = strtod (nptr, endptr);
  if (errno == ERANGE)
    {
      return 0.0f;
    }

  if (d_val > FLT_MAX)		/* overflow */
    {
      errno = ERANGE;
      *endptr = nptr;
      return (HUGE_VAL);
    }
  else if (d_val < (-FLT_MAX))	/* overflow */
    {
      errno = ERANGE;
      *endptr = nptr;
      return (-HUGE_VAL);
    }
  else if (((d_val > 0) && (d_val < FLT_MIN)) || ((d_val < 0) && (d_val > (-FLT_MIN))))	/* underflow */
    {
      errno = ERANGE;
      *endptr = nptr;
      return 0.0f;
    }

  f_val = (float) d_val;
  return f_val;
}
#endif

#ifndef HAVE_STRLCPY
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy (char *dst, const char *src, size_t siz)
{
  char *d = dst;
  const char *s = src;
  size_t n = siz;

  assert (dst != NULL);
  assert (src != NULL);

  /* Copy as many bytes as will fit */
  if (n != 0 && --n != 0)
    {
      do
	{
	  if ((*d++ = *s++) == 0)
	    break;
	}
      while (--n != 0);
    }

  /* Not enough room in dst, add NUL and traverse rest of src */
  if (n == 0)
    {
      if (siz != 0)
	*d = '\0';		/* NUL-terminate dst */
      while (*s++)
	;
    }

  return (s - src - 1);		/* count does not include NUL */
}
#endif /* !HAVE_STRLCPY */

#if (defined(WINDOWS) && defined(_WIN32))
time_t
mktime_for_win32 (struct tm * tm)
{
  struct tm tm_tmp;
  __time32_t t_32;
  __time64_t t_64;

  tm_tmp = *tm;

  t_32 = _mktime32 (tm);
  if (t_32 != -1)
    {
      return (time_t) t_32;
    }

  *tm = tm_tmp;

  t_64 = _mktime64 (tm);
  /* '(time_t) 0x7FFFFFFF' is equal to '01-19-2038 03:14:07(UTC)' */
  if (t_64 >= 0x00 && t_64 <= 0x7FFFFFFF)
    {
      return (time_t) t_64;
    }

  /* There is a possibility that *tm was changed. (e.g. tm->tm_isdst) */
  if (t_64 != -1)
    {
      *tm = tm_tmp;
    }
  return (time_t) (-1);
}
#endif

/* msleep (...)  million second sleep
 *
 * return errno
 * msec(in):
 */
int
msleep (const long msec)
{
  int error = 0;

  assert (msec >= 0);

#if defined (WINDOWS)
  Sleep (msec);
#else
  struct timeval tv;

  tv.tv_sec = msec / 1000L;
  tv.tv_usec = msec % 1000L * 1000L;

  errno = 0;
  select (0, NULL, NULL, NULL, &tv);
  error = errno;

  /* can only be 0 or EINTR here */
  assert (error == 0 || error == EINTR);
#endif

  return error;
}
