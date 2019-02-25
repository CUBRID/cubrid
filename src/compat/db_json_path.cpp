#include "db_json_path.hpp"
#include "string_opfunc.h"
#include <algorithm>
#include <string>
#include <cctype>

static void db_json_trim_leading_spaces (std::string &path_string);
static JSON_PATH_TYPE db_json_get_path_type (std::string &path_string);
static bool db_json_isspace (const unsigned char &ch);
static std::size_t skip_whitespaces (const std::string &path, std::size_t token_begin);
static bool db_json_path_is_token_valid_array_index (const std::string &str, bool allow_wildcards,
    std::size_t start = 0, std::size_t end = 0);
static bool db_json_path_is_token_valid_quoted_object_key (const std::string &path, std::size_t &token_begin);
static bool db_json_path_quote_and_validate_unquoted_object_key (std::string &path, std::size_t &token_begin);
static bool db_json_path_is_token_valid_unquoted_object_key (const std::string &path, std::size_t &token_begin);
static void json_path_strip_whitespaces (std::string &sql_path);
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
 * return                  : true/false
 * path (in)               : path to be checked
 * token_begin (in/out)    : beginning offset of the token, is replaced with beginning of the next token or path.length ()
 */
static bool
db_json_path_is_token_valid_quoted_object_key (const std::string &path, std::size_t &token_begin)
{
  std::size_t i = token_begin + 1;
  bool unescaped_backslash = false;
  std::size_t backslash_nr = 0;
  // stop at unescaped '"'; note that there should be an odd nr of backslashes before '"' for it to be escaped
  for (; i < path.length() && (path[i] != '"' || unescaped_backslash); ++i)
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

  if (i == path.length())
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

  // todo: this needs change. Besides alphanumerics, object keys can be valid ECMAScript identifiers as defined in
  // http://www.ecma-international.org/ecma-262/5.1/#sec-7.6
  if (i < path.length() && !std::isalpha (static_cast<unsigned char> (path[i])))
    {
      return false;
    }

  ++i;
  for (; i < path.length() && std::isalnum (static_cast<unsigned char> (path[i])); ++i);

  token_begin = i;

  return true;
}

/*
 * db_json_path_is_token_valid_array_index () - verify if token is a valid array index. token can be a substring of
 *                                              first argument (by default the entire argument).
 *
 * return          : true if all token characters are digits followed by spaces (valid index)
 * str (in)        : token or the string that token belong to
 * allow_wildcards : whether json_path wildcards are allowed
 * start (in)      : start of token; default is start of string
 * end (in)        : end of token; default is end of string; 0 is considered default value
 */
