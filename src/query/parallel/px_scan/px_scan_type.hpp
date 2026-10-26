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
 * px_scan_type.hpp - SCAN_TYPE enum and scan_traits template
 */

#ifndef _PX_SCAN_TYPE_HPP_
#define _PX_SCAN_TYPE_HPP_

#include "px_scan_input_handler_heap.hpp"
#include "px_scan_input_handler_index.hpp"
#include "px_scan_input_handler_list.hpp"
#include "px_scan_slot_iterator.hpp"
#include "px_scan_slot_iterator_index.hpp"
#include "px_scan_slot_iterator_list.hpp"
#include "px_scan_type_enum.hpp"

namespace parallel_scan
{

  template <SCAN_TYPE>
  struct scan_traits;

  template <>
  struct scan_traits<SCAN_TYPE::HEAP>
  {
    using input_handler_type = input_handler_heap;
    using slot_iterator_type = slot_iterator;
  };

  template <>
  struct scan_traits<SCAN_TYPE::LIST>
  {
    using input_handler_type = input_handler_list;
    using slot_iterator_type = slot_iterator_list;
  };

  template <>
  struct scan_traits<SCAN_TYPE::INDEX>
  {
    using input_handler_type = input_handler_index;
    using slot_iterator_type = slot_iterator_index;
  };

} /* namespace parallel_scan */

#endif /*_PX_SCAN_TYPE_HPP_ */
