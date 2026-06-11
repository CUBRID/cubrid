/*
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
 * oos_util.cpp - Generic OOS (Out-of-row Overflow Storage) helper utilities
 */

#include "oos_util.hpp"

#include "oid.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * oos_oid_in_vector () - True if oid appears in oids (linear scan; vector is small by design).
 */
bool
oos_oid_in_vector (const std::vector<OID> &oids, const OID *oid)
{
  for (const OID &candidate : oids)
    {
      if (OID_EQ (&candidate, oid))
	{
	  return true;
	}
    }
  return false;
}
