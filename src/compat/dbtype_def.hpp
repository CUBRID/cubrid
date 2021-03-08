#ifndef DBTYPE_DEF_HPP
#define DBTYPE_DEF_HPP

#include "dbtype_def.h"

/* C++ extensions for strict-C structures defined in corresponding '.h' header
 */

inline constexpr bool operator== (const VPID &_left, const VPID &_rite)
{
  return (_left.volid == _rite.volid && _left.pageid == _rite.pageid);
}

inline constexpr bool operator!= (const VPID &_left, const VPID &_rite)
{
  return ! (_left == _rite);
}

inline constexpr bool operator< (const VPID &_left, const VPID &_rite)
{
  return (_left != _rite)
	 && (
		 (_left.volid < _rite.volid)
		 || (_left.volid == _rite.volid && _left.pageid < _rite.pageid)
	 );
}

#endif // DBTYPE_DEF_HPP
