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
 * pinning.cpp
 */

#ident "$Id$"

#include "pinning.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  int pinner::pin (pinnable *reference)
  {
    if (reference != NULL
	&& reference->add_pinner (this) == NO_ERROR)
      {
	references.insert (reference);
	return NO_ERROR;
      }

    return NO_ERROR;
  }

  int pinner::unpin (pinnable *reference)
  {
    if (reference->remove_pinner (this) == NO_ERROR)
      {
	references.erase (reference);
	return NO_ERROR;
      }

    return NO_ERROR;
  }

  int pinner::unpin_all (void)
  {
    for (auto it = references.begin (); it != references.end ();)
      {
	unpin (*it++);
      }

    return NO_ERROR;
  }

} /* namespace pinning */
