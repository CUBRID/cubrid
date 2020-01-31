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
#include <string>

namespace cubregex
{
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

  template< class CharT, class Reg_Traits >
  int compile_regex (const CharT *pattern, std::basic_regex<CharT, Reg_Traits> *&rx_compiled_regex,
		     std::regex_constants::syntax_option_type &reg_flags)
  {
    {
      int error_status = NO_ERROR;
      std::basic_string <CharT> pattern_str (pattern);
      try
	{
#if defined(WINDOWS)
	  /* HACK: collating element features doesn't work well on Windows.
	  *  And lookup_collatename is not invoked when regex pattern has collating element.
	  *  It is hacky code finding collating element pattern and throw error.
	  */
	  CharT *collate_elem_pattern = "[[.";
	  int found = pattern_str.find ( std::string (collate_elem_pattern));
	  if (found != std::string::npos)
	    {
	      throw std::regex_error (std::regex_constants::error_collate);
	    }
#endif
	  rx_compiled_regex = new std::basic_regex<CharT, Reg_Traits> (pattern_str, reg_flags);
	}
      catch (std::regex_error &e)
	{
	  // regex compilation exception
	  error_status = ER_REGEX_COMPILE_ERROR;
	  std::string error_message = parse_regex_exception (e);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
	  if (rx_compiled_regex != NULL)
	    {
	      delete rx_compiled_regex;
	      rx_compiled_regex = NULL;
	    }
	}

      return error_status;
    }
  }

  template int compile_regex <char> (const char *pattern, std::basic_regex<char, cub_reg_traits> *&rx_compiled_regex,
				     std::regex_constants::syntax_option_type &reg_flags);

}