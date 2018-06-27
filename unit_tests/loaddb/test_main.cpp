#include <sstream>
#include <thread>

#include "driver.hpp"
#include "language_support.h"

static const int num_threads = 50;

void parse ()
{
  cubloaddb::driver driver (0, 0);
  std::string s = "1 '2' '3 4' 3.14159265F\n"
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

  std::thread threads[num_threads];

  for (int i = 0; i < num_threads; ++i)
    {
      threads[i] = std::thread (parse);
    }

  for (int i = 0; i < num_threads; ++i)
    {
      threads[i].join ();
    }

  return 0;
}
