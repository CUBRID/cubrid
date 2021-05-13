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

#ifndef _VPID_HPP_
#define _VPID_HPP_

#include "dbtype_def.h"

/* C++ extensions for VPID struct; not defined in 'compat' - where VPID is also
 * declared - because of strict requirements for c-style to be kept in there
 */

inline constexpr bool operator== (const VPID &left, const VPID &rite)
{
  return (left.volid == rite.volid && left.pageid == rite.pageid);
}

inline constexpr bool operator!= (const VPID &left, const VPID &rite)
{
  return ! (left == rite);
}

inline constexpr bool operator< (const VPID &left, const VPID &rite)
{
  return (left != rite)
	 && (
		 (left.volid < rite.volid)
		 || (left.volid == rite.volid && left.pageid < rite.pageid)
	 );
}

#endif // _VPID_HPP_
