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
 * locale_helper.cpp
 */

#include "locale_helper.hpp"

#include <locale>
#include <codecvt>
#include <string>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
   * convert_utf8_to_string () -
   *
   * Arguments:
   *        out:  (Out) Output string
   *        in: (In) Input string (UTF-8)
   *        codeset: (In) codeset to convert
   *
   * Returns: bool
   *
   * Note:
   *   This function converts from a unicode string (UTF-8) into a string with specified codeset
   */
  bool convert_utf8_to_string (std::string &out_string, const std::string &utf8_string, const INTL_CODESET codeset)
  {
    if (utf8_string.empty())
      {
	out_string.clear ();
	return true;
      }

    if (codeset == INTL_CODESET_UTF8)
      {
	out_string.assign (std::move (utf8_string));
      }
    else
      {
	std::string to_str;
	to_str.resize (utf8_string.size());
	std::string::pointer to_str_ptr = (char *) to_str.data();

	int conv_status = 0;
	int conv_size = 0;
	switch (codeset)
	  {
	  case INTL_CODESET_ISO88591:
	    conv_status = intl_utf8_to_iso88591 ((const unsigned char *) utf8_string.data (), utf8_string.size (),
						 (unsigned char **) &to_str_ptr,
						 &conv_size);
	    break;
	  case INTL_CODESET_KSC5601_EUC:
	    conv_status = intl_utf8_to_euckr ((const unsigned char *) utf8_string.data (), utf8_string.size (),
					      (unsigned char **) &to_str_ptr,
					      &conv_size);
	    break;
	  case INTL_CODESET_RAW_BYTES:
	    /* when coercing multibyte to binary charset, we just reinterpret each byte as one character */
	    to_str.assign (utf8_string.begin(), utf8_string.end());
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
	out_string.assign (std::move (to_str));
      }
    return true;
  }

  /*
   * convert_string_to_utf8 () -
   *
   * Arguments:
   *        out:  (Out) Output UTF-8 string
   *        in: (In) Input string
   *        codeset: (In) code of the input string
   *
   * Returns: bool
   *
   * Note:
   *   This function converts from a string of specified codeset into a unicoe string (UTF-8))
   */
  bool convert_string_to_utf8 (std::string &utf8_string, const std::string &input_string, const INTL_CODESET codeset)
  {
    if (input_string.empty ())
      {
	utf8_string.clear ();
	return true;
      }

    if (codeset != INTL_CODESET_UTF8)
      {
	std::string utf8_converted;
	utf8_converted.resize (input_string.size() * INTL_CODESET_MULT (INTL_CODESET_UTF8));
	std::string::pointer utf8_str_ptr = (char *) utf8_converted.data ();

	int conv_status = 0;
	int conv_size = 0;
	switch (codeset)
	  {
	  case INTL_CODESET_ISO88591:
	    conv_status = intl_fast_iso88591_to_utf8 ((const unsigned char *) input_string.data (), input_string.size (),
			  (unsigned char **) &utf8_str_ptr, &conv_size);
	    break;
	  case INTL_CODESET_KSC5601_EUC:
	    conv_status = intl_euckr_to_utf8 ((const unsigned char *) input_string.data (), input_string.size (),
					      (unsigned char **) &utf8_str_ptr,
					      &conv_size);
	    break;
	  case INTL_CODESET_RAW_BYTES:
	    intl_binary_to_utf8 ((const unsigned char *) input_string.data (), input_string.size (),
				 (unsigned char **) &utf8_str_ptr,
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
	utf8_string.assign (utf8_converted);
      }
    else
      {
	utf8_string.assign (input_string);
      }
    return true;
  }

  /*
   * convert_utf8_to_wstring () -
   *
   * Arguments:
   *        out:  (Out) Output wide string
   *        in: (In) Input UTF-8 string
   *
   * Returns: bool
   *
   * Note:
   *   This function converts from a multi-byte encoded string into a wide string
   *   to perform locale-aware functionality such as searching or replacing by the regular expression with <regex>
   *   It convert given string into utf8 string and then make wide string
   */
  bool convert_utf8_to_wstring (std::wstring &out, const std::string &in)
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
	std::wstring converted;
	int nLen = MultiByteToWideChar (CP_UTF8, 0, in.data (), in.size (), NULL, NULL);
	converted.resize (nLen);
	MultiByteToWideChar (CP_UTF8, 0, in.data (), in.size (), &converted[0], nLen);
#else
	std::wstring converted = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {} .from_bytes (in);
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
   * convert_wstring_to_utf8 () -
   *
   * Arguments:
   *        out:  (Out) Output wide string
   *        in: (In) Input string
   *
   * Returns: bool
   *
   * Note:
   *   This function converts from a wide string into a multi-byte encoded string
   */
  bool convert_wstring_to_utf8 (std::string &out, const std::wstring &in)
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
	out.clear ();
#if defined(WINDOWS)
	int nLen = WideCharToMultiByte (CP_UTF8, 0, in.data (), in.size (), NULL, 0, NULL, NULL);
	out.resize (nLen);
	WideCharToMultiByte (CP_UTF8, 0, in.data (), in.size (), &out[0], nLen, NULL, NULL);
#else
	out = std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> {} .to_bytes (in);
#endif
	is_success = true;
      }
    catch (const std::range_error &re)
      {
	// do nothing
      }

    return is_success;
  }
}
