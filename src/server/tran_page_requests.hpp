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

#ifndef _TRAN_PAGE_REQUESTS_HPP_
#define _TRAN_PAGE_REQUESTS_HPP_

enum class tran_to_page_request
{
  // Reserve for responses
  RESPOND,

  // Common
  GET_BOOT_INFO,
  SEND_LOG_PAGE_FETCH,
  SEND_DATA_PAGE_FETCH,
  SEND_DISCONNECT_MSG, /* response-less */

  // Active only
  SEND_LOG_PRIOR_LIST, /* response-less */

  // Passive only
  SEND_LOG_BOOT_INFO_FETCH,
  SEND_STOP_LOG_PRIOR_DISPATCH,
};

enum class page_to_tran_request
{
  // Reserve for responses
  RESPOND,

  // Common
  SEND_BOOT_INFO,

  // Active only
  SEND_SAVED_LSA,
  SEND_LOG_PAGE,
  SEND_DATA_PAGE,

  // Passive only
  SEND_LOG_BOOT_INFO,
  SEND_TO_PTS_LOG_PRIOR_LIST, /* response-less */
  SEND_CONFIRM_LOG_PRIOR_DISPATCH_STOPPED,
};

#endif // !_TRAN_PAGE_REQUESTS_HPP_

