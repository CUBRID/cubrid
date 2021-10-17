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

      void set_server_info (int rc, char *host);
      void free_query_handle_all ();

    protected:
      int col_get (DB_COLLECTION *col, DB_TYPE col_type, DB_TYPE ele_type, DB_DOMAIN *ele_domain);
      int col_size (DB_COLLECTION *col);
      int col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val);
      int col_set_add (DB_COLLECTION *col, DB_VALUE *ele_val);
      int col_seq_drop (DB_COLLECTION *col, int seq_index);
      int col_seq_insert (DB_COLLECTION *col, int seq_index, DB_VALUE *ele_val);
      int col_seq_put (DB_COLLECTION *col, int seq_index, DB_VALUE *ele_val);

// TODO: implement remaining functions on another issues
#if 0
      int lob_new (DB_TYPE lob_type);
      int lob_write (DB_VALUE *lob_dbval, int64_t offset, int size,  char *data);
      int lob_read (DB_TYPE lob_type);
#endif

    private:
      int check_object (DB_OBJECT *obj);

      /* ported from cas_handle */
      int new_query_handler ();
      query_handler *find_query_handler (int id);
      void free_query_handle (int id);

      template<typename ... Args>
      int send_packable_object_to_server (Args &&... args);

      /* server info */
      int m_rid; // method callback's rid
      char *m_host;

      error_context m_error_ctx;

      std::vector<query_handler *> m_query_handlers;
      oid_handler *m_oid_handler;
  };
}

#endif
