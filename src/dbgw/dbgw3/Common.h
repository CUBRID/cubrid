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

#ifndef COMMON_H_
#define COMMON_H_

#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include <stack>
#include <set>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <exception>
#include <memory>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_array.hpp>
#include <boost/unordered_map.hpp>
#include "cci_log.h"

#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#include <sys/timeb.h>
typedef __int64                         int64;
typedef unsigned long long int          uint64_t;
#define __func__                        __FUNCTION__
#define __FILENAME__                    (strrchr(__FILE__,'\\')+1)
#define __thread                        __declspec( thread )
#define SLEEP_MILISEC(SEC, MSEC)        Sleep((SEC) * 1000 + (MSEC))
#define snprintf                        _snprintf

int pthread_detach(void *);
#else
#include <sys/time.h>
typedef int64_t                         int64;
#define __FILENAME__                    __FILE__
#define SLEEP_MILISEC(sec, msec)            \
  do {                                      \
    struct timeval sleep_time_val;          \
    sleep_time_val.tv_sec = sec;            \
    sleep_time_val.tv_usec = (msec) * 1000; \
    select(0, 0, 0, 0, &sleep_time_val);    \
  } while(0)
#endif

namespace dbgw
{

  template<typename T>
  struct trait
  {
    typedef std::vector<T> vector;
    typedef std::list<T> list;
    typedef boost::shared_ptr<T> sp;
    typedef std::vector<boost::shared_ptr<T> > spvector;
    typedef std::list<boost::shared_ptr<T> > splist;
  };

  namespace func
  {

    struct compareConstChar
    {
      bool operator()(const char *s1, const char *s2) const
      {
        return strcmp(s1, s2) == 0;
      }
    };

    struct compareString
    {
      bool operator()(const std::string &s1, const std::string &s2) const
      {
        return s1.compare(s2) == 0;
      }
    };

    struct compareInt
    {
      bool operator()(int n1, int n2) const
      {
        return n1 == n2;
      }
    };

  }

}

#endif
