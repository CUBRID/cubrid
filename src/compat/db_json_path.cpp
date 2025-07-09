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

#include "db_json_path.hpp"

#include "db_json.hpp"
#include "db_rapidjson.hpp"
#include "memory_alloc.h"
#include "string_opfunc.h"
#include "system_parameter.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

enum class JSON_PATH_TYPE
{
  JSON_PATH_SQL_JSON,
  JSON_PATH_POINTER
};

static void db_json_trim_leading_spaces (std::string &path_string);
static JSON_PATH_TYPE db_json_get_path_type (std::string &path_string);
static bool db_json_isspace (const unsigned char &ch);
static std::size_t skip_whitespaces (const std::string &path, std::size_t token_begin);
static int db_json_path_is_token_valid_array_index (const std::string &str, bool allow_wildcards, unsigned long &index,
    std::size_t start = 0, std::size_t end = 0);
static bool db_json_path_is_token_valid_quoted_object_key (const std::string &path, std::size_t &token_begin);
static bool db_json_path_quote_and_validate_unquoted_object_key (std::string &path, std::size_t &token_begin);
static bool db_json_path_is_token_valid_unquoted_object_key (const std::string &path, std::size_t &token_begin);
static bool db_json_path_is_valid_identifier_start_char (unsigned char ch);
static bool db_json_path_is_valid_identifier_char (unsigned char ch);
static void db_json_remove_leading_zeros_index (std::string &index);
static bool db_json_iszero (const unsigned char &ch);

static bool
db_json_iszero (const unsigned char &ch)
{
  return ch == '0';
}

/*
 * db_json_path_is_token_valid_quoted_object_key () - Check if a quoted object_key is valid
 *
 * return               : true/false
 * path (in)            : path to be checked
 * token_begin (in/out) : beginning offset of the token, is replaced with beginning of the next token or path.length ()
 */
static bool
db_json_path_is_token_valid_quoted_object_key (const std::string &path, std::size_t &token_begin)
{
  std::size_t i = token_begin + 1;
  bool unescaped_backslash = false;
  // stop at unescaped '"'; note that there should be an odd nr of backslashes before '"' for it to be escaped
  for (; i < path.length () && (path[i] != '"' || unescaped_backslash); ++i)
    {
      if (path[i] == '\\')
	{
	  unescaped_backslash = !unescaped_backslash;
	}
      else
	{
	  unescaped_backslash = false;
	}
    }

  if (i == path.length ())
    {
      return false;
    }

  token_begin = skip_whitespaces (path, i + 1);
  return true;
}

/*
 * db_json_path_is_token_valid_unquoted_object_key () - Validate and quote an object_key
 *
 * return               : validation result
 * path (in/out)        : path to be checked
 * token_begin (in/out) : is replaced with beginning of the next token or path.length ()
 */
static bool
db_json_path_quote_and_validate_unquoted_object_key (std::string &path, std::size_t &token_begin)
{
  std::size_t i = token_begin;
  bool validation_result = db_json_path_is_token_valid_unquoted_object_key (path, i);
  if (validation_result)
    {
      // we normalize object_keys by quoting them - e.g. $.objectkey we represent as $."objectkey"
      path.insert (token_begin, "\"");
      path.insert (i + 1, "\"");

      token_begin = skip_whitespaces (path, i + 2 /* we inserted 2 quotation marks */);
    }
  return validation_result;
}

static bool
db_json_path_is_valid_identifier_start_char (unsigned char ch)
{
  // todo: As per SQL Standard accept Ecmascript Identifier start:
  // \UnicodeEscapedSequence
  // Any char in Unicode categories: Titlecase letter (Lt), Modifier letter (Lm), Other letter (Lo), Letter number (Nl)

  return ch == '_' || std::isalpha (ch);
}

static bool
db_json_path_is_valid_identifier_char (unsigned char ch)
{
  // todo: As per SQL Standard accept Ecmascript Identifier:
  // \UnicodeEscapedSequence
  // Any char in Unicode categories: Connector punctuation (Pc), Non-spacing mark (Mn),
  // Combining spacing mark (Mc), Decimal number (Nd), Titlecase letter (Lt), Modifier letter (Lm), Other letter (Lo)
  // Letter number (Nl)

  return ch == '_' || std::isalnum (ch);
}

