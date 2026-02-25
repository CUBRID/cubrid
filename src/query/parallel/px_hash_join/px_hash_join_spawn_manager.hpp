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
 * px_hash_join_spawn_manager.hpp
 */

#pragma once

#include "error_manager.h"		/* er_errid, NO_ERROR, assert_release_error */
#include "thread_entry.hpp"		/* cubthread::entry */
#include "xasl_spawner.hpp"		/* cubxasl::spawner */

namespace parallel_query
{
  namespace hash_join
  {
    class spawn_manager
    {
      public:
	explicit spawn_manager (cubthread::entry &thread_ref);
	~spawn_manager () noexcept;

	spawn_manager (const spawn_manager &) = delete;
	spawn_manager &operator= (const spawn_manager &) = delete;
	spawn_manager (spawn_manager &&) = delete;
	spawn_manager &operator= (spawn_manager &&) = delete;

	static spawn_manager *get_instance (cubthread::entry &thread_ref) noexcept;
	static void destroy_instance () noexcept;

	cubthread::entry &get_thread_ref () const noexcept;

	/* get_val_descr must be called first,
	 * because it creates a DB_VALUE reused by other spawned structures. */
	VAL_DESCR *get_val_descr (VAL_DESCR *src);
	PRED_EXPR *get_during_join_pred (PRED_EXPR *src);
	REGU_VARIABLE_LIST get_outer_regu_list_pred (REGU_VARIABLE_LIST src);
	REGU_VARIABLE_LIST get_inner_regu_list_pred (REGU_VARIABLE_LIST src);

      private:
	cubthread::entry &m_thread_ref;

	cubxasl::spawner *m_spawner;
	VAL_DESCR *m_val_descr;
	PRED_EXPR *m_during_join_pred;
	REGU_VARIABLE_LIST m_outer_regu_list_pred;
	REGU_VARIABLE_LIST m_inner_regu_list_pred;

	inline static thread_local spawn_manager *tls_spawn_manager = nullptr;

	cubxasl::spawner *get_spawner() noexcept;

	template <typename T>
	T *spawn (T *src, T *&dest) noexcept;
    };
  } /* namespace hash_join */
} /* namespace parallel_query */

/*
 * Function Definitions
 */

namespace parallel_query
{
  namespace hash_join
  {
    template <typename T>
    T *
    spawn_manager::spawn (T *src, T *&dest) noexcept
    {
      if (dest != nullptr)
	{
	  return dest;
	}

      auto *spawner = get_spawner ();
      if (spawner == nullptr)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return nullptr;
	}

      try
	{
	  dest = spawner->spawn (src);
	}
      catch (...)
	{
	  assert_release_error (er_errid() != NO_ERROR);
	  dest = nullptr;
	}

      return dest;
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
