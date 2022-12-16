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
// string_regex_re2 - regular expression engine using Google RE2 library
//

#include "string_regex.hpp"

#include "locale_helper.hpp"

#include <iostream>

namespace cubregex
{
  using namespace re2;

  template <typename Func>
  static bool re2_on_match (const RE2 &re2, const re2::StringPiece &target, const int occurrence, Func &&lambda);

  RE2::Options
  re2_parse_match_type (const opt_flag_type &opt_type)
  {
    RE2::Options opt;
    // default
    opt.set_log_errors (false);
    opt.set_case_sensitive ((opt_type & OPT_ICASE) == 0);
    // TODO
    // opt.set_max_mem (opt.max_mem ());
    return opt;
  }

  int
  re2_compile (REFPTR (compiled_regex, cr))
  {
    RE2::Options opt = re2_parse_match_type (cr->flags);
    REFPTR (RE2, reg) = cr->compiled->re2_obj = new RE2 (cr->pattern, opt);

    if (!reg->ok())
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REGEX_COMPILE_ERROR, 1, reg->error());
	return ER_REGEX_COMPILE_ERROR;
      }

    return NO_ERROR;
  }

  template <typename Func>
  static bool
  re2_on_match (const RE2 &re2, const re2::StringPiece &target, const int occurrence, Func &&lambda)
  {
    bool is_matched = false;
    int n = 1;
    char *current_p = (char *) target.data();
    const char *end_p = current_p + target.size();
    re2::StringPiece match;
    while (current_p <= end_p)
      {
	is_matched = re2.Match (target, static_cast<size_t> (current_p - target.data()),
				target.size(), RE2::UNANCHORED, &match, 1);
	if (!is_matched)
	  {
	    break;
	  }

	if (n == occurrence)
	  {
	    is_matched = true;
	    lambda (match, current_p); // do something on regex is matched
	    break;
	  }
	else
	  {
	    is_matched = false;
	  }

	++n; // next occurence idx
	current_p = (char *) match.data() + match.size (); // next pos to search a non-overapping regex match
      }
    return is_matched;
  }

  int
  re2_search (int &result, const compiled_regex &reg, const std::string &src)
  {
    bool is_matched = RE2::PartialMatch (src, GET_RE2_OBJ (reg));
    result = is_matched ? V_TRUE : V_FALSE;
    return NO_ERROR;
  }

  int
  re2_count (int &result, const compiled_regex &reg, const std::string &src, const int position)
  {
    assert (position >= 0);

    /* split source string by position value */
    std::string target = src.substr (position, src.size () - position);

    result = 0;
    re2::StringPiece piece (target);
    while (RE2::FindAndConsume (&piece, GET_RE2_OBJ (reg)))
      {
	++result;
      }
    return NO_ERROR;
  }

  int
  re2_instr (int &result, const compiled_regex &reg, const std::string &src,
	     const int position, const int occurrence, const int return_opt)
  {
    assert (position >= 0);
    assert (occurrence >= 1);

    /* split source string by position value */
    std::string target = src.substr (position, src.size () - position);

    int match_idx = -1;
    re2::StringPiece target_piece (target);

    auto lambda = [&] (re2::StringPiece &match, char *current_p)
    {
      match_idx = static_cast<size_t> (match.data() - target.data());
      match_idx += (return_opt == 1) ? match.size() : 0;
    };

    bool is_matched = re2_on_match (GET_RE2_OBJ (reg), target_piece, occurrence, lambda);
    if (match_idx != -1)
      {
	result = position + match_idx + 1;
      }
    else
      {
	result = 0;
      }

    return NO_ERROR;
  }

  int
  re2_replace (std::string &result, const compiled_regex &reg, const std::string &src,
	       const std::string &repl, const int position,
	       const int occurrence)
  {
    assert (position >= 0);
    assert (occurrence >= 0);

    /* split source string by position value */
    std::string result_string (src.substr (0, position));
    std::string target = src.substr (position, src.size () - position);

    re2::StringPiece rewrite (repl);
    if (occurrence == 0)
      {
	RE2::GlobalReplace (&target, GET_RE2_OBJ (reg), rewrite);
	result.assign (result_string.append (target));
      }
    else if (occurrence == 1)
      {
	RE2::Replace (&target, GET_RE2_OBJ (reg), rewrite);
	result.assign (result_string.append (target));
      }
    else
      {
	int match_pos = -1;
	re2::StringPiece target_piece (target);
	auto lambda = [&] (re2::StringPiece &match, char *current_p)
	{
	  // append prefix of the matched sub-string
	  result.append (result_string);

	  if (target.data () < current_p)
	    {
	      result.append (target.data (), current_p - target.data());
	    }

	  if (current_p < match.data())
	    {
	      result.append (current_p, match.data() - current_p);
	    }

	  // replace
	  result.append (rewrite);

	  // the remaining part
	  match_pos = match.data() + match.size () - target.data();
	  int size = target.size() - match_pos;
	  result.append (target.substr (match_pos, size));
	};

	bool is_matched = re2_on_match (GET_RE2_OBJ (reg), target_piece, occurrence, lambda);
	if (match_pos == -1)
	  {
	    result.append (result_string.append (target));
	  }
      }

    return NO_ERROR;
  }

  int
  re2_substr (std::string &result, bool &is_matched, const compiled_regex &reg, const std::string &src,
	      const int position, const int occurrence)
  {
    assert (position >= 0);
    assert (occurrence >= 1);

    /* split source string by position value */
    std::string target = src.substr (position, src.size () - position);

    re2::StringPiece target_piece (target);
    auto lambda = [&] (re2::StringPiece &match, char *current_p)
    {
      result.assign (match.as_string());
    };

    is_matched = re2_on_match (GET_RE2_OBJ (reg), target_piece, occurrence, lambda);
    return NO_ERROR;
  }
}
