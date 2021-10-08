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

#ifndef _METHOD_OID_HANDLER_HPP_
#define _METHOD_OID_HANDLER_HPP_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <vector>

#include "method_error.hpp"
#include "method_struct_oid_info.hpp"

namespace cubmethod
{
  /*
   * cubmethod::oid_handler
   *
   * description
   * To support CUBIRD OID related functions
   *
   * how to use
   *
   */
  class oid_handler
  {
    public:
      oid_handler (error_context &ctx)
	: m_error_ctx (ctx)
      {
	//
      }
      ~oid_handler ();

      /* request */
      oid_get_info oid_get (DB_OBJECT *obj, std::vector<std::string> &attr_names);
      int oid_put (DB_OBJECT *obj, std::vector<std::string> &attr_names, std::vector<DB_VALUE> &attr_values);

    protected:
      char get_attr_type (DB_OBJECT *obj, const char *attr_name);


    private:
      error_context &m_error_ctx;
  };

} // cubmethod

#endif // _METHOD_OID_HANDLER_HPP_