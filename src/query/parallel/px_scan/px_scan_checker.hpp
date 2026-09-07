/*
 *
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
 * px_scan_checker.hpp
 */

#ifndef _PX_SCAN_CHECKER_HPP_
#define _PX_SCAN_CHECKER_HPP_
#include "xasl.h"

extern "C" int scan_check_parallel_scan_possible (XASL_NODE *xasl);

namespace parallel_scan
{
  /* header-only: px_scan_checker.cpp is linked into cs/sa only, but the server-side scan open
   * (px_scan.cpp) needs the same test. */
  inline bool
  has_nonlinked_dptr (const XASL_NODE *xasl)
  {
    for (const XASL_NODE *dptr = xasl->dptr_list; dptr != nullptr; dptr = dptr->next)
      {
	if (!XASL_IS_FLAGED (dptr, XASL_LINK_TO_REGU_VARIABLE))
	  {
	    return true;
	  }
      }
    return false;
  }
}

#endif /*_PX_SCAN_CHECKER_HPP_ */
