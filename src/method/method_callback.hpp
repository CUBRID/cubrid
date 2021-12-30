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

//
// method_callback.hpp: implement callback for server-side driver's request
//

#ifndef _METHOD_CALLBACK_HPP_
#define _METHOD_CALLBACK_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "method_def.hpp"
#include "method_error.hpp"
#include "method_oid_handler.hpp"
#include "method_query_handler.hpp"
#include "method_struct_query.hpp"

#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  class callback_handler
  {
    public:
      callback_handler (int max_query_handler);
      ~callback_handler ();

      int callback_dispatch (packing_unpacker &unpacker);

      /* query handler required */
      int prepare (packing_unpacker &unpacker);
      int execute (packing_unpacker &unpacker);
      int schema_info (packing_unpacker &unpacker);
      int make_out_resultset (packing_unpacker &unpacker);
      int generated_keys (packing_unpacker &unpacker);

      /* handle related to OID */
      int oid_get (packing_unpacker &unpacker);
      int oid_put (packing_unpacker &unpacker);
      int oid_cmd (packing_unpacker &unpacker);
      int collection_cmd (packing_unpacker &unpacker);

      void set_server_info (int idx, int rc, char *host);
      void free_query_handle_all (bool is_free);

      query_handler *get_query_handler_by_qid (uint64_t qid);

    private:
      /* ported from cas_handle */
      int new_query_handler ();
      query_handler *find_query_handler (int id);
      void free_query_handle (int id, bool is_free);

      int new_oid_handler ();

      /* statement handler cache */
      int find_query_handler_by_sql (std::string &sql);


      template<typename ... Args>
      int send_packable_object_to_server (Args &&... args);

      /* server info */
      method_server_conn_info m_conn_info [METHOD_MAX_RECURSION_DEPTH];

      std::multimap <std::string, int> m_sql_handler_map;
      std::unordered_map <uint64_t, int> m_qid_handler_map;

      error_context m_error_ctx;

      std::vector<query_handler *> m_query_handlers;
      oid_handler *m_oid_handler;
  };

  //////////////////////////////////////////////////////////////////////////
  // global functions
  //////////////////////////////////////////////////////////////////////////

  callback_handler *get_callback_handler (void);

} // namespace cubmethod

extern int method_make_out_rs (DB_BIGINT query_id);

#endif // _METHOD_CALLBACK_HPP_