/*
 * db_json_path_is_token_valid_unquoted_object_key () - Check if an unquoted object_key is valid
 *
 * return                  : true/false
 * path (in)               : path to be checked
 * token_begin (in/out)    : beginning offset of the token, is replaced with first char's position
 *                           outside of the current valid token
 */
static bool
db_json_path_is_token_valid_unquoted_object_key (const std::string &path, std::size_t &token_begin)
{
  if (path == "")
    {
      return false;
    }
  std::size_t i = token_begin;

  // todo: this needs change. SQL standard specifies that object key format must obey
  // JavaScript rules of an Identifier (6.10.1).
  // Besides alphanumerics, object keys can be valid ECMAScript identifiers as defined in
  // http://www.ecma-international.org/ecma-262/5.1/#sec-7.6

  // Defined syntax (approx.):
  // IdentifierName -> IdentifierStart | (IdentifierName IdentifierPart)
  // IdentifierStart -> $ ( note: this is the ONLY specified forbidden by SQL Standard) | _ | \UnicodeEscapeSequence
  // IdentifierPart -> IdentifierStart | InicodeCombinigMark | UnicodeDigit | UnicodeConnectorPunctuation | <ZWNJ>
  // | <ZWJ>

  if (i < path.length () && !db_json_path_is_valid_identifier_start_char (static_cast<unsigned char> (path[i])))
    {
      return false;
    }

  ++i;
  for (; i < path.length () && db_json_path_is_valid_identifier_char (static_cast<unsigned char> (path[i])); ++i);

  token_begin = i;

  return true;
}

/*
 * db_json_path_is_token_valid_array_index () - verify if token is a valid array index. token can be a substring of
 *                                              first argument (by default the entire argument).
 *
 * return          : no error if token can be converted successfully to an integer smaller than json_max_array_idx
 *                   variable
 * str (in)        : token or the string that token belong to
 * allow_wildcards : whether json_path wildcards are allowed
 * index (out)     : created index token
 * start (in)      : start of token; default is start of string
 * end (in)        : end of token; default is end of string; 0 is considered default value
 */
static int
db_json_path_is_token_valid_array_index (const std::string &str, bool allow_wildcards,
    unsigned long &index, std::size_t start, std::size_t end)
{
  // json pointer will corespond the symbol '-' to JSON_ARRAY length
  // so if we have the json {"A":[1,2,3]} and the path /A/-
  // this will point to the 4th element of the array (zero indexed)
  if (str == "-")
    {
      return NO_ERROR;
    }

  if (end == 0)
    {
      // default is end of string
      end = str.length ();
    }

  if (start == end)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  std::size_t last_non_space = end - 1;
  for (; last_non_space > start && str[last_non_space] == ' '; --last_non_space);
  if (allow_wildcards && start == last_non_space && str[start] == '*')
    {
      return NO_ERROR;
    }

  // Remaining invalid cases are: 1. Non-digits are present
  //                              2. Index overflows Rapidjson's index representation type

  // we need to check for non-digits since strtoul simply returns 0 in case conversion
  // can not be made
  for (auto it = str.cbegin () + start; it < str.cbegin () + last_non_space + 1; ++it)
    {
      if (!std::isdigit (static_cast<unsigned char> (*it)))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	  return ER_JSON_INVALID_PATH;
	}
    }

  char *end_str;
  index = std::strtoul (str.c_str () + start, &end_str, 10);
  if (errno == ERANGE)
    {
      errno = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_ARRAY_INDEX_TOO_LARGE, 0);
      return ER_JSON_ARRAY_INDEX_TOO_LARGE;
    }

  if (index > (unsigned long) prm_get_integer_value (PRM_ID_JSON_MAX_ARRAY_IDX))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_ARRAY_INDEX_TOO_LARGE, 0);
      return ER_JSON_ARRAY_INDEX_TOO_LARGE;
    }

  // this is a valid array index
  return NO_ERROR;
}

/*
 * skip_whitespaces  () - Advance offset to first non_space
 *
 * return              : offset of first non_space character
 * sql_path (in)       : path
 * pos (in)            : starting position offset
 */
static std::size_t
skip_whitespaces (const std::string &path, std::size_t pos)
{
  for (; pos < path.length () && path[pos] == ' '; ++pos);
  return pos;
}

static bool
db_json_isspace (const unsigned char &ch)
{
  return std::isspace (ch) != 0;
}

