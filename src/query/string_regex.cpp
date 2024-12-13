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

#include "string_regex.hpp"

#include "error_manager.h"
#include "memory_alloc.h"
#include "language_support.h"
#include "string_opfunc.h"
#include "system_parameter.h"
#include "locale_helper.hpp"

#include <algorithm>
#include <string>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubregex
{
  //////////////////////////////////////////////////////////////////////////
  // global functions
  //////////////////////////////////////////////////////////////////////////
  compiled_regex::~compiled_regex ()
  {
    if (compiled)
      {
	switch (type)
	  {
	  case engine_type::LIB_CPPSTD:
	    if (compiled->std_obj)
	      {
		delete compiled->std_obj;
	      }
	    break;
	  case engine_type::LIB_RE2:
	    if (compiled->re2_obj)
	      {
		delete compiled->re2_obj;
	      }
	    break;
	  default:
	    assert (false);
	  }
	delete compiled;
	compiled = {nullptr};
      }
  }

  const char *engine_name[] =
  {
    "cppstd",
    "re2"
  };

  const char *get_engine_name (const engine_type &type)
  {
    return engine_name[type];
  }

  //////////////////////////////////////////////////////////////////////////
  // regexp interface API
  //////////////////////////////////////////////////////////////////////////

  int compile_regex_internal (REFPTR (compiled_regex, cr), const std::string &pattern_string, const engine_type type,
			      const opt_flag_type opt_flag, const LANG_COLLATION *collation);
  bool should_compile_skip (REFPTR (compiled_regex, cr), const std::string &pattern, const engine_type type,
			    const opt_flag_type opt_flag,
			    const INTL_CODESET codeset);
  opt_flag_type parse_match_type (const std::string &match_type);

  int
  compile (REFPTR (compiled_regex, cr), const std::string &pattern_string, const std::string &opt_str,
	   const LANG_COLLATION *collation)
  {
    assert (collation);

    opt_flag_type opt_flag = parse_match_type (opt_str);
    if (opt_flag & opt_flag::OPT_ERROR)
      {
	return ER_QPROC_INVALID_PARAMETER;
      }

    engine_type type = static_cast<engine_type> (prm_get_integer_value (PRM_ID_REGEXP_ENGINE));

    std::string utf8_pattern;
    if (cublocale::convert_string_to_utf8 (utf8_pattern, pattern_string, collation->codeset) == false)
      {
	return ER_QSTR_BAD_SRC_CODESET;
      }

    if (should_compile_skip (cr, utf8_pattern, type, opt_flag, collation->codeset) == true)
      {
	return NO_ERROR;
      }

    int error_code = compile_regex_internal (cr, utf8_pattern, type, opt_flag, collation);
    return error_code;
  }

  int
  search (int &result, const compiled_regex &reg, const std::string &src)
  {
    std::string utf8_src;
    if (cublocale::convert_string_to_utf8 (utf8_src, src, reg.codeset) == false)
      {
	result = V_FALSE;
	return ER_QSTR_BAD_SRC_CODESET;
      }

    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_search (result, reg, utf8_src);
	break;
      case engine_type::LIB_RE2:
	return re2_search (result, reg, utf8_src);
	break;
      default:
	assert (false);
      }
    return ER_FAILED;
  }

  int
  count (int &result, const compiled_regex &reg, const std::string &src, const int position)
  {
    std::string utf8_src;
    if (cublocale::convert_string_to_utf8 (utf8_src, src, reg.codeset) == false)
      {
	result = 0;
	return ER_QSTR_BAD_SRC_CODESET;
      }

    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_count (result, reg, utf8_src, position);
	break;
      case engine_type::LIB_RE2:
	return re2_count (result, reg, utf8_src, position);
	break;
      default:
	assert (false);
      }
    return ER_FAILED;
  }

  int
  instr (int &result, const compiled_regex &reg, const std::string &src, const int position, const int occurrence,
	 const int return_opt)
  {
    result = 0;
    std::string utf8_src;
    if (cublocale::convert_string_to_utf8 (utf8_src, src, reg.codeset) == false)
      {
	return ER_QSTR_BAD_SRC_CODESET;
      }

    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_instr (result, reg, src, position, occurrence, return_opt);
	break;
      case engine_type::LIB_RE2:
	return re2_instr (result, reg, src, position, occurrence, return_opt);
	break;
      default:
	assert (false);
      }

    return ER_FAILED;
  }

  int replace (std::string &result, const compiled_regex &reg, const std::string &src, const std::string &repl,
	       const int position, const int occurrence)
  {
    std::string utf8_src;
    if (cublocale::convert_string_to_utf8 (utf8_src, src, reg.codeset) == false)
      {
	result.assign (src);
	return ER_QSTR_BAD_SRC_CODESET;
      }

    std::string utf8_repl;
    if (cublocale::convert_string_to_utf8 (utf8_repl, repl, reg.codeset) == false)
      {
	result.assign (src);
	return ER_QSTR_BAD_SRC_CODESET;
      }

    int error = NO_ERROR;
    std::string utf8_result;
    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	error = std_replace (utf8_result, reg, utf8_src, utf8_repl, position, occurrence);
	break;
      case engine_type::LIB_RE2:
	error = re2_replace (utf8_result, reg, utf8_src, utf8_repl, position, occurrence);
	break;
      default:
	assert (false);
	error = ER_FAILED;
      }

    if (cublocale::convert_utf8_to_string (result, utf8_result, reg.codeset) == false)
      {
	result.assign (src);
	return ER_QSTR_BAD_SRC_CODESET;
      }

    return error;
  }

  int substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
	      const int position, const int occurrence)
  {
    std::string utf8_src;
    if (cublocale::convert_string_to_utf8 (utf8_src, src, reg.codeset) == false)
      {
	return ER_QSTR_BAD_SRC_CODESET;
      }

    int error = NO_ERROR;
    std::string utf8_result;
    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	error = std_substr (utf8_result, is_matched, reg, utf8_src, position, occurrence);
	break;
      case engine_type::LIB_RE2:
	error = re2_substr (utf8_result, is_matched, reg, utf8_src, position, occurrence);
	break;
      default:
	assert (false);
	error = ER_FAILED;
      }

    if (cublocale::convert_utf8_to_string (result, utf8_result, reg.codeset) == false)
      {
	return ER_QSTR_BAD_SRC_CODESET;
      }

    return error;
  }

  int
  compile_regex_internal (REFPTR (compiled_regex, cr),  const std::string &pattern_string, const engine_type type,
			  const opt_flag_type opt_flag,
			  const LANG_COLLATION *collation)
  {
    int error = NO_ERROR;

    // delete previous compiled object
    if (cr)
      {
	delete cr;
      }

    cr = new compiled_regex ();
    cr->type = type;
    cr->compiled = new compiled_regex_object { nullptr };
    cr->pattern.assign (pattern_string);
    cr->flags = opt_flag;
    cr->codeset = collation->codeset;

    switch (cr->type)
      {
      case engine_type::LIB_CPPSTD:
	error = std_compile (cr, collation);
	break;
      case engine_type::LIB_RE2:
	error = re2_compile (cr);
	break;
      default:
	assert (false);
	error = ER_FAILED;
	break;
      }

    return error;
  }

  bool
  should_compile_skip (REFPTR (compiled_regex, cr), const std::string &pattern, const engine_type type,
		       const opt_flag_type opt_flag,
		       const INTL_CODESET codeset)
  {
    if (cr == nullptr
	|| cr->type != type
	|| cr->flags != opt_flag
	|| cr->pattern.size() != pattern.size()
	|| cr->pattern.compare (pattern) != 0
	|| cr->codeset != codeset)
      {
	return false;
      }
    else
      {
	return true;
      }
  }

  opt_flag_type parse_match_type (const std::string &opt_str)
  {
    opt_flag_type opt_flag = opt_flag::OPT_ICASE;
    auto mt_iter = opt_str.begin ();
    while ((mt_iter != opt_str.end ()))
      {
	char opt = *mt_iter;
	switch (opt)
	  {
	  case 'c':
	    opt_flag &= ~opt_flag::OPT_ICASE;
	    break;
	  case 'i':
	    opt_flag |= opt_flag::OPT_ICASE;
	    break;
	  default:
	    opt_flag |= opt_flag::OPT_ERROR;
	    break;
	  }
	++mt_iter;
      }
    return opt_flag;
  }
}
