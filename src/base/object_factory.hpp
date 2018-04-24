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