static void
db_json_trim_leading_spaces (std::string &path_string)
{
  // trim leading spaces
  auto first_non_space = std::find_if_not (path_string.begin (), path_string.end (), db_json_isspace);
  path_string.erase (path_string.begin (), first_non_space);
}

static JSON_PATH_TYPE
db_json_get_path_type (std::string &path_string)
{
  db_json_trim_leading_spaces (path_string);

  if (path_string.empty () || path_string[0] != '$')
    {
      return JSON_PATH_TYPE::JSON_PATH_POINTER;
    }
  else
    {
      return JSON_PATH_TYPE::JSON_PATH_SQL_JSON;
    }
}

/*
 * validate_and_create_from_json_path () - Check if a given path is a SQL valid path
 *
 * return                  : ER_JSON_INVALID_PATH if path is invalid
 * sql_path (in/out)       : path to be checked
 */
int
JSON_PATH::validate_and_create_from_json_path (std::string &sql_path)
{
  // skip leading white spaces
  db_json_trim_leading_spaces (sql_path);
  if (sql_path.empty ())
    {
      // empty
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  if (sql_path[0] != '$')
    {
      // first character should always be '$'
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }
  // start parsing path string by skipping dollar character
  std::size_t i = skip_whitespaces (sql_path, 1);

  while (i < sql_path.length ())
    {
      // to begin a next token we have only 3 possibilities:
      // with dot we start an object name
      // with bracket we start an index
      // with * we have the beginning of a '**' wildcard
      switch (sql_path[i])
	{
	case '[':
	{
	  std::size_t end_bracket_offset;
	  i = skip_whitespaces (sql_path, i + 1);

	  end_bracket_offset = sql_path.find_first_of (']', i);
	  if (end_bracket_offset == std::string::npos)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	    }
	  unsigned long index;
	  int error_code = db_json_path_is_token_valid_array_index (sql_path, true, index, i, end_bracket_offset);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }

	  // todo check if it is array_index or array_index_wildcard
	  if (sql_path[i] == '*')
	    {
	      push_array_index_wildcard ();
	    }
	  else
	    {
	      // note that db_json_path_is_token_valid_array_index () checks the index to not overflow
	      // a rapidjson::SizeType (unsinged int).
	      push_array_index (index);
	    }
	  i = skip_whitespaces (sql_path, end_bracket_offset + 1);
	  break;
	}
	case '.':
	  i = skip_whitespaces (sql_path, i + 1);
	  if (i == sql_path.length ())
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	    }
	  switch (sql_path[i])
	    {
	    case '"':
	    {
	      size_t old_idx = i;
	      if (!db_json_path_is_token_valid_quoted_object_key (sql_path, i))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
		  return ER_JSON_INVALID_PATH;
		}
	      push_object_key (sql_path.substr (old_idx, i - old_idx));
	      break;
	    }
	    case '*':
	      push_object_key_wildcard ();
	      i = skip_whitespaces (sql_path, i + 1);
	      break;
	    default:
	    {
	      size_t old_idx = i;
	      // unquoted object_keys
	      if (!db_json_path_quote_and_validate_unquoted_object_key (sql_path, i))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
		  return ER_JSON_INVALID_PATH;
		}
	      push_object_key (sql_path.substr (old_idx, i - old_idx));
	      break;
	    }
	    }
	  break;

	case '*':
	  // only ** wildcard is allowed in this case
	  if (++i >= sql_path.length () || sql_path[i] != '*')
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	    }
	  push_double_wildcard ();
	  i = skip_whitespaces (sql_path, i + 1);
	  if (i == sql_path.length ())
	    {
	      // ** wildcard requires suffix
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	    }
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	  return ER_JSON_INVALID_PATH;
	}
    }
  return NO_ERROR;
}

