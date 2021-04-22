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

namespace cubcomm
{

  class node
  {
      int m_port;
      const char *m_host;

      node() = delete;
      node (const node &) = delete;
      node (int port, char *host);
  };

}

#endif /* _COMMUNICATION_NODE_HPP_ */
