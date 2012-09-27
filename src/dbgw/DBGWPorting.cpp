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

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#else
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#endif

namespace dbgw
{
#if defined(WINDOWS)
  class WindowsMutex : public Mutex
  {
  public:
    WindowsMutex();
    ~ WindowsMutex();

    void lock();
    void unlock();

  private:
    HANDLE m_stMutex;

    WindowsMutex(const WindowsMutex &);
    void operator=(const WindowsMutex &);
  };
#else /* WINDOWS */
  class PosixMutex : public Mutex
  {
  public:
    PosixMutex();
    ~ PosixMutex();

    void lock();
    void unlock();

  private:
    pthread_mutex_t m_stMutex;

    PosixMutex(const PosixMutex &);
    void operator=(const PosixMutex &);
  };
#endif /* !WINDOWS */

  Mutex::Mutex()
  {
  }

  Mutex::~Mutex()
  {
  }

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

#if defined(WINDOWS)
  WindowsMutex::WindowsMutex()
  {
    m_stMutex = CreateMutex(NULL, FALSE, NULL);

    if (m_stMutex == NULL)
      {
        MutexInitFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  WindowsMutex::~WindowsMutex()
  {
    CloseHandle(m_stMutex);
  }

  void WindowsMutex::lock()
  {
    WaitForSingleObject(m_stMutex, INFINITE);
  }

  void WindowsMutex::unlock()
  {
    ReleaseMutex(m_stMutex);
  }
#else
  PosixMutex::PosixMutex()
  {
    if (pthread_mutex_init(&m_stMutex, NULL) != 0)
      {
        MutexInitFailException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }
  }

  PosixMutex::~PosixMutex()
  {
    pthread_mutex_destroy(&m_stMutex);
  }

  void PosixMutex::lock()
  {
    pthread_mutex_lock(&m_stMutex);
  }

  void PosixMutex::unlock()
  {
    pthread_mutex_unlock(&m_stMutex);
  }
#endif

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

  namespace system
  {
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

    void sleepMilliSecond(unsigned int ms)
    {
      Sleep(ms);
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
#endif /* !WINDOWS */

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

#if defined(WINDOWS)
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
#else
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
#endif

  }

}
