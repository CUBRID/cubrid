#include <deque>
#include "oid.h"
#include "oos_oid_queue.hpp"

thread_local std::deque<OID> g_oos_oid_queue;

std::deque<OID> &oos_oid_queue ()
{
  return g_oos_oid_queue;
}

bool oos_oid_queue_pop (OID *out)
{
  auto &q = oos_oid_queue ();
  if (q.empty ())
    {
      return false;
    }
  *out = q.front ();
  q.pop_front ();
  return true;
}