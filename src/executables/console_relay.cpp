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


// Private executable for background stdout/stderr. Descriptor assignments are
// an internal spawn contract, not a user-facing command line interface.
#include "config.h"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static bool
write_all (int fd, const char *buffer, size_t size)
{
  while (size > 0)
    {
      ssize_t written = write (fd, buffer, size);
      if (written < 0 && errno == EINTR)
	{
	  continue;
	}
      if (written <= 0)
	{
	  return false;
	}
      buffer += written;
      size -= written;
    }
  return true;
}

int
main ()
{
  signal (SIGPIPE, SIG_IGN);
  bool startup = true;
  unsigned char status = 0;
  pollfd inputs[3] = {{4, POLLIN, 0}, {5, POLLIN, 0}, {8, POLLIN, 0}};
  for (int i = 0; i < 2; ++i)
    {
      if (fcntl (inputs[i].fd, F_SETFL, O_NONBLOCK) < 0)
	{
	  return 1;
	}
    }
  while (inputs[0].fd >= 0 || inputs[1].fd >= 0 || startup)
    {
      if (poll (inputs, 3, -1) < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  return 1;
	}
      bool complete = startup && inputs[2].revents != 0;
      for (int stream = 0; stream < 2; ++stream)
	{
	  if (inputs[stream].fd < 0)
	    {
	      continue;
	    }
	  int remaining = 8192;
	  // Snapshot queued bytes once, so continuous output cannot delay the
	  // start command indefinitely. Later output still reaches the log.
	  if (complete && ioctl (inputs[stream].fd, FIONREAD, &remaining) < 0)
	    {
	      status = 1;
	      remaining = 0;
	    }
	  if (!complete && inputs[stream].revents == 0)
	    {
	      continue;
	    }
	  do
	    {
	      char buffer[8192];
	      size_t size = remaining < 8192 ? remaining : 8192;
	      if (size == 0)
		{
		  break;
		}
	      ssize_t count = read (inputs[stream].fd, buffer, size);
	      if (count < 0 && errno == EINTR)
		{
		  continue;
		}
	      if (count < 0 && errno == EAGAIN)
		{
		  break;
		}
	      if (count <= 0)
		{
		  close (inputs[stream].fd);
		  inputs[stream].fd = -1;
		  if (count < 0)
		    {
		      status = 1;
		    }
		  break;
		}
	      if (!write_all (3, buffer, count))
		{
		  status = 1;
		}
	      if (startup && !write_all (6 + stream, buffer, count))
		{
		  status = 1;
		}
	      remaining -= count;
	    }
	  while (complete && remaining > 0);
	}
      if (complete)
	{
	  close (6);
	  close (7);
	  close (8);
	  inputs[2].fd = -1;
	  write_all (9, reinterpret_cast<const char *> (&status), 1);
	  close (9);
	  startup = false;
	}
    }
  close (3);
  return status;
}
