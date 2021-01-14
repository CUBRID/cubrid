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
  inline log_lsa &operator= (const log_lsa &olsa) = default;

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
  offset = NULL_LOG_OFFSET;   // this is how LOG_LSA is initialized many times; we need to initialize both fields or
  // we'll have "conditional jump or move on uninitialized value"
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
