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

#include "error_manager.h"

namespace cubregex
{
  /* it throws the error_collate when collatename syntax ([[. .]]), which gives an inconsistent result, is detected. */
  struct cub_reg_traits : std::regex_traits<char>
  {
    template< class Iter >
    string_type lookup_collatename ( Iter first, Iter last ) const
    {
      throw std::regex_error (std::regex_constants::error_collate);
    }
  };

  /* because regex_error::what() gives different messages depending on compiler, an error message should be returned by error code of regex_error explicitly. */
  std::string parse_regex_exception (std::regex_error &e);

  template< class CharT, class Reg_Traits >
  int compile_regex (const CharT *pattern, std::basic_regex<CharT, Reg_Traits> *&rx_compiled_regex,
		     std::regex_constants::syntax_option_type &reg_flags);
}

using cub_regex_object = std::basic_regex <char, cubregex::cub_reg_traits>;
#endif

#endif // _STRING_REGEX_HPP_
