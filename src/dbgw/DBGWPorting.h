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
#include <Winsock2.h>

#ifndef DBGW_ADAPTER_API
#define DECLSPECIFIER __declspec(dllexport)
#else
#define DECLSPECIFIER __declspec(dllimport)
#endif

#define snprintf                        _snprintf
#define strcasecmp(str1, str2)          _stricmp(str1, str2)
#define strncasecmp(str1, str2, size)   _strnicmp(str1, str2, size)
#define __thread                        __declspec( thread )
#define __func__                        __FUNCTION__
#define __FILENAME__                    (strrchr(__FILE__,'\\')+1)
#define SLEEP_MILISEC(SEC, MSEC)        Sleep((SEC) * 1000 + (MSEC))

typedef __int64   int64;

extern int gettimeofday(struct timeval *tp, void *tzp);

#else /* WINDOWS */
#include <sys/time.h>

#define __stdcall
#define DECLSPECIFIER

#define __FILENAME__                    __FILE__
#define SLEEP_MILISEC(sec, msec)            \
  do {                                      \
    struct timeval sleep_time_val;          \
    sleep_time_val.tv_sec = sec;            \
    sleep_time_val.tv_usec = (msec) * 1000; \
    select(0, 0, 0, 0, &sleep_time_val);    \
  } while(0)

typedef int64_t   int64;

#endif /* !WINDOWS */

namespace dbgw
{

  namespace system
  {

    class _Mutex
    {
    public:
      _Mutex();
      virtual ~_Mutex();

      virtual void lock() = 0;
      virtual void unlock() = 0;
      virtual void *get() = 0;

    private:

      _Mutex(const _Mutex &);
      void operator=(const _Mutex &);
    };

    typedef shared_ptr<_Mutex> _MutexSharedPtr;

    class _MutexFactory
    {
    public:
      static _MutexSharedPtr create();

    private:
      ~_MutexFactory();
    };

    class _MutexLock
    {
    public:
      explicit _MutexLock(_MutexSharedPtr pMutex);
      ~_MutexLock();
      void unlock();

    private:
      _MutexSharedPtr m_pMutex;
      bool m_bUnlocked;

      _MutexLock(const _MutexLock &);
      void operator=(const _MutexLock &);
    };

    class _ConditionVariable
    {
    public:
      _ConditionVariable();
      virtual ~_ConditionVariable();

      virtual void notify() = 0;
      virtual void notifyAll() = 0;
      virtual void wait(_MutexSharedPtr pMutex) = 0;
      virtual void timedWait(_MutexSharedPtr pMutex, long lWaitTimeMilSec) = 0;
    };

    typedef shared_ptr<_ConditionVariable> _ConditionVariableSharedPtr;

    class _ConditionVariableFactory
    {
    public:
      static _ConditionVariableSharedPtr create();
    };

    const string getFileExtension(const string &fileName);

    class _Directory
    {
    public:
      _Directory(const char *szPath);
      virtual ~_Directory();

    public:
      const char *getPath() const;
      virtual bool isDirectory() const = 0;
      virtual void getFileFullPathList(DBGWStringList &fileNameList) = 0;

    private:
      string m_path;
    };

    typedef shared_ptr<_Directory> _DirectorySharedPtr;

    class _DirectoryFactory
    {
    public:
      static _DirectorySharedPtr create(const char *szPath);
    };

    class _Thread;

    typedef shared_ptr<_Thread> _ThreadSharedPtr;

    /**
     * By using shared_from_this(), guarantee that
     * ThreadData will be alive until thread is dead.
     */
    class _ThreadData : public enable_shared_from_this<_ThreadData>
    {
    };

    typedef shared_ptr<_ThreadData> _ThreadDataSharedPtr;

    enum _ThreadStatus
    {
      THREAD_STATUS_STOP = 0,
      THREAD_STATUS_RUNNING,
      THREAD_STATUS_WAITING
    };

    enum _ThreadOperation
    {
      THREAD_OP_NONE,
      THREAD_OP_START,
      THREAD_OP_STOP,
      THREAD_OP_DETACH
    };

    typedef void (*_ThreadFunction)(const _Thread *, _ThreadDataSharedPtr);

#ifdef WINDOWS
#define _THREAD_RETURN_TYPE unsigned int __stdcall
#define _THREAD_RETURN_VALUE 0
#else
#define _THREAD_RETURN_TYPE void *
#define _THREAD_RETURN_VALUE NULL
#endif

    class _Thread : public enable_shared_from_this<_Thread>
    {
    public:
      static long MIN_SLEEP_TIME_MILSEC();

    public:
      _Thread(_ThreadFunction pFunc);
      virtual ~_Thread();

      void start();
      void start(_ThreadDataSharedPtr pData);
      void join();
      void timedJoin(int nWaitTimeMilSec);

    public:
      bool isRunning() const;
      bool sleep(long lMilSec) const;

    protected:
      static _THREAD_RETURN_TYPE run(void *pData);
      virtual void doStart() = 0;
      virtual void doJoin() = 0;
      virtual void doDetach() = 0;

    private:
      void execute();
      void changeThreadStatus(_ThreadStatus status);

    protected:
      _ThreadStatus m_status;
      _ThreadOperation m_op;
      _ThreadDataSharedPtr m_pData;
      _ThreadFunction m_pFunc;
      _MutexSharedPtr m_pMutex;
      _ConditionVariableSharedPtr m_pCond;
    };

    class _ThreadFactory
    {
    public:
      static _ThreadSharedPtr create(_ThreadFunction pFunc);
    };

  }

}

#endif				/* DBGWPORTING_H_ */
