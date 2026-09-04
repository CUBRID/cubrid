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

#include "porting.h"

#include <cassert>
#include <cstring>
#include <cinttypes>
#include <cstddef>
#include <type_traits>

struct log_lsa
{
  std::int64_t pageid:48;		/* Log page identifier : 6 bytes length */
  std::int64_t offset:16;		/* Offset in page : 2 bytes length.
                                          offset == 'area offset' */
  /* The offset field is defined as 16bit-INT64 type (not short), because of alignment */

  inline log_lsa () = default;
  inline constexpr log_lsa (std::int64_t log_pageid, std::int16_t log_offset)
    : pageid (log_pageid)
    , offset (log_offset)
  {
  }
  inline log_lsa (const log_lsa &olsa) = default;
  inline log_lsa &operator= (const log_lsa &olsa) = default;

  constexpr inline bool is_null () const;
  constexpr inline bool is_max () const;
  inline void set_null ();

  constexpr inline bool operator== (const log_lsa &olsa) const;
  inline bool operator!= (const log_lsa &olsa) const;
  inline bool operator< (const log_lsa &olsa) const;
  inline bool operator<= (const log_lsa &olsa) const;
  inline bool operator> (const log_lsa &olsa) const;
  inline bool operator>= (const log_lsa &olsa) const;
};

using LOG_LSA = log_lsa;	/* Log address identifier */

/*
 * An LSA that one thread advances under its own lock while other threads read it without that lock.
 * Every access moves the whole 64-bit word at once, so a reader never composes pageid and offset from two moments,
 * and a two-field update never exposes an intermediate value. The bit-fields are reachable only through load ()/store ().
 * The layout is that of log_lsa: a struct holding it keeps its disk image and stays trivially copyable.
 */
struct log_lsa_atomic
{
  log_lsa m_lsa;

  inline log_lsa_atomic () = default;
  inline log_lsa_atomic (const log_lsa &lsa);

  inline log_lsa load () const;
  inline void store (const log_lsa &lsa);
  inline void advance (int add);	/* offset += add, published as one store */
  inline operator log_lsa () const;
};

using LOG_LSA_ATOMIC = log_lsa_atomic;

static_assert (sizeof (log_lsa_atomic) == sizeof (log_lsa), "log_lsa_atomic must keep the log_lsa layout");
static_assert (alignof (log_lsa_atomic) == alignof (log_lsa), "log_lsa_atomic must keep the log_lsa alignment");
static_assert (std::is_trivially_copyable<log_lsa_atomic>::value, "log_lsa_atomic must stay trivially copyable");
static_assert (std::is_standard_layout<log_lsa_atomic>::value, "log_lsa_atomic must stay standard layout");

constexpr std::int64_t NULL_LOG_PAGEID = -1;
constexpr std::int16_t NULL_LOG_OFFSET = -1;
constexpr log_lsa NULL_LSA { NULL_LOG_PAGEID, NULL_LOG_OFFSET };

// maximum representable log lsa value based on bit-field members
constexpr std::int64_t MAX_LOG_LSA_PAGEID = (static_cast<std::int64_t> (1u) << (48u - 1)) - 1;
constexpr std::int16_t MAX_LOG_LSA_OFFSET = (static_cast<std::int16_t> (1u) << (16u - 1)) - 1;
constexpr log_lsa MAX_LSA = { MAX_LOG_LSA_PAGEID, MAX_LOG_LSA_OFFSET };

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

constexpr bool
log_lsa::is_null () const
{
  return pageid == NULL_LOG_PAGEID;
}

constexpr bool
log_lsa::is_max () const
{
  return *this == MAX_LSA;
}

void
log_lsa::set_null ()
{
  pageid = NULL_LOG_PAGEID;
  offset = NULL_LOG_OFFSET;   // this is how LOG_LSA is initialized many times; we need to initialize both fields or
  // we'll have "conditional jump or move on uninitialized value"
}

constexpr bool
log_lsa::operator== (const log_lsa &olsa) const
{
  return pageid == olsa.pageid && offset == olsa.offset;
}

bool
log_lsa::operator!= (const log_lsa &olsa) const
{
  return ! (*this == olsa);
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

log_lsa_atomic::log_lsa_atomic (const log_lsa &lsa)
  : m_lsa (lsa)
{
}

log_lsa
log_lsa_atomic::load () const
{
  return ATOMIC_LOAD_64_ACQUIRE (&m_lsa);
}

void
log_lsa_atomic::store (const log_lsa &lsa)
{
  ATOMIC_STORE_64_RELEASE (&m_lsa, lsa);
}

void
log_lsa_atomic::advance (int add)
{
  log_lsa lsa = load ();
  lsa.offset += add;
  store (lsa);
}

log_lsa_atomic::operator log_lsa () const
{
  return load ();
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
