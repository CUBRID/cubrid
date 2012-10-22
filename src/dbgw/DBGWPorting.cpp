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
#include <sys/types.h>
#include <sys/stat.h>
#include "DBGWCommon.h"
#include "DBGWLogger.h"
#include "DBGWError.h"
#include "DBGWPorting.h"
#if defined(WINDOWS)
#include <windows.h>
#include <errno.h>
#include <process.h>
#include <sys/timeb.h>

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)

int
gettimeofday(struct timeval *tp, void *tzp)
{
  struct _timeb tm;
  _ftime(&tm);
  tp->tv_sec = (long) tm.time;
  tp->tv_usec = (long) tm.millitm * 1000;
  return 0;
}
#else
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#endif

namespace dbgw
{

  namespace system
  {

    Mutex::Mutex()
    {
    }

    Mutex::~Mutex()
    {
    }

#if defined(WINDOWS)
    typedef struct
    {
      CRITICAL_SECTION cs;
      CRITICAL_SECTION *csp;
    } pthread_mutex_t;

    class WindowsMutex : public Mutex
    {
    public:
      WindowsMutex();
      virtual ~WindowsMutex();

      void lock();
      void unlock();
      void *get();

    private:
      pthread_mutex_t m_mutex_t;

      WindowsMutex(const WindowsMutex &);
      void operator=(const WindowsMutex &);
    };

    WindowsMutex::WindowsMutex()
    {
      m_mutex_t.csp = &m_mutex_t.cs;
      InitializeCriticalSection(m_mutex_t.csp);
    }

    WindowsMutex::~WindowsMutex()
    {
      if (m_mutex_t.csp != &m_mutex_t.cs)
        {
          if (m_mutex_t.csp == NULL)
            {
              return;
            }

          m_mutex_t.csp = NULL;
          return;
        }

      DeleteCriticalSection(m_mutex_t.csp);
      m_mutex_t.csp = NULL;
    }

    void WindowsMutex::lock()
    {
      EnterCriticalSection(m_mutex_t.csp);
    }

    void WindowsMutex::unlock()
    {
      if (m_mutex_t.csp->LockCount == -1)
        {
          return;
        }

      LeaveCriticalSection(m_mutex_t.csp);
    }

    void *WindowsMutex::get()
    {
      return m_mutex_t.csp;
    }
#else /* WINDOWS */
    class PosixMutex : public Mutex
    {
    public:
      PosixMutex();
      virtual ~PosixMutex();

      void lock();
      void unlock();
      void *get();

    private:
      pthread_mutex_t m_stMutex;

      PosixMutex(const PosixMutex &);
      void operator=(const PosixMutex &);
    };

