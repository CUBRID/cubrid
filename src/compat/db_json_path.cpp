#include "db_json_path.hpp"
#include "string_opfunc.h"
#include <algorithm>
#include <string>
#include <cctype>

typedef rapidjson::GenericPointer <JSON_VALUE>::Token TOKEN;
static const rapidjson::SizeType kPointerInvalidIndex = rapidjson::kPointerInvalidIndex;

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
  if (i < path.length () && !std::isalpha (static_cast<unsigned char> (path[i])))
    {
      return false;
    }

  ++i;
  for (; i < path.length () && std::isalnum (static_cast<unsigned char> (path[i])); ++i);

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
      end = str.length ();
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
  for (auto it = str.cbegin () + start; it < str.cbegin () + last_non_space + 1; ++it)
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
 * validate_and_make_json_path () - Check if a given path is a SQL valid path
 *
 * return                  : true/false
 * sql_path (in/out)       : path to be checked
 * allow_wild_cards (in)   : whether json_path wildcards are allowed
 */
bool
JSON_PATH::validate_and_make_json_path (std::string &sql_path)
{
  // skip leading white spaces
  db_json_trim_leading_spaces (sql_path);
  if (sql_path.empty ())
    {
      // empty
      return false;
    }

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
	  if (!db_json_path_is_token_valid_array_index (sql_path, true, i, end_bracket_offset))
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
	  if (++i >= sql_path.length () || sql_path[i] != '*')
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
  result.reserve (sql_path.length () + 1);

  bool skip_spaces = true;
  bool unescaped_backslash = false;
  for (size_t i = 0; i < sql_path.length (); ++i)
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
	      tokens.clear ();
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
	  if (!substring.empty () || allow_empty)
	    {
	      tokens.push_back (substring);
	    }
	  start = end + 1;
	}

      end = path.find_first_of (delim, end + 1);
    }

  const std::string &substring = path.substr (start, end);
  if (!substring.empty () || allow_empty)
    {
      tokens.push_back (substring);
    }

  std::size_t tokens_size = tokens.size ();
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
db_json_normalize_path (const char *pointer_path, JSON_PATH &json_path)
{
  std::string pointer_path_string (pointer_path);
  JSON_PATH_TYPE json_path_type = db_json_get_path_type (pointer_path_string);

  if (json_path_type == JSON_PATH_TYPE::JSON_PATH_SQL_JSON)
    {
      // path is not JSON path format; consider it SQL path.
      // sql_path_out = pointer_path_string;
      if (!json_path.validate_and_make_json_path (pointer_path_string))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
	  return ER_JSON_INVALID_PATH;
	}
      return NO_ERROR;
    }

  int error_code = json_path.init (pointer_path);
  if (error_code)
    {
      ASSERT_ERROR ();
      return error_code;
    }
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
  for (size_t token_idx = 0; token_idx < token.length (); /* incremented in for body */)
    {
      replaced = false;
      // compare with special characters
      for (auto special_it = special_chars.begin (); special_it != special_chars.end (); ++special_it)
	{
	  // compare special characters with sequence following token_it
	  if (token_idx + special_it->first.length () <= token.length ())
	    {
	      if (token.compare (token_idx, special_it->first.length (), special_it->first) == 0)
		{
		  // replace
		  token.replace (token_idx, special_it->first.length (), special_it->second);
		  // skip replaced
		  token_idx += special_it->second.length ();

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
  if (it1 == m_path_tokens.end () && it2 == other_tokens.end ())
    {
      return true;
    }

  if (it1 == m_path_tokens.end ())
    {
      return match_prefix;
    }

  if (it2 == other_tokens.end ())
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

void JSON_PATH::set (JSON_DOC &jd, const JSON_VALUE &jv) const
{
  JSON_PATH_SETTER setter (*this, jd, jv);
  setter.WalkDocument (jd);
}

std::vector<JSON_VALUE *> JSON_PATH::get (JSON_DOC &jd) const
{
  JSON_VALUE *found = nullptr;
  JSON_PATH_GETTER getter (*this, found);

  getter.WalkDocument (jd);

  return { found };
}

std::vector<const JSON_VALUE *> JSON_PATH::get (const JSON_DOC &jd) const
{
  JSON_VALUE *found = nullptr;
  JSON_PATH_GETTER getter (*this, found);

  getter.WalkDocument (const_cast<JSON_DOC &> (jd));

  return { found };
}

DB_JSON_TYPE JSON_PATH::get_value_type (const JSON_DOC &jd) const
{
  return db_json_get_type_of_value (get (jd)[0]);
}

bool JSON_PATH::erase (JSON_DOC &jd) const
{
  if (get_token_count () == 0)
    {
      return false;
    }

  auto value = get_parent ().get (jd)[0];

  const PATH_TOKEN &tkn = m_path_tokens.back ();

  switch (db_json_get_type_of_value (value))
    {
    case DB_JSON_ARRAY:
      return value->Erase (value->Begin () + std::stoi (tkn.token_string));
    case DB_JSON_OBJECT:
      return value->EraseMember (tkn.token_string.c_str ());
    default:
      return false;
    }
}

const JSON_PATH::PATH_TOKEN *JSON_PATH::get_last_token () const
{
  return get_token_count () > 0 ? &m_path_tokens[get_token_count () - 1] : NULL;
}

size_t JSON_PATH::get_token_count () const
{
  return m_path_tokens.size ();
}

bool JSON_PATH::is_root_path () const
{
  return get_token_count () == 0;
}

const JSON_PATH JSON_PATH::get_parent () const
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
      std::vector<PATH_TOKEN> parent_tokens (m_path_tokens);
      parent_tokens.pop_back ();

      JSON_PATH parent (parent_tokens);
      return parent;
    }
}

bool JSON_PATH::is_last_array_index_less_than (size_t size) const
{
  const PATH_TOKEN *last_token = get_last_token ();
  assert (last_token != NULL);
  //todo: needed?
  return ! (last_token->type == PATH_TOKEN::last_index_special || (last_token->type == PATH_TOKEN::array_index
	    && std::stoi (last_token->token_string) >= size));
}

bool JSON_PATH::is_last_token_array_index_zero () const
{
  const PATH_TOKEN *last_token = get_last_token ();
  assert (last_token != NULL);
  return (last_token->type == PATH_TOKEN::array_index && std::stoi (last_token->token_string) == 0);
}

bool JSON_PATH::points_to_array_cell () const
{
  const PATH_TOKEN *last_token = get_last_token ();
  return (last_token != NULL && (last_token->type == PATH_TOKEN::array_index
				 || (last_token->type == PATH_TOKEN::last_index_special)));
}

bool JSON_PATH::parent_exists (JSON_DOC &jd) const
{
  if (get_token_count () == 0)
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
  // todo: make sure this is called only after path is checked to be json_pointer
  replace_json_pointer (path);
  return NO_ERROR;
}

JSON_PATH::JSON_PATH ()
{

}

JSON_PATH::JSON_PATH (std::vector<PATH_TOKEN> tokens)
  : m_path_tokens (std::move (tokens))
{

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

  if (!validate_and_make_json_path (sql_path_string))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALID_PATH, 0);
      return ER_JSON_INVALID_PATH;
    }
  return NO_ERROR;
}

