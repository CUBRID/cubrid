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

//
// string_regex - definitions and functions related to regular expression
//

#ifndef _STRING_REGEX_HPP_
#define _STRING_REGEX_HPP_

#include <regex>
#include "re2/re2.h"

#include "error_manager.h"
#include "intl_support.h"
#include "language_support.h"

#include "string_regex_constants.hpp"

#define GET_RE2_OBJ(cr) (*(cr.compiled->re2_obj))
#define GET_STD_OBJ(cr) (*(cr.compiled->std_obj))

namespace cubregex
{
  struct cub_reg_traits;
}

// alias
using cub_std_regex_iterator = std::regex_iterator<std::wstring::iterator, wchar_t, cubregex::cub_reg_traits>;
using cub_std_regex_results = std::match_results <std::wstring::iterator>;
using cub_std_regex = std::basic_regex <wchar_t, cubregex::cub_reg_traits>;

namespace cubregex
{
  union compiled_regex_object
  {
    cub_std_regex *std_obj;
    re2::RE2 *re2_obj;
  };

  struct compiled_regex
  {
    engine_type type;
    compiled_regex_object *compiled;
    std::string pattern;
    opt_flag_type flags;
    INTL_CODESET codeset;

    compiled_regex ()
      : type (LIB_NONE)
      , compiled (nullptr)
      , pattern ()
      , flags (0)
      , codeset (INTL_CODESET_NONE)
    {
      //
    }
    ~compiled_regex ();
  };

  /* related to system parameter */
  bool check_regexp_engine_prm (const char *param_name);
  engine_type get_engine_type_by_name (const char *param_name);

  /*
  * compile() - Compile regex object
  *   return: Error code
  *   cr(in/out): Compiled regex object
  *   collation(in)
  */
  int compile (REFPTR (compiled_regex, cr), const std::string &pattern_string, const std::string &opt_str,
	       const LANG_COLLATION *collation);
  int search (int &result, const compiled_regex &reg, const std::string &src);
  int count (int &result, const compiled_regex &reg, const std::string &src, const int position);
  int instr (int &result, const compiled_regex &reg, const std::string &src,
	     const int position, const int occurrence, const int return_opt);
  int replace (std::string &result, const compiled_regex &reg, const std::string &src,
	       const std::string &repl, const int position,
	       const int occurrence);
  int substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
	      const int position, const int occurrence);

  //***********************************************************************************************
  // C++ <regex> standard library
  //***********************************************************************************************

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

  std::regex_constants::syntax_option_type std_parse_match_type (const opt_flag_type &opt_type);

  /* because regex_error::what() gives different messages depending on compiler, an error message should be returned by error code of regex_error explicitly. */
  std::string std_parse_regex_exception (std::regex_error &e);

  int std_compile (REFPTR (compiled_regex, cr), const LANG_COLLATION *collation);
  int std_search (int &result, const compiled_regex &reg, const std::string &src);
  int std_count (int &result, const compiled_regex &reg, const std::string &src, const int position);
  int std_instr (int &result, const compiled_regex &reg, const std::string &src,
		 const int position, const int occurrence, const int return_opt);
  int std_replace (std::string &result, const compiled_regex &reg, const std::string &src,
		   const std::string &repl, const int position,
		   const int occurrence);
  int std_substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
		  const int position, const int occurrence);

  //***********************************************************************************************
  // Google RE2 library
  //***********************************************************************************************

  RE2::Options re2_parse_match_type (const opt_flag_type &opt_type);

  int re2_compile (REFPTR (compiled_regex, cr));
  int re2_search (int &result, const compiled_regex &reg, const std::string &src);
  int re2_count (int &result, const compiled_regex &reg, const std::string &src, const int position);
  int re2_instr (int &result, const compiled_regex &reg, const std::string &src,
		 const int position, const int occurrence, const int return_opt);
  int re2_replace (std::string &result, const compiled_regex &reg, const std::string &src,
		   const std::string &repl, const int position,
		   const int occurrence);
  int re2_substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
		  const int position, const int occurrence);
}

using cub_compiled_regex = cubregex::compiled_regex;

#endif // _STRING_REGEX_HPP_
