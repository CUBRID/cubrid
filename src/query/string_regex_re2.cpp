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
#include "intl_support.h"

#include <iostream>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubregex
{
  using namespace re2;

  template <typename Func>
  static bool re2_on_match (const RE2 &re2, const re2::StringPiece &target, const int occurrence, Func &&lambda);

  static void re2_split_string_utf8 (const std::string &src, const int position, std::string &a, std::string &b);
  static void re2_distance_utf8 (int &length, const char *s, const char *e);

  RE2::Options
  re2_parse_match_type (const opt_flag_type &opt_type)
  {
    RE2::Options opt;
    // default
    opt.set_log_errors (false);
    opt.set_case_sensitive ((opt_type & OPT_ICASE) == 0);

    // TODO: set max memory limits from a system paramter
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
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REGEX_COMPILE_ERROR, 1, reg->error().c_str());
	return ER_REGEX_COMPILE_ERROR;
      }

    return NO_ERROR;
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
    std::string prefix;
    std::string target;
    re2_split_string_utf8 (src, position, prefix, target);

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
    std::string prefix;
    std::string target;
    re2_split_string_utf8 (src, position, prefix, target);

    int match_idx = -1;
    re2::StringPiece target_piece (target);

    auto lambda = [&] (re2::StringPiece &match, char *current_p)
    {
      re2_distance_utf8 (match_idx, target.data(), match.data());

      if (return_opt == 1)
	{
	  int match_utf8_length = 0;
	  re2_distance_utf8 (match_utf8_length, match.data(), match.data() + match.size ());
	  match_idx += match_utf8_length;
	}
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
    std::string prefix;
    std::string target;
    re2_split_string_utf8 (src, position, prefix, target);

    char *last_matched_ptr = NULL;
    int last_matched_size = 0;

    std::string mid;
    auto lambda = [&] (re2::StringPiece &match, char *current_p)
    {
      if (last_matched_ptr == NULL && target.data() < current_p)
	{
	  // before the first match
	  mid.append (target.data (), current_p - target.data());
	}

      last_matched_ptr = (char *) match.data ();
      last_matched_size = match.size ();

      if (current_p < last_matched_ptr)
	{
	  // the prefix of the matched
	  mid.append (current_p, last_matched_ptr - current_p);
	}

      // replace
      mid.append (repl);
    };

    re2::StringPiece target_piece (target);
    bool is_matched = re2_on_match (GET_RE2_OBJ (reg), target_piece, occurrence, lambda);
    if (last_matched_ptr == NULL)
      {
	result.append (src);
      }
    else
      {
	// append prefix of the replaced sub-string
	result.append (prefix);

	// if the occurrence is zero, append a sub-string with all concatenated from the replaced first match to the replaced last match
	// else append a sub-string with a replaced match corresponding to the occurrence
	result.append (mid);

	// the remaining after the last matched
	char *end_ptr = (char *) target.data () + target.size ();
	char *last_matched_end_ptr = (char *) last_matched_ptr + last_matched_size;
	if (last_matched_end_ptr < end_ptr)
	  {
	    result.append (last_matched_end_ptr, end_ptr - last_matched_end_ptr);
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
    std::string prefix;
    std::string target;
    re2_split_string_utf8 (src, position, prefix, target);

    re2::StringPiece target_piece (target);
    auto lambda = [&] (re2::StringPiece &match, char *current_p)
    {
      result.assign (match.as_string());
    };

    is_matched = re2_on_match (GET_RE2_OBJ (reg), target_piece, occurrence, lambda);
    return NO_ERROR;
  }

  static void
  re2_distance_utf8 (int &length, const char *s, const char *e)
  {
    length = 0;
    char *current = (char *) s;
    int dummy;
    while (current < e)
      {
	current = (char *) intl_nextchar_utf8 ((const unsigned char *) current, &dummy);

	if (current <= e)
	  {
	    length++;
	  }
      }
  }

  static void
  re2_split_string_utf8 (const std::string &src, const int position, std::string &a, std::string &b)
  {
    int dummy;
    char *s = (char *) src.data ();
    const char *end = src.data() + src.size ();
    int i;
    for (i = 0; i < position && s < end; i++)
      {
	s = (char *) intl_nextchar_utf8 ((const unsigned char *) s, &dummy);
      }

    if (i == position && s < end)
      {
	int pos = s - src.data ();
	a.assign (src.substr (0, pos));
	b.assign (src.substr (pos, src.size() - pos));
      }
    else
      {
	a.clear ();
	b.clear ();
      }
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

	if (n == occurrence || occurrence == 0)
	  {
	    is_matched = true;
	    lambda (match, current_p); // do something on regex is matched
	    if (n == occurrence)
	      {
		break;
	      }
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
}
