/*
 *
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

//
// string_regex_std - regular expression engine using C++ <regex> standard library
//
#include "string_regex.hpp"

#include "error_manager.h"
#include "locale_helper.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubregex
{
  std::string
  std_parse_regex_exception (std::regex_error &e)
  {
    std::string error_message;
    using namespace std::regex_constants;
    switch (e.code ())
      {
      case error_collate:
	error_message.assign ("regex_error(error_collate): the expression contains an invalid collating element name");
	break;
      case error_ctype:
	error_message.assign ("regex_error(error_ctype): the expression contains an invalid character class name");
	break;
      case error_escape:
	error_message.assign ("regex_error(error_escape): the expression contains an invalid escaped character or a trailing escape");
	break;
      case error_backref:
	error_message.assign ("regex_error(error_backref): the expression contains an invalid back reference");
	break;
      case error_brack:
	error_message.assign ("regex_error(error_brack): the expression contains mismatched square brackets ('[' and ']')");
	break;
      case error_paren:
	error_message.assign ("regex_error(error_paren): the expression contains mismatched parentheses ('(' and ')')");
	break;
      case error_brace:
	error_message.assign ("regex_error(error_brace): the expression contains mismatched curly braces ('{' and '}')");
	break;
      case error_badbrace:
	error_message.assign ("regex_error(error_badbrace): the expression contains an invalid range in a {} expression");
	break;
      case error_range:
	error_message.assign ("regex_error(error_range): the expression contains an invalid character range (e.g. [b-a])");
	break;
      case error_space:
	error_message.assign ("regex_error(error_space): there was not enough memory to convert the expression into a finite state machine");
	break;
      case error_badrepeat:
	error_message.assign ("regex_error(error_badrepeat): one of *?+{ was not preceded by a valid regular expression");
	break;
      case error_complexity:
	error_message.assign ("regex_error(error_complexity): the complexity of an attempted match exceeded a predefined level");
	break;
      case error_stack:
	error_message.assign ("regex_error(error_stack): there was not enough memory to perform a match");
	break;
      default:
	error_message.assign ("regex_error(error_unknown)");
	break;
      }
    return error_message;
  }

  std::regex_constants::syntax_option_type
  std_parse_match_type (const opt_flag_type &opt_type)
  {
    std::regex_constants::syntax_option_type reg_flags = std::regex_constants::ECMAScript |
	std::regex_constants::icase; // default
    if ((opt_type & OPT_ICASE) == 0) // OPT_ICASE is not set
      {
	reg_flags &= ~std::regex_constants::icase;
      }
    return reg_flags;
  }

  int std_compile (REFPTR (compiled_regex, cr), const LANG_COLLATION *collation)
  {
    int error_status = NO_ERROR;

    std::wstring pattern_wstring;
    if (cublocale::convert_utf8_to_wstring (pattern_wstring, cr->pattern) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    try
      {
#if defined(WINDOWS)
	/* HACK: collating element features doesn't work well on Windows.
	*  And lookup_collatename is not invoked when regex pattern has collating element.
	*  It is hacky code finding collating element pattern and throw error.
	*/
	wchar_t *collate_elem_pattern = L"[[.";
	int found = pattern_wstring.find ( std::wstring (collate_elem_pattern));
	if (found != std::wstring::npos)
	  {
	    throw std::regex_error (std::regex_constants::error_collate);
	  }