static bool
db_json_path_is_token_valid_array_index (const std::string &str, bool allow_wildcards, std::size_t start,
    std::size_t end)
{
  // json pointer will corespond the symbol '-' to JSON_ARRAY length
  // so if we have the json {"A":[1,2,3]} and the path /A/-
  // this will point to the 4th element of the array (zero indexed)
  if (str == "-")
    {
      return true;
    }

  if (end == 0)
    {
      // default is end of string
      end = str.length();
    }

  if (start == end)
    {
      return false;
    }

  std::size_t last_non_space = end - 1;
  for (; last_non_space > start && str[last_non_space] == ' '; --last_non_space);
  if (allow_wildcards && start == last_non_space && str[start] == '*')
    {
      return true;
    }

  // Remaining invalid cases are: 1. Non-digits are present
  //                              2. Index overflows Rapidjson's index representation type
  rapidjson::SizeType n = 0;
  for (auto it = str.cbegin() + start; it < str.cbegin() + last_non_space + 1; ++it)
    {
      if (!std::isdigit (static_cast<unsigned char> (*it)))
	{
	  return false;
	}
      rapidjson::SizeType m = n * 10 + static_cast<unsigned> (*it - '0');
      if (m < n)
	{
	  return false;
	}
      n = m;
    }

  // this is a valid array index
  return true;
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
  for (; pos < path.length() && path[pos] == ' '; ++pos);
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
 * validate_and_make_json_path () - Check if a given path is a SQL valid path
 *
 * return                  : true/false
 * sql_path (in/out)       : path to be checked
 * allow_wild_cards (in)   : whether json_path wildcards are allowed
 */
bool
JSON_PATH::validate_and_make_json_path (std::string &sql_path, bool allow_wildcards)
{
  // skip leading white spaces
  db_json_trim_leading_spaces (sql_path);
  if (sql_path.empty ())
    {
      // empty
      return false;
    }

  m_backend_json_format = JSON_PATH::JSON_PATH_TYPE::JSON_PATH_SQL_JSON;

  if (sql_path[0] != '$')
    {
      // first character should always be '$'
      return false;
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
	      return false;
	    }
	  if (!db_json_path_is_token_valid_array_index (sql_path, allow_wildcards, i, end_bracket_offset))
	    {
	      return false;
	    }

	  // todo check if it is array_index or array_index_wildcard
	  if (sql_path[i] == '*')
	    {
	      emplace_back ({ JSON_PATH::PATH_TOKEN::token_type::array_index_wild_card, "*" });
	    }
	  else
	    {
	      emplace_back ({ JSON_PATH::PATH_TOKEN::token_type::array_index, sql_path.substr (i, end_bracket_offset - i) });
	    }

	  i = skip_whitespaces (sql_path, end_bracket_offset + 1);
	  break;
	}
	case '.':
	  i = skip_whitespaces (sql_path, i + 1);
	  if (i == sql_path.length())
	    {
	      return false;
	    }
	  switch (sql_path[i])
	    {
	    case '"':
	    {
	      size_t old_idx = i;
	      if (!db_json_path_is_token_valid_quoted_object_key (sql_path, i))
		{
		  return false;
		}
	      emplace_back ({ JSON_PATH::PATH_TOKEN::token_type::object_key, sql_path.substr (old_idx, i - old_idx) });
	      break;
	    }
	    case '*':
	      if (!allow_wildcards)
		{
		  return false;
		}
	      emplace_back ({ JSON_PATH::PATH_TOKEN::token_type::object_key_wild_card, "*" });
	      i = skip_whitespaces (sql_path, i + 1);
	      break;
	    default:
	    {
	      size_t old_idx = i;
	      // unquoted object_keys
	      if (!db_json_path_quote_and_validate_unquoted_object_key (sql_path, i))
		{
		  return false;
		}
	      emplace_back ({ JSON_PATH::PATH_TOKEN::token_type::object_key, sql_path.substr (old_idx, i - old_idx) });
	      break;
	    }
	    }
	  break;

	case '*':
	  // only ** wildcard is allowed in this case
	  if (!allow_wildcards || ++i >= sql_path.length () || sql_path[i] != '*')
	    {
	      return false;
	    }
	  emplace_back ({ JSON_PATH::PATH_TOKEN::token_type::double_wild_card, "**" });
	  i = skip_whitespaces (sql_path, i + 1);
	  if (i == sql_path.length ())
	    {
	      // ** wildcard requires suffix
	      return false;
	    }
	  break;

	default:
	  return false;
	}
    }
  // todo: remove this?
  // sql_path not used after call?
  json_path_strip_whitespaces (sql_path);
  return true;
}


/*
 * json_path_strip_whitespaces () - Remove whitespaces in json_path
 *
 * sql_path (in/out)       : json path
 * NOTE: This can be only called after validation because spaces are not allowed in some cases (e.g. $[1 1] is illegal)
 */
static void
json_path_strip_whitespaces (std::string &sql_path)
{
  std::string result;
  result.reserve (sql_path.length() + 1);

  bool skip_spaces = true;
  bool unescaped_backslash = false;
  for (size_t i = 0; i < sql_path.length(); ++i)
    {
      if (i > 0 && !unescaped_backslash && sql_path[i] == '"')
	{
	  skip_spaces = !skip_spaces;
	}

      if (sql_path[i] == '\\')
	{
	  unescaped_backslash = !unescaped_backslash;
	}
      else
	{
	  unescaped_backslash = false;
	}

      if (skip_spaces && sql_path[i] == ' ')
	{
	  continue;
	}

      result.push_back (sql_path[i]);
    }

  sql_path = std::move (result);
}


std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim, bool allow_empty)
{
  std::vector<std::string> tokens;
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
	      tokens.clear();
	      return tokens;
	      /* this should have been catched earlier */
	    }
	  else
	    {
	      tokens.push_back (path.substr (end + 1, index_of_closing_quote - end - 1));
	      end = index_of_closing_quote;
	      start = end + 1;
	    }
	}
      // do not tokenize on escaped quotes
      else if (path[end] != '"' || ((end >= 1) && path[end - 1] != '\\'))
	{
	  const std::string &substring = path.substr (start, end - start);
	  if (!substring.empty() || allow_empty)
	    {
	      tokens.push_back (substring);
	    }
	  start = end + 1;
	}

      end = path.find_first_of (delim, end + 1);
    }

  const std::string &substring = path.substr (start, end);
  if (!substring.empty() || allow_empty)
    {
      tokens.push_back (substring);
    }

  std::size_t tokens_size = tokens.size();
  for (std::size_t i = 0; i < tokens_size; i++)
    {
      if (db_json_path_is_token_valid_array_index (tokens[i], false))
	{
	  db_json_remove_leading_zeros_index (tokens[i]);
	}
    }

  return tokens;
}

