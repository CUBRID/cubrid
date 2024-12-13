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

#include "client_credentials.hpp"

#include "porting.h"

#include <algorithm>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static void
string_ncopy (std::string &dest, const char *src, size_t max_size)
{
  const char *copyupto = std::find (src, src + max_size, 0);  // find terminator
  dest.assign (src, copyupto);
}

const char *
clientids::UNKNOWN_ID = "(unknown)";

clientids::clientids ()
  : client_type (DB_CLIENT_TYPE_UNKNOWN)
  , client_info {}
  , db_user {}
  , program_name {}
  , login_name {}
  , host_name {}
  , client_ip_addr {}
  , process_id (0)
{
}

clientids::~clientids ()
{
}

const char *
clientids::get_client_info () const
{
  return client_info.c_str ();
}

const char *
clientids::get_db_user () const
{
  return db_user.c_str ();
}

const char *
clientids::get_program_name () const
{
  return program_name.c_str ();
}

const char *
clientids::get_login_name () const
{
  return login_name.c_str ();
}

const char *
clientids::get_host_name () const
{
  return host_name.c_str ();
}

const char *
clientids::get_client_ip_addr () const
{
  return client_ip_addr.c_str ();
}

void
clientids::set_ids (db_client_type type_arg, const char *client_info_arg, const char *db_user_arg,
		    const char *program_name_arg, const char *login_name_arg, const char *host_name_arg, const char *client_ip_addr,
		    int process_id_arg)
{
  set_client_info (client_info_arg);
  set_user (db_user_arg);
  set_program_name (program_name_arg);
  set_login_name (login_name_arg);
  set_host_name (host_name_arg);
  set_client_ip_addr (client_ip_addr);

  client_type = type_arg;
  process_id = process_id_arg;
}

void
clientids::set_ids (const clientids &other)
{
  set_ids (other.client_type, other.client_info.c_str (), other.get_db_user (), other.get_program_name (),
	   other.get_login_name (), other.get_host_name (), other.get_client_ip_addr(), other.process_id);
}

void
clientids::set_client_info (const char *client_info_arg)
{
  string_ncopy (client_info, client_info_arg, DB_MAX_IDENTIFIER_LENGTH);
}

void
clientids::set_user (const char *db_user_arg)
{
  string_ncopy (db_user, db_user_arg, LOG_USERNAME_MAX);
}

void
clientids::set_program_name (const char *program_name_arg)
{
  char prgname_buf[PATH_MAX] = {'\0'};

  if (program_name_arg == NULL || basename_r (program_name_arg, prgname_buf, PATH_MAX) < 0)
    {
      string_ncopy (program_name, UNKNOWN_ID, PATH_MAX);
    }
  else
    {
      string_ncopy (program_name, prgname_buf, PATH_MAX);
    }
}

void
clientids::set_login_name (const char *login_name_arg)
{
  string_ncopy (login_name, login_name_arg, L_cuserid);
}

void
clientids::set_host_name (const char *host_name_arg)
{
  string_ncopy (host_name, host_name_arg, CUB_MAXHOSTNAMELEN);
}

void
clientids::set_client_ip_addr (const char *client_ip_addr_arg)
{
  if (client_ip_addr_arg == NULL)
    {
      string_ncopy (client_ip_addr, UNKNOWN_ID, 16);
    }
  else
    {
      string_ncopy (client_ip_addr, client_ip_addr_arg, 16);
    }
}

void
clientids::set_system_internal ()
{
  reset ();
  client_type = DB_CLIENT_TYPE_SYSTEM_INTERNAL;
}

void
clientids::set_system_internal_with_user (const char *db_user_arg)
{
  set_system_internal ();
  db_user = db_user_arg;
}

void
clientids::reset ()
{
  client_info.clear ();
  db_user.clear ();
  program_name.clear ();
  login_name.clear ();
  host_name.clear ();
  client_ip_addr.clear ();
  process_id = 0;
  client_type = DB_CLIENT_TYPE_UNKNOWN;
}

//
// packing of clientids
//

#define CLIENTID_PACKER_ARGS(client_type_as_int) \
  client_type_as_int, client_info, db_user, program_name, login_name, host_name, client_ip_addr, process_id

size_t
clientids::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
{
  return serializator.get_all_packed_size_starting_offset (start_offset,
	 CLIENTID_PACKER_ARGS (static_cast<int> (client_type)));
}

void
clientids::pack (cubpacking::packer &serializator) const
{
  serializator.pack_all (CLIENTID_PACKER_ARGS (static_cast<int> (client_type)));
}

void
clientids::unpack (cubpacking::unpacker &deserializator)
{
  int read_int;
  deserializator.unpack_all (CLIENTID_PACKER_ARGS (read_int));
  client_type = static_cast<db_client_type> (read_int);
}

//
// boot_client_credential
//

boot_client_credential::boot_client_credential ()
  : clientids ()
  , db_name {}
  , db_password {}
  , preferred_hosts (NULL)
  , connect_order (0)
{
}

boot_client_credential::~boot_client_credential ()
{
}

const char *
boot_client_credential::get_db_name () const
{
  return db_name.c_str ();
}

const char *
boot_client_credential::get_db_password () const
{
  return db_password.c_str ();
}

//
// packing of boot_client_credential
//

#define BOOTCLCRED_PACKER_ARGS \
  db_name, db_password

size_t
boot_client_credential::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
{
  std::size_t size = clientids::get_packed_size (serializator, start_offset);
  return size + serializator.get_all_packed_size_starting_offset (size, BOOTCLCRED_PACKER_ARGS);
}

void
boot_client_credential::pack (cubpacking::packer &serializator) const
{
  clientids::pack (serializator);
  serializator.pack_all (BOOTCLCRED_PACKER_ARGS);
}

void
boot_client_credential::unpack (cubpacking::unpacker &deserializator)
{
  clientids::unpack (deserializator);
  deserializator.unpack_all (BOOTCLCRED_PACKER_ARGS);
}
