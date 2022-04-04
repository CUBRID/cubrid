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
#include "cas_handle_procedure.hpp"

#include <cassert>
#include <algorithm>

/* implemented in transaction_cl.c */
extern void tran_begin_libcas_function (void);
extern void tran_end_libcas_function (void);
extern bool tran_is_in_libcas (void);

/* implemented in cas.c */
extern void libcas_srv_handle_free (int h_id);

void
cas_procedure_handle_table::iterate_by_key (int key, const map_func_type &func)
{
  auto const &r = srv_handler_map.equal_range (key);
  for (auto it = r.first /* lower */; it != r.second /* upper */; ++it)
    {
      if (!func (it))
	{
	  break;
	}
    }
}

void
cas_procedure_handle_table::add (int key, int value)
{
  // To prevent self-destorying, it should not happen
  assert (key != value);
  srv_handler_map.emplace (key, value);
}

void
cas_procedure_handle_table::remove (int key, int value)
{
  auto remove_value = [&] (const map_iter_type& it)
  {
    if (it->second == value)
      {
	srv_handler_map.erase (it);
	return false; /* stop iteration */
      }
    else
      {
	return true;
      }
  };

  iterate_by_key (key, remove_value);
}

void
cas_procedure_handle_table::destroy (int key)
{
  auto destroy_srv_handle = [&] (const map_iter_type& it)
  {
    libcas_srv_handle_free (it->second);
    return true;
  };

  tran_begin_libcas_function ();
  iterate_by_key (key, destroy_srv_handle);
  tran_end_libcas_function ();

  srv_handler_map.erase (key);
}

void
cas_procedure_handle_free (cas_procedure_handle_table &handle_table, int current_handle_id, int sp_h_id)
{
  if (tran_is_in_libcas ())
    {
      /* it will be removed by srv_handler_map.erase (key) in cas_procedure_handle_table::destroy() */
      /* do nothing */
    }
  else
    {
      /* destory nested query handlers by Server-side JDBC first */
      handle_table.destroy (sp_h_id);
    }
}

void
cas_procedure_handle_add (cas_procedure_handle_table &handle_table, int current_handle_id, int sp_h_id)
{
  if (tran_is_in_libcas ())
    {
      /* register handler id created from server-side JDBC */
      handle_table.add (current_handle_id, sp_h_id);
    }
  else
    {
      /* do nothing */
    }
}
