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

#ifndef _BUILD_VERSION_H_
#define _BUILD_VERSION_H_

/*
 * The per-commit version values (commit count and hash). Include this file only where they are needed
 * (src/base/release_string.c); use rel_build_number () or rel_copy_version_string () elsewhere.
 * The bundled CCI library cannot call them, so CMakeLists.txt force-includes this file into it.
 */

#define EXTRA_VERSION @CUBRID_EXTRA_VERSION@
#define BUILD_NUMBER @BUILD_NUMBER@
#define VERSION_STRING "@CUBRID_VERSION@"

#endif /* _BUILD_VERSION_H_ */
