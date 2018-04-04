/* Copyright (C) 2002-2013 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

/*
 * perf.cpp - implementation of performance statistics basic utilities
 */

#include "perf.hpp"

namespace cubperf
{
  //////////////////////////////////////////////////////////////////////////
  // stat_def
  //////////////////////////////////////////////////////////////////////////
  stat_def::stat_def (stat_id &idref, type stat_type, const char *first_name, const char *second_name /* = NULL */)
    : m_idr (idref)
    , m_type (stat_type)
    , m_names { first_name, second_name }
  {
    //
  }

  stat_def::stat_def (const stat_def &other)
    : m_idr (other.m_idr)
    , m_type (other.m_type)
    , m_names { other.m_names[0], other.m_names[1] }
  {
    //
  }

  std::size_t
  stat_def::get_value_count (void)
  {
    return m_type == type::COUNTER_AND_TIMER ? 2 : 1;
  }

  //////////////////////////////////////////////////////////////////////////
  // stat_factory
  //////////////////////////////////////////////////////////////////////////
  void
  stat_factory::build (std::size_t &crt_offset, stat_def &def)
  {

  }

  void
  stat_factory::preprocess_def (stat_def &def)
  {

  }

  void
  stat_factory::postprocess_def (std::size_t &crt_offset, stat_def &def)
  {

  }

} // namespace cubperf