int
db_json_split_path_by_delimiters (const std::string &path, const std::string &delim, bool allow_empty,
				  std::vector<std::string> &split_path)
{
  std::size_t start = 0;
  std::size_t end = path.find_first_of (delim, start);

  while (end != std::string::npos)
    {
      if (path[end] == '"')
	{
	  std::size_t index_of_closing_quote = path.find_first_of ('"', end + 1);
	  if (index_of_closing_quote == std::string::npos)
	    {
	      assert (false);
	      split_path.clear ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	      return ER_JSON_INVALID_PATH;
	      /* this should have been catched earlier */
	    }
	  else
	    {
	      split_path.push_back (path.substr (end + 1, index_of_closing_quote - end - 1));
	      end = index_of_closing_quote;
	      start = end + 1;
	    }
	}
      // do not tokenize on escaped quotes
      else if (path[end] != '"' || ((end >= 1) && path[end - 1] != '\\'))
	{
	  const std::string &substring = path.substr (start, end - start);
	  if (!substring.empty () || allow_empty)
	    {
	      split_path.push_back (substring);
	    }
	  start = end + 1;
	}

      end = path.find_first_of (delim, end + 1);
    }

  const std::string &substring = path.substr (start, end);
  if (!substring.empty () || allow_empty)
    {
      split_path.push_back (substring);
    }

  std::size_t tokens_size = split_path.size ();
  for (std::size_t i = 0; i < tokens_size; i++)
    {
      unsigned long index;
      int error_code = db_json_path_is_token_valid_array_index (split_path[i], false, index);
      if (error_code != NO_ERROR)
	{
	  // ignore error. We only need to decide whether to skip it in case it is not array_idx
	  er_clear ();
	  continue;
	}

      db_json_remove_leading_zeros_index (split_path[i]);
    }

  return NO_ERROR;
}

JSON_PATH::MATCH_RESULT
JSON_PATH::match_pattern (const JSON_PATH &pattern, const JSON_PATH::token_containter_type::const_iterator &it1,
			  const JSON_PATH &path, const JSON_PATH::token_containter_type::const_iterator &it2)
{
  if (it1 == pattern.m_path_tokens.end () && it2 == path.m_path_tokens.end ())
    {
      return FULL_MATCH;
    }

  if (it1 == pattern.m_path_tokens.end ())
    {
      return PREFIX_MATCH;
    }

  if (it2 == path.m_path_tokens.end ())
    {
      // note that in case of double wildcard we have guaranteed a token after it
      return NO_MATCH;
    }

  if (it1->m_type == PATH_TOKEN::double_wildcard)
    {
      // for "**" wildcard we try to match the remaining pattern against each suffix of the path
      MATCH_RESULT advance_pattern = match_pattern (pattern, it1 + 1, path, it2);
      if (advance_pattern == FULL_MATCH)
	{
	  // return early if we have a full result
	  return advance_pattern;
	}

      MATCH_RESULT advance_path = match_pattern (pattern, it1, path, it2 + 1);
      if (advance_path == FULL_MATCH)
	{
	  return advance_path;
	}
      return (advance_pattern == PREFIX_MATCH || advance_path == PREFIX_MATCH) ? PREFIX_MATCH : NO_MATCH;
    }

  return !PATH_TOKEN::match_pattern (*it1, *it2) ? NO_MATCH : match_pattern (pattern, it1 + 1, path, it2 + 1);
}

JSON_PATH::MATCH_RESULT
JSON_PATH::match_pattern (const JSON_PATH &pattern, const JSON_PATH &path)
{
  assert (!path.contains_wildcard ());

  return match_pattern (pattern, pattern.m_path_tokens.begin (), path,  path.m_path_tokens.begin ());
}

/*
 * db_json_path_unquote_object_keys () - Unquote, when possible, object_keys of the json_path
 *
 * return                  : ER_JSON_INVALID_PATH if a validation error occured
 * sql_path (in/out)       : path
 */
int
db_json_path_unquote_object_keys (std::string &sql_path)
{
  // todo: rewrite as json_path.dump () + unquoting the object_keys
  std::vector<std::string> tokens;
  int error_code = db_json_split_path_by_delimiters (sql_path, ".[", false, tokens);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  std::string res = "$";

  assert (!tokens.empty () && tokens[0] == "$");
  for (std::size_t i = 1; i < tokens.size(); ++i)
    {
      if (tokens[i][0] == '"')
	{
	  res += ".";
	  std::string unquoted = tokens[i].substr (1, tokens[i].length () - 2);
	  std::size_t start = 0;

	  if (db_json_path_is_token_valid_unquoted_object_key (unquoted, start) && start >= unquoted.length ())
	    {
	      res.append (unquoted);
	    }
	  else
	    {
	      res += tokens[i];
	    }
	}
      else
	{
	  res += "[";
	  res += tokens[i];
	}
    }

  sql_path = std::move (res);
  return NO_ERROR;
}

