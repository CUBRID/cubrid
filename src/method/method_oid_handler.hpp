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

#include "dbtype_def.h"
#include "method_error.hpp"
#include "method_struct_oid_info.hpp"

namespace cubmethod
{
  /* OID_CMD are ported from cas_cci.h */
  enum OID_CMD
  {
    OID_CMD_FIRST = 1,

    OID_DROP = 1,
    OID_IS_INSTANCE = 2,
    OID_LOCK_READ = 3,
    OID_LOCK_WRITE = 4,
    OID_CLASS_NAME = 5,

    OID_CMD_LAST = OID_CLASS_NAME
  };

  enum COLLECTION_CMD
  {
    COL_CMD_FIRST = 1,
    COL_GET = 1,
    COL_SIZE = 2,
    COL_SET_DROP = 3,
    COL_SET_ADD = 4,
    COL_SEQ_DROP = 5,
    COL_SEQ_INSERT = 6,
    COL_SEQ_PUT = 7,
    COL_CMD_LAST = COL_SEQ_PUT
  };

  /*
   * cubmethod::oid_handler
   *
   * description
   * To support CUBIRD OID (including collection) related functions
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

      oid_get_info oid_get (OID &oid, std::vector<std::string> &attr_names);
      int oid_put (OID &oid, std::vector<std::string> &attr_names, std::vector<DB_VALUE> &attr_values);
      int oid_cmd (OID &oid, int cmd, std::string &res_msg);
      int collection_cmd (OID &oid, int cmd, int index, std::string &attr_name, DB_VALUE &value);

    protected:
      int check_object (DB_OBJECT *obj);
      char get_attr_type (DB_OBJECT *obj, const char *attr_name);

      /* collection related commands */
      int col_get (DB_COLLECTION *col, DB_TYPE col_type, DB_TYPE ele_type, DB_DOMAIN *ele_domain);
      int col_size (DB_COLLECTION *col);
      int col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val);
      int col_set_add (DB_COLLECTION *col, DB_VALUE *ele_val);
      int col_seq_drop (DB_COLLECTION *col, int seq_index);
      int col_seq_insert (DB_COLLECTION *col, int seq_index, DB_VALUE *ele_val);
      int col_seq_put (DB_COLLECTION *col, int seq_index, DB_VALUE *ele_val);

    private:
      error_context &m_error_ctx;
  };

} // namespace cubmethod

#endif // _METHOD_OID_HANDLER_HPP_
