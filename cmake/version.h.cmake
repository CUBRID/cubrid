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

#ifndef _VERSION_H_
#define _VERSION_H_

#define MAJOR_VERSION @CUBRID_MAJOR_VERSION@
#define MINOR_VERSION @CUBRID_MINOR_VERSION@
#define PATCH_VERSION @CUBRID_PATCH_VERSION@
#define EXTRA_VERSION @CUBRID_EXTRA_VERSION@
#define MAJOR_RELEASE_STRING @MAJOR_RELEASE_STRING@
#define RELEASE_STRING @RELEASE_STRING@

#define BUILD_NUMBER @BUILD_NUMBER@
#define BUILD_OS @CMAKE_SYSTEM_NAME@
#define BUILD_TYPE @BUILD_TYPE@

#define PACKAGE_STRING "@PACKAGE_STRING@"
#define PRODUCT_STRING "@PRODUCT_STRING@"
#define VERSION_STRING "@CUBRID_VERSION@"

#endif /* _VERSION_H_ */
