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
