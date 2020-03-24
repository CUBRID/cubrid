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

#ifndef _STRING_REGEX_HPP_
#define _STRING_REGEX_HPP_

#ifdef __cplusplus
#include <regex>
#include <locale>

#include "error_manager.h"
#include "language_support.h"

// forward declarations
namespace cubregex
{
  struct compiled_regex;
  struct cub_reg_traits;
}

// alias
using cub_compiled_regex = cubregex::compiled_regex;
using cub_regex_object = std::basic_regex <wchar_t, cubregex::cub_reg_traits>;
using cub_regex_iterator = std::regex_iterator<std::wstring::iterator, wchar_t, cubregex::cub_reg_traits>;
using cub_regex_results = std::match_results <std::wstring::iterator>;

namespace cubregex
{
  struct compiled_regex
  {
    cub_regex_object *regex;
    char *pattern;

    compiled_regex ();
    ~compiled_regex ();
  };

  /* it throws the error_collate when collatename syntax ([[. .]]), which gives an inconsistent result, is detected. */
  struct cub_reg_traits : std::regex_traits<wchar_t>
  {
    template< class Iter >
    string_type lookup_collatename ( Iter first, Iter last ) const
    {
      throw std::regex_error (std::regex_constants::error_collate);
    }

    bool isctype ( char_type c, char_class_type f ) const
    {
#if !defined(WINDOWS)
      // HACK: matching '[[:blank:]]' for blank character doesn't work on gcc
      // C++ regex uses std::ctype<char_type>::is () to match character class
      // It does not support blank char class type so '[[:blank:]]' doesn't work to match ' '(0x20).
      // For backward compatability, Here use iswblank () explicitly to match blank character.
      if ((f & std::ctype_base::blank) == 1)
	{
	  return std::iswblank (c);
	}
#endif
      return std::regex_traits<char_type>::isctype (c, f);
    }
  };

  void clear (cub_regex_object *&compiled_regex, char *&compiled_pattern);
  int parse_match_type (std::regex_constants::syntax_option_type &reg_flags, std::string &opt_str);

  /* because regex_error::what() gives different messages depending on compiler, an error message should be returned by error code of regex_error explicitly. */
  std::string parse_regex_exception (std::regex_error &e);

  bool check_should_recompile (const cub_regex_object *compiled_regex, const char *compiled_pattern,
			       const std::string &pattern,
			       const std::regex_constants::syntax_option_type reg_flags);

  int compile (cub_regex_object *&rx_compiled_regex, const char *pattern,
	       const std::regex_constants::syntax_option_type reg_flags, const LANG_COLLATION *collation);
  int search (int &result, const cub_regex_object &reg, const std::string &src, const INTL_CODESET codeset);

  int count (int &result, const cub_regex_object &reg, const std::string &src, const int position,
	     const INTL_CODESET codeset);
  int instr (int &result, const cub_regex_object &reg, const std::string &src,
	     const int position, const int occurrence, const int return_opt, const INTL_CODESET codeset);
  int replace (std::string &result, const cub_regex_object &reg, const std::string &src,
	       const std::string &repl, const int position,
	       const int occurrence, const INTL_CODESET codeset);
  int substr (std::string &result, bool &is_matched, const cub_regex_object &reg, const std::string &src,
	      const int position, const int occurrence, const INTL_CODESET codeset);
}
#endif

#endif // _STRING_REGEX_HPP_
