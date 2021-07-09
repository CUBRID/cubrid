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

#include <functional>
#include <map>

#ifndef _CAS_HANDLE_PROCEDURE_
#define _CAS_HANDLE_PROCEDURE_

class cas_procedure_handle_table
{
  public:
    using map_iter_type = std::multimap<int, int>::iterator;
    using map_func_type = std::function<bool (const map_iter_type &)>;

    cas_procedure_handle_table () = default;
    ~cas_procedure_handle_table () { /* do nothing */ }

    cas_procedure_handle_table (const cas_procedure_handle_table &other) = delete;
    cas_procedure_handle_table (cas_procedure_handle_table &&other) = delete;
    cas_procedure_handle_table &operator= (const cas_procedure_handle_table &other) = delete;
    cas_procedure_handle_table &operator= (cas_procedure_handle_table &&other) = delete;

    void add (int key, int value);
    void remove (int key, int value);
    void destroy (int key);

  private:
    void iterate_by_key (int key, const map_func_type &func);

    /* key: current executing query's srv_h_id by client, value: nested query's srv_h_id by server-side JDBC */
    std::multimap <int, int> srv_handler_map;
};

/* wrapper functions to use cas_procedure_handle_table at cas_handle.c */
void cas_procedure_handle_free (cas_procedure_handle_table &handle_table, int current_handle_id, int sp_h_id);
void cas_procedure_handle_add (cas_procedure_handle_table &handle_table, int current_handle_id, int sp_h_id);

#endif /* _CAS_HANDLE_PROCEDURE_ */
