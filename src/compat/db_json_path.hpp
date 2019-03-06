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

/*
 * db_json_path.hpp - functions related to json paths
 */

#ifndef _DB_JSON_PATH_HPP_
#define _DB_JSON_PATH_HPP_

#include "db_json_types_internal.hpp"
#include "db_json.hpp"

#include <string>

class JSON_PATH
{
    struct PATH_TOKEN
    {
	enum token_type
	{
	  object_key_wildcard,
	  array_index_wildcard,
	  double_wildcard,
	  object_key,
	  array_index,
	  // token type used to signify json_pointers' '-' special token
	  array_end_index
	} type;

	union token_representation
	{
public :
	  token_representation ()
	    : array_idx (0)
	  {

	  }

	  token_representation (rapidjson::SizeType array_idx)
	    : array_idx (array_idx)
	  {

	  }

	  token_representation (std::string &&s)
	    : object_key (std::move (s))
	  {

	  }

	  ~token_representation ()
	  {

	  }

	  std::string object_key;
	  rapidjson::SizeType array_idx;
	} repr;

	PATH_TOKEN ()
	  : type (array_index)
	{

	}

	PATH_TOKEN (token_type type, rapidjson::SizeType array_idx)
	  : type (type)
	  , repr (array_idx)
	{

	}

	PATH_TOKEN (token_type type, std::string &&s)
	  : type (type)
	  , repr (std::move (s))
	{

	}

	PATH_TOKEN (const PATH_TOKEN &other)
	  : type (other.type)
	{
	  memcpy (&repr, &other.repr, sizeof (repr));
	}

	const std::string &get_object_key () const;

	rapidjson::SizeType get_array_index () const;

	bool is_wildcard () const;

	bool match (const PATH_TOKEN &other) const;
    };

  private:
    using token_containter_type = std::vector<PATH_TOKEN>;

  public:
    std::string dump_json_path () const;

    JSON_VALUE *get (JSON_DOC &jd) const;

    const JSON_VALUE *get (const JSON_DOC &jd) const;

    void set (JSON_DOC &jd, const JSON_VALUE &jv) const;

    bool erase (JSON_DOC &jd) const;

    const PATH_TOKEN *get_last_token () const;

    size_t get_token_count () const;

    bool is_root_path () const;

    const JSON_PATH get_parent () const;

    bool is_last_array_index_less_than (size_t size) const;

    bool is_last_token_array_index_zero () const;

    bool points_to_array_cell () const;

    bool parent_exists (JSON_DOC &jd) const;

    int init (const char *path);

    bool validate_and_create_from_json_path (std::string &sql_path);

    explicit JSON_PATH ();

    explicit JSON_PATH (token_containter_type tokens);

    bool match (const JSON_PATH &other, bool match_prefix = false) const;

    void push_array_index (unsigned idx);

    void push_array_index_wildcard ();

    void push_object_key (std::string &&object_key);

    void push_object_key_wildcard ();

    void push_double_wildcard ();

    void pop ();

    bool contains_wildcard () const;

  private:
    int from_json_pointer (const std::string &pointer_path);

    // todo: find a way to avoid passing reference to get other_tokens.end ()
    bool match (const token_containter_type::const_iterator &it1, const token_containter_type::const_iterator &it2,
		const token_containter_type &other_tokens, bool match_prefix = false) const;

    token_containter_type m_path_tokens;
};

// exposed free funcs
std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim, bool allow_empty);
void db_json_path_unquote_object_keys (std::string &sql_path);
#endif /* _DB_JSON_HPP_ */
