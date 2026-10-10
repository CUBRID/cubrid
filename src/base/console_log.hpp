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


// Shared by the launcher marker and every relay writing the same console log.
#ifndef _CONSOLE_LOG_HPP_
#define _CONSOLE_LOG_HPP_

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace console_log
{
  // Per service: active plus three archives, each at most 1 MiB. The stable
  // lock inode also holds the latest failure (256 bytes); never rotate it.
  constexpr off_t size_limit = 1024 * 1024;
  constexpr int archive_count = 3;

  inline int open_owned (const char *path)
  {
    int fd = open (path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0)
      {
	return -1;
      }
    struct stat st;
    if (fstat (fd, &st) != 0 || !S_ISREG (st.st_mode) || st.st_uid != geteuid () || st.st_nlink != 1
	|| fchmod (fd, 0600) != 0)
      {
	close (fd);
	errno = EACCES;
	return -1;
      }
    return fd;
  }

  inline int open_lock (const char *path)
  {
    char name[PATH_MAX];
    if (snprintf (name, sizeof (name), "%s.lock", path) >= static_cast<int> (sizeof (name)))
      {
	errno = ENAMETOOLONG;
	return -1;
      }
    int fd = open_owned (name);
    if (fd >= 0)
      {
	// pwrite must overwrite the bounded diagnostic, including on Linux.
	if (fcntl (fd, F_SETFL, O_NONBLOCK) < 0)
	  {
	    int saved = errno;
	    close (fd);
	    errno = saved;
	    return -1;
	  }
      }
    return fd;
  }

  inline bool lock (int fd)
  {
    // A stopped/crashed writer must not create a new indefinite service hang.
    for (int attempt = 0; attempt < 100; ++attempt)
      {
	if (flock (fd, LOCK_EX | LOCK_NB) == 0)
	  {
	    return true;
	  }
	if (errno != EACCES && errno != EAGAIN && errno != EINTR)
	  {
	    return false;
	  }
	struct timespec delay = {0, 1000000};
	nanosleep (&delay, nullptr);
      }
    errno = EWOULDBLOCK;
    return false;
  }

  inline void unlock (int fd)
  {
    flock (fd, LOCK_UN);
  }

  inline bool write_all (int fd, const char *buffer, size_t size)
  {
    while (size > 0)
      {
	ssize_t count = write (fd, buffer, size);
	if (count < 0 && errno == EINTR)
	  {
	    continue;
	  }
	if (count <= 0)
	  {
	    if (count == 0)
	      {
		errno = EIO;
	      }
	    return false;
	  }
	buffer += count;
	size -= count;
      }
    return true;
  }

  inline void failure (int fd, const char *path, const char *operation, int error, bool locked)
  {
    char record[256] = {};
    snprintf (record, sizeof (record), "console failure time=%lld pid=%ld operation=%s errno=%d\n",
	      static_cast<long long> (time (nullptr)), static_cast<long> (getpid ()), operation, error);
    if (locked)
      {
	// Successful writers never erase another relay's failure evidence.
	ssize_t count;
	do
	  {
	    count = pwrite (fd, record, sizeof (record), 0);
	  }
	while (count < 0 && errno == EINTR);
	if (count == sizeof (record))
	  {
	    ftruncate (fd, sizeof (record));
	  }
      }
    // Best effort syslog datagram: do not block on an unavailable/full logger.
#if defined(LINUX)
    int socket_fd = socket (AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
    int socket_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd >= 0 && (fcntl (socket_fd, F_SETFL, O_NONBLOCK) < 0
			   || fcntl (socket_fd, F_SETFD, FD_CLOEXEC) < 0))
      {
	close (socket_fd);
	socket_fd = -1;
      }
#endif
    if (socket_fd >= 0)
      {
	struct sockaddr_un address = {};
	address.sun_family = AF_UNIX;
	strcpy (address.sun_path, "/dev/log");
	char message[PATH_MAX + 320];
	int length = snprintf (message, sizeof (message), "<27>cub_console: %s log=%s", record, path);
	if (length > 0 && length < static_cast<int> (sizeof (message)))
	  {
	    sendto (socket_fd, message, length, MSG_DONTWAIT, reinterpret_cast<struct sockaddr *> (&address),
		    sizeof (address));
	  }
	close (socket_fd);
      }
  }

  inline bool retain_tail (int fd)
  {
    struct stat st;
    if (fstat (fd, &st) != 0)
      {
	return false;
      }
    if (st.st_size <= size_limit)
      {
	return true;
      }
    if (fcntl (fd, F_SETFL, O_NONBLOCK) < 0)
      {
	return false;
      }
    // Upgrade from the previous unbounded logger: keep the newest 1 MiB.
    // Bounded stack copying avoids another temporary file or an oversized archive.
    char buffer[8192];
    off_t offset = 0;
    while (offset < size_limit)
      {
	ssize_t count = pread (fd, buffer, sizeof (buffer), st.st_size - size_limit + offset);
	if (count < 0 && errno == EINTR)
	  {
	    continue;
	  }
	if (count <= 0)
	  {
	    errno = EIO;
	    return false;
	  }
	ssize_t written = 0;
	while (written < count)
	  {
	    ssize_t part = pwrite (fd, buffer + written, count - written, offset + written);
	    if (part < 0 && errno == EINTR)
	      {
		continue;
	      }
	    if (part <= 0)
	      {
		return false;
	      }
	    written += part;
	  }
	offset += count;
      }
    return ftruncate (fd, size_limit) == 0 && fcntl (fd, F_SETFL, O_APPEND | O_NONBLOCK) == 0;
  }

  inline bool initialize (int lock_fd, const char *path)
  {
    if (!lock (lock_fd))
      {
	return false;
      }
    bool success = ftruncate (lock_fd, 256) == 0;
    for (int i = 0; success && i <= archive_count; ++i)
      {
	char name[PATH_MAX];
	snprintf (name, sizeof (name), i == 0 ? "%s" : "%s.%d", path, i);
	struct stat st;
	if (lstat (name, &st) != 0 && errno == ENOENT)
	  {
	    continue;
	  }
	int fd = open_owned (name);
	success = fd >= 0 && retain_tail (fd);
	int saved = errno;
	if (fd >= 0)
	  {
	    close (fd);
	  }
	errno = saved;
      }
    int saved = errno;
    unlock (lock_fd);
    errno = saved;
    return success;
  }

  inline bool append (int lock_fd, const char *path, const char *buffer, size_t size, int &last_error)
  {
    bool locked = lock (lock_fd);
    int fd = -1;
    const char *operation = "lock";
    bool success = false;
    if (locked)
      {
	operation = "open";
	// Reopen under the stable lock for EVERY append: no relay retains an
	// archive or an unlinked inode while another relay rotates the path.
	fd = open_owned (path);
	if (fd >= 0)
	  {
	    struct stat st;
	    operation = "stat";
	    if (fstat (fd, &st) == 0)
	      {
		success = true;
		if (st.st_size > size_limit - static_cast<off_t> (size))
		  {
		    operation = "rotate";
		    close (fd);
		    fd = -1;
		    for (int i = archive_count; i > 0; --i)
		      {
			char from[PATH_MAX], to[PATH_MAX];
			snprintf (to, sizeof (to), "%s.%d", path, i);
			if (i == 1)
			  {
			    snprintf (from, sizeof (from), "%s", path);
			  }
			else
			  {
			    snprintf (from, sizeof (from), "%s.%d", path, i - 1);
			  }
			if (rename (from, to) != 0 && errno != ENOENT)
			  {
			    success = false;
			    break;
			  }
		      }
		    if (success)
		      {
			fd = open_owned (path);
			success = fd >= 0;
		      }
		  }
		if (success)
		  {
		    operation = "write";
		    success = write_all (fd, buffer, size);
		  }
	      }
	  }
      }
    int saved = errno;
    if (!success && saved != last_error)
      {
	failure (lock_fd, path, operation, saved, locked);
      }
    last_error = success ? 0 : saved;
    if (fd >= 0)
      {
	close (fd);
      }
    if (locked)
      {
	unlock (lock_fd);
      }
    errno = saved;
    return success;
  }
  inline bool start (int lock_fd, const char *path, const char *marker, size_t size)
  {
    // Logging may exceed an inherited RLIMIT_FSIZE. Block only this thread's
    // synchronous SIGXFSZ while checking write/ftruncate errors; preserve the
    // caller's disposition, mask and any signal already pending on entry.
    sigset_t blocked, original, pending;
    sigemptyset (&blocked);
    sigaddset (&blocked, SIGXFSZ);
    int error = pthread_sigmask (SIG_BLOCK, &blocked, &original);
    if (error != 0)
      {
	errno = error;
	return false;
      }
    sigpending (&pending);
    bool was_pending = sigismember (&pending, SIGXFSZ) == 1;
    int last_error = 0;
    bool success = initialize (lock_fd, path) && append (lock_fd, path, marker, size, last_error);
    int saved = errno;
    if (!was_pending)
      {
	struct timespec immediately = {0, 0};
	while (sigtimedwait (&blocked, nullptr, &immediately) < 0 && errno == EINTR) {}
      }
    pthread_sigmask (SIG_SETMASK, &original, nullptr);
    errno = saved;
    return success;
  }

}
#endif
