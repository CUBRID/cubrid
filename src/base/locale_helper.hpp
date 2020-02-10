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
 * locale_helper.hpp
 */

#ifndef _LOCALE_HELPER_HPP_
#define _LOCALE_HELPER_HPP_

#include "language_support.h"
#include "intl_support.h"

#include <locale>
#include <codecvt>
#include <string>

/* TODO: need to make compatible fully between CUBRID Language support and cpp standard locale */
namespace cublocale
{
  std::string get_lang_name (const LANG_COLLATION *lang_coll);
  std::string get_codeset_name (const LANG_COLLATION *lang_coll);
  std::locale get_locale (const std::string &charset, const std::string &lang);

  bool convert_to_wstring (std::wstring &out, const std::string &in, const INTL_CODESET codeset);
  bool convert_to_string (std::string &out, const std::wstring &in, const INTL_CODESET codeset);
}

#endif /* _LOCALE_HELPER_HPP_ */