/*
 * db_json_normalize_path ()
 * pointer_path (in)
 * sql_path_out (out): the result
 * allow_wildcards (in):
 * A pointer path is converted to SQL standard path
 * Example: /0/name1/name2/2 -> $[0]."name1"."name2"[2]
 */
int
db_json_normalize_path (const char *pointer_path, JSON_PATH &json_path, bool allow_wildcards)
{
  std::string pointer_path_string (pointer_path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (pointer_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
    {
      // path is not JSON path format; consider it SQL path.
      // sql_path_out = pointer_path_string;
      if (!json_path.validate_and_make_json_path (pointer_path_string, allow_wildcards))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	  return ER_JSON_INVALID_PATH;
	}
      return NO_ERROR;
    }

  int error_code = json_path.init (pointer_path);
  if (error_code)
    {
      ASSERT_ERROR();
      return error_code;
    }

  db_json_normalize_path (json_path.dump_json_path ().c_str (), json_path, allow_wildcards);
  return NO_ERROR;
}

/*
 * db_json_path_unquote_object_keys () - Unquote, when possible, object_keys of the json_path
 *
 * sql_path (in/out)       : path
 */
void
db_json_path_unquote_object_keys (std::string &sql_path)
{
  // note: sql_path should not have wildcards, it comes as output of json_search function
  auto tokens = db_json_split_path_by_delimiters (sql_path, ".[", false);
  std::size_t crt_idx = 0;
  std::string res = "$";

  assert (!tokens.empty() && tokens[0] == "$");
  for (std::size_t i = 1; i < tokens.size(); ++i)
    {
      if (tokens[i][0] == '"')
	{
	  res += ".";
	  std::string unquoted = tokens[i].substr (1, tokens[i].length() - 2);
	  std::size_t start = 0;

	  if (db_json_path_is_token_valid_unquoted_object_key (unquoted, start) && start >= unquoted.length())
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


/*
 * replace_special_chars_in_tokens ()
 * token (in)
 * special_chars (in)
 * this function does the special characters replacements in a token based on mapper
 * Example: object~1name -> object/name
 */
void
JSON_PATH::replace_special_chars_in_tokens (std::string &token,
    const std::unordered_map<std::string, std::string> &special_chars) const
{
  bool replaced = false;
  size_t start = 0;
  size_t end = 0;
  size_t step = 1;

  // iterate character by character and detect special characters
  for (size_t token_idx = 0; token_idx < token.length(); /* incremented in for body */)
    {
      replaced = false;
      // compare with special characters
      for (auto special_it = special_chars.begin(); special_it != special_chars.end(); ++special_it)
	{
	  // compare special characters with sequence following token_it
	  if (token_idx + special_it->first.length() <= token.length())
	    {
	      if (token.compare (token_idx, special_it->first.length(), special_it->first) == 0)
		{
		  // replace
		  token.replace (token_idx, special_it->first.length(), special_it->second);
		  // skip replaced
		  token_idx += special_it->second.length();

		  replaced = true;
		  // next loop
		  break;
		}
	    }
	}

      if (!replaced)
	{
	  // no match; next character
	  token_idx++;
	}

      start += step;
    }
}

bool JSON_PATH::PATH_TOKEN::is_wildcard () const
{
  return type == object_key_wild_card || type == array_index_wild_card || type == double_wild_card;
}

bool JSON_PATH::PATH_TOKEN::match (const PATH_TOKEN &other) const
{
  switch (type)
    {
    case double_wild_card:
      return other.type == object_key || other.type == array_index;
    case object_key_wild_card:
      return other.type == object_key;
    case array_index_wild_card:
      return other.type == array_index;
    case object_key:
      return other.type == object_key && token_string == other.token_string;
    case array_index:
      return other.type == array_index && token_string == other.token_string;
    default:
      return false;
    }
}

// todo: find a way to avoid passing reference to get other_tokens.end ()
bool JSON_PATH::match (const std::vector<PATH_TOKEN>::const_iterator &it1,
		       const std::vector<PATH_TOKEN>::const_iterator &it2, const std::vector<PATH_TOKEN> &other_tokens,
		       bool match_prefix) const
{
  if (it1 == m_path_tokens.end() && it2 == other_tokens.end())
    {
      return true;
    }

  if (it1 == m_path_tokens.end())
    {
      return match_prefix;
    }

  if (it2 == other_tokens.end())
    {
      // note that in case of double wildcard we have guaranteed a token after it
      return false;
    }

  if (it1->type == PATH_TOKEN::double_wild_card)
    {
      return match (it1 + 1, it2, other_tokens, match_prefix) || match (it1, it2 + 1, other_tokens, match_prefix);
    }

  return it1->match (*it2) && match (it1 + 1, it2 + 1, other_tokens, match_prefix);
}

bool JSON_PATH::match (const JSON_PATH &other, bool match_prefix) const
{
  return match (m_path_tokens.begin (), other.m_path_tokens.begin (), other.m_path_tokens, match_prefix);
}

void JSON_PATH::emplace_back (PATH_TOKEN &&path_token)
{
  m_path_tokens.emplace_back (std::move (path_token));
}

void JSON_PATH::pop ()
{
  m_path_tokens.pop_back ();
}

bool JSON_PATH::contains_wildcard () const
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

/*
 * build_special_chars_map ()
 * json_path_type (in)
 * special_chars (out)
 * rapid json pointer supports URI Fragment Representation
 * https://tools.ietf.org/html/rfc3986
 * we need a map in order to know how to escape special characters
 * example from sql_path to pointer_path: $."/a" -> #/~1a
 */
void
JSON_PATH::build_special_chars_map (const JSON_PATH_TYPE &json_path_type,
				    std::unordered_map<std::string, std::string> &special_chars) const
{
  for (auto it = uri_fragment_conversions.begin (); it != uri_fragment_conversions.end (); ++it)
    {
      if (json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
	{
	  special_chars.insert (*it);
	}
      else
	{
	  special_chars.insert (std::make_pair (it->second, it->first));
	}
    }
}

std::string
JSON_PATH::dump_json_pointer () const
{
  std::string res;
  for (const PATH_TOKEN &tkn : m_path_tokens)
    {
      res += '/';
      res += tkn.token_string;
    }

  return res;
}

std::string
JSON_PATH::dump_json_path (bool skip_json_pointer_minus) const
{
  if (m_backend_json_format == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
    {
      std::string res = "$";

      for (const auto &tkn : m_path_tokens)
	{
	  switch (tkn.type)
	    {
	    case PATH_TOKEN::array_index:
	      res += '[';
	      res += tkn.token_string;
	      res += ']';
	      break;
	    case PATH_TOKEN::object_key:
	      res += '.';
	      res += tkn.token_string;
	      break;
	    default:
	      // assert (false);
	      break;
	    }
	}

      return res;
    }

  std::unordered_map<std::string, std::string> special_chars;
  build_special_chars_map (JSON_PATH_TYPE::JSON_PATH_POINTER, special_chars);

  const TOKEN *tokens = GetTokens();
  const size_t token_cnt = GetTokenCount();
  std::string res = "$";
  for (size_t i = 0; i < GetTokenCount(); ++i)
    {
      if (tokens[i].index != kPointerInvalidIndex || (tokens[i].length == 1 && tokens[i].name[0] == '-'))
	{
	  if (tokens[i].index == kPointerInvalidIndex && skip_json_pointer_minus)
	    {
	      continue;
	    }

	  res += "[";
	  res += tokens[i].name;
	  res += "]";
	}
      else
	{
	  std::string token_str (tokens[i].name);
	  replace_special_chars_in_tokens (token_str, special_chars);
	  char *quoted_token = NULL;
	  size_t quoted_size;
	  (void) db_string_escape (token_str.c_str (), token_str.length(), &quoted_token, &quoted_size);

	  res += ".";
	  res += quoted_token;

	  db_private_free (NULL, quoted_token);
	}
    }
  return res;
}

std::vector<JSON_VALUE *> JSON_PATH::get (JSON_DOC &jd) const
{
  return { Get (jd) };
}

std::vector<const JSON_VALUE *> JSON_PATH::get (const JSON_DOC &jd) const
{
  return { Get (jd) };
}

DB_JSON_TYPE JSON_PATH::get_value_type (const JSON_DOC &jd) const
{
  return db_json_get_type_of_value (get (jd)[0]);
}

JSON_VALUE &JSON_PATH::set (JSON_DOC &jd, const JSON_VALUE &jv) const
{
  return Set (jd, jv, jd.GetAllocator ());
}

JSON_VALUE &JSON_PATH::set (JSON_DOC &jd, JSON_VALUE &jv) const
{
  return Set (jd, jv, jd.GetAllocator ());
}

bool JSON_PATH::erase (JSON_DOC &jd) const
{
  return Erase (jd);
}

const TOKEN *JSON_PATH::get_last_token () const
{
  size_t token_cnt = GetTokenCount ();

  return token_cnt > 0 ? GetTokens () + (token_cnt - 1) : NULL;
}

bool JSON_PATH::is_root_path () const
{
  return GetTokenCount () == 0;
}

const JSON_PATH JSON_PATH::get_parent () const
{
  if (GetTokenCount () == 0)
    {
      // this should not happen
      assert (false);
      JSON_PATH parent;
      return parent;
    }
  else
    {
      JSON_PATH parent (GetTokens (), GetTokenCount () - 1);
      return parent;
    }
}

bool JSON_PATH::is_last_array_index_less_than (size_t size) const
{
  const TOKEN *last_token = get_last_token ();
  assert (last_token != NULL && ((last_token->length == 1 && last_token->name[0] == '-')
				 || (last_token->index != kPointerInvalidIndex)));
  return ! ((last_token->length == 1 && last_token->name[0] == '-') || last_token->index >= size);
}

bool JSON_PATH::is_last_token_array_index_zero () const
{
  const TOKEN *last_token = get_last_token ();
  assert (last_token != NULL);
  return (last_token->index != kPointerInvalidIndex && last_token->index == 0);
}

bool JSON_PATH::points_to_array_cell () const
{
  if (m_backend_json_format == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
    {
      return !m_path_tokens.empty () && m_path_tokens.back ().type == PATH_TOKEN::token_type::array_index;
    }

  const TOKEN *last_token = get_last_token ();
  if (last_token == NULL || (last_token->index == kPointerInvalidIndex && ! (last_token->length == 1
			     && last_token->name[0] == '-')))
    {
      return false;
    }
  return true;
}

bool JSON_PATH::parent_exists (JSON_DOC &jd) const
{
  if (GetTokenCount () == 0)
    {
      return false;
    }

  if (get_parent ().get (jd)[0] != NULL)
    {
      return true;
    }

  return false;
}


int JSON_PATH::init (const char *path)
{
  int error_code = replace_json_pointer (path);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR();
      return error_code;
    }

  return NO_ERROR;
}

JSON_PATH::JSON_PATH()
  : JSON_POINTER ("")
{
  m_backend_json_format = JSON_PATH_TYPE::JSON_PATH_POINTER;
}

JSON_PATH::JSON_PATH (const TOKEN *tokens, size_t token_cnt)
  : JSON_POINTER (tokens, token_cnt)
{
  // this object does not get owernship over the TOKEN* resources and does not dealloc them
  // during destruction
  m_backend_json_format = JSON_PATH_TYPE::JSON_PATH_POINTER;
}

/*
 * replace_json_pointer ()
 *
 * sql_path (in)
 * An sql_path is normalized to rapidjson standard path
 * Example: $[0]."name1".name2[2] -> /0/name1/name2/2
 */
int
JSON_PATH::replace_json_pointer (const char *sql_path)
{
  std::string sql_path_string (sql_path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (sql_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_POINTER)
    {
      // path is not SQL path format; consider it JSON pointer.
      return assign_pointer (sql_path_string);
    }

  if (!validate_and_make_json_path (sql_path_string, false))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }

  std::unordered_map<std::string, std::string> special_chars;

  build_special_chars_map (json_path_type, special_chars);

  // split in tokens and convert to JSON pointer format
  std::vector<std::string> tokens = db_json_split_path_by_delimiters (sql_path_string, db_Json_sql_path_delimiters,
				    false);
  sql_path_string = "";
  for (unsigned int i = 0; i < tokens.size (); ++i)
    {
      replace_special_chars_in_tokens (tokens[i], special_chars);
      sql_path_string += "/" + tokens[i];
    }

  return assign_pointer (sql_path_string);
}

int
JSON_PATH::assign_pointer (const std::string &pointer_path)
{
  // Call assignment operator on base class object
  JSON_POINTER::operator= (JSON_POINTER (pointer_path.c_str ()));
  if (!IsValid ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }
  m_backend_json_format = JSON_PATH_TYPE::JSON_PATH_POINTER;
  return NO_ERROR;
}