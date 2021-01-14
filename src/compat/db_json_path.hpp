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

/*
 * db_json_path.hpp - functions related to json paths
 */

#ifndef _DB_JSON_PATH_HPP_
#define _DB_JSON_PATH_HPP_

#include "db_json_allocator.hpp"
#include "db_json_types_internal.hpp"
#include "db_rapidjson.hpp"

#include <string>
#include <unordered_set>
#include <vector>

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
  };

  token_type m_type;
  std::string m_object_key;
  unsigned long m_array_idx;

  PATH_TOKEN ();
  PATH_TOKEN (token_type type, unsigned long array_idx);
  PATH_TOKEN (token_type type, std::string &&s);

  const std::string &get_object_key () const;
  unsigned long get_array_index () const;

  bool is_wildcard () const;
  bool static match_pattern (const PATH_TOKEN &matcher, const PATH_TOKEN &matchee);
};

class JSON_PATH
{
  public:
    enum MATCH_RESULT
    {
      NO_MATCH,
      PREFIX_MATCH,
      FULL_MATCH
    };

    std::string dump_json_path () const;
    int parse (const char *path);
    JSON_PATH get_parent () const;

    JSON_VALUE *get (JSON_DOC &jd) const;
    const JSON_VALUE *get (const JSON_DOC &jd) const;
    std::vector<const JSON_VALUE *> extract (const JSON_DOC &) const;

    void set (JSON_DOC &jd, const JSON_VALUE &jv) const;
    void set (JSON_VALUE &jd, const JSON_VALUE &jv, JSON_PRIVATE_MEMPOOL &allocator) const;
    bool erase (JSON_DOC &jd) const;

    const PATH_TOKEN *get_last_token () const;
    size_t get_token_count () const;
    bool is_root_path () const;
    bool is_last_array_index_less_than (size_t size) const;
    bool is_last_token_array_index_zero () const;
    bool points_to_array_cell () const;
    bool parent_exists (JSON_DOC &jd) const;
    bool contains_wildcard () const;

    void push_array_index (unsigned long idx);
    void push_array_index_wildcard ();
    void push_object_key (std::string &&object_key);
    void push_object_key_wildcard ();
    void push_double_wildcard ();
    void pop ();

    static MATCH_RESULT match_pattern (const JSON_PATH &pattern, const JSON_PATH &path);

  private:
    using token_containter_type = std::vector<PATH_TOKEN>;

    int from_json_pointer (const std::string &pointer_path);

    int validate_and_create_from_json_path (std::string &sql_path);

    static MATCH_RESULT match_pattern (const JSON_PATH &pattern, const token_containter_type::const_iterator &it1,
				       const JSON_PATH &path, const token_containter_type::const_iterator &it2);

    static void extract_from_subtree (const JSON_PATH &path, size_t tkn_array_offset,
				      const JSON_VALUE &jv, std::unordered_set<const JSON_VALUE *> &unique_elements,
				      std::vector<const JSON_VALUE *> &vals);

    token_containter_type m_path_tokens;
};

int db_json_path_unquote_object_keys (std::string &sql_path);
#endif /* _DB_JSON_HPP_ */
