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

#ifndef _ATS_PS_REQUEST_HPP_
#define _ATS_PS_REQUEST_HPP_

enum class ats_to_ps_request
{
  SEND_LOG_PRIOR_LIST,
  SEND_LOG_PAGE_FETCH
};

enum class ps_to_ats_request
{
  SEND_SAVED_LSA,
};

#endif // !_ATS_PS_REQUEST_IDS_HPP_
