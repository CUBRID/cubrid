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

#include "clientid.hpp"

#include <cstring>

// str_safe_copy - copy string to char array without overflowing and always ending in a terminator character
//
template <size_t Size>
static void str_safe_copy (std::array<char, Size> &buffer, const char *str);

clientids::~clientids ()
{
  delete m_own_buffer;
}

void
clientids::init_buffers ()
{
  if (m_own_buffer == NULL)
    {
      m_own_buffer = new membuf ();

      client_info = m_own_buffer->m_client_info_buffer.data ();
      db_user = m_own_buffer->m_db_user_buffer.data ();
      program_name = m_own_buffer->m_program_name_buffer.data ();
      login_name = m_own_buffer->m_login_name_buffer.data ();
      host_name = m_own_buffer->m_host_name_buffer.data ();
    }
}

void
clientids::set_ids (boot_client_type type_arg, const char *client_info_arg, const char *db_user_arg,
		    const char *program_name_arg, const char *login_name_arg, const char *host_name_arg,
		    int process_id_arg)
{
  init_buffers ();

  // macro helper to safely copy each string to its corresponding buffer
#define LOCAL_STRNCPY(idname) \
  str_safe_copy (m_own_buffer->m_##idname##_buffer, idname##_arg);

  LOCAL_STRNCPY (client_info);
  LOCAL_STRNCPY (db_user);
  LOCAL_STRNCPY (program_name);
  LOCAL_STRNCPY (login_name);
  LOCAL_STRNCPY (host_name);
#undef LOCAL_STRNCPY

  client_type = type_arg;
  process_id = process_id_arg;
}

void
clientids::set_ids (const clientids &other)
{
  set_ids (other.client_type, other.client_info, other.db_user, other.program_name, other.login_name, other.host_name,
	   other.process_id);
}

void
clientids::set_system_internal ()
{
  clear ();
  client_type = BOOT_CLIENT_SYSTEM_INTERNAL;
}

void
clientids::set_system_internal_with_user (const char *db_user_arg)
{
  set_system_internal ();

  init_buffers ();
  str_safe_copy (m_own_buffer->m_db_user_buffer, db_user_arg);
}

void
clientids::reset ()
{
  clear ();
  client_type = BOOT_CLIENT_UNKNOWN;
}

void
clientids::clear ()
{
  if (m_own_buffer != NULL)
    {
      m_own_buffer->clear ();
    }
}

//
// membuf
//
clientids::membuf::membuf ()
{
  clear ();
}

void
clientids::membuf::clear ()
{
  m_client_info_buffer.fill (0);
  m_db_user_buffer.fill (0);
  m_program_name_buffer.fill (0);
  m_login_name_buffer.fill (0);
  m_host_name_buffer.fill (0);
}

//
// static functions
//

template <size_t Size>
static void
str_safe_copy (std::array<char, Size> &buffer, const char *str)
{
  std::strncpy (buffer.data (), str, Size - 1);
  assert (buffer.at (Size) == '\0');
}
