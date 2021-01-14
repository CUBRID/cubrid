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
 * load_sa_loader.hpp: Loader client definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOAD_SA_LOADER_HPP_
#define _LOAD_SA_LOADER_HPP_

#include "load_common.hpp"

namespace cubload
{

  class sa_class_installer : public class_installer
  {
    public:
      sa_class_installer () = default;
      ~sa_class_installer () override = default;

      void set_class_id (class_id clsid) override;

      void check_class (const char *class_name, int class_id) override;
      int install_class (const char *class_name) override;
      void install_class (string_type *class_name, class_command_spec_type *cmd_spec) override;
  };

  class sa_object_loader : public object_loader
  {
    public:
      sa_object_loader () = default;
      ~sa_object_loader () override = default;

      void init (class_id clsid) override;
      void destroy () override;

      void start_line (int object_id) override;
      void process_line (constant_type *cons) override;
      void finish_line () override;
      void flush_records () override;
      std::size_t get_rows_number () override;
  };
}

/* start load functions */
void ldr_sa_load (cubload::load_args *args, int *status, bool *interrupted);

/* log functions */
void print_log_msg (int verbose, const char *fmt, ...);

void ldr_increment_err_total ();
void ldr_increment_fails ();

#endif /* _LOAD_SA_LOADER_HPP_ */
