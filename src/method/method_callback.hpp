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
// method_callback.hpp: implement callback for server-side JDBC's request
//

#ifndef _METHOD_CALLBACK_HPP_
#define _METHOD_CALLBACK_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <unordered_map>
#include <queue>

#include "method_connection_cl.hpp"
#include "method_def.hpp"
#include "method_error.hpp"
#include "method_oid_handler.hpp"
#include "method_query_handler.hpp"
#include "method_struct_invoke.hpp"
#include "method_struct_query.hpp"

#include "transaction_cl.h"

#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  /*
   * cubmethod::callback_handler
   *
   * description
   *    This class serves as an entry point to perform CAS functions requested by Server-side JDBC.
   *
   *    * callback_dispatch() is the main public function that performs each function according to the code in the header of the request.
   *    * free_query_handle_all () frees all used resources at the end of each Stored procedure call.
   *
   */

  class EXPORT_IMPORT callback_handler
  {
    public:
      callback_handler (int max_query_handler);
      ~callback_handler ();

      callback_handler (callback_handler &&other) = delete; // Not MoveConstructible
      callback_handler (const callback_handler &copy) = delete; // Not CopyConstructible

      callback_handler &operator= (callback_handler &&other) = delete; // Not MoveAssignable
      callback_handler &operator= (const callback_handler &copy) = delete; // Not CopyAssignable

      /* main functions */
      int callback_dispatch (packing_unpacker &unpacker);

      void free_query_handle (int id, bool is_free);
      void free_query_handle_all (bool is_free);

      /* find query handler */
      query_handler *get_query_handler_by_id (const int id);
      query_handler *get_query_handler_by_query_id (const uint64_t qid); /* used for out resultset */
      query_handler *get_query_handler_by_sql (const std::string &sql); /* used for statement handler cache */

      oid_handler *get_oid_handler ();

    private:
      /* handle related to query */
      int prepare (packing_unpacker &unpacker);
      int execute (packing_unpacker &unpacker);
      int make_out_resultset (packing_unpacker &unpacker);
      int generated_keys (packing_unpacker &unpacker);

      /* handle related to OID */
      int oid_get (packing_unpacker &unpacker);
      int oid_put (packing_unpacker &unpacker);
      int oid_cmd (packing_unpacker &unpacker);
      int collection_cmd (packing_unpacker &unpacker);

      /* ported from cas_handle */
      query_handler *new_query_handler ();

      std::multimap <std::string, int> m_sql_handler_map;
      std::unordered_map <uint64_t, int> m_qid_handler_map;

      error_context m_error_ctx;

      std::vector<query_handler *> m_query_handlers;
      oid_handler *m_oid_handler;
  };

  //////////////////////////////////////////////////////////////////////////
  // global functions
  //////////////////////////////////////////////////////////////////////////

  EXPORT_IMPORT callback_handler *get_callback_handler (void);

} // namespace cubmethod

#endif // _METHOD_CALLBACK_HPP_
