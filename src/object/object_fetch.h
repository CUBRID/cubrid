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

//
// object_fetch.h - how to fetch objects on client
//

#ifndef _OBJECT_FETCH_H_
#define _OBJECT_FETCH_H_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

typedef enum au_fetchmode
{
  AU_FETCH_READ,
  AU_FETCH_SCAN,		/* scan that does not allow write */
  AU_FETCH_EXCLUSIVE_SCAN,	/* scan that does allow neither write nor other exclusive scan, i.e, scan for load
				 * index. */
  AU_FETCH_WRITE,
  AU_FETCH_UPDATE
} AU_FETCHMODE;

#endif // not _OBJECT_FETCH_H_