/*
 * db_json_remove_leading_zeros_index () - Erase leading zeros from sql path index
 *
 * index (in)                : current object
 * example: $[000123] -> $[123]
 */
static void
db_json_remove_leading_zeros_index (std::string &index)
{
  // trim leading zeros
  auto first_non_zero = std::find_if_not (index.begin (), index.end (), db_json_iszero);
  index.erase (index.begin (), first_non_zero);

  if (index.empty ())
    {
      index = "0";
    }
}

PATH_TOKEN::PATH_TOKEN ()
  : m_type (array_index)
{

}

PATH_TOKEN::PATH_TOKEN (token_type type, unsigned long array_idx)
  : m_type (type)
  , m_array_idx (array_idx)
{

}

PATH_TOKEN::PATH_TOKEN (token_type type, std::string &&s)
  : m_type (type)
  , m_object_key (std::move (s))
{

}

const std::string &
PATH_TOKEN::get_object_key () const
{
  assert (m_type == object_key);

  return m_object_key;
}

unsigned long
PATH_TOKEN::get_array_index () const
{
  assert (m_type == array_index);

  return m_array_idx;
}

bool
PATH_TOKEN::is_wildcard () const
{
  return m_type == object_key_wildcard || m_type == array_index_wildcard || m_type == double_wildcard;
}

bool
PATH_TOKEN::match_pattern (const PATH_TOKEN &matcher, const PATH_TOKEN &matchee)
{
  assert (!matchee.is_wildcard ());

  switch (matcher.m_type)
    {
    case double_wildcard:
      return matchee.m_type == object_key || matchee.m_type == array_index;
    case object_key_wildcard:
      return matchee.m_type == object_key;
    case array_index_wildcard:
      return matchee.m_type == array_index;
    case object_key:
      return matchee.m_type == object_key && matcher.get_object_key () == matchee.get_object_key ();
    case array_index:
      return matchee.m_type == array_index && matcher.get_array_index () == matchee.get_array_index ();
    default:
      return false;
    }
}

void
JSON_PATH::push_array_index (unsigned long idx)
{
  m_path_tokens.emplace_back (PATH_TOKEN::token_type::array_index, idx);
}

void
JSON_PATH::push_array_index_wildcard ()
{
  m_path_tokens.emplace_back (PATH_TOKEN::token_type::array_index_wildcard, std::string ("*"));
}

void
JSON_PATH::push_object_key (std::string &&object_key)
{
  m_path_tokens.emplace_back (PATH_TOKEN::token_type::object_key, std::move (object_key));
}

void
JSON_PATH::push_object_key_wildcard ()
{
  m_path_tokens.emplace_back (PATH_TOKEN::token_type::object_key_wildcard, std::string ("*"));
}

void
JSON_PATH::push_double_wildcard ()
{
  m_path_tokens.emplace_back (PATH_TOKEN::token_type::double_wildcard, std::string ("**"));
}

void
JSON_PATH::pop ()
{
  m_path_tokens.pop_back ();
}

bool
JSON_PATH::contains_wildcard () const
{
  for (const PATH_TOKEN &tkn : m_path_tokens)
    {
      if (tkn.is_wildcard ())
	{
	  return true;
	}
    }
  return false;
}

std::string
JSON_PATH::dump_json_path () const
{
  std::string res = "$";

  for (const auto &tkn : m_path_tokens)
    {
      switch (tkn.m_type)
	{
	case PATH_TOKEN::array_index:
	  res += '[';
	  res += std::to_string (tkn.get_array_index ());
	  res += ']';
	  break;
	case PATH_TOKEN::array_index_wildcard:
	  res += "[*]";
	  break;
	case PATH_TOKEN::object_key:
	  res += '.';
	  res += tkn.get_object_key ();
	  break;
	case PATH_TOKEN::object_key_wildcard:
	  res += ".*";
	  break;
	case PATH_TOKEN::double_wildcard:
	  res += "**";
	  break;
	case PATH_TOKEN::array_end_index:
	  // this case is valid and possible in case of ER_JSON_PATH_DOES_NOT_EXIST
	  // we don't have the JSON in this context and cannot replace '-' with last index
	  // for json_pointer -> json_path conversion so we leave empty suffix
	  break;
	default:
	  assert (false);
	  break;
	}
    }

  return res;
}

