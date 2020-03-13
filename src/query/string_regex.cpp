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

//
// string_regex - definitions and functions related to regular expression
//

#include "string_regex.hpp"

#include "locale_helper.hpp"
#include "error_manager.h"
#include "memory_alloc.h"
#include "language_support.h"

#include <algorithm>
#include <regex>
#include <string>

namespace cubregex
{

  compiled_regex::compiled_regex () : regex (NULL), pattern (NULL)
  {}

  compiled_regex::~compiled_regex ()
  {
    clear (regex, pattern);
  }

  std::string
  parse_regex_exception (std::regex_error &e)
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

  int
  parse_match_type (std::regex_constants::syntax_option_type &reg_flags, std::string &opt_str)
  {
    int error_status = NO_ERROR;

    auto mt_iter = opt_str.begin ();
    while ((mt_iter != opt_str.end ()) && (error_status == NO_ERROR))
      {
	char opt = *mt_iter;
	switch (opt)
	  {
	  case 'c':
	    reg_flags &= ~std::regex_constants::icase;
	    break;
	  case 'i':
	    reg_flags |= std::regex_constants::icase;
	    break;
	  default:
	    error_status = ER_QPROC_INVALID_PARAMETER;
	    break;
	  }
	++mt_iter;
      }

    return error_status;
  }

  void
  clear (cub_regex_object *&regex, char *&pattern)
  {
    if (regex != NULL)
      {
	delete regex;
	regex = NULL;
      }

    if (pattern != NULL)
      {
	db_private_free_and_init (NULL, pattern);
      }
  }

  bool check_should_recompile (const cub_regex_object *compiled_regex, const char *compiled_pattern,
			       const std::string &pattern,
			       const std::regex_constants::syntax_option_type reg_flags)
  {
    /* regex must be recompiled if regex object is not specified or different flags are set */
    if (compiled_regex == NULL || reg_flags != compiled_regex->flags ())
      {
	return true;
      }

    /* regex must be recompiled if pattern is not specified or compiled pattern does not match current pattern */
    if (compiled_pattern == NULL || pattern.size () != strlen (compiled_pattern)
	|| pattern.compare (compiled_pattern) != 0)
      {
	return true;
      }

    return false;
  }

  int compile (cub_regex_object *&compiled_regex, const char *pattern,
	       const std::regex_constants::syntax_option_type reg_flags, const LANG_COLLATION *collation)
  {
    int error_status = NO_ERROR;

    std::wstring pattern_wstring;
    if (cublocale::convert_to_wstring (pattern_wstring, std::string (pattern), collation->codeset) == false)
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

	// delete to avoid memory leak
	if (compiled_regex != NULL)
	  {
	    delete compiled_regex;
	  }

	compiled_regex = new cub_regex_object ();
	if (compiled_regex == NULL)
	  {
	    error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	  }
	else
	  {
	    std::locale loc = cublocale::get_locale (std::string ("utf-8"), cublocale::get_lang_name (collation));
	    compiled_regex->imbue (loc);
	    compiled_regex->assign (pattern_wstring, reg_flags);
	  }
      }
    catch (std::regex_error &e)
      {
	// regex compilation exception
	error_status = ER_REGEX_COMPILE_ERROR;
	std::string error_message = parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }

