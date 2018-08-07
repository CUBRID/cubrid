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
 * load_db_value_converter.hpp - conversion from string to DB_VALUE
 */

#ifndef _LOAD_DB_VALUE_CONVERTER_HPP_
#define _LOAD_DB_VALUE_CONVERTER_HPP_

#ident "$Id$"

#include "dbtype_def.h"
#include "intl_support.h"
#include "object_domain.h"

namespace cubload
{

  typedef void (*conv_func) (const char *, const TP_DOMAIN *, DB_VALUE *);

  conv_func get_conv_func (int ldr_type, const TP_DOMAIN *domain);

  // TODO CBRD-21654 reused conversion function in load_client_loader.c source file
  void to_db_null (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_short (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_int (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_bigint (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_char (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_varchar (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_string (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_float (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_double (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_date (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_time (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_timestamp (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_timestampltz (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_timestamptz (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_datetime (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_datetimeltz (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_datetimetz (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_json (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);
  void to_db_monetary (const char *str, const TP_DOMAIN *domain, DB_VALUE *val);

} // namespace cubload

#endif /* _LOAD_DB_VALUE_CONVERTER_HPP_ */