void
JSON_PATH::set (JSON_DOC &jd, const JSON_VALUE &jv) const
{
  set (db_json_doc_to_value (jd), jv, jd.GetAllocator ());
}

/*
 * set () - Create or replace a value at path in the document
 *
 * jd (in) - document we insert in
 * jv (in) - value to be inserted
 * allocator
 * return : found value at path
 *
 * Our implementation does not follow the JSON Pointer https://tools.ietf.org/html/rfc6901#section-4 standard fully
 * We normalize json_pointers to json_paths and resolve token types independently of the document that gets operated
 * by the normalized path.
 * Therefore, we cannot traverse the doc contextually as described in the rfc e.g. both '{"0":10}' an '[10]' to provide
 * same results for '/1' json_pointer.
 */
void
JSON_PATH::set (JSON_VALUE &jd, const JSON_VALUE &jv, JSON_PRIVATE_MEMPOOL &allocator) const
{
  JSON_VALUE *val = &jd;
  for (const PATH_TOKEN &tkn : m_path_tokens)
    {
      switch (tkn.m_type)
	{
	case PATH_TOKEN::token_type::array_index:
	case PATH_TOKEN::token_type::array_end_index:
	  if (!val->IsArray ())
	    {
	      val->SetArray ();
	    }
	  break;
	case PATH_TOKEN::token_type::object_key:
	  if (!val->IsObject ())
	    {
	      val->SetObject ();
	    }
	  break;
	case PATH_TOKEN::token_type::array_index_wildcard:
	case PATH_TOKEN::token_type::object_key_wildcard:
	case PATH_TOKEN::token_type::double_wildcard:
	  // error? unexpected set - wildcards not allowed for set
	  assert (false);
	  return;
	}

      if (val->IsArray ())
	{
	  JSON_VALUE::Array arr = val->GetArray ();
	  if (tkn.m_type == PATH_TOKEN::token_type::array_end_index)
	    {
	      // insert dummy
	      arr.PushBack (JSON_VALUE ().SetNull (), allocator);
	      val = &val->GetArray ()[val->GetArray ().Size () - 1];
	    }
	  else
	    {
	      rapidjson::SizeType idx = (rapidjson::SizeType) tkn.get_array_index ();
	      while (idx >= arr.Size ())
		{
		  arr.PushBack (JSON_VALUE ().SetNull (), allocator);
		}
	      val = &val->GetArray ()[idx];
	    }
	}
      else if (val->IsObject ())
	{
	  std::string encoded_key = db_json_json_string_as_utf8 (tkn.get_object_key ());
	  JSON_VALUE::MemberIterator m = val->FindMember (encoded_key.c_str ());
	  if (m == val->MemberEnd ())
	    {
	      // insert dummy
	      unsigned int len = (rapidjson::SizeType) encoded_key.length ();
	      val->AddMember (JSON_VALUE (encoded_key.c_str (), len, allocator), JSON_VALUE ().SetNull (), allocator);

	      val = & (--val->MemberEnd ())->value; // Assume AddMember() appends at the end
	    }
	  else
	    {
	      val = &m->value;
	    }
	}
    }

  val->CopyFrom (jv, allocator);
}

JSON_VALUE *
JSON_PATH::get (JSON_DOC &jd) const
{
  return const_cast<JSON_VALUE *> (get (const_cast<const JSON_DOC &> (jd)));
}

/*
 * get () - Walk a doc following a path and retrive the value pointed at
 *
 * jd (in)
 * return : found value at path
 */
const JSON_VALUE *
JSON_PATH::get (const JSON_DOC &jd) const
{
  const JSON_VALUE *val = &db_json_doc_to_value (jd);
  for (const PATH_TOKEN &tkn : m_path_tokens)
    {
      if (val->IsArray ())
	{
	  if (tkn.m_type != PATH_TOKEN::token_type::array_index)
	    {
	      return NULL;
	    }

	  unsigned idx = tkn.get_array_index ();
	  if (idx >= val->GetArray ().Size ())
	    {
	      return NULL;
	    }

	  val = &val->GetArray ()[idx];
	}
      else if (val->IsObject ())
	{
	  if (tkn.m_type != PATH_TOKEN::token_type::object_key)
	    {
	      return NULL;
	    }
	  std::string encoded_key = db_json_json_string_as_utf8 (tkn.get_object_key ());
	  JSON_VALUE::ConstMemberIterator m = val->FindMember (encoded_key.c_str ());
	  if (m == val->MemberEnd ())
	    {
	      return NULL;
	    }
	  val = &m->value;
	}
      else
	{
	  return NULL;
	}
    }
  return val;
}