#endif
	REFPTR (cub_std_regex, reg) = cr->compiled->std_obj = new cub_std_regex ();
	std::locale loc = cublocale::get_locale (std::string ("utf-8"), cublocale::get_lang_name (collation));
	reg->imbue (loc);
	reg->assign (pattern_wstring, std_parse_match_type (cr->flags));
      }
    catch (std::regex_error &e)
      {
	// regex compilation exception
	error_status = ER_REGEX_COMPILE_ERROR;
	std::string error_message = std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }

  int std_search (int &result, const compiled_regex &reg, const std::string &src)
  {
    int error_status = NO_ERROR;
    bool is_matched = false;

    std::wstring src_wstring;
    if (cublocale::convert_utf8_to_wstring (src_wstring, src) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	result = V_FALSE;
	return error_status;
      }

    try
      {
	cub_std_regex &std_reg = GET_STD_OBJ (reg);

#if defined(WINDOWS)
	/* HACK: case insensitive doesn't work well on Windows.
	*  This code transforms source string into lowercase
	*  and perform searching regular expression pattern.
	*/
	if (std_reg.flags() & std::regex_constants::icase)
	  {
	    std::wstring src_lower;
	    src_lower.resize (src_wstring.size ());
	    std::transform (src_wstring.begin(), src_wstring.end(), src_lower.begin(), ::towlower);
	    is_matched = std::regex_search (src_lower, std_reg);
	  }
	else
	  {
	    is_matched = std::regex_search (src_wstring, std_reg);
	  }
#else
	is_matched = std::regex_search (src_wstring, std_reg);
#endif
      }
    catch (std::regex_error &e)
      {
	// regex execution exception
	is_matched = false;
	error_status = ER_REGEX_EXEC_ERROR;
	std::string error_message = std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    result = is_matched ? V_TRUE : V_FALSE;
    return error_status;
  }

  int std_count (int &result, const compiled_regex &reg, const std::string &src, const int position)
  {
    assert (position >= 0);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_utf8_to_wstring (src_wstring, src) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    cub_std_regex &std_reg = GET_STD_OBJ (reg);

#if defined(WINDOWS)
    /* HACK: case insensitive doesn't work well on Windows.
    *  This code transforms source string into lowercase
    *  and perform searching regular expression pattern.
    */
    std::wstring target_lower;
    if (std_reg.flags() & std::regex_constants::icase)
      {
	target_lower.resize (target.size ());
	std::transform (target.begin(), target.end(), target_lower.begin(), ::towlower);
      }
    else
      {
	target_lower = target;
      }
#endif

    result = 0;
    try
      {
#if defined(WINDOWS)
	auto reg_iter = cub_std_regex_iterator (target_lower.begin (), target_lower.end (), std_reg);
#else
	auto reg_iter = cub_std_regex_iterator (target.begin (), target.end (), std_reg);
#endif
	auto reg_end = cub_std_regex_iterator ();
	result = std::distance (reg_iter, reg_end);
      }
    catch (std::regex_error &e)
      {
	// regex execution exception, error_complexity or error_stack
	error_status = ER_REGEX_EXEC_ERROR;
	std::string error_message = cubregex::std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }
    return error_status;
  }

  int std_instr (int &result, const compiled_regex &reg, const std::string &src,
		 const int position, const int occurrence, const int return_opt)
  {
    assert (position >= 0);
    assert (occurrence >= 1);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_utf8_to_wstring (src_wstring, src) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    cub_std_regex &std_reg = GET_STD_OBJ (reg);
#if defined(WINDOWS)
    /* HACK: case insensitive doesn't work well on Windows.
    *  This code transforms source string into lowercase
    *  and perform searching regular expression pattern.
    */
    std::wstring target_lower;
    if (std_reg.flags() & std::regex_constants::icase)
      {
	target_lower.resize (target.size ());
	std::transform (target.begin(), target.end(), target_lower.begin(), ::towlower);
      }
    else
      {
	target_lower = target;
      }
#endif

    int match_idx = -1;
    try
      {
#if defined(WINDOWS)
	auto reg_iter = cub_std_regex_iterator (target_lower.begin (), target_lower.end (), std_reg);
#else
	auto reg_iter = cub_std_regex_iterator (target.begin (), target.end (), std_reg);
#endif
	auto reg_end = cub_std_regex_iterator ();

	int n = 1;
	while (reg_iter != reg_end)
	  {
	    /* match */
	    if (n == occurrence)
	      {
		cub_std_regex_results match_result = *reg_iter;
		match_idx = match_result.position ();
		if (return_opt == 1)
		  {
		    match_idx += reg_iter->length ();
		  }
		break;
	      }
	    ++reg_iter;
	    ++n;
	  }

	if (match_idx != -1)
	  {
	    result = position + match_idx + 1;
	  }
	else
	  {
	    result = 0;
	  }
      }
    catch (std::regex_error &e)
      {
	// regex execution exception, error_complexity or error_stack
	error_status = ER_REGEX_EXEC_ERROR;
	result = 0;
	std::string error_message = cubregex::std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }

#if defined(WINDOWS)
  /* HACK: case insensitive doesn't work well on Windows.
  *  This code transforms source string into lowercase
  *  and perform searching regular expression pattern.
  */
  int std_replace (std::string &result, const compiled_regex &reg, const std::string &src,
		   const std::string &repl, const int position,
		   const int occurrence)
  {
    assert (position >= 0);
    assert (occurrence >= 0);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_utf8_to_wstring (src_wstring, src) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    std::wstring repl_wstring;
    if (cublocale::convert_utf8_to_wstring (repl_wstring, repl) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring result_wstring (src_wstring.substr (0, position));
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    cub_std_regex &std_reg = GET_STD_OBJ (reg);
    std::wstring target_lowercase;
    if (std_reg.flags() & std::regex_constants::icase)
      {
	target_lowercase.resize (target.size ());
	std::transform (target.begin(), target.end(), target_lowercase.begin(), ::towlower);
      }
    else
      {
	target_lowercase = target;
      }

    try
      {
	auto reg_iter = cub_std_regex_iterator (target_lowercase.begin (), target_lowercase.end (), std_reg);
	auto reg_end = cub_std_regex_iterator ();

	int match_pos = -1;
	int last_pos = 0;
	int n = 1;
	auto out = std::back_inserter (result_wstring);

	cub_std_regex_results match_result;
	while (reg_iter != reg_end)
	  {
	    match_result = *reg_iter;

	    /* prefix */
	    match_pos = match_result.position ();
	    size_t match_length = match_result.length ();
	    std::wstring match_prefix = target.substr (last_pos, match_pos - last_pos);
	    out = std::copy (match_prefix.begin (), match_prefix.end (), out);

	    /* match */
	    if (n == occurrence || occurrence == 0)
	      {
		out = match_result.format (out, repl_wstring);
	      }
	    else
	      {
		std::wstring match_str = target.substr (match_pos, match_length);
		out = std::copy (match_str.begin (), match_str.end (), out);
	      }

	    ++reg_iter;

	    /* suffix */
	    last_pos = match_pos + match_length;
	    if (((occurrence != 0) && (n == occurrence)) || reg_iter == reg_end)
	      {
		std::wstring match_suffix = target.substr (match_pos + match_length, std::string::npos);
		out = std::copy (match_suffix.begin (), match_suffix.end (), out);
		if (occurrence != 0 && n == occurrence)
		  {
		    break;
		  }
	      }
	    ++n;
	  }

	/* nothing matched */
	if (match_pos == -1 && reg_iter == reg_end)
	  {
	    out = std::copy (target.begin (), target.end (), out);
	  }

	if (cublocale::convert_wstring_to_utf8 (result, result_wstring) == false)
	  {
	    error_status = ER_QSTR_BAD_SRC_CODESET;
	    return error_status;
	  }
      }
    catch (std::regex_error &e)
      {
	// regex execution exception, error_complexity or error_stack
	error_status = ER_REGEX_EXEC_ERROR;
	result.clear ();
	std::string error_message = cubregex::std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }
#else
  int std_replace (std::string &result, const compiled_regex &reg, const std::string &src,
		   const std::string &repl, const int position,
		   const int occurrence)
  {
    assert (position >= 0);
    assert (occurrence >= 0);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_utf8_to_wstring (src_wstring, src) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    std::wstring repl_wstring;
    if (cublocale::convert_utf8_to_wstring (repl_wstring, repl) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring result_wstring (src_wstring.substr (0, position));
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    try
      {
	cub_std_regex &std_reg = GET_STD_OBJ (reg);
	if (occurrence == 0)
	  {
	    result_wstring.append (
		    std::regex_replace (target, std_reg, repl_wstring)
	    );
	  }
	else
	  {
	    auto reg_iter = cub_std_regex_iterator (target.begin (), target.end (), std_reg);
	    auto reg_end = cub_std_regex_iterator ();

	    int n = 1;
	    auto out = std::back_inserter (result_wstring);

	    int match_pos = -1;
	    while (reg_iter != reg_end)
	      {
		const cub_std_regex_results match_result = *reg_iter;

		std::wstring match_prefix = match_result.prefix ().str ();
		out = std::copy (match_prefix.begin (), match_prefix.end (), out);

		/* match */
		match_pos = match_result.position ();
		size_t match_length = match_result.length ();
		if (n == occurrence)
		  {
		    out = match_result.format (out, repl_wstring);
		  }
		else
		  {
		    std::wstring match_str = match_result.str ();
		    out = std::copy (match_str.begin (), match_str.end (), out);
		  }

		++reg_iter;

		/* suffix */
		if (n == occurrence || reg_iter == reg_end)
		  {
		    /* occurrence option specified or end of matching */
		    std::wstring match_suffix = match_result.suffix (). str ();
		    out = std::copy (match_suffix.begin (), match_suffix.end (), out);
		    break;
		  }
		++n;
	      }

	    /* nothing matched */
	    if (match_pos == -1 && reg_iter == reg_end)
	      {
		out = std::copy (target.begin (), target.end (), out);
	      }
	  }

	if (cublocale::convert_wstring_to_utf8 (result, result_wstring) == false)
	  {
	    error_status = ER_QSTR_BAD_SRC_CODESET;
	    return error_status;
	  }

      }
    catch (std::regex_error &e)
      {
	// regex execution exception, error_complexity or error_stack
	error_status = ER_REGEX_EXEC_ERROR;
	result.clear ();
	std::string error_message = cubregex::std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }
#endif

  int std_substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
		  const int position, const int occurrence)
  {
    assert (position >= 0);
    assert (occurrence >= 1);

    int error_status = NO_ERROR;
    is_matched = false;

    std::wstring src_wstring;
    if (cublocale::convert_utf8_to_wstring (src_wstring, src) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring result_wstring;
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    cub_std_regex &std_reg = GET_STD_OBJ (reg);

#if defined(WINDOWS)
    /* HACK: case insensitive doesn't work well on Windows.
    *  This code transforms source string into lowercase
    *  and perform searching regular expression pattern.
    */
    std::wstring target_lower;
    if (std_reg.flags() & std::regex_constants::icase)
      {
	target_lower.resize (target.size ());
	std::transform (target.begin(), target.end(), target_lower.begin(), ::towlower);
      }
    else
      {
	target_lower = target;
      }
#endif

    int match_pos = -1;
    size_t match_length = 0;
    try
      {
#if defined(WINDOWS)
	auto reg_iter = cub_std_regex_iterator (target_lower.begin (), target_lower.end (), std_reg);
#else
	auto reg_iter = cub_std_regex_iterator (target.begin (), target.end (), std_reg);
#endif
	auto reg_end = cub_std_regex_iterator ();
	auto out = std::back_inserter (result_wstring);

	int n = 1;
	while (reg_iter != reg_end)
	  {
	    cub_std_regex_results match_result = *reg_iter;

	    /* match */
	    match_pos = match_result.position ();
	    match_length = match_result.length ();
	    if (n == occurrence)
	      {
		std::wstring match_str = target.substr (match_pos, match_length);
		out = std::copy (match_str.begin (), match_str.end (), out);
		is_matched = true;
		break;
	      }
	    ++reg_iter;
	    ++n;
	  }

	if (cublocale::convert_wstring_to_utf8 (result, result_wstring) == false)
	  {
	    error_status = ER_QSTR_BAD_SRC_CODESET;
	    return error_status;
	  }

      }
    catch (std::regex_error &e)
      {
	// regex execution exception, error_complexity or error_stack
	error_status = ER_REGEX_EXEC_ERROR;
	result.clear ();
	std::string error_message = cubregex::std_parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }
}
