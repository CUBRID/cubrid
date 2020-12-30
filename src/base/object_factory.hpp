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
 * object_factory.hpp
 */

#ifndef _OBJECT_FACTORY_HPP_
#define _OBJECT_FACTORY_HPP_

#ident "$Id$"

#include <cassert>
#include <functional>
#include <map>

/*
 * factory of objects:
 * 'object_base' is the base class having derived classed 'object_type'
 * each 'object_type' is identified by a value of 'object_key'
 */
namespace cubbase
{
  template <typename object_key, typename object_base> class factory
  {
    public:
      template <typename object_type>
      int register_creator (object_key k,
			    std::function <object_base*()> object_creator = default_creator <object_type>)
      {
	typename std::map<object_key, std::function <object_base*()>>::iterator it = m_creators_map.find (k);
	if (it != m_creators_map.end ())
	  {
	    /* key already used */
	    assert (false);
	    return -1;
	  }

	m_creators_map[k] = object_creator;
	return 0;
      };

      object_base *create_object (object_key k)
      {
	typename std::map<object_key, std::function <object_base*()>>::iterator it = m_creators_map.find (k);
	if (it == m_creators_map.end ())
	  {
	    return NULL;
	  }
	return (it->second) ();
      };

    private:
      template <typename object_type>
      static object_base *default_creator ()
      {
	return new object_type;
      };

      std::map <object_key, std::function <object_base*()>> m_creators_map;
  };

} /* namespace cubbase */

#endif /* _OBJECT_FACTORY_HPP_ */