void
JSON_PATH::extract_from_subtree (const JSON_PATH &path, size_t tkn_array_offset, const JSON_VALUE &jv,
				 std::unordered_set<const JSON_VALUE *> &vals_hash_set,
				 std::vector<const JSON_VALUE *> &vals)
{
  if (tkn_array_offset == path.get_token_count ())
    {
      // No suffix remaining -> collect match
      // Note: some nodes of the tree are encountered multiple times (only during double wildcards)
      // therefore the use of unordered_set
      if (vals_hash_set.find (&jv) == vals_hash_set.end ())
	{
	  vals_hash_set.insert (&jv);
	  vals.push_back (&jv);
	}
      return;
    }

  const PATH_TOKEN &crt_tkn = path.m_path_tokens[tkn_array_offset];
  if (jv.IsArray ())
    {
      switch (crt_tkn.m_type)
	{
	case PATH_TOKEN::token_type::array_index:
	{
	  unsigned idx = crt_tkn.get_array_index ();
	  if (idx >= jv.GetArray ().Size ())
	    {
	      return;
	    }
	  extract_from_subtree (path, tkn_array_offset + 1, jv.GetArray ()[idx], vals_hash_set, vals);
	  return;
	}
	case PATH_TOKEN::token_type::array_index_wildcard:
	  for (rapidjson::SizeType i = 0; i < jv.GetArray ().Size (); ++i)
	    {
	      extract_from_subtree (path, tkn_array_offset + 1, jv.GetArray ()[i], vals_hash_set, vals);
	    }
	  return;
	case PATH_TOKEN::token_type::double_wildcard:
	  // Advance token_array_offset
	  extract_from_subtree (path, tkn_array_offset + 1, jv, vals_hash_set, vals);
	  for (rapidjson::SizeType i = 0; i < jv.GetArray ().Size (); ++i)
	    {
	      // Advance in tree, keep current token_array_offset
	      extract_from_subtree (path, tkn_array_offset, jv.GetArray ()[i], vals_hash_set, vals);
	    }
	  return;
	default:
	  return;
	}
    }
  else if (jv.IsObject ())
    {
      switch (crt_tkn.m_type)
	{
	case PATH_TOKEN::token_type::object_key:
	{
	  std::string encoded_key = db_json_json_string_as_utf8 (crt_tkn.get_object_key ());
	  JSON_VALUE::ConstMemberIterator m = jv.FindMember (encoded_key.c_str ());
	  if (m == jv.MemberEnd ())
	    {
	      return;
	    }
	  extract_from_subtree (path, tkn_array_offset + 1, m->value, vals_hash_set, vals);
	  return;
	}
	case PATH_TOKEN::token_type::object_key_wildcard:
	  for (JSON_VALUE::ConstMemberIterator m = jv.MemberBegin (); m != jv.MemberEnd (); ++m)
	    {
	      extract_from_subtree (path, tkn_array_offset + 1, m->value, vals_hash_set, vals);
	    }
	  return;
	case PATH_TOKEN::token_type::double_wildcard:
	  // Advance token_array_offset
	  extract_from_subtree (path, tkn_array_offset + 1, jv, vals_hash_set, vals);
	  for (JSON_VALUE::ConstMemberIterator m = jv.MemberBegin (); m != jv.MemberEnd (); ++m)
	    {
	      // Advance in tree, keep current token_array_offset
	      extract_from_subtree (path, tkn_array_offset, m->value, vals_hash_set, vals);
	    }
	  return;
	default:
	  return;
	}
    }
  // Json scalars are ignored if there is a remaining suffix
}

std::vector<const JSON_VALUE *>
JSON_PATH::extract (const JSON_DOC &jd) const
{
  std::unordered_set<const JSON_VALUE *> vals_hash_set;
  std::vector<const JSON_VALUE *> res;

  extract_from_subtree (*this, 0, db_json_doc_to_value (jd), vals_hash_set, res);

  return res;
}

