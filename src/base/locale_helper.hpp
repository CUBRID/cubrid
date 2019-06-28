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

#ident "$Id$"

namespace cublocale
{
  std::string get_lang_name (const LANG_COLLATION *lang_coll)
  {
    const char *lang_name = lang_coll->default_lang->lang_name;
    std::string lang_str (lang_name);
    return lang_str;
  }

  std::string get_codeset_name (const LANG_COLLATION *lang_coll)
  {
    const char *codeset_name = lang_get_codeset_name (lang_coll->codeset);
    std::string codeset_str (codeset_name);
    return codeset_str;
  }

  std::string get_locale_name (const LANG_COLLATION *lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    std::string locale_str;
    switch (codeset)
      {
      case INTL_CODESET_ISO88591:
      case INTL_CODESET_UTF8:
      case INTL_CODESET_KSC5601_EUC:
        locale_str = get_lang_name(lang_coll) + "." + get_codeset_name(lang_coll);
	      break;
      }
    return locale_str;
  }

  std::locale get_locale (const std::string& charset, const std::string& lang)
  {
    try
      {
	std::locale loc (lang + "." + charset);
	return loc;
      }
    catch (std::exception &e)
      {
	// return the environment's default locale, locale name is not supported
	return std::locale ("");
      }
  }

  bool convert_to_wstring (std::wstring &out, const std::string &in, const LANG_COLLATION *lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    bool success = false;

    std::string utf8_str;
    if (codeset != INTL_CODESET_UTF8)
      {
	std::string utf8_converted;
	utf8_converted.resize (in.size() * INTL_CODESET_MULT (INTL_CODESET_UTF8));
	std::string::pointer utf8_str_ptr = (char *) utf8_converted.data();

	int conv_size = 0;
	switch (codeset)
	  {
	  case INTL_CODESET_ISO88591:
	    intl_fast_iso88591_to_utf8 ((const unsigned char *) in.data(), in.size(), (unsigned char **) &utf8_str_ptr, &conv_size);
	    break;
	  case INTL_CODESET_KSC5601_EUC:
	    intl_euckr_to_utf8 ((const unsigned char *) in.data(), in.size(), (unsigned char **) &utf8_str_ptr, &conv_size);
	    break;
	  case INTL_CODESET_RAW_BYTES:
	    intl_binary_to_utf8 ((const unsigned char *) in.data(), in.size(), (unsigned char **) &utf8_str_ptr, &conv_size);
	    break;
	  default:
	    // unrecognized codeset
	    assert (false);
	    intl_binary_to_utf8 ((const unsigned char *) in.data(), in.size(), (unsigned char **) &utf8_str_ptr, &conv_size);
	    break;
	  }

	utf8_converted.resize (conv_size);
	utf8_str.assign (std::move (utf8_converted));
      }
    else
      {
	utf8_str.assign (std::move (in));
      }

    try
      {
	std::wstring converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {}.from_bytes (utf8_str);
	out.assign (std::move (converted));
	success = true;
      }
    catch (const std::range_error &re)
      {
	// do nothing
	success = false;
      }

    return success;
  }

  bool convert_to_string (std::string &out, const std::wstring &in, const LANG_COLLATION *lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    bool success = false;

    try
      {
	std::string converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {}.to_bytes (in);
	if (codeset == INTL_CODESET_UTF8)
	  {
	    out.assign (std::move (converted));
	    success = true;
	  }
	else
	  {
	    std::string to_str;
	    to_str.resize (converted.size());
	    std::string::pointer to_str_ptr = (char *) to_str.data();

	    int conv_size = 0;
	    switch (codeset)
	      {
	      case INTL_CODESET_ISO88591:
		intl_utf8_to_iso88591 ((const unsigned char *) converted.data(), converted.size(), (unsigned char **) &to_str_ptr,
				       &conv_size);
		break;
	      case INTL_CODESET_KSC5601_EUC:
		intl_utf8_to_euckr ((const unsigned char *) converted.data(), converted.size(), (unsigned char **) &to_str_ptr,
				    &conv_size);
		break;
	      case INTL_CODESET_RAW_BYTES:
		/* when coercing multibyte to binary charset, we just reinterpret each byte as one character */
		to_str.assign (in.begin(), in.end());
		break;
	      default:
		// unrecognized codeset
		assert (false);
		to_str.assign (in.begin(), in.end());
		break;
	      }
	    to_str.resize (conv_size);
	    out.assign (std::move (to_str));
	    success = true;
	  }
      }
    catch (const std::range_error &re)
      {
	// do nothing
	success = false;
      }

    return success;
  }
}

#endif /* _LOCALE_HELPER_HPP_ */