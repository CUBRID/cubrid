/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * load_sa_loader.hpp: Loader client definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOAD_SA_LOADER_HPP_
#define _LOAD_SA_LOADER_HPP_

#include "load_common.hpp"

/* *INDENT-OFF* */
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
  };
}
/* *INDENT-ON* */

/* start load functions */
void ldr_sa_load (cubload::load_args *args, int *status, bool *interrupted);

/* log functions */
void print_log_msg (int verbose, const char *fmt, ...);

void ldr_increment_err_total ();
void ldr_increment_fails ();

#endif /* _LOAD_SA_LOADER_HPP_ */