  int search (int &result, const cub_regex_object &reg, const std::string &src, const INTL_CODESET codeset)
  {
    int error_status = NO_ERROR;
    bool is_matched = false;

    std::wstring src_wstring;
    if (cublocale::convert_to_wstring (src_wstring, src, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	result = V_FALSE;
	return error_status;
      }

    try
      {
#if defined(WINDOWS)
	/* HACK: case insensitive doesn't work well on Windows.
	*  This code transforms source string into lowercase
	*  and perform searching regular expression pattern.
	*/
	if (reg.flags() & std::regex_constants::icase)
	  {
	    std::wstring src_lower;
	    src_lower.resize (src_wstring.size ());
	    std::transform (src_wstring.begin(), src_wstring.end(), src_lower.begin(), ::towlower);
	    is_matched = std::regex_search (src_lower, reg);
	  }
	else
	  {
	    is_matched = std::regex_search (src_wstring, reg);
	  }
#else
	is_matched = std::regex_search (src_wstring, reg);
#endif
      }
    catch (std::regex_error &e)
      {
	// regex execution exception
	is_matched = false;
	error_status = ER_REGEX_EXEC_ERROR;
	std::string error_message = parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    result = is_matched ? V_TRUE : V_FALSE;
    return error_status;
  }

  int count (int &result, const cub_regex_object &reg, const std::string &src, const int position,
	     const INTL_CODESET codeset)
  {
    assert (position >= 0);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_to_wstring (src_wstring, src, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

#if defined(WINDOWS)
    /* HACK: case insensitive doesn't work well on Windows.
    *  This code transforms source string into lowercase
    *  and perform searching regular expression pattern.
    */
    std::wstring target_lower;
    if (reg.flags() & std::regex_constants::icase)
      {
	target_lower.resize (target.size ());
	std::transform (target.begin(), target.end(), target_lower.begin(), ::towlower);
      }
    else
      {
	target_lower = target;
      }
#endif

    int count = 0;
    try
      {
#if defined(WINDOWS)
	auto reg_iter = cub_regex_iterator (target_lower.begin (), target_lower.end (), reg);
#else
	auto reg_iter = cub_regex_iterator (target.begin (), target.end (), reg);
#endif
	auto reg_end = cub_regex_iterator ();
	count = std::distance (reg_iter, reg_end);
      }
    catch (std::regex_error &e)
      {
	// regex execution exception, error_complexity or error_stack
	error_status = ER_REGEX_EXEC_ERROR;
	std::string error_message = cubregex::parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    /* assgin result */
    result = count;
    return error_status;
  }

  int instr (int &result, const cub_regex_object &reg, const std::string &src,
	     const int position, const int occurrence, const int return_opt, const INTL_CODESET codeset)
  {
    assert (position >= 0);
    assert (occurrence >= 1);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_to_wstring (src_wstring, src, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

#if defined(WINDOWS)
    /* HACK: case insensitive doesn't work well on Windows.
    *  This code transforms source string into lowercase
    *  and perform searching regular expression pattern.
    */
    std::wstring target_lower;
    if (reg.flags() & std::regex_constants::icase)
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
	auto reg_iter = cub_regex_iterator (target_lower.begin (), target_lower.end (), reg);
#else
	auto reg_iter = cub_regex_iterator (target.begin (), target.end (), reg);
#endif
	auto reg_end = cub_regex_iterator ();

	int n = 1;
	while (reg_iter != reg_end)
	  {
	    /* match */
	    if (n == occurrence)
	      {
		cub_regex_results match_result = *reg_iter;
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
	std::string error_message = cubregex::parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }

#if defined(WINDOWS)
  /* HACK: case insensitive doesn't work well on Windows.
  *  This code transforms source string into lowercase
  *  and perform searching regular expression pattern.
  */
  int replace (std::string &result, const cub_regex_object &reg, const std::string &src,
	       const std::string &repl, const int position,
	       const int occurrence, const INTL_CODESET codeset)
  {
    assert (position >= 0);
    assert (occurrence >= 0);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_to_wstring (src_wstring, src, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    std::wstring repl_wstring;
    if (cublocale::convert_to_wstring (repl_wstring, repl, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring result_wstring (src_wstring.substr (0, position));
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    std::wstring target_lowercase;
    if (reg.flags() & std::regex_constants::icase)
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
	auto reg_iter = cub_regex_iterator (target_lowercase.begin (), target_lowercase.end (), reg);
	auto reg_end = cub_regex_iterator ();

	int last_pos = 0;
	int match_pos = -1;
	size_t match_length;
	int n = 1;
	auto out = std::back_inserter (result_wstring);

	cub_regex_results match_result;
	while (reg_iter != reg_end)
	  {
	    match_result = *reg_iter;

	    /* prefix */
	    match_pos = match_result.position ();
	    match_length = match_result.length ();
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

	if (cublocale::convert_to_string (result, result_wstring, codeset) == false)
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
	std::string error_message = cubregex::parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }
#else
  int replace (std::string &result, const cub_regex_object &reg, const std::string &src,
	       const std::string &repl, const int position,
	       const int occurrence, const INTL_CODESET codeset)
  {
    assert (position >= 0);
    assert (occurrence >= 0);

    int error_status = NO_ERROR;

    std::wstring src_wstring;
    if (cublocale::convert_to_wstring (src_wstring, src, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    std::wstring repl_wstring;
    if (cublocale::convert_to_wstring (repl_wstring, repl, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring result_wstring (src_wstring.substr (0, position));
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

    int match_pos = -1;
    size_t match_length = 0;
    try
      {
	if (occurrence == 0)
	  {
	    result_wstring.append (
		    std::regex_replace (target, reg, repl_wstring)
	    );
	  }
	else
	  {
	    auto reg_iter = cub_regex_iterator (target.begin (), target.end (), reg);
	    auto reg_end = cub_regex_iterator ();

	    int n = 1;
	    auto out = std::back_inserter (result_wstring);

	    while (reg_iter != reg_end)
	      {
		const cub_regex_results match_result = *reg_iter;

		std::wstring match_prefix = match_result.prefix ().str ();
		out = std::copy (match_prefix.begin (), match_prefix.end (), out);

		/* match */
		match_pos = match_result.position ();
		match_length = match_result.length ();
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

	if (cublocale::convert_to_string (result, result_wstring, codeset) == false)
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
	std::string error_message = cubregex::parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }
#endif

  int substr (std::string &result, bool &is_matched, const cub_regex_object &reg, const std::string &src,
	      const int position, const int occurrence, const INTL_CODESET codeset)
  {
    assert (position >= 0);
    assert (occurrence >= 1);

    int error_status = NO_ERROR;
    is_matched = false;

    std::wstring src_wstring;
    if (cublocale::convert_to_wstring (src_wstring, src, codeset) == false)
      {
	error_status = ER_QSTR_BAD_SRC_CODESET;
	return error_status;
      }

    /* split source string by position value */
    std::wstring result_wstring;
    std::wstring target (
	    src_wstring.substr (position, src_wstring.size () - position)
    );

#if defined(WINDOWS)
    /* HACK: case insensitive doesn't work well on Windows.
    *  This code transforms source string into lowercase
    *  and perform searching regular expression pattern.
    */
    std::wstring target_lower;
    if (reg.flags() & std::regex_constants::icase)
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
	auto reg_iter = cub_regex_iterator (target_lower.begin (), target_lower.end (), reg);
#else
	auto reg_iter = cub_regex_iterator (target.begin (), target.end (), reg);
#endif
	auto reg_end = cub_regex_iterator ();
	auto out = std::back_inserter (result_wstring);

	int n = 1;
	while (reg_iter != reg_end)
	  {
	    cub_regex_results match_result = *reg_iter;

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

	if (cublocale::convert_to_string (result, result_wstring, codeset) == false)
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
	std::string error_message = cubregex::parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }

    return error_status;
  }
}
