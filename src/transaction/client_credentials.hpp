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

//
// clientid.hpp -
//

#ifndef _CLIENT_CREDENTIALS_HPP_
#define _CLIENT_CREDENTIALS_HPP_

#include "dbtype_def.h"
#include "db_client_type.hpp"
#include "packable_object.hpp"

#include <string>

/* BOOT_CLIENT_TYPE : needed for legacy code */
typedef enum db_client_type BOOT_CLIENT_TYPE;

const size_t LOG_USERNAME_MAX = DB_MAX_USER_LENGTH + 1;

typedef struct clientids CLIENTIDS;
struct clientids : public cubpacking::packable_object
{
  public:
    db_client_type client_type;
    std::string client_info;
    std::string db_user;
    std::string program_name;
    std::string login_name;
    std::string host_name;
    int process_id;

    clientids ();
    ~clientids () override;

    const char *get_client_info () const;
    const char *get_db_user () const;
    const char *get_program_name () const;
    const char *get_login_name () const;
    const char *get_host_name () const;

    void set_ids (db_client_type type, const char *client_info, const char *db_user, const char *program_name,
		  const char *login_name, const char *host_name, int process_id);
    void set_ids (const clientids &other);
    void set_user (const char *db_user);

    void set_system_internal ();
    void set_system_internal_with_user (const char *db_user);
    void reset ();

    // packable_object
    virtual size_t get_packed_size (cubpacking::packer &serializator) const override;
    virtual void pack (cubpacking::packer &serializator) const override;
    virtual void unpack (cubpacking::unpacker &deserializator) override;

    static const char *UNKNOWN_ID;

  private:
    void set_client_info (const char *client_info);
    void set_program_name (const char *program_name);
    void set_login_name (const char *login_name);
    void set_host_name (const char *host_name);
};

typedef struct boot_client_credential BOOT_CLIENT_CREDENTIAL;
struct boot_client_credential : public clientids
{
  std::string db_name;		/* DB_MAX_IDENTIFIER_LENGTH */
  std::string db_password;		/* DB_MAX_PASSWORD_LENGTH */
  char *preferred_hosts;	/* LINE_MAX */
  int connect_order;

  boot_client_credential ();
  ~boot_client_credential () override;

  const char *get_db_name () const;
  const char *get_db_password () const;

  // packable_object
  virtual size_t get_packed_size (cubpacking::packer &serializator) const override;
  virtual void pack (cubpacking::packer &serializator) const override;
  virtual void unpack (cubpacking::unpacker &deserializator) override;
};

#endif // !_CLIENT_CREDENTIALS_HPP_
