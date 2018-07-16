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
 * loader_sr.cpp: Loader server definitions. Updated using design from fast loaddb prototype
 */

#ident "$Id$"

#include <cassert>

#include "loader_sr.hpp"

namespace cubload
{

  void
  ldr_act_setup_class_command_spec (LDR_STRING **class_name, LDR_CLASS_COMMAND_SPEC **cmd_spec)
  {
    //
  }

  void
  ldr_load_failed_error ()
  {
    //
  }

  void ldr_increment_fails ()
  {
    //
  }

  void ldr_string_free (LDR_STRING **str)
  {
    //
  }

  void ldr_increment_err_total ()
  {
    //
  }

  void ldr_act_finish (int parse_error)
  {
    //
  }

  void ldr_act_finish_line ()
  {
    //
  }

  void ldr_act_start_id (char *name)
  {
    //
  }

  void ldr_act_set_id (int id)
  {
    //
  }

  void ldr_act_start_instance (int id, LDR_CONSTANT *cons)
  {
    //
  }

  void ldr_process_constants (LDR_CONSTANT *c)
  {
    //
  }
} // namespace cubload
