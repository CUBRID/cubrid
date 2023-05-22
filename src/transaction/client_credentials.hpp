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
    std::string client_ip_addr;
    int process_id;

    clientids ();
    ~clientids () override;

    const char *get_client_info () const;
    const char *get_db_user () const;
    const char *get_program_name () const;
    const char *get_login_name () const;
    const char *get_host_name () const;
    const char *get_client_ip_addr () const;

    void set_ids (db_client_type type, const char *client_info, const char *db_user, const char *program_name,
		  const char *login_name, const char *host_name, const char *client_ip_addr, int process_id);
    void set_ids (const clientids &other);
    void set_user (const char *db_user);

    void set_system_internal ();
    void set_system_internal_with_user (const char *db_user);
    void reset ();

    // packable_object
    virtual size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const override;
    virtual void pack (cubpacking::packer &serializator) const override;
    virtual void unpack (cubpacking::unpacker &deserializator) override;

    static const char *UNKNOWN_ID;

  private:
    void set_client_info (const char *client_info);
    void set_program_name (const char *program_name);
    void set_login_name (const char *login_name);
    void set_host_name (const char *host_name);
    void set_client_ip_addr (const char *client_ip_addr);
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
  virtual size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const override;
  virtual void pack (cubpacking::packer &serializator) const override;
  virtual void unpack (cubpacking::unpacker &deserializator) override;
};

#endif // !_CLIENT_CREDENTIALS_HPP_
