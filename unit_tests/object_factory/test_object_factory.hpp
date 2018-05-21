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

#ifndef _TEST_OBJECT_FACTORY_HPP_
#define _TEST_OBJECT_FACTORY_HPP_

#include "object_factory.hpp"

namespace test_object_factory
{

  int test_object_factory1 (void);
  int test_object_factory2 (void);

  class animal
  {
    public:
      virtual int get_height () = 0;
  };

  class lion : public animal
  {
    public:
      static animal *create ()
      {
	return new lion;
      };
      int get_height ()
      {
	return 100;
      }
  };

  class beetle : public animal
  {
    public:
      static animal *create ()
      {
	return new beetle;
      };
      int get_height ()
      {
	return 2;
      }
  };

  class hawk : public animal
  {
    public:
      static animal *create ()
      {
	return new hawk;
      };
      int get_height ()
      {
	return 20;
      }
  };

} /* namespace test_object_factory*/

#endif /* _TEST_OBJECT_FACTORY_HPP_ */
