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
 * loader_sr.hpp: Loader server definitions. Updated using design from fast loaddb prototype
 */

#ifndef _LOADER_SR_HPP_
#define _LOADER_SR_HPP_

#ident "$Id$"

#include <cstddef>

namespace cubload
{

  enum LDR_TYPE
  {
    LDR_NULL,
    LDR_INT,
    LDR_STR,
    LDR_NSTR,
    LDR_NUMERIC,                 /* Default real */
    LDR_DOUBLE,                  /* Reals specified with scientific notation, 'e', or 'E' */
    LDR_FLOAT,                   /* Reals specified with 'f' or 'F' notation */
    LDR_OID,                     /* Object references */
    LDR_CLASS_OID,               /* Class object reference */
    LDR_DATE,
    LDR_TIME,
    LDR_TIMELTZ,
    LDR_TIMETZ,
    LDR_TIMESTAMP,
    LDR_TIMESTAMPLTZ,
    LDR_TIMESTAMPTZ,
    LDR_COLLECTION,
    LDR_ELO_INT,                 /* Internal ELO's */
    LDR_ELO_EXT,                 /* External ELO's */
    LDR_SYS_USER,
    LDR_SYS_CLASS,               /* This type is not allowed currently. */
    LDR_MONETARY,
    LDR_BSTR,                    /* Binary bit strings */
    LDR_XSTR,                    /* Hexidecimal bit strings */
    LDR_BIGINT,
    LDR_DATETIME,
    LDR_DATETIMELTZ,
    LDR_DATETIMETZ,
    LDR_JSON,

    LDR_TYPE_MAX = LDR_JSON
  };

  /*
   * LDR_ATTRIBUTE_TYPE
   *
   * attribute type identifiers for ldr_act_restrict_attributes().
   * These attributes are handled specially since there modify the class object
   * directly.
   */

  enum LDR_ATTRIBUTE_TYPE
  {
    LDR_ATTRIBUTE_ANY = 0,
    LDR_ATTRIBUTE_SHARED,
    LDR_ATTRIBUTE_CLASS,
    LDR_ATTRIBUTE_DEFAULT
  };

  enum LDR_INTERRUPT_TYPE
  {
    LDR_NO_INTERRUPT,
    LDR_STOP_AND_ABORT_INTERRUPT,
    LDR_STOP_AND_COMMIT_INTERRUPT
  };

  struct LDR_STRING
  {
    LDR_STRING *next;
    LDR_STRING *last;
    char *val;
    size_t size;
    bool need_free_val;
    bool need_free_self;
  };

  struct LDR_CONSTRUCTOR_SPEC
  {
    LDR_STRING *id_name;
    LDR_STRING *arg_list;
  };

  struct LDR_CLASS_COMMAND_SPEC
  {
    int qualifier;
    LDR_STRING *attr_list;
    LDR_CONSTRUCTOR_SPEC *ctor_spec;
  };

  struct LDR_CONSTANT
  {
    LDR_CONSTANT *next;
    LDR_CONSTANT *last;
    void *val;
    int type;
    bool need_free;
  };

  struct LDR_OBJECT_REF
  {
    LDR_STRING *class_id;
    LDR_STRING *class_name;
    LDR_STRING *instance_number;
  };

  struct LDR_MONETARY_VALUE
  {
    LDR_STRING *amount;
    int currency_type;
  };

  void ldr_load_failed_error ();
  void ldr_increment_fails ();
  void ldr_string_free (LDR_STRING **str);
  void ldr_increment_err_total ();

  void ldr_act_finish (int parse_error);
  void ldr_act_finish_line ();

  void ldr_act_start_id (char *name);
  void ldr_act_set_id (int id);

  void ldr_act_setup_class_command_spec (LDR_STRING **class_name, LDR_CLASS_COMMAND_SPEC **cmd_spec);

  void ldr_act_start_instance (int id, LDR_CONSTANT *cons);
  void ldr_process_constants (LDR_CONSTANT *c);
} // namespace cubload
#endif /* _LOADER_SR_HPP_ */
