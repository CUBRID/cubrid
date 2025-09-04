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

/*
 * px_hash_join_spawn_manager.cpp
 */

#include "px_hash_join_spawn_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace hash_join
  {
    spawn_manager::spawn_manager (cubthread::entry &thread_ref)
      : m_thread_ref (thread_ref)
      , m_spawner (nullptr)
      , m_val_descr (nullptr)
      , m_during_join_pred (nullptr)
      , m_outer_regu_list_pred (nullptr)
      , m_inner_regu_list_pred (nullptr)
    {
      //
    }

    spawn_manager::~spawn_manager () noexcept
    {
      if (m_spawner != nullptr)
	{
	  m_spawner->~spawner();
	  db_private_free_and_init (&m_thread_ref, m_spawner);
	}
    }

    spawn_manager *
    spawn_manager::get_instance (cubthread::entry &thread_ref) noexcept
    {
      if (tls_spawn_manager == nullptr)
	{
	  void *raw_memory = db_private_alloc (&thread_ref, sizeof (spawn_manager));
	  if (raw_memory == nullptr)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      return nullptr;
	    }

	  try
	    {
	      /* placement new */
#undef new
	      new (raw_memory) spawn_manager (thread_ref);
#define new new(__FILE__, __LINE__)

	      tls_spawn_manager = (spawn_manager *) raw_memory;
	    }
	  catch ( ...)
	    {
	      /* cleanup */
	      db_private_free_and_init (&thread_ref, raw_memory);

	      assert_release_error (er_errid () != NO_ERROR);
	      return nullptr;
	    }
	}

      return tls_spawn_manager;
    }

    void
    spawn_manager::destroy_instance () noexcept
    {
      if (tls_spawn_manager != nullptr)
	{
	  cubthread::entry &thread_ref = tls_spawn_manager->get_thread_ref();
	  tls_spawn_manager->~spawn_manager();
	  db_private_free_and_init (&thread_ref, tls_spawn_manager);
	}
    }

    cubthread::entry &
    spawn_manager::get_thread_ref () const noexcept
    {
      return m_thread_ref;
    }

    VAL_DESCR *
    spawn_manager::get_val_descr (VAL_DESCR *src)
    {
      return spawn (src, m_val_descr);
    }

    PRED_EXPR *
    spawn_manager::get_during_join_pred (PRED_EXPR *src)
    {
      return spawn (src, m_during_join_pred);
    }

    REGU_VARIABLE_LIST
    spawn_manager::get_outer_regu_list_pred (REGU_VARIABLE_LIST src)
    {
      return spawn (src, m_outer_regu_list_pred);
    }

    REGU_VARIABLE_LIST
    spawn_manager::get_inner_regu_list_pred (REGU_VARIABLE_LIST src)
    {
      return spawn (src, m_inner_regu_list_pred);
    }

    cubxasl::spawner *
    spawn_manager::get_spawner () noexcept
    {
      if (m_spawner == nullptr)
	{
	  void *raw_memory =  db_private_alloc (&m_thread_ref, sizeof (cubxasl::spawner));
	  if (raw_memory == nullptr)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      return nullptr;
	    }

	  try
	    {
	      /* placement new */
#undef new
	      new (raw_memory) cubxasl::spawner (m_thread_ref);
#define new new(__FILE__, __LINE__)

	      m_spawner = (cubxasl::spawner *) raw_memory;
	    }
	  catch ( ...)
	    {
	      /* cleanup */
	      db_private_free_and_init (&m_thread_ref, raw_memory);

	      assert_release_error (er_errid () != NO_ERROR);
	      return nullptr;
	    }
	}

      return m_spawner;
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
