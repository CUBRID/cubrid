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

#ifndef _COMMUNICATION_NODE_HPP_
#define _COMMUNICATION_NODE_HPP_

#include <string>

namespace cubcomm
{

  class node
  {
    public:
      node () = default;
      node (const node &nd) = delete;
      node (node &&nd) = default;
      node (int32_t port, std::string host);

      node &operator= (const node &nd) = delete;
      node &operator= (node &&nd) = default;

      int32_t get_port () const;
      std::string get_host () const;

    private:
      int m_port = -1; // initialize with an invalid port
      std::string m_host;
  };

  inline node::node (int32_t port, std::string host)
  {
    m_port = port;
    m_host = host;
  }

  inline int node::get_port () const
  {
    return m_port;
  }

  inline std::string node::get_host () const
  {
    return m_host;
  }

}

#endif /* _COMMUNICATION_NODE_HPP_ */
