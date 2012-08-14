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
#if defined(WINDOWS)
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#endif
#include "DBGWCommon.h"
#include "DBGWLogger.h"
#include "DBGWError.h"
#include "DBGWPorting.h"

namespace dbgw
{

  namespace system
  {

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
#else
      p = DirectorySharedPtr(new PosixDirectory(szPath));
#endif
      return p;
    }

#if defined(WINDOWS)
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
