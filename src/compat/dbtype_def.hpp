#ifndef DBTYPE_DEF_HPP
#define DBTYPE_DEF_HPP

#include "dbtype_def.h"

/* C++ extensions for strict-C structures defined in corresponding '.h' header
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

#endif // DBTYPE_DEF_HPP
