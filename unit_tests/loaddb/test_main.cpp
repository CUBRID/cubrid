#include <sstream>
#include <thread>

#include "driver.hpp"
#include "language_support.h"

static const int num_threads = 50;

void parse ()
{
  cubload::driver driver;
  std::string s = "%id [foo] 44\n"
		  "%class [foo] ([id] [name])\n"
		  "@44 1 @foo 2\n"
		  "1 '2' '3 4' 3.14159265F\n"
		  "'a' 'aaa' 'bbb' 'c' NULL 'NULL' $2.0F\n"
		  "1 1 1 1815 '2017-12-22 12:10:21' '2017-12-22' '12:10:21' 1\n";

  driver.parse (s);
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
