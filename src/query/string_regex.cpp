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

#include "error_manager.h"
#include "memory_alloc.h"

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
    if (pattern != NULL)
      {
	db_private_free_and_init (NULL, pattern);
      }

    if (regex != NULL)
      {
	delete regex;
	regex = NULL;
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

  int compile (cub_regex_object *&rx_compiled_regex, const std::string &pattern,
	       const std::regex_constants::syntax_option_type reg_flags)
  {
    int error_status = NO_ERROR;
    try
      {
#if defined(WINDOWS)
	/* HACK: collating element features doesn't work well on Windows.
	*  And lookup_collatename is not invoked when regex pattern has collating element.
	*  It is hacky code finding collating element pattern and throw error.
	*/
	char *collate_elem_pattern = "[[.";
	int found = pattern.find ( std::string (collate_elem_pattern));
	if (found != std::string::npos)
	  {
	    throw std::regex_error (std::regex_constants::error_collate);
	  }
#endif
	rx_compiled_regex = new cub_regex_object (pattern, reg_flags);
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

  int search (bool &result, const cub_regex_object &reg, const std::string &src)
  {
    int error_status = NO_ERROR;
    try
      {
	result = std::regex_search (src, reg);
      }
    catch (std::regex_error &e)
      {
	// regex execution exception
	result = false;
	error_status = ER_REGEX_EXEC_ERROR;
	std::string error_message = parse_regex_exception (e);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
      }
    return error_status;
  }

  int replace (std::string &result, const cub_regex_object &reg, const std::string &src,
	       const std::string &repl, const int position,
	       const int occurrence)
  {
    assert (position >= 0 && (size_t) position < src.size ());
    assert (occurrence >= 0);

    int error_status = NO_ERROR;

    /* split source string by position value */
    result.assign (src.substr (0, position));
    std::string target (
	    src.substr (position, src.size () - position)
    );

    try
      {
	if (occurrence == 0)
	  {
	    result.append (
		    std::regex_replace (target, reg, repl)
	    );
	  }
	else
	  {
	    auto reg_iter = cub_regex_iterator (target.begin (), target.end (), reg);
	    auto reg_end = cub_regex_iterator ();

	    int n = 1;
	    auto out = std::back_inserter (result);

	    while (reg_iter != reg_end)
	      {
		const cub_regex_results &match_result = *reg_iter;

		std::string match_prefix = match_result.prefix ().str ();
		out = std::copy (match_prefix.begin (), match_prefix.end (), out);

		if (n == occurrence)
		  {
		    out = match_result.format (out, repl);
		  }
		else
		  {
		    std::string match_str = match_result.str ();
		    out = std::copy (match_str.begin (), match_str.end (), out);
		  }

		++n;
		++reg_iter;
		if (reg_iter == reg_end)
		  {
		    std::string match_suffix = match_result.suffix (). str ();
		    out = std::copy (match_suffix.begin (), match_suffix.end (), out);
		  }
	      }
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