    PosixMutex::PosixMutex()
    {
      int nStatus = pthread_mutex_init(&m_stMutex, NULL);
      if (nStatus != 0)
        {
          MutexOperationFailException e("init", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    PosixMutex::~PosixMutex()
    {
      int nStatus = pthread_mutex_destroy(&m_stMutex);
      if (nStatus == EBUSY)
        {
          nStatus = pthread_mutex_destroy(&m_stMutex);
        }

      if (nStatus != 0)
        {
          MutexOperationFailException e("destroy", nStatus);
          DBGW_LOG_ERROR(e.what());
        }
    }

    void PosixMutex::lock()
    {
      int nStatus = pthread_mutex_lock(&m_stMutex);
      if (nStatus != 0)
        {
          MutexOperationFailException e("lock", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void PosixMutex::unlock()
    {
      int nStatus = pthread_mutex_unlock(&m_stMutex);
      if (nStatus != 0)
        {
          MutexOperationFailException e("unlock", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void *PosixMutex::get()
    {
      return &m_stMutex;
    }
#endif /* !WINDOWS */

    MutexSharedPtr MutexFactory::create()
    {
      MutexSharedPtr p;
#if defined(WINDOWS)
      p = MutexSharedPtr(new WindowsMutex());
#else
      p = MutexSharedPtr(new PosixMutex());
#endif
      return p;
    }

    MutexLock::MutexLock(MutexSharedPtr pMutex) :
      m_pMutex(pMutex), m_bUnlocked(false)
    {
      m_pMutex->lock();
    }

    MutexLock::~MutexLock()
    {
      unlock();
    }

    void MutexLock::unlock()
    {
      if (m_bUnlocked == false)
        {
          m_bUnlocked = true;
          m_pMutex->unlock();
        }
    }

    ConditionVariable::ConditionVariable()
    {
    }

    ConditionVariable::~ConditionVariable()
    {
    }

#ifdef WINDOWS
    typedef void (WINAPI *InitializeConditionVariable_t)(CONDITION_VARIABLE *);
    typedef bool (WINAPI *SleepConditionVariableCS_t)(CONDITION_VARIABLE *,
        CRITICAL_SECTION *,
        DWORD dwMilliseconds);

    typedef void (WINAPI *WakeAllConditionVariable_t)(CONDITION_VARIABLE *);
    typedef void (WINAPI *WakeConditionVariable_t)(CONDITION_VARIABLE *);

    InitializeConditionVariable_t fp_InitializeConditionVariable;
    SleepConditionVariableCS_t fp_SleepConditionVariableCS;
    WakeAllConditionVariable_t fp_WakeAllConditionVariable;
    WakeConditionVariable_t fp_WakeConditionVariable;

    static bool g_have_CONDITION_VARIABLE = false;

    enum
    {
      COND_SIGNAL = 0,
      COND_BROADCAST = 1,
      MAX_EVENTS = 2
    } EVENTS;

    typedef union
    {
      CONDITION_VARIABLE native_cond;

      struct
      {
        unsigned int waiting;
        CRITICAL_SECTION lock_waiting;
        HANDLE events[MAX_EVENTS];
        HANDLE broadcast_block_event;
      };
    } pthread_cond_t;

    static void
    check_CONDITION_VARIABLE(void)
    {
      HMODULE kernel32 = GetModuleHandle("kernel32");

      g_have_CONDITION_VARIABLE = true;
      fp_InitializeConditionVariable = (InitializeConditionVariable_t)
          GetProcAddress(kernel32, "InitializeConditionVariable");
      if (fp_InitializeConditionVariable == NULL)
        {
          g_have_CONDITION_VARIABLE = false;
          return;
        }

      fp_SleepConditionVariableCS = (SleepConditionVariableCS_t)
          GetProcAddress(kernel32, "SleepConditionVariableCS");
      fp_WakeAllConditionVariable = (WakeAllConditionVariable_t)
          GetProcAddress(kernel32, "WakeAllConditionVariable");
      fp_WakeConditionVariable = (WakeConditionVariable_t)
          GetProcAddress(kernel32, "WakeConditionVariable");
    }

    class WindowsXpConditionVariable : public ConditionVariable
    {
    public:
      WindowsXpConditionVariable();
      virtual ~WindowsXpConditionVariable();

      void notify();
      void notifyAll();
      void wait(MutexSharedPtr pMutex);
      void timedWait(MutexSharedPtr pMutex, long lWaitTimeMilSec);

    private:
      pthread_cond_t m_cond_t;
    };

    WindowsXpConditionVariable::WindowsXpConditionVariable()
    {
      m_cond_t.waiting = 0;
      InitializeCriticalSection(&m_cond_t.lock_waiting);

      m_cond_t.events[COND_SIGNAL] = CreateEvent(NULL, FALSE, FALSE, NULL);
      m_cond_t.events[COND_BROADCAST] = CreateEvent(NULL, TRUE, FALSE, NULL);
      m_cond_t.broadcast_block_event = CreateEvent(NULL, TRUE, TRUE, NULL);

      if (m_cond_t.events[COND_SIGNAL] == NULL ||
          m_cond_t.events[COND_BROADCAST] == NULL ||
          m_cond_t.broadcast_block_event == NULL)
        {
          CondVarOperationFailException e("init", ENOMEM);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    WindowsXpConditionVariable::~WindowsXpConditionVariable()
    {
      DeleteCriticalSection(&m_cond_t.lock_waiting);

      if (CloseHandle(m_cond_t.events[COND_SIGNAL]) == 0
          || CloseHandle(m_cond_t.events[COND_BROADCAST]) == 0
          || CloseHandle(m_cond_t.broadcast_block_event) == 0)
        {
          CondVarOperationFailException e("destroy", EINVAL);
          DBGW_LOG_ERROR(e.what());
        }
    }

    void WindowsXpConditionVariable::notify()
    {
      EnterCriticalSection(&m_cond_t.lock_waiting);

      if (m_cond_t.waiting > 0)
        {
          SetEvent(m_cond_t.events[COND_SIGNAL]);
        }

      LeaveCriticalSection(&m_cond_t.lock_waiting);
    }

    void WindowsXpConditionVariable::notifyAll()
    {
      EnterCriticalSection(&m_cond_t.lock_waiting);

      if (m_cond_t.waiting > 0)
        {
          ResetEvent(m_cond_t.broadcast_block_event);
          SetEvent(m_cond_t.events[COND_BROADCAST]);
        }

      LeaveCriticalSection(&m_cond_t.lock_waiting);
    }

    void WindowsXpConditionVariable::wait(MutexSharedPtr pMutex)
    {
      timedWait(pMutex, INFINITE);
    }

    void WindowsXpConditionVariable::timedWait(MutexSharedPtr pMutex,
        long lWaitTimeMilSec)
    {
      int nResult;
      pthread_mutex_t *mutex_t = (pthread_mutex_t *) pMutex->get();

      WaitForSingleObject(m_cond_t.broadcast_block_event, INFINITE);

      EnterCriticalSection(&m_cond_t.lock_waiting);
      m_cond_t.waiting++;
      LeaveCriticalSection(&m_cond_t.lock_waiting);

      LeaveCriticalSection(mutex_t->csp);
      nResult = WaitForMultipleObjects(2, m_cond_t.events, FALSE, lWaitTimeMilSec);
      assert(nResult == WAIT_TIMEOUT || nResult <= 2);

      EnterCriticalSection(&m_cond_t.lock_waiting);
      m_cond_t.waiting--;

      if (m_cond_t.waiting == 0)
        {
          ResetEvent(m_cond_t.events[COND_BROADCAST]);
          SetEvent(m_cond_t.broadcast_block_event);
        }

      LeaveCriticalSection(&m_cond_t.lock_waiting);
      EnterCriticalSection(mutex_t->csp);

      if (nResult != 0)
        {
          CondVarOperationFailException e("wait", nResult);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    class WindowsVistaConditionVariable : public ConditionVariable
    {
    public:
      WindowsVistaConditionVariable();
      virtual ~WindowsVistaConditionVariable();

      void notify();
      void notifyAll();
      void wait(MutexSharedPtr pMutex);
      void timedWait(MutexSharedPtr pMutex, long lWaitTimeMilSec);

    private:
      pthread_cond_t m_cond_t;
    };

    WindowsVistaConditionVariable::WindowsVistaConditionVariable()
    {
      fp_InitializeConditionVariable(&m_cond_t.native_cond);
    }

    WindowsVistaConditionVariable::~WindowsVistaConditionVariable()
    {
    }

    void WindowsVistaConditionVariable::notify()
    {
      fp_WakeConditionVariable(&m_cond_t.native_cond);
    }

    void WindowsVistaConditionVariable::notifyAll()
    {
      fp_WakeAllConditionVariable(&m_cond_t.native_cond);
    }

    void WindowsVistaConditionVariable::wait(MutexSharedPtr pMutex)
    {
      fp_SleepConditionVariableCS(&m_cond_t.native_cond,
          (CRITICAL_SECTION *) pMutex->get(), INFINITE);
    }

    void WindowsVistaConditionVariable::timedWait(MutexSharedPtr pMutex,
        long lWaitTimeMilSec)
    {
      if (fp_SleepConditionVariableCS(&m_cond_t.native_cond,
          (CRITICAL_SECTION *) pMutex->get(), lWaitTimeMilSec) == false)
        {
          CondVarOperationFailException e("timed wait", WAIT_TIMEOUT);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }
#else
    class PosixConditionVariable : public ConditionVariable
    {
    public:
      PosixConditionVariable();
      virtual ~PosixConditionVariable();

      void notify();
      void notifyAll();
      void wait(MutexSharedPtr pMutex);
      void timedWait(MutexSharedPtr pMutex, long lWaitTimeMilSec);

    private:
      pthread_cond_t m_cond_t;
    };

    PosixConditionVariable::PosixConditionVariable()
    {
      int nStatus = pthread_cond_init(&m_cond_t, NULL);
      if (nStatus != 0)
        {
          CondVarOperationFailException e("init", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    PosixConditionVariable::~PosixConditionVariable()
    {
      int nStatus = pthread_cond_destroy(&m_cond_t);
      if (nStatus == EBUSY)
        {
          notifyAll();
          nStatus = pthread_cond_destroy(&m_cond_t);
        }

      if (nStatus != 0)
        {
          CondVarOperationFailException e("destroy", nStatus);
          DBGW_LOG_ERROR(e.what());
        }
    }

    void PosixConditionVariable::notify()
    {
      int nStatus = pthread_cond_signal(&m_cond_t);
      if (nStatus != 0)
        {
          CondVarOperationFailException e("signal", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void PosixConditionVariable::notifyAll()
    {
      int nStatus = pthread_cond_broadcast(&m_cond_t);
      if (nStatus != 0)
        {
          CondVarOperationFailException e("broadcast", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void PosixConditionVariable::wait(MutexSharedPtr pMutex)
    {
      int nStatus = pthread_cond_wait(&m_cond_t,
          (pthread_mutex_t *) pMutex->get());
      if (nStatus != 0)
        {
          CondVarOperationFailException e("wait", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void PosixConditionVariable::timedWait(MutexSharedPtr pMutex,
        long lWaitTimeMilSec)
    {
      struct timespec ts;

      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += lWaitTimeMilSec / 1000;
      ts.tv_nsec += (lWaitTimeMilSec % 1000) * 1000 * 1000;

      int nStatus = pthread_cond_timedwait(&m_cond_t,
          (pthread_mutex_t *) pMutex->get(), &ts);
      if (nStatus != 0)
        {
          CondVarOperationFailException e("timed wait", nStatus);
          if (nStatus != ETIMEDOUT)
            {
              DBGW_LOG_ERROR(e.what());
            }
          throw e;
        }
    }
#endif

    ConditionVariableSharedPtr ConditionVariableFactory::create()
    {
      ConditionVariableSharedPtr p;
#if defined(WINDOWS)
      static bool bChecked = false;
      if (bChecked == false)
        {
          check_CONDITION_VARIABLE();
          bChecked = true;
        }

      if (g_have_CONDITION_VARIABLE)
        {
          p = ConditionVariableSharedPtr(new WindowsVistaConditionVariable());
        }
      else
        {
          p = ConditionVariableSharedPtr(new WindowsXpConditionVariable());
        }
#else
      p = ConditionVariableSharedPtr(new PosixConditionVariable());
#endif
      return p;
    }

    const string getFileExtension(const string &fileName)
    {
      return fileName.substr(fileName.find_last_of(".") + 1);
    }

    Directory::Directory(const char *szPath) :
      m_path(szPath)
    {
    }

    Directory::~Directory()
    {
    }

    const char *Directory::getPath() const
    {
      return m_path.c_str();
    }

#if defined(WINDOWS)
    class WindowsDirectory : public Directory
    {
    public:
      WindowsDirectory(const char *szPath);
      virtual ~WindowsDirectory();

    public:
      bool isDirectory() const;
      void getFileFullPathList(DBGWStringList &fileNameList);
    };

    WindowsDirectory::WindowsDirectory(const char *szPath) :
      Directory(szPath)
    {
    }

    WindowsDirectory::~WindowsDirectory()
    {
    }

    bool WindowsDirectory::isDirectory() const
    {
      string path = getPath();

      struct stat pathInfo;
      int nError = stat(path.c_str(), &pathInfo);
      if (nError < 0)
        {
          NotExistConfFileException e(path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      return S_ISDIR(pathInfo.st_mode);
    }

    void WindowsDirectory::getFileFullPathList(DBGWStringList &fileNameList)
    {
      string path = getPath();

      HANDLE hSrc;
      WIN32_FIND_DATA wfd;

      hSrc = FindFirstFile(path.c_str(), &wfd);

      if (hSrc == INVALID_HANDLE_VALUE)
        {
          NotExistConfFileException e(path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      BOOL pResult = true;

      while (pResult != false)
        {
          fileNameList.push_back(path + "/" + wfd.cFileName);
          pResult = FindNextFile(hSrc, &wfd);
        }

      FindClose(hSrc);
    }
#else /* WINDOWS */
    class PosixDirectory : public Directory
    {
    public:
      PosixDirectory(const char *szPath);
      virtual ~PosixDirectory();

    public:
      bool isDirectory() const;
      void getFileFullPathList(DBGWStringList &fileNameList);
    };

    PosixDirectory::PosixDirectory(const char *szPath) :
      Directory(szPath)
    {
    }

    PosixDirectory::~PosixDirectory()
    {
    }

    bool PosixDirectory::isDirectory() const
    {
      string path = getPath();

      struct stat pathInfo;
      int nError = stat(path.c_str(), &pathInfo);
      if (nError < 0)
        {
          NotExistConfFileException e(path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      return S_ISDIR(pathInfo.st_mode);
    }

    void PosixDirectory::getFileFullPathList(DBGWStringList &fileNameList)
    {
      string path = getPath();

      DIR *pDir = opendir(path.c_str());
      if (pDir == NULL)
        {
          NotExistConfFileException e(path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      struct dirent entry;
      struct dirent *pResult = NULL;
      readdir_r(pDir, &entry, &pResult);

      while (pResult != NULL)
        {
          fileNameList.push_back(path + "/" + entry.d_name);
          readdir_r(pDir, &entry, &pResult);
        }

      closedir(pDir);
    }
#endif /* !WINDOWS */

    DirectorySharedPtr DirectoryFactory::create(const char *szPath)
    {
      DirectorySharedPtr p;
#if defined(WINDOWS)
      p = DirectorySharedPtr(new WindowsDirectory(szPath));
#else
      p = DirectorySharedPtr(new PosixDirectory(szPath));
#endif
      return p;
    }

    long Thread::MIN_SLEEP_TIME_MILSEC()
    {
      return 1;
    }

    Thread::Thread(ThreadFunction pFunc) :
      m_status(THREAD_STATUS_STOP), m_op(THREAD_OP_NONE),
      m_pFunc(pFunc), m_pMutex(MutexFactory::create()),
      m_pCond(ConditionVariableFactory::create())
    {
    }

    Thread::~Thread()
    {
    }

    void Thread::start()
    {
      start(ThreadDataSharedPtr());
    }

    void Thread::start(ThreadDataSharedPtr pData)
    {
      MutexLock lock(m_pMutex);

      m_pData = pData;

      if (m_op != THREAD_OP_NONE || m_status != THREAD_STATUS_STOP)
        {
          return;
        }

      m_op = THREAD_OP_START;

      lock.unlock();

      doStart();

      changeThreadStatus(THREAD_STATUS_RUNNING);
    }

    void Thread::join()
    {
      MutexLock lock(m_pMutex);

      if (m_op != THREAD_OP_NONE || m_status != THREAD_STATUS_RUNNING)
        {
          return;
        }

      m_op = THREAD_OP_STOP;
      m_status = THREAD_STATUS_WAITING;

      m_pCond->wait(m_pMutex);

      lock.unlock();

      doJoin();

      changeThreadStatus(THREAD_STATUS_STOP);
    }

    void Thread::timedJoin(int nWaitTimeMilSec)
    {
      try
        {
          m_pMutex->lock();

          if (m_op != THREAD_OP_NONE || m_status != THREAD_STATUS_RUNNING)
            {
              m_pMutex->unlock();
              return;
            }

          m_op = THREAD_OP_STOP;
          m_status = THREAD_STATUS_WAITING;

          m_pCond->timedWait(m_pMutex, nWaitTimeMilSec);

          m_pMutex->unlock();

          doJoin();
        }
      catch (DBGWException &)
        {
          m_op = THREAD_OP_DETACH;
          m_status = THREAD_STATUS_WAITING;

          m_pMutex->unlock();

          doDetach();
        }
    }

    bool Thread::isRunning() const
    {
      MutexLock lock(m_pMutex);

      return m_op == THREAD_OP_START
          || (m_status == THREAD_STATUS_RUNNING && m_op == THREAD_OP_NONE);
    }

    /**
     * if thread is stopped, this method return false
     */
    bool Thread::sleep(long lMilSec) const
    {
      struct timeval beginTime;
      struct timeval endTime;
      long lWaitTimeMilSec = 0;

      gettimeofday(&beginTime, NULL);

      while (true)
        {
          gettimeofday(&endTime, NULL);

          lWaitTimeMilSec += ((endTime.tv_sec - beginTime.tv_sec) * 1000);
          lWaitTimeMilSec += ((endTime.tv_usec - beginTime.tv_usec) / 1000);

          if (lMilSec >= lWaitTimeMilSec)
            {
              return true;
            }

          if (isRunning() == false)
            {
              return false;
            }

          SLEEP_MILISEC(0, MIN_SLEEP_TIME_MILSEC());
        }
    }

    THREAD_RETURN_TYPE Thread::run(void *pData)
    {
      if (pData == NULL)
        {
          return THREAD_RETURN_VALUE;
        }

      Thread *pThread = (Thread *) pData;
      pThread->execute();

      return THREAD_RETURN_VALUE;
    }

    void Thread::execute()
    {
      /**
       * By using shared_from_this(), guarantee that
       * thread instance will be alive until thread is dead.
       */
      ThreadSharedPtr pThread = shared_from_this();

      if (m_pFunc != NULL)
        {
          (*m_pFunc)(this, m_pData);
        }

      MutexLock lock(m_pMutex);

      if (m_status == THREAD_STATUS_WAITING && m_op == THREAD_OP_STOP)
        {
          m_pCond->notifyAll();
        }

      m_op = THREAD_OP_NONE;
      m_status = THREAD_STATUS_STOP;
    }

    void Thread::changeThreadStatus(ThreadStatus status)
    {
      system::MutexLock lock(m_pMutex);

      m_status = status;
      m_op = THREAD_OP_NONE;
    }

#ifdef WINDOWS
    class WindowsThread : public Thread
    {
    public:
      WindowsThread::WindowsThread(ThreadFunction pFunc);
      virtual WindowsThread::~WindowsThread();

      void doStart();

    protected:
      void doJoin();
      void doDetach();

    private:
      HANDLE m_hThread;
      unsigned int m_nTid;
    };

    WindowsThread::WindowsThread(ThreadFunction pFunc) :
      Thread(pFunc), m_nTid(-1)
    {
    }

    WindowsThread::~WindowsThread()
    {
      if (m_nTid > 0)
        {
          CloseHandle(m_hThread);
        }
    }

    void WindowsThread::doStart()
    {
      m_hThread = (HANDLE) _beginthreadex(NULL, 0, Thread::run, this, 0, &m_nTid);
      if (m_hThread <= 0)
        {
          ThreadOperationFailException e("create", (int) m_hThread);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void WindowsThread::doJoin()
    {
      int nStatus = WaitForSingleObject(m_hThread, INFINITE);
      if (nStatus != WAIT_OBJECT_0)
        {
          ThreadOperationFailException e("join", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void WindowsThread::doDetach()
    {
      /**
       * Windows O/S manage thread reference.
       * so thread will be destroyed normally if you close thread handle.
       */
    }
#else
    class PosixThread : public Thread
    {
    public:
      PosixThread(ThreadFunction pFunc);
      virtual ~PosixThread();

      void doStart();

    protected:
      void doJoin();
      void doDetach();

    private:
      pthread_t m_thread_t;
    };

    PosixThread::PosixThread(ThreadFunction pFunc) :
      Thread(pFunc)
    {
    }

    PosixThread::~PosixThread()
    {
    }

    void PosixThread::doStart()
    {
      int nStatus = pthread_create(&m_thread_t, NULL, Thread::run, this);
      if (nStatus != 0)
        {
          ThreadOperationFailException e("create", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void PosixThread::doJoin()
    {
      int nStatus = pthread_join(m_thread_t, NULL);
      if (nStatus != 0)
        {
          ThreadOperationFailException e("join", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    void PosixThread::doDetach()
    {
      int nStatus = pthread_detach(m_thread_t);
      if (nStatus != 0)
        {
          ThreadOperationFailException e("detach", nStatus);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }
#endif

    ThreadSharedPtr ThreadFactory::create(ThreadFunction pFunc)
    {
      ThreadSharedPtr p;
#ifdef WINDOWS
      p = ThreadSharedPtr(new WindowsThread(pFunc));
#else
      p = ThreadSharedPtr(new PosixThread(pFunc));
#endif
      return p;
    }

  }

}
