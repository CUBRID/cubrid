#pragma once
#include <deque>
#include "oid.h"

std::deque<OID> &oos_oid_queue ();

bool oos_oid_queue_pop (OID *out);
