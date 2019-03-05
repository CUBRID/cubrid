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
  public:
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

      // todo: optimize representation (e.g. have an ull for indexes)
      std::string token_string;

      bool is_wildcard () const;

      bool match (const PATH_TOKEN &other) const;
    };

    std::string dump_json_path (bool skip_json_pointer_minus = true) const;

    JSON_VALUE *get (JSON_DOC &jd) const;

    const JSON_VALUE *get (const JSON_DOC &jd) const;

    DB_JSON_TYPE get_value_type (const JSON_DOC &jd) const;

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

    explicit JSON_PATH (std::vector<PATH_TOKEN> tokens);

    bool match (const JSON_PATH &other, bool match_prefix = false) const;

    void push_array_index (size_t idx);

    void push_array_index_wildcard ();

    void push_object_key (std::string &&object_key);

    void push_object_key_wildcard ();

    void push_double_wildcard ();

    void pop ();

    bool contains_wildcard () const;

  private:
    int from_json_pointer (const std::string &pointer_path);

    // todo: find a way to avoid passing reference to get other_tokens.end ()
    bool match (const std::vector<PATH_TOKEN>::const_iterator &it1, const std::vector<PATH_TOKEN>::const_iterator &it2,
		const std::vector<PATH_TOKEN> &other_tokens, bool match_prefix = false) const;

    std::vector<PATH_TOKEN> m_path_tokens;
    friend class JSON_PATH_SETTER;
    friend class JSON_PATH_GETTER;
};

// exposed free funcs
std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim, bool allow_empty);
void db_json_path_unquote_object_keys (std::string &sql_path);
#endif /* _DB_JSON_HPP_ */
