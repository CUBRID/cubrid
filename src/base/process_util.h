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
 * process_util.h - functions for process manipulation
 */

#ifndef _PROCESS_UTIL_H_
#define _PROCESS_UTIL_H_

int
create_child_process (const char *const argv[], int wait_flag, const char *stdin_file, char *stdout_file,
		      char *stderr_file, int *exit_status);

#endif /* _PROCESS_UTIL_H_ */
