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

#include "method_error.hpp"
#include "method_oid_handler.hpp"
#include "method_query_handler.hpp"
#include "method_struct_query.hpp"

#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  struct lob_handle
  {
    int db_type;
    INT64 lob_size;
    int locator_size;
    char *locator;
  };
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

      /* handle related to OID */
      int oid_get (packing_unpacker &unpacker);
      int oid_put (packing_unpacker &unpacker);
      int oid_cmd (packing_unpacker &unpacker);
      int collection_cmd (packing_unpacker &unpacker);

// TODO: implement remaining functions on another issues
#if 0
      int lob_new (DB_TYPE lob_type);
      int lob_write (DB_VALUE *lob_dbval, int64_t offset, int size,  char *data);
      int lob_read (DB_TYPE lob_type);
#endif

      void set_server_info (int rc, char *host);
      void free_query_handle_all (bool is_free);

    private:
      /* ported from cas_handle */
      int new_query_handler ();
      query_handler *find_query_handler (int id);
      void free_query_handle (int id, bool is_free);

      int new_oid_handler ();

      /* statement handler cache */
      int find_query_handler_cache (std::string &sql);

      template<typename ... Args>
      int send_packable_object_to_server (Args &&... args);

      /* server info */
      int m_rid; // method callback's rid
      char *m_host;

      std::multimap <std::string, int> m_query_handler_map;

      error_context m_error_ctx;

      std::vector<query_handler *> m_query_handlers;
      oid_handler *m_oid_handler;
  };
}

#endif
