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
 * px_scan_type_enum.hpp - SCAN_TYPE enum (lightweight header without scan_traits)
 */

#ifndef _PX_SCAN_TYPE_ENUM_HPP_
#define _PX_SCAN_TYPE_ENUM_HPP_

namespace parallel_scan
{

  enum class SCAN_TYPE
  {
    HEAP = 0,
    LIST = 1,
    INDEX = 2,
  };

} /* namespace parallel_scan */

#endif /* _PX_SCAN_TYPE_ENUM_HPP_ */