bool
JSON_PATH::erase (JSON_DOC &jd) const
{
  if (get_token_count () == 0)
    {
      return false;
    }

  JSON_VALUE *value = get_parent ().get (jd);
  if (value == nullptr)
    {
      return false;
    }

  const PATH_TOKEN &tkn = m_path_tokens.back ();

  if (value->IsArray ())
    {
      if (!is_last_array_index_less_than (value->GetArray ().Size ()))
	{
	  return false;
	}
      value->Erase (value->Begin () + tkn.get_array_index ());
      return true;
    }
  else if (value->IsObject ())
    {
      if (tkn.m_type != PATH_TOKEN::object_key)
	{
	  return false;
	}
      std::string encoded_key = db_json_json_string_as_utf8 (tkn.get_object_key ());
      return value->EraseMember (encoded_key.c_str ());
    }

  return false;
}

const PATH_TOKEN *
JSON_PATH::get_last_token () const
{
  return get_token_count () > 0 ? &m_path_tokens[get_token_count () - 1] : NULL;
}

size_t
JSON_PATH::get_token_count () const
{
  return m_path_tokens.size ();
}

bool
JSON_PATH::is_root_path () const
{
  return get_token_count () == 0;
}

JSON_PATH
JSON_PATH::get_parent () const
{
  if (get_token_count () == 0)
    {
      // this should not happen
      assert (false);
      JSON_PATH parent;
      return parent;
    }
  else
    {
      // todo: improve getting a slice of the m_path_tokens vector
      JSON_PATH parent (*this);
      parent.pop ();
      return parent;
    }
}

bool
JSON_PATH::is_last_array_index_less_than (size_t size) const
{
  const PATH_TOKEN *last_token = get_last_token ();
  assert (last_token != NULL);

  return last_token->m_type == PATH_TOKEN::array_index && last_token->get_array_index () < size;
}

bool
JSON_PATH::is_last_token_array_index_zero () const
{
  return is_last_array_index_less_than (1);
}

bool
JSON_PATH::points_to_array_cell () const
{
  const PATH_TOKEN *last_token = get_last_token ();
  return (last_token != NULL && (last_token->m_type == PATH_TOKEN::array_index
				 || (last_token->m_type == PATH_TOKEN::array_end_index)));
}

bool
JSON_PATH::parent_exists (JSON_DOC &jd) const
{
  if (get_token_count () == 0)
    {
      return false;
    }

  if (get_parent ().get (jd) != NULL)
    {
      return true;
    }

  return false;
}

/*
 * init ()
 *
 * path (in)
 * An sql_path is normalized to rapidjson standard path
 * Example: $[0]."name1".name2[2] -> /0/name1/name2/2
 */
int
JSON_PATH::parse (const char *path)
{
  std::string sql_path_string (path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (sql_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_POINTER)
    {
      // path is not SQL path format; consider it JSON pointer.
      int error_code = from_json_pointer (sql_path_string);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	}
      return error_code;
    }

  int error_code = validate_and_create_from_json_path (sql_path_string);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  return error_code;
}

int
JSON_PATH::from_json_pointer (const std::string &pointer_path)
{
  typedef rapidjson::GenericPointer<JSON_VALUE>::Token TOKEN;
  static const rapidjson::SizeType kPointerInvalidIndex = rapidjson::kPointerInvalidIndex;

  typedef rapidjson::GenericPointer<JSON_VALUE> JSON_POINTER;

  JSON_POINTER jp (pointer_path.c_str ());
  if (!jp.IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  size_t tkn_cnt = jp.GetTokenCount ();
  const TOKEN *tokens = jp.GetTokens ();

  // convert rapidjson's tokens to our tokens:
  for (size_t i = 0; i < tkn_cnt; ++i)
    {
      const TOKEN &rapid_token = tokens[i];

      if (rapid_token.index != kPointerInvalidIndex)
	{
	  // array_index
	  push_array_index (rapid_token.index);
	}
      else if (rapid_token.length == 1 && rapid_token.name[0] == '-' )
	{
	  // '-' special idx token
	  m_path_tokens.emplace_back (PATH_TOKEN::token_type::array_end_index, "-");
	}
      else
	{
	  // object_key
	  char *escaped;
	  size_t escaped_size;
	  db_string_escape_str (rapid_token.name, rapid_token.length, &escaped, &escaped_size);

	  push_object_key (escaped);
	  db_private_free (NULL, escaped);
	}
    }

  return NO_ERROR;
}
