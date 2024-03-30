/*
 *
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

/*
 * authenticate_auth_access.hpp -
 */

#ifndef _AUTHENTICATE_AUTH_ACCESS_HPP_
#define _AUTHENTICATE_AUTH_ACCESS_HPP_

#define AU_AUTH_ATTR_OWNER     "owner"
#define AU_AUTH_ATTR_GRANTS    "grants"

#define AU_AUTH_ATTR_GRANTOR    "grantor"
#define AU_AUTH_ATTR_GRANTEE    "grantee"

#include "dbtype_def.h"


//
#include "authenticate_grant.hpp"
#include "set_object.h"
#include "dbtype.h"
#include "error_manager.h"
#include "object_accessor.h"

class au_auth_accessor
{
  private:
    // TODO: thread safe?
    static MOP au_class_mop;

    MOP m_au_obj;

    enum
    {
      INDEX_FOR_GRANTEE_NAME = 0,
      INDEX_FOR_GRANTOR_NAME = 1,
      INDEX_FOR_CLASS_NAME = 2,
      INDEX_FOR_AUTH_TYPE = 3,
      /* Total count for the above */
      COUNT_FOR_VARIABLES
    };

    int create_new_auth ();
    int set_new_auth (MOP au_object, MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type, bool grant_option);
    int get_new_auth (MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type);

  public:
    explicit au_auth_accessor ();

    int insert_auth (MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type, int grant_option);
    int update_auth (MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type, int grant_option);
    int delete_auth (MOP grantor, MOP user, MOP class_mop, DB_AUTH auth_type);

    static MOP get_auth_class_mop ()
    {
      return au_class_mop;
    }

    MOP get_auth_object ()
    {
      return m_au_obj;
    }
};

#endif // _AUTHENTICATE_AUTH_ACCESS_HPP_