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

    class Mutex
    {
    public:
      Mutex();
      virtual ~Mutex();

      virtual void lock() = 0;
      virtual void unlock() = 0;
      virtual void *get() = 0;

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

    class ConditionVariable
    {
    public:
      ConditionVariable();
      virtual ~ConditionVariable();

      virtual void notify() = 0;
      virtual void notifyAll() = 0;
      virtual void wait(MutexSharedPtr pMutex) = 0;
      virtual void timedWait(MutexSharedPtr pMutex, long lWaitTimeMilSec) = 0;
    };

    typedef shared_ptr<ConditionVariable> ConditionVariableSharedPtr;

    class ConditionVariableFactory
    {
    public:
      static ConditionVariableSharedPtr create();
    };

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
    };

    class Thread;

    typedef shared_ptr<Thread> ThreadSharedPtr;

    /**
     * By using shared_from_this(), guarantee that
     * ThreadData will be alive until thread is dead.
     */
    class ThreadData : public enable_shared_from_this<ThreadData>
    {
    };

    typedef shared_ptr<ThreadData> ThreadDataSharedPtr;

    enum ThreadStatus
    {
      THREAD_STATUS_STOP = 0,
      THREAD_STATUS_RUNNING,
      THREAD_STATUS_WAITING
    };

    enum ThreadOperation
    {
      THREAD_OP_NONE,
      THREAD_OP_START,
      THREAD_OP_STOP,
      THREAD_OP_DETACH
    };

    typedef void (*ThreadFunction)(const Thread *, ThreadDataSharedPtr);

#ifdef WINDOWS
#define THREAD_RETURN_TYPE unsigned int __stdcall
#define THREAD_RETURN_VALUE 0
#else
#define THREAD_RETURN_TYPE void *
#define THREAD_RETURN_VALUE NULL
#endif

    class Thread : public enable_shared_from_this<Thread>
    {
    public:
      static long MIN_SLEEP_TIME_MILSEC();

    public:
      Thread(ThreadFunction pFunc);
      virtual ~Thread();

      void start();
      void start(ThreadDataSharedPtr pData);
      void join();
      void timedJoin(int nWaitTimeMilSec);

    public:
      bool isRunning() const;
      bool sleep(long lMilSec) const;

    protected:
      static THREAD_RETURN_TYPE run(void *pData);
      virtual void doStart() = 0;
      virtual void doJoin() = 0;
      virtual void doDetach() = 0;

    private:
      void execute();
      void changeThreadStatus(ThreadStatus status);

    protected:
      ThreadStatus m_status;
      ThreadOperation m_op;
      ThreadDataSharedPtr m_pData;
      ThreadFunction m_pFunc;
      MutexSharedPtr m_pMutex;
      ConditionVariableSharedPtr m_pCond;
    };

    class ThreadFactory
    {
    public:
      static ThreadSharedPtr create(ThreadFunction pFunc);
    };

  }

}

#endif				/* DBGWPORTING_H_ */
