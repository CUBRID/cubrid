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
 * px_scan_result_type.hpp
 */

#ifndef _PX_SCAN_RESULT_TYPE_HPP_
#define _PX_SCAN_RESULT_TYPE_HPP_

namespace parallel_scan
{
  enum class RESULT_TYPE
  {
    MERGEABLE_LIST = 0x1,	/* fast: list-per-thread, merged set-dependent. */
    XASL_SNAPSHOT = 0x2,	/* slow: row-by-row snapshot. */
    BUILDVALUE_OPT = 0x3,	/* fast: buildvalue proc aggregate optimization. */

  };
}

#endif /* _PX_SCAN_RESULT_TYPE_HPP_ */
