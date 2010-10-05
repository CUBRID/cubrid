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

#if defined(WINDOWS)
#include <tchar.h>
#include <float.h>
#include <io.h>
#include <conio.h>
#include <math.h>
#else
#include <curses.h>
#endif

#include "porting.h"
#include "storage_common.h"

#if defined (WINDOWS)
#define PATH_SEPARATOR  '\\'
#else
#define PATH_SEPARATOR  '/'
#endif
#define PATH_CURRENT    '.'

#if defined (WINDOWS)
/*
 * gettimeofday - Windows port of Unix gettimeofday()
 *   return: none
 *   tp(out): where time is stored
 *   tzp(in): unused
 */
int
gettimeofday (struct timeval *tp, void *tzp)
{
#if 1				/* _ftime() version */
  struct _timeb tm;
  _ftime (&tm);
  tp->tv_sec = tm.time;
  tp->tv_usec = tm.millitm * 1000;
  return 0;
#else /* GetSystemTimeAsFileTime version */
  FILETIME ft;
  unsigned __int64 tmpres = 0;
  static int tzflag;

  GetSystemTimeAsFileTime (&ft);

  tmpres |= ft.dwHighDateTime;
  tmpres <<= 32;
  tmpres |= ft.dwLowDateTime;

  tmpres -= DELTA_EPOCH_IN_MICROSECS;

  tmpres /= 10;

  tv->tv_sec = (tmpres / 1000000UL);
  tv->tv_usec = (tmpres % 1000000UL);

  return 0;
#endif
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

#include "environment_variable.h"
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
      if (GetVolumeInformation
	  (NULL, NULL, 0, NULL, (LPDWORD) & namelen, (LPDWORD) & filesysflags,
	   NULL, 0))
	{
	  /* WARNING!, for "old" DOS style file systems, namelen will be 12
	   * right now, totaling the 8 bytes for name with the 3 bytes for
	   * for extension plus a dot.  This ISN'T what the caller wants,
	   * It really wants the maximum size of an unqualified pathname.
	   * I'm not sure what this works out to be under the new file system.
	   * We probably need to make a similar adjustment but hopefully
	   * we'll have more breathing room.
	   */
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

  tmp.abrt_state =
    signal (SIGABRT, (tmp.mask |= SIGABRT_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.abrt_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGABRT, tmp.abrt_state);

  tmp.fpe_state =
    signal (SIGFPE, (tmp.mask |= SIGFPE_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.fpe_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGFPE, tmp.fpe_state);

  tmp.ill_state =
    signal (SIGILL, (tmp.mask |= SIGILL_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.ill_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGILL, tmp.ill_state);

  tmp.int_state =
    signal (SIGINT, (tmp.mask |= SIGINT_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.int_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGINT, tmp.int_state);

  tmp.sev_state =
    signal (SIGSEGV, (tmp.mask |= SIGSEGV_BIT) ? SIG_IGN : SIG_DFL);
  if (tmp.sev_state < 0)
    goto whoops;
  if (!set)
    (void) signal (SIGSEGV, tmp.sev_state);

  tmp.term_state =
    signal (SIGTERM, (tmp.mask |= SIGTERM_BIT) ? SIG_IGN : SIG_DFL);
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
 *
 * Note:
 *   This function is designed to be compatible with both wide character
 *   and single byte character strings.  Hence, the use of tchar.h.
 *   The definition of 'UNICODE' during compilation determines that TCHAR
 *   becomes 'wchar_t' and not 'char'.  If so, we assume that 'path' is
 *   already a wide character type.
 */
int
free_space (const char *path)
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
  if (!GetDiskFreeSpaceEx ((temp) ? disk : NULL,
			   &freebytes_user, &total_bytes, &freebytes_system))
    {
      return (-1);
    }
  else
    {
      return ((int) (freebytes_user.QuadPart / IO_PAGESIZE));
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
dirname_r (const char *path, char *pathbuf, size_t buflen)
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
  if (len + 1 > buflen)
    {
      return (errno = ERANGE);
    }
  (void) strncpy (pathbuf, path, len);
  pathbuf[len] = '\0';
  return len;
}

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

  return (dirname_r (path, bname, PATH_MAX) < 0) ? NULL : bname;
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
  if (len + 1 > buflen)
    {
      return (errno = ERANGE);
    }
  (void) strncpy (pathbuf, startp, len);
  pathbuf[len] = '\0';
  return len;
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
  if (MoveFileEx (src_path, dest_path,
		  MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
      return 0;
    }
  else
    {
      return -1;
    }
  /* TODO:
   *   Windows 95/98 does not replace the file if it already exists.
   *   (void) _unlink (dest_path);
   *   return rename (src_path, dest_path);
   */
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
      act.sa_flags |= SA_RESTART;	/* making certain system calls
					   restartable across signals */
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

  if (len > count)
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
      return count;
    }

  return _vsprintf_p (buffer, count, format, argptr);
}

double
round (double d)
{
  return d >= 0 ? floor (d + 0.5) : ceil (d - 0.5);
}
#endif /* WINDOWS */
