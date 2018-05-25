
#include "test_worker_pool.hpp"
#include "test_manager.hpp"

int
main (int, char **)
{
  (void) test_thread::test_worker_pool ();
  (void) test_thread::test_manager ();

  return 0;
}
