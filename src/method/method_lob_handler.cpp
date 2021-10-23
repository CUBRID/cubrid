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

#include "method_lob_handler.hpp"

#include "dbtype.h"
#include "dbtype_def.h"

#include "db_elo.h"
#include "mem_block.hpp"

namespace cubmethod
{
  lob_handler::lob_handler () : m_is_buffer_initialized (false)
  {
    //
  }

  int
  lob_handler::lob_new (DB_TYPE lob_type, lob_info &info)
  {
    /* check lob_type */
    if (lob_type != DB_TYPE_BLOB && lob_type != DB_TYPE_CLOB)
      {
	return ER_FAILED;
      }

    DB_VALUE lob_dbval;
    int error = db_create_fbo (&lob_dbval, lob_type);
    if (error < 0)
      {
	// TODO: error handling
	return error;
      }

    DB_ELO *elo = db_get_elo (&lob_dbval);
    if (elo == NULL)
      {
	// TODO: error handling
	return ER_FAILED;
      }

    // TODO: return elo
    info.db_type = lob_type;
    db_elo_copy_structure (elo, &info.lob_handle);

    db_value_clear (&lob_dbval);
    return error;
  }

  int
  lob_handler::lob_write (DB_VALUE *lob_dbval, DB_BIGINT offset, cubmem::block &blk, DB_BIGINT &size_written)
  {
    DB_ELO *elo = db_get_elo (lob_dbval);
    if (elo == NULL)
      {
	// TODO: error handling
	return ER_FAILED;
      }

    int error = db_elo_write (elo, offset, blk.ptr, blk.dim, &size_written);
    if (error < 0)
      {
	// TODO: error handling
	return ER_FAILED;
      }

    return error;
  }

  int
  lob_handler::lob_read (DB_VALUE *lob_dbval, DB_BIGINT offset, int size, cubmem::extensible_block &eb,
			 DB_BIGINT &size_read)
  {
    DB_ELO *elo = db_get_elo (lob_dbval);
    if (elo == NULL)
      {
	// TODO: error handling
	return ER_FAILED;
      }

    eb.extend_to (size);
    int error = db_elo_read (elo, offset, eb.get_ptr(), size, &size_read);
    if (error < 0)
      {
	// TODO: error handling
	return ER_FAILED;
      }

    return error;
  }

  cubmem::block
  lob_handler::get_lob_buffer ()
  {
    if (m_is_buffer_initialized == false)
      {
	m_is_buffer_initialized = true;
	m_lob_buffer.extend_to (LOB_MAX_IO_LENGTH);
      }

    return cubmem::block (LOB_MAX_IO_LENGTH, m_lob_buffer.get_ptr());
  }

} //namespace cubmethod
