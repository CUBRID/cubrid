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

#include <fstream>
#include <sstream>
#include <thread>

#include "driver.hpp"
#include "language_support.h"
#include "test_loaddb.hpp"

namespace test_loaddb
{
  static const int num_threads = 50;

  void parse (cubload::driver &driver)
  {
    std::string s = "%id [foo] 44\n"
		    "%class [foo] ([id] [name])\n"
		    "@44 1 @foo 2\n"
		    "1 '2' '3 4' 3.14159265F\n"
		    "'aaaa' + \n"
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

  bool ends_with (const std::string &str, const std::string &suffix)
  {
    std::size_t str_size = str.size ();
    std::size_t suffix_size = suffix.size ();
    return str_size >= suffix_size && 0 == str.compare (str_size - suffix_size, suffix_size, suffix);
  }

  bool starts_with (const std::string &str, const std::string &prefix)
  {
    return str.size () >= prefix.size () && 0 == str.compare (0, prefix.size (), prefix);
  }

  void send_batch (std::string &batch)
  {
    if (!batch.empty ())
      {
	cubload::driver driver;
	std::istringstream iss (batch);
	driver.parse (iss);

	std::cout << batch << "\n";
	batch.clear ();
      }
  }

  void
  test_read_object_file ()
  {
    lang_init ();
    lang_set_charset_lang ("en_US.iso88591");

    std::ifstream object_file ("/home/blackie/projects/CUBRID/cubrid/demo/demodb_objects", std::fstream::in);
    int batch_size = 10;

    std::string batch_buffer;
    int rows = 0;

    assert (batch_size > 0);
    assert (object_file); // call bool operator

    for (std::string line; std::getline (object_file, line); )
      {
	if (starts_with (line, "%id"))
	  {
	    // do nothing for now
	    continue;
	  }

	if (starts_with (line, "%class"))
	  {
	    // close current batch
	    send_batch (batch_buffer);

	    // rewind forward rows counter until batch is full
	    for (; (rows % batch_size) != 0; rows++)
	      ;

	    // start new batch for new class
	    std::cout << line << "\n";
	    continue;
	  }

	// it is a line containing row data so append it
	batch_buffer.append (line);

	// it could be that a row is wrapped on the next line,
	// this means that the row ends on the last line that does not end with '+' (plus) character
	if (!ends_with (line, "+"))
	  {
	    // since std::getline eats endline character, add it back in order to make loaddb lexer happy
	    batch_buffer.append ("\n");

	    rows++;

	    // check if we have a full batch
	    if ((rows % batch_size) == 0)
	      {
		send_batch (batch_buffer);
	      }
	  }
      }

    // collect remaining rows
    send_batch (batch_buffer);

    object_file.close ();
  }
} // namespace test_loaddb
