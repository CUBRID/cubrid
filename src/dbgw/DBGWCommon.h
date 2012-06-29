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

#ifndef DBGWCOMMON_H_
#define DBGWCOMMON_H_

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
#include <tr1/memory>
#include <ext/hash_map>
#include <cci_log.h>
#include <cas_cci.h>

namespace __gnu_cxx
{
  template<> struct hash<std::string>
  {
    size_t operator()(const std::string &x) const
    {
      return hash<const char *> ()(x.c_str());
    }
  };
}

namespace dbgw
{

  using namespace __gnu_cxx;
  using namespace std;
  using namespace std::tr1;

  struct dbgwConstCharCompareFunc
  {
    bool operator()(const char *s1, const char *s2) const
    {
      return strcmp(s1, s2) == 0;
    }
  };

  struct dbgwStringCompareFunc
  {
    bool operator()(const string &s1, const string &s2) const
    {
      return s1.compare(s2) == 0;
    }
  };

  struct dbgwIntCompareFunc
  {
    bool operator()(int n1, int n2) const
    {
      return n1 == n2;
    }
  };

  typedef vector<string> DBGWStringList;
  typedef int64_t int64;

  class Mutex
  {
  public:
    Mutex();
    ~ Mutex();

    void lock();
    void unlock();

  private:
    pthread_mutex_t m_stMutex;

    Mutex(const Mutex &);
    void operator=(const Mutex &);
  };

  class MutexLock
  {
  public:
    explicit MutexLock(Mutex *pMutex);
    ~MutexLock();
    void unlock();

  private:
    Mutex *m_pMutex;
    bool m_bUnlocked;

    MutexLock(const MutexLock &);
    void operator=(const MutexLock &);
  };

}

#endif				/* DBGWHASHMAP_H_ */
