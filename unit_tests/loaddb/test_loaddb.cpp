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
 * test_loaddb.cpp - implementation for loaddb parse tests
 */

#include <sstream>
#include <thread>

#include "language_support.h"
#include "load_driver.hpp"
#include "test_loaddb.hpp"

namespace test_loaddb
{
  static const int num_threads = 50;
  cubload::driver driver;

  void parse (cubload::driver &driver)
  {
    std::string s = "%id [foo] 44\n"
		    "%class [foo] ([id] [name])\n"
		    "%class bar (code \"name\" \"event\")\n"
		    "@44 1 @foo 2\n"
		    "1 '2' '3 4' 3.14159265F 1e10\n"
		    "\"double quoted string in instance line\"\n"
		    "'aaaa' + \n" // instance line spans over multiple lines
		    "'bbbb'\n"
		    "'a' 'aaa' 'bbb' 'c' NULL 'NULL' $2.0F\n"
		    "1 1 1 1815 '2017-12-22 12:10:21' '2017-12-22' '12:10:21' 1\n";

    std::istringstream iss (s);
    driver.parse (iss);
  }

  void
  test_parse_with_multiple_threads ()
  {
    lang_init ();
    lang_set_charset_lang ("en_US.iso88591");

    std::thread threads[num_threads];
    cubload::driver *drivers[num_threads];

    for (int i = 0; i < num_threads; ++i)
      {
	cubload::driver *driver = new cubload::driver ();
	drivers[i] = driver;

	threads[i] = std::thread (parse, std::ref (*driver));
      }

    for (int i = 0; i < num_threads; ++i)
      {
	threads[i].join ();
	delete drivers[i];
      }
  }

  void
  test_parse_reusing_driver ()
  {
    lang_init ();
    lang_set_charset_lang ("en_US.iso88591");

    std::thread threads[num_threads];
    cubload::driver driver;

    for (int i = 0; i < num_threads; ++i)
      {
	threads[i] = std::thread (parse, std::ref (driver));
	threads[i].join ();
      }
  }
} // namespace test_loaddb
