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

#include <algorithm>
#include <string>

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
	    delete compiled->std_obj;
	    break;
	  case engine_type::LIB_RE2:
	    delete compiled->re2_obj;
	    break;
	  default:
	    assert (false);
	  }
	delete compiled;
      }
  }

  const char *engine_name[] =
  {
    "cppstd",
    "re2"
  };

  bool check_regexp_engine_prm (const char *param_name)
  {
    assert (param_name);

    if (strcmp (param_name, engine_name[LIB_CPPSTD]) == 0
	|| strcmp (param_name, engine_name[LIB_RE2]) == 0)
      {
	return true;
      }
    return false;
  }

  engine_type get_engine_type_by_name (const char *param_name)
  {
    if (strcmp (param_name, engine_name[LIB_CPPSTD]) == 0)
      {
	return engine_type::LIB_CPPSTD;
      }
    else if (strcmp (param_name, engine_name[LIB_RE2]) == 0)
      {
	return engine_type::LIB_RE2;
      }
    return engine_type::LIB_NONE;
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
    opt_flag_type opt_flag = parse_match_type (opt_str);
    if (opt_flag & opt_flag::OPT_ERROR)
      {
	return ER_QPROC_INVALID_PARAMETER;
      }

    const char *engine_type_prm = prm_get_string_value (PRM_ID_REGEXP_ENGINE);

#if !defined (NDEBUG)
    assert (check_regexp_engine_prm (engine_type_prm));
#endif

    engine_type type = get_engine_type_by_name (engine_type_prm);


    if (should_compile_skip (cr, pattern_string, type, opt_flag, collation->codeset) == true)
      {
	return NO_ERROR;
      }

    return compile_regex_internal (cr, pattern_string, type, opt_flag, collation);
  }

  int
  search (int &result, const compiled_regex &reg, const std::string &src)
  {
    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_search (result, reg, src);
	break;
      case engine_type::LIB_RE2:
	return re2_search (result, reg, src);
	break;
      default:
	assert (false);
      }
    return ER_FAILED;
  }

  int
  count (int &result, const compiled_regex &reg, const std::string &src, const int position)
  {
    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_count (result, reg, src, position);
	break;
      case engine_type::LIB_RE2:
	return re2_count (result, reg, src, position);
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
    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_replace (result, reg, src, repl, position, occurrence);
	break;
      case engine_type::LIB_RE2:
	return re2_replace (result, reg, src, repl, position, occurrence);
	break;
      default:
	assert (false);
      }
    return ER_FAILED;
  }

  int substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
	      const int position, const int occurrence)
  {
    switch (reg.type)
      {
      case engine_type::LIB_CPPSTD:
	return std_substr (result, is_matched, reg, src, position, occurrence);
	break;
      case engine_type::LIB_RE2:
	return re2_substr (result, is_matched, reg, src, position, occurrence);
	break;
      default:
	assert (false);
      }
    return ER_FAILED;
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
	if (cr->compiled)
	  {
	    delete cr->compiled;
	  }
      }
    else
      {
	cr = new compiled_regex ();
      }

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