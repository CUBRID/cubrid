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
 * db_value_converter.hpp - conversion from string to DB_VALUE
 */

#ifndef _DB_VALUE_CONVERTER_HPP_
#define _DB_VALUE_CONVERTER_HPP_

#ident "$Id$"

#include "dbtype_def.h"
#include "intl_support.h"
#include "object_domain.h"

namespace cubload
{

  using db_value_t = DB_VALUE;
  using tp_domain_t = TP_DOMAIN;
  using codeset_t = INTL_CODESET;
  typedef void (*conv_func) (const char *, const tp_domain_t *, db_value_t *);

  conv_func get_conv_func (int ldr_type, const tp_domain_t *domain);

  // TODO CBRD-21654 reused conversion function in loader_cl.c source file
  void to_db_null (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_short (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_int (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_bigint (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_char (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_varchar (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_string (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_float (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_double (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_date (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_time (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_timeltz (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_timetz (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_timestamp (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_timestampltz (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_timestamptz (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_datetime (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_datetimeltz (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_datetimetz (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_json (const char *str, const tp_domain_t *domain, db_value_t *val);
  void to_db_monetary (const char *str, const tp_domain_t *domain, db_value_t *val);

} // namespace cubload

#endif // _DB_VALUE_CONVERTER_HPP_
