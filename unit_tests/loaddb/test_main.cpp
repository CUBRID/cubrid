#include <sstream>
#include <thread>

#include "driver.hpp"
#include "language_support.h"

static const int num_threads = 50;

void parse ()
{
  cubloaddb::driver driver (1, 1);
  std::string s = "1 '2' '3 4' 3.14159265\n"
		  "'a' 'aaa' 'bbb' 'c'\n"
		  "1 1 1 1815 '2017-12-22 12:10:21' '2017-12-22' '12:10:21' 1\n";

  std::istringstream ss (s);

  driver.parse (ss);
}

int
main (int, char **)
{
  lang_init ();
  lang_set_charset_lang ("en_US.iso88591");

  std::thread t[num_threads];

  //Launch a group of threads
  for (int i = 0; i < num_threads; ++i)
    {
      t[i] = std::thread (parse);
    }

  //Join the threads with the main thread
  for (int i = 0; i < num_threads; ++i)
    {
      t[i].join ();
    }

  return 0;
}