int
JSON_PATH::assign_pointer (const std::string &pointer_path)
{
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
      PATH_TOKEN path_token;

      if (rapid_token.index != kPointerInvalidIndex)
	{
	  // array_index
	  path_token.type = PATH_TOKEN::array_index;
	  path_token.token_string = std::to_string (rapid_token.index);
	}
      else if (rapid_token.length == 1 && rapid_token.name[0] == '-' )
	{
	  // '-' special idx token
	  path_token.type = PATH_TOKEN::last_index_special;
	}
      else
	{
	  // object_key
	  path_token.type = PATH_TOKEN::object_key;
	  path_token.token_string = rapid_token.name;
	}
      m_path_tokens.push_back (path_token);
    }

  return NO_ERROR;
}


JSON_PATH_GETTER::JSON_PATH_GETTER (const JSON_PATH &json_path, JSON_VALUE *&found_json_value)
  : m_path (json_path)
  , m_found_json_value (found_json_value)
  , m_token_idx (0)
{

}

int JSON_PATH_GETTER::CallBefore (JSON_VALUE &value)
{
  if (m_token_idx == m_path.m_path_tokens.size ())
    {
      // we have now reached the end of the path
      m_found_json_value = &value;
      m_stop = true;
      return NO_ERROR;
    }

  const JSON_PATH::PATH_TOKEN &tkn = m_path.m_path_tokens[m_token_idx];
  if ((value.IsArray () && !JSON_PATH::PATH_TOKEN::token_type::array_index) || (value.IsObject ()
      && !JSON_PATH::PATH_TOKEN::token_type::object_key))
    {
      m_stop = true;
    }

  ++m_token_idx;
  return NO_ERROR;
}

