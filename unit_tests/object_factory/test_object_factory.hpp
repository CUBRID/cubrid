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
      virtual ~animal ()
      {
      }
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
