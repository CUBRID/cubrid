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
 * cm_portable.h -
 */

#ifndef _CM_PORTABLE_H_
#define _CM_PORTABLE_H_

#if defined(WINDOWS)
#include <string.h>

#define PATH_MAX		256
#define CUB_MAXHOSTNAMELEN	256

#if defined(_MSC_VER) && _MSC_VER < 1900
/* Ref: https://msdn.microsoft.com/en-us/library/2ts7cx93(v=vs.120).aspx */
#define snprintf        _snprintf
#endif /* _MSC_VER && _MSC_VER < 1900 */

#define getpid        _getpid
#define strcasecmp(str1, str2)  _stricmp(str1, str2)

#endif

#endif /* _CM_PORTABLE_H_ */
