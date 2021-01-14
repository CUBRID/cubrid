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

#include "test_object_factory.hpp"
#include <cassert>
#include <iostream>

namespace test_object_factory
{

  int test_object_factory1 (void)
  {
    int res = 0;
    static const int lion_id = 1;
    static const int beetle_id = 2;
    static const int hawk_id = 3;

    cubbase::factory<int, animal> animal_builder;

    animal_builder.register_creator<lion> (lion_id, &lion::create);
    animal_builder.register_creator<beetle> (beetle_id, []()
    {
      return new beetle ();
    });
    animal_builder.register_creator<hawk> (hawk_id);

    animal *a1 = animal_builder.create_object (lion_id);
    if (dynamic_cast <lion *> (a1) == NULL)
      {
	res = -1;
	assert (false);
      }
    std::cout << " a1 height : " << a1->get_height () << std::endl;

    animal *a2 = animal_builder.create_object (beetle_id);
    if (dynamic_cast <beetle *> (a2) == NULL)
      {
	res = -1;
	assert (false);
      }
    std::cout << " a2 height : " << a2->get_height () << std::endl;

    animal *a3 = animal_builder.create_object (hawk_id);
    if (dynamic_cast <hawk *> (a3) == NULL)
      {
	res = -1;
	assert (false);
      }
    std::cout << " a3 height : " << a3->get_height () << std::endl;

    return res;
  }

  int test_object_factory2 (void)
  {
#define OBJ_CNT 100
    int res = 0;
    int i;
    animal *a[OBJ_CNT];
    animal *copy_of_a[OBJ_CNT];
    int ids[OBJ_CNT];

    cubbase::factory<int, animal> animal_builder;

    animal_builder.register_creator<lion> (1);
    animal_builder.register_creator<beetle> (2);
    animal_builder.register_creator<hawk> (3);

    for (i = 0; i < OBJ_CNT; i++)
      {
	int id = 1 + std::rand () % 3;
	ids[i] = id;
	a[i] = animal_builder.create_object (id);
      }

    for (i = 0; i < OBJ_CNT; i++)
      {
	int id = ids[i];
	copy_of_a[i] = animal_builder.create_object (id);

	if (copy_of_a[i]->get_height () != a[i]->get_height ())
	  {
	    res = -1;
	  }
      }

    for (i = 0; i < OBJ_CNT; i++)
      {
	delete a[i];
	delete copy_of_a[i];
      }

    return res;
  }
}
