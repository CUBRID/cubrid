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

#include <unordered_map>
#include <string>

class JSON_PATH
{
    typedef rapidjson::GenericPointer<JSON_VALUE> JSON_POINTER;

  public:
    enum class JSON_PATH_TYPE
    {
      JSON_PATH_SQL_JSON,
      JSON_PATH_POINTER
    };

    struct PATH_TOKEN
    {
      enum token_type
      {
	object_key_wild_card,
	array_index_wild_card,
	double_wild_card,
	object_key,
	array_index,
	// token type used to signify json_pointers' '-' special token
	last_index_special
      } type;

      // todo: optimize representation (e.g. have an ull for indexes)
      std::string token_string;

      bool is_wildcard () const;

      bool match (const PATH_TOKEN &other) const;
    };

    std::string dump_json_pointer () const;

    std::string dump_json_path (bool skip_json_pointer_minus = true) const;

    std::vector<JSON_VALUE *> get (JSON_DOC &jd) const;

    std::vector<const JSON_VALUE *> get (const JSON_DOC &jd) const;

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

    bool validate_and_make_json_path (std::string &sql_path);

    explicit JSON_PATH ();

    explicit JSON_PATH (std::vector<PATH_TOKEN> tokens);

    bool match (const JSON_PATH &other, bool match_prefix = false) const;

    void emplace_back (PATH_TOKEN &&path_token);

    void pop ();

    bool contains_wildcard () const;

  private:
    int replace_json_pointer (const char *sql_path);

    void build_special_chars_map (const JSON_PATH_TYPE &json_path_type,
				  std::unordered_map<std::string, std::string> &special_chars) const;

    int assign_pointer (const std::string &pointer_path);

    void replace_special_chars_in_tokens (std::string &token,
					  const std::unordered_map<std::string, std::string> &special_chars) const;

    // todo: find a way to avoid passing reference to get other_tokens.end ()
    bool match (const std::vector<PATH_TOKEN>::const_iterator &it1, const std::vector<PATH_TOKEN>::const_iterator &it2,
		const std::vector<PATH_TOKEN> &other_tokens, bool match_prefix = false) const;

    std::vector<PATH_TOKEN> m_path_tokens;
    friend class JSON_PATH_SETTER;
    friend class JSON_PATH_GETTER;
};

typedef JSON_PATH::JSON_PATH_TYPE JSON_PATH_TYPE;

// exposed free funcs
std::vector<std::string> db_json_split_path_by_delimiters (const std::string &path,
    const std::string &delim, bool allow_empty);
int db_json_normalize_path (const char *pointer_path, JSON_PATH &json_path);
void db_json_path_unquote_object_keys (std::string &sql_path);
std::vector<std::vector<const JSON_VALUE *>> db_json_get_values_from_path (const std::vector<JSON_PATH> &json_paths);

class JSON_PATH_GETTER : public JSON_WALKER
{
  public:
    JSON_PATH_GETTER () = delete;
    JSON_PATH_GETTER (const JSON_PATH &json_path, JSON_VALUE *&found_json_value);
    JSON_PATH_GETTER (JSON_PATH_GETTER &) = delete;
    ~JSON_PATH_GETTER () override = default;

  private:
    int CallBefore (JSON_VALUE &value) override;
    int CallAfter (JSON_VALUE &value) override;
    int CallOnArrayIterate () override;
    int CallOnKeyIterate (JSON_VALUE &key) override;

    const JSON_PATH &m_path;
    size_t m_token_idx;
    rapidjson::SizeType m_array_idx;
    JSON_VALUE *&m_found_json_value;
};

class JSON_PATH_SETTER : public JSON_WALKER
{
  public:
    JSON_PATH_SETTER () = delete;
    JSON_PATH_SETTER (const JSON_PATH &json_path, JSON_DOC &json_doc, const JSON_VALUE &json_value);
    JSON_PATH_SETTER (JSON_PATH_SETTER &) = delete;
    ~JSON_PATH_SETTER () override = default;

  private:
    int CallBefore (JSON_VALUE &value) override;
    int CallAfter (JSON_VALUE &value) override;
    int CallOnArrayIterate () override;
    int CallOnKeyIterate (JSON_VALUE &key) override;

    const JSON_PATH &m_path;
    JSON_PRIVATE_MEMPOOL &m_allocator;
    const JSON_VALUE &m_value;
    size_t m_token_idx;

    // todo: probably there is no need for a stack, a single siztype is enough
    rapidjson::SizeType m_array_idx;
    rapidjson::SizeType m_allowed_idx;
};

#endif /* _DB_JSON_HPP_ */
