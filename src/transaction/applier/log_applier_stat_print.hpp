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
 */

/*
 * log_applier_stat_print.hpp - interface for printing parallel apply statistics in applyinfo
 */

#ifndef _LOG_APPLIER_STAT_PRINT_HPP_
#define _LOG_APPLIER_STAT_PRINT_HPP_

extern void la_stat_print_applyinfo (const char *log_path, const char *prefix_name, bool verbose);

#endif /* _LOG_APPLIER_STAT_PRINT_HPP_ */
