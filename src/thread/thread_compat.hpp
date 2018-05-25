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
 * thread_compat - interface for compatibility with cubrid projects.
 *
 * NOTE - normally thread module belongs to server stand-alone projects. however, some functions may require thread
 *        entries on server module, but are also used on client module
 */

#ifndef _THREAD_COMPAT_HPP_
#define _THREAD_COMPAT_HPP_

// forward definition for THREAD_ENTRY
#if defined (SERVER_MODE) || (defined (SA_MODE) && defined (__cplusplus))
#include <thread>
#ifndef _THREAD_ENTRY_HPP_
namespace cubthread
{
  class entry;
} // namespace cubthread
typedef cubthread::entry THREAD_ENTRY;
typedef std::thread::id thread_id_t;
#endif // _THREAD_ENTRY_HPP_

// system parameter flags for thread logging
// manager flags
const int THREAD_LOG_MANAGER = 0x1;
const int THREAD_LOG_MANAGER_ALL = 0xFF;          // reserved for thread manager

const int THREAD_LOG_WORKER_POOL_VACUUM = 0x100;
const int THREAD_LOG_WORKER_POOL_ALL = 0xFF00;    // reserved for thread worker pool

const int THREAD_LOG_DAEMON_VACUUM = 0x10000;
const int THREAD_LOG_DAEMON_ALL = 0xFFFF0000;     // reserved for thread daemons

#else // not SERVER_MODE and not SA_MODE-C++
// client or SA_MODE annoying grammars
typedef void THREAD_ENTRY;
typedef unsigned long int thread_id_t;
#endif // not SERVER_MODE and not SA_MODE

#endif // _THREAD_COMPAT_HPP_
