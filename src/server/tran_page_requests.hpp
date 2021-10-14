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

#ifndef _TS_PS_REQUEST_HPP_
#define _TS_PS_REQUEST_HPP_

enum class tran_to_page_request
{
  // Common
  GET_BOOT_INFO,
  SEND_LOG_PAGE_FETCH,
  SEND_DATA_PAGE_FETCH,
  SEND_DISCONNECT_MSG,

  // Active only
  SEND_LOG_PRIOR_LIST,

  // Passive only
};

enum class page_to_tran_request
{
  SEND_BOOT_INFO,
  SEND_SAVED_LSA,
  SEND_LOG_PAGE,
  SEND_DATA_PAGE
};

#endif // !_TS_PS_REQUEST_HPP_

