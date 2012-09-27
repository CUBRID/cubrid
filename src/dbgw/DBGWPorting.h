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
#ifndef DBGWPORTING_H_
#define DBGWPORTING_H_

#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#if defined(WINDOWS)
#define usleep(usec)                    dbgw::system::sleepMilliSecond((usec)/1000)
#define snprintf                        _snprintf
#define strcasecmp(str1, str2)          _stricmp(str1, str2)
#define strncasecmp(str1, str2, size)   _strnicmp(str1, str2, size)
#define __thread                        __declspec( thread )
#define __func__                        __FUNCTION__
#define __FILENAME__                    (strrchr(__FILE__,'\\')+1)

typedef __int64   int64;

#else /* WINDOWS */
#define __FILENAME__                    __FILE__

typedef int64_t   int64;

#endif /* !WINDOWS */

namespace dbgw
{
#if !defined(WINDOWS)
  using namespace __gnu_cxx;
#endif /* !WINDOWS */

  class Mutex
  {
  public:
    Mutex();
    virtual ~Mutex();

    virtual void lock() = 0;
    virtual void unlock() = 0;

  private:

    Mutex(const Mutex &);
    void operator=(const Mutex &);
  };

  typedef shared_ptr<Mutex> MutexSharedPtr;

  class MutexFactory
  {
  public:
    static MutexSharedPtr create();

  private:
    ~MutexFactory();
  };

  class MutexLock
  {
  public:
    explicit MutexLock(MutexSharedPtr pMutex);
    ~MutexLock();
    void unlock();

  private:
    MutexSharedPtr m_pMutex;
    bool m_bUnlocked;

    MutexLock(const MutexLock &);
    void operator=(const MutexLock &);
  };

  namespace system
  {

#ifdef WINDOWS
#ifndef DBGW_ADAPTER_API
#define DECLSPECIFIER __declspec(dllexport)
#else
#define DECLSPECIFIER __declspec(dllimport)
#endif
#else
#define __stdcall
#define DECLSPECIFIER
#endif

    void sleepMilliSecond(unsigned int ms);

    const string getFileExtension(const string &fileName);

    class Directory
    {
    public:
      Directory(const char *szPath);
      virtual ~Directory();

    public:
      const char *getPath() const;
      virtual bool isDirectory() const = 0;
      virtual void getFileFullPathList(DBGWStringList &fileNameList) = 0;

    private:
      string m_path;
    };

    typedef shared_ptr<Directory> DirectorySharedPtr;

    class DirectoryFactory
    {
    public:
      static DirectorySharedPtr create(const char *szPath);

    private:
      virtual ~DirectoryFactory();
    };

  }

}

#endif				/* DBGWPORTING_H_ */