int JSON_PATH_GETTER::CallAfter (JSON_VALUE &value)
{
  // break previous object/array iterations
  m_stop = true;
  return false;
}

int JSON_PATH_GETTER::CallOnArrayIterate ()
{
  const JSON_PATH::PATH_TOKEN &tkn = m_path.m_path_tokens[m_token_idx - 1];

  rapidjson::SizeType array_idx_token = (rapidjson::SizeType) std::stoi (tkn.token_string);

  m_skip = ! (array_idx_token == m_array_idxs.top ());

  ++m_array_idxs.top ();

  return NO_ERROR;
}

int JSON_PATH_GETTER::CallOnKeyIterate (JSON_VALUE &key)
{
  const JSON_PATH::PATH_TOKEN &tkn = m_path.m_path_tokens[m_token_idx - 1];
  assert (tkn.type == JSON_PATH::PATH_TOKEN::token_type::object_key && key.IsString ());

  m_skip = (strcmp (key.GetString (), tkn.token_string.c_str ()) != 0);

  return NO_ERROR;
}

JSON_PATH_SETTER::JSON_PATH_SETTER (const JSON_PATH &json_path, JSON_DOC &json_doc, const JSON_VALUE &json_value)
  : m_path (json_path)
  , m_allocator (json_doc.GetAllocator ())
  , m_value (json_value)
  , m_token_idx (0)
{

}

int JSON_PATH_SETTER::CallBefore (JSON_VALUE &value)
{
  if (m_token_idx == m_path.m_path_tokens.size ())
    {
      // we finished the path
      // set somehow
      // value.Set (m_value, m_allocator);
      return NO_ERROR;
    }

  const JSON_PATH::PATH_TOKEN &tkn = m_path.m_path_tokens[m_token_idx];

  switch (tkn.type)
    {
    case JSON_PATH::PATH_TOKEN::token_type::array_index:
    case JSON_PATH::PATH_TOKEN::token_type::last_index_special:
      if (!value.IsArray ())
	{
	  value.SetArray ();
	}
      break;
    case JSON_PATH::PATH_TOKEN::token_type::object_key:
      if (!value.IsObject ())
	{
	  value.SetObject ();
	}
      break;
    case JSON_PATH::PATH_TOKEN::token_type::array_index_wild_card:
    case JSON_PATH::PATH_TOKEN::token_type::object_key_wild_card:
    case JSON_PATH::PATH_TOKEN::token_type::double_wild_card:
      // error? unexpected set - wildcards not allowed for set
      assert (false);
    }

  if (value.IsArray ())
    {
      JSON_VALUE::Array &arr = value.GetArray ();
      if (tkn.type == JSON_PATH::PATH_TOKEN::token_type::last_index_special)
	{
	  // insert dummy
	  arr.PushBack (JSON_VALUE ().SetNull (), m_allocator);
	  m_allowed_idxs.push (arr.Size () - 1);
	}
      else
	{
	  rapidjson::SizeType idx = (rapidjson::SizeType) std::stoi (tkn.token_string);
	  while (idx >= arr.Size ())
	    {
	      arr.PushBack (JSON_VALUE ().SetNull (), m_allocator);
	    }
	  m_allowed_idxs.push (idx);
	}
      m_array_idxs.push (0);
    }
  else if (value.IsObject ())
    {
      JSON_VALUE::MemberIterator m = value.FindMember (tkn.token_string.c_str ());
      if (m == value.MemberEnd ())
	{
	  // insert dummy
	  value.AddMember (JSON_VALUE (tkn.token_string.c_str (), (rapidjson::SizeType) tkn.token_string.length ()),
			   JSON_VALUE ().SetNull (),
			   m_allocator);
	}
    }

  ++m_token_idx;
  return NO_ERROR;
}

int JSON_PATH_SETTER::CallAfter (JSON_VALUE &value)
{
  // break previous object/array iterations
  m_stop = true;
  return NO_ERROR;
}

int JSON_PATH_SETTER::CallOnArrayIterate ()
{
  m_skip = ! (m_array_idxs.top () == m_allowed_idxs.top ());

  ++m_array_idxs.top ();
  return NO_ERROR;
}

int JSON_PATH_SETTER::CallOnKeyIterate (JSON_VALUE &key)
{
  const JSON_PATH::PATH_TOKEN &tkn = m_path.m_path_tokens[m_token_idx - 1];

  assert (tkn.type == JSON_PATH::PATH_TOKEN::token_type::object_key && key.IsString ());

  m_skip = (strcmp (key.GetString (), tkn.token_string.c_str ()) != 0);

  return NO_ERROR;
}
