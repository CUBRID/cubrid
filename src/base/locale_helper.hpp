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
  std::string get_locale_name (const LANG_COLLATION *lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    const char *codeset_name = lang_get_codeset_name (codeset);
    const char *lang_name = lang_coll->default_lang->lang_name;

    std::string locale_str;
    switch (codeset)
      {
      case INTL_CODESET_ISO88591:
      case INTL_CODESET_UTF8:
      case INTL_CODESET_KSC5601_EUC:
	locale_str = lang_name;
	locale_str += ".";
	locale_str += codeset_name;
	break;
      }
    return locale_str;
  }

  std::locale get_locale (const LANG_COLLATION *lang_coll)
  {
    std::string locale_str = get_locale_name (lang_coll);
    try
      {
	std::locale loc (locale_str.c_str());
	return loc;
      }
    catch (std::exception &e)
      {
	// return default locale, locale name is not supported
	return std::locale ();
      }
  }

  bool convert_to_wstring (std::wstring &out, const std::string &in, const LANG_COLLATION *lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;

    switch (codeset)
      {
      case INTL_CODESET_UTF8:
      {
	std::wstring converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {}.from_bytes (in);
	out.assign (std::move (converted));
      }
      break;
      case INTL_CODESET_ISO88591:
      case INTL_CODESET_RAW_BYTES:
      case INTL_CODESET_KSC5601_EUC:
      {
	typedef std::ctype<wchar_t> wchar_facet;
	std::locale loc = get_locale (lang_coll);
	std::wstring return_value;
	if (in.empty())
	  {
	    return_value.assign (L"");
	  }
	if (std::has_facet<wchar_facet> (loc))
	  {
	    std::vector<wchar_t> to (in.size() + 2, 0);
	    std::vector<wchar_t>::pointer toPtr = to.data();
	    const wchar_facet &facet = std::use_facet<wchar_facet> (loc);
	    if (facet.widen (in.c_str(), in.c_str() + in.size(), toPtr) != 0)
	      {
		return_value = to.data();
	      }
	  }
	out.assign (std::move (return_value));
      }
      break;
      default:
	assert (0);
	break;
      }

    return true;
  }

  bool convert_to_string (std::string &out, const std::wstring &in, const LANG_COLLATION *lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;

    switch (codeset)
      {
      case INTL_CODESET_UTF8:
      {
	std::string converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {}.to_bytes (in);
	out.assign (std::move (converted));
      }
      break;
      case INTL_CODESET_ISO88591:
      case INTL_CODESET_RAW_BYTES:
      case INTL_CODESET_KSC5601_EUC:
      {
	std::locale loc = get_locale (lang_coll);

	typedef std::codecvt<wchar_t, char, std::mbstate_t> converter_type;
	typedef std::ctype<wchar_t> wchar_facet;
	std::string return_value;
	if (in.empty())
	  {
	    return "";
	  }
	const wchar_t *from = in.c_str();
	size_t len = in.length();

	size_t converterMaxLength = 6;
	size_t vectorSize = ((len + 6) * converterMaxLength);
	if (std::has_facet<converter_type> (loc))
	  {
	    const converter_type &converter = std::use_facet<converter_type> (loc);
	    if (converter.always_noconv())
	      {
		converterMaxLength = converter.max_length();
		if (converterMaxLength != 6)
		  {
		    vectorSize = ((len + 6) * converterMaxLength);
		  }
		std::mbstate_t state;
		const wchar_t *from_next = nullptr;
		std::vector<char> to (vectorSize, 0);
		std::vector<char>::pointer toPtr = to.data();
		std::vector<char>::pointer to_next = nullptr;
		const converter_type::result result = converter.out (
		    state, from, from + len, from_next,
		    toPtr, toPtr + vectorSize, to_next);
		if (
			(converter_type::ok == result || converter_type::noconv == result)
			&& 0 != toPtr[0]
		)
		  {
		    return_value.assign (toPtr, to_next);
		  }
	      }
	  }
	if (return_value.empty() && std::has_facet<wchar_facet> (loc))
	  {
	    std::vector<char> to (vectorSize, 0);
	    std::vector<char>::pointer toPtr = to.data();
	    const wchar_facet &facet = std::use_facet<wchar_facet> (loc);
	    if (facet.narrow (from, from + len, '?', toPtr) != 0)
	      {
		return_value = toPtr;
	      }
	  }
	out.assign (std::move (return_value));
      }
      break;
      default:
	assert (0);
	break;
      }

    return true;
  }
}

#endif /* _LOCALE_HELPER_HPP_ */