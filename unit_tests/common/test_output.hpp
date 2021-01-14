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
 * test_output.hpp - interface for outputting test results
 */
#ifndef _TEST_OUTPUT_HPP_
#define _TEST_OUTPUT_HPP_

#include <string>

namespace test_common
{

/* Sync output */
void sync_cout (const std::string &str);

} // namespace test_common

#endif // _TEST_OUTPUT_HPP_
