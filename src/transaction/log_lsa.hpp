/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// log_lsa.hpp - log sequence address header
//

#ifndef _LOG_LSA_HPP_
#define _LOG_LSA_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE) && !defined (CS_MODE)
#define Wrong module
#endif

#include <cassert>
#include <cinttypes>
#include <cstddef>

struct log_lsa
{
  std::int64_t pageid:48;		/* Log page identifier : 6 bytes length */
  std::int64_t offset:16;		/* Offset in page : 2 bytes length */
  /* The offset field is defined as 16bit-INT64 type (not short), because of alignment */

  inline log_lsa () = default;
  inline log_lsa (std::int64_t log_pageid, std::int16_t log_offset);
  inline log_lsa (const log_lsa &olsa) = default;

  inline bool is_null () const;
  inline void set_null ();

  inline bool operator== (const log_lsa &olsa) const;
  inline bool operator< (const log_lsa &olsa) const;
  inline bool operator<= (const log_lsa &olsa) const;
  inline bool operator> (const log_lsa &olsa) const;
  inline bool operator>= (const log_lsa &olsa) const;
};

using LOG_LSA = log_lsa;	/* Log address identifier */

static const std::int64_t NULL_LOG_PAGEID = -1;
static const std::int16_t NULL_LOG_OFFSET = -1;
const log_lsa NULL_LSA = { NULL_LOG_PAGEID, NULL_LOG_OFFSET };

// functions
void lsa_to_string (char *buf, int buf_size, const log_lsa *lsa);

//
// macro replacements
//
inline void LSA_COPY (log_lsa *plsa1, const log_lsa *plsa2);
inline void LSA_SET_NULL (log_lsa *lsa_ptr);
inline bool LSA_ISNULL (const log_lsa *lsa_ptr);
inline bool LSA_EQ (const log_lsa *plsa1, const log_lsa *plsa2);
inline bool LSA_LE (const log_lsa *plsa1, const log_lsa *plsa2);
inline bool LSA_LT (const log_lsa *plsa1, const log_lsa *plsa2);
inline bool LSA_GE (const log_lsa *plsa1, const log_lsa *plsa2);
inline bool LSA_GT (const log_lsa *plsa1, const log_lsa *plsa2);

#define LSA_INITIALIZER	{NULL_LOG_PAGEID, NULL_LOG_OFFSET}

#define LSA_AS_ARGS(lsa_ptr) (long long int) (lsa_ptr)->pageid, (int) (lsa_ptr)->offset

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

log_lsa::log_lsa (std::int64_t log_pageid, std::int16_t log_offset)
  : pageid (log_pageid)
  , offset (log_offset)
{
  //
}

bool
log_lsa::is_null () const
{
  return pageid == NULL_LOG_PAGEID;
}

void
log_lsa::set_null ()
{
  pageid = NULL_LOG_PAGEID;
}

bool
log_lsa::operator== (const log_lsa &olsa) const
{
  return pageid == olsa.pageid && offset == olsa.offset;
}

bool
log_lsa::operator< (const log_lsa &olsa) const
{
  return (pageid < olsa.pageid) || (pageid == olsa.pageid && offset < olsa.offset);
}

bool
log_lsa::operator> (const log_lsa &olsa) const
{
  return olsa.operator< (*this);
}

bool
log_lsa::operator<= (const log_lsa &olsa) const
{
  return !operator> (olsa);
}

bool
log_lsa::operator>= (const log_lsa &olsa) const
{
  return !operator< (olsa);
}

//
// macro replacements
//
void
LSA_COPY (log_lsa *plsa1, const log_lsa *plsa2)
{
  assert (plsa1 != NULL && plsa2 != NULL);
  *plsa1 = *plsa2;
}

void
LSA_SET_NULL (log_lsa *lsa_ptr)
{
  assert (lsa_ptr != NULL);
  lsa_ptr->set_null ();
}

bool
LSA_ISNULL (const log_lsa *lsa_ptr)
{
  assert (lsa_ptr != NULL);
  return lsa_ptr->is_null ();
}

bool
LSA_EQ (const log_lsa *plsa1, const log_lsa *plsa2)
{
  assert (plsa1 != NULL && plsa2 != NULL);
  return *plsa1 == *plsa2;
}

bool
LSA_LE (const log_lsa *plsa1, const log_lsa *plsa2)
{
  assert (plsa1 != NULL && plsa2 != NULL);
  return *plsa1 <= *plsa2;
}

bool
LSA_LT (const log_lsa *plsa1, const log_lsa *plsa2)
{
  assert (plsa1 != NULL && plsa2 != NULL);
  return *plsa1 < *plsa2;
}

bool
LSA_GE (const log_lsa *plsa1, const log_lsa *plsa2)
{
  assert (plsa1 != NULL && plsa2 != NULL);
  return *plsa1 >= *plsa2;
}

bool
LSA_GT (const log_lsa *plsa1, const log_lsa *plsa2)
{
  assert (plsa1 != NULL && plsa2 != NULL);
  return *plsa1 > *plsa2;
}

#endif  // _LOG_LSA_HPP_
