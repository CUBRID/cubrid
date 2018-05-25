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
 * object_printer.hpp - parser specific, object_print related code
 */

#ifndef _OBJECT_PRINTER_HPP_
#define _OBJECT_PRINTER_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

#include "class_description.hpp"

struct db_object;
struct sm_attribute;
struct sm_class;
struct sm_class_constraint;
struct sm_method;
struct sm_method_argument;
struct sm_method_file;
struct sm_method_signature;
struct sm_partition;
struct sm_resolution;
struct tp_domain;
struct tr_triglist;
struct tr_trigger;
class string_buffer;

namespace object_print
{
  struct strlist;
}

class object_printer
{
  private:
    string_buffer &m_buf;
  public:
    object_printer (string_buffer &buf)
      : m_buf (buf)
    {}

    void describe_comment (const char *comment);
    void describe_partition_parts (const sm_partition &parts, class_description::type prt_type);
    void describe_identifier (const char *identifier, class_description::type prt_type);
    void describe_domain (/*const*/ tp_domain &domain, class_description::type prt_type, bool force_print_collation);
    void describe_argument (const sm_method_argument &argument, class_description::type prt_type);
    void describe_method (const struct db_object &op, const sm_method &method_p, class_description::type prt_type);
    void describe_signature (const sm_method_signature &signature_p, class_description::type prt_type);
    void describe_attribute (const struct db_object &class_p, const sm_attribute &attribute_p, bool is_inherited,
			     class_description::type prt_type, bool force_print_collation);
    void describe_constraint (const sm_class &class_p,
			      const sm_class_constraint &constraint_p,
			      class_description::type prt_type);
    void describe_resolution (const sm_resolution &resolution, class_description::type prt_type);
    void describe_method_file (const struct db_object &obj, const sm_method_file &file);
    void describe_class_trigger (const tr_trigger &trigger);
    void describe_class (struct db_object *class_op);
    void describe_partition_info (const sm_partition &partinfo);

    static const char *describe_trigger_condition_time (const tr_trigger &trigger);
    static const char *describe_trigger_action_time (const tr_trigger &trigger);

  protected:
};

#endif // _OBJECT_PRINTER_HPP_
