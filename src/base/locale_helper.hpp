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
  bool convert_to_string (std::string &out, const std::string &in, const INTL_CODESET codeset);
}

#endif /* _LOCALE_HELPER_HPP_ */
