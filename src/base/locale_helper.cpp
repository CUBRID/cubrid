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
 * locale_helper.cpp
 */

#include "locale_helper.hpp"

#include <locale>
#include <codecvt>
#include <string>

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

  std::locale get_locale (const std::string &charset, const std::string &lang)
  {
    try
      {
	std::locale loc (lang + "." + charset);
	return loc;
      }
    catch (std::exception &e)
      {
	// return the environment's default locale, locale name is not supported
	assert (false);
	return std::locale ("");
      }
  }

  /*
   * convert_to_wstring () -
   *
   * Arguments:
   *        out:  (Out) Output wide string
   *        in: (In) Input string
   *        codeset: (In) code of the input string
   *
   * Returns: bool
   *
   * Note:
   *   This function converts from a multi-byte encoded string into a wide string
   *   to perform locale-aware functionality such as searching or replacing by the regular expression with <regex>
   *   It convert given string into utf8 string and then make wide string
   */
  bool convert_to_wstring (std::wstring &out, const std::string &in, const INTL_CODESET codeset)
  {
    bool is_success = false;

    if (in.empty ())
      {
	// don't need to convert for empty string
	out.clear ();
	return true;
      }

    std::string utf8_str;
    if (codeset != INTL_CODESET_UTF8)
      {
	std::string utf8_converted;
	utf8_converted.resize (in.size() * INTL_CODESET_MULT (INTL_CODESET_UTF8));
	std::string::pointer utf8_str_ptr = (char *) utf8_converted.data ();

	int conv_status = 0;
	int conv_size = 0;
	switch (codeset)
	  {
	  case INTL_CODESET_ISO88591:
	    conv_status = intl_fast_iso88591_to_utf8 ((const unsigned char *) in.data (), in.size (),
			  (unsigned char **) &utf8_str_ptr, &conv_size);
	    break;
	  case INTL_CODESET_KSC5601_EUC:
	    conv_status = intl_euckr_to_utf8 ((const unsigned char *) in.data (), in.size (), (unsigned char **) &utf8_str_ptr,
					      &conv_size);
	    break;
	  case INTL_CODESET_RAW_BYTES:
	    intl_binary_to_utf8 ((const unsigned char *) in.data (), in.size (), (unsigned char **) &utf8_str_ptr,
				 &conv_size);
	    break;
	  default:
	    // unrecognized codeset
	    conv_status = 1;
	    assert (false);
	    break;
	  }

	/* conversion failed */
	if (conv_status != 0)
	  {
	    return false;
	  }

	utf8_converted.resize (conv_size);
	utf8_str.assign (utf8_converted);
      }
    else
      {
	utf8_str.assign (in);
      }

    try
      {
#if defined(WINDOWS)
	std::wstring converted;
	int nLen = MultiByteToWideChar (CP_UTF8, 0, utf8_str.data (), utf8_str.size (), NULL, NULL);
	converted.resize (nLen);
	MultiByteToWideChar (CP_UTF8, 0, utf8_str.data (), utf8_str.size (), &converted[0], nLen);
#else
	std::wstring converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {}.from_bytes (utf8_str);
#endif
	out.assign (std::move (converted));
	is_success = true;
      }
    catch (const std::range_error &re)
      {
	// do nothing
      }

    return is_success;
  }

  /*
   * convert_to_string () -
   *
   * Arguments:
   *        out:  (Out) Output wide string
   *        in: (In) Input string
   *        codeset: (In) code of the input string
   *
   * Returns: bool
   *
   * Note:
   *   This function converts from a wide string into a multi-byte encoded string
   */
  bool convert_to_string (std::string &out, const std::wstring &in, const INTL_CODESET codeset)
  {
    bool is_success = false;

    if (in.empty ())
      {
	// don't need to convert for empty string
	out.clear ();
	return true;
      }

    try
      {
#if defined(WINDOWS)
	int nLen = WideCharToMultiByte (CP_UTF8, 0, in.data (), in.size (), NULL, 0, NULL, NULL);
	std::string converted;
	converted.resize (nLen);
	WideCharToMultiByte (CP_UTF8, 0, in.data (), in.size (), &converted[0], nLen, NULL, NULL);
#else
	std::string converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {}.to_bytes (in);
#endif
	if (codeset == INTL_CODESET_UTF8)
	  {
	    out.assign (std::move (converted));
	    is_success = true;
	  }
	else
	  {
	    std::string to_str;
	    to_str.resize (converted.size());
	    std::string::pointer to_str_ptr = (char *) to_str.data();

	    int conv_status = 0;
	    int conv_size = 0;
	    switch (codeset)
	      {
	      case INTL_CODESET_ISO88591:
		conv_status = intl_utf8_to_iso88591 ((const unsigned char *) converted.data (), converted.size (),
						     (unsigned char **) &to_str_ptr,
						     &conv_size);
		break;
	      case INTL_CODESET_KSC5601_EUC:
		conv_status = intl_utf8_to_euckr ((const unsigned char *) converted.data (), converted.size (),
						  (unsigned char **) &to_str_ptr,
						  &conv_size);
		break;
	      case INTL_CODESET_RAW_BYTES:
		/* when coercing multibyte to binary charset, we just reinterpret each byte as one character */
		to_str.assign (in.begin(), in.end());
		break;
	      default:
		// unrecognized codeset
		conv_status = 1;
		assert (false);
		break;
	      }

	    /* conversion failed */
	    if (conv_status != 0)
	      {
		return false;
	      }

	    to_str.resize (conv_size);
	    out.assign (std::move (to_str));
	    is_success = true;
	  }
      }
    catch (const std::range_error &re)
      {
	// do nothing
      }

    return is_success;
  }
}