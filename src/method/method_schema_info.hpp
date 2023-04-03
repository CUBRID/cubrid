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

#ifndef _METHOD_SCHEMA_INFO_HPP_
#define _METHOD_SCHEMA_INFO_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <vector>

#include "method_error.hpp"
#include "method_struct_schema_info.hpp"
#include "method_query_result.hpp"
#include "storage_common.h"

namespace cubmethod
{
  struct class_table
  {
    std::string class_name;
    short class_type;
  };

  struct priv_table
  {
    std::string class_name;
    char priv;
    char grant;
  };

  struct fk_info_result
  {
    struct fk_info_result *prev;
    struct fk_info_result *next;

    std::string pktable_name;
    std::string pkcolumn_name;
    std::string fktable_name;
    std::string fkcolumn_name;
    short key_seq;
    SM_FOREIGN_KEY_ACTION update_action;
    SM_FOREIGN_KEY_ACTION delete_action;
    std::string fk_name;
    std::string pk_name;
  };

  class query_handler;

  class schema_info_handler
  {
    public:
      schema_info_handler (error_context &ctx);
      ~schema_info_handler ();

      schema_info get_schema_info (int schema_type, std::string &arg1, std::string &arg2, int flag);

    protected:
      int execute_schema_info_query (std::string &sch_query);

      int sch_class_info (schema_info &info, std::string &class_name, int pattern_flag,
			  int v_class_flag);
      int sch_queryspec (schema_info &info, std::string &class_name);
      int sch_attr_info (schema_info &info, std::string &class_name, std::string &attr_name,
			 int pattern_flag,
			 int class_attr_flag);
      int sch_method_info (schema_info &info, std::string &class_name, int flag);
      int sch_methfile_info (schema_info &info, std::string &class_name);
      int sch_superclass (schema_info &info, std::string &class_name, int flag);
      int sch_constraint (schema_info &info, std::string &class_name);
      int sch_trigger (schema_info &info, std::string &class_name, int flag);
      int sch_class_priv (schema_info &info, std::string &class_name, int pat_flag);
      int sch_attr_priv (schema_info &info, std::string &class_name, std::string &attr_name_pat,
			 int pat_flag);
      int sch_direct_super_class (schema_info &info, std::string &class_name, int flag);
      int sch_primary_key (schema_info &info, std::string &class_name);
      int sch_imported_keys (schema_info &info, std::string &fktable_name);
      int sch_exported_keys_or_cross_reference (schema_info &info, std::string &pktable_name,
	  std::string &fktable_name,
	  bool find_cross_ref);

      int class_type (DB_OBJECT *class_obj);
      int set_priv_table (std::vector<priv_table> &pts, int index, char *name, unsigned int class_priv);
    private:

      void close_and_free_session ();

      error_context &m_error_ctx;

      // sch_class_info, sch_queryspec, sch_direct_super_class, sch_primary_key
      DB_SESSION *m_session;
      query_result m_q_result;

      DB_METHOD *m_method_list; // sch_method_info
      DB_METHFILE *m_method_files; // sch_methfile_info
      std::vector<class_table> m_obj_list; // sch_superclass
      DB_CONSTRAINT *m_constraint; // sch_constraint
      DB_OBJLIST *m_all_trigger; // sch_trigger
      std::vector<priv_table> m_priv_tbl; // sch_class_priv, sch_attr_priv
      fk_info_result *m_fk_res; // sch_import_keys , sch_exported_keys_or_cross_reference
  };
}

#endif
