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
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/File.h"

#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#else
#include <dirent.h>
#endif

namespace dbgw
{

  namespace system
  {

    std::string getFileExtension(const std::string &fileName)
    {
      return fileName.substr(fileName.find_last_of(".") + 1);
    }

    _Directory::_Directory(const char *szPath) :
      m_path(szPath)
    {
    }

    _Directory::~_Directory()
    {
    }

    const char *_Directory::getPath() const
    {
      return m_path.c_str();
    }

    bool _Directory::isDirectory() const
    {
      struct stat pathInfo;
      int nError = stat(m_path.c_str(), &pathInfo);
      if (nError < 0)
        {
          NotExistConfFileException e(m_path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      return S_ISDIR(pathInfo.st_mode);
    }

    void _Directory::getFullPathList(std::vector<std::string> &fileNameList)
    {
#if defined(WINDOWS) || defined(_WIN32) || defined(_WIN64)
      HANDLE hSrc;
      WIN32_FIND_DATA wfd;
      std::string path = m_path + "\\*";

      hSrc = FindFirstFile(path.c_str(), &wfd);

      if (hSrc == INVALID_HANDLE_VALUE)
        {
          NotExistConfFileException e(m_path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      BOOL bResult = true;

      while (bResult)
        {
          fileNameList.push_back(m_path + "\\" + wfd.cFileName);
          bResult = FindNextFile(hSrc, &wfd);
        }

      FindClose(hSrc);
#else
      DIR *pDir = opendir(m_path.c_str());
      if (pDir == NULL)
        {
          NotExistConfFileException e(m_path.c_str());
          DBGW_LOG_INFO(e.what());
          throw e;
        }

      struct dirent entry;
      struct dirent *pResult = NULL;
      readdir_r(pDir, &entry, &pResult);

      while (pResult != NULL)
        {
          fileNameList.push_back(m_path + "/" + entry.d_name);
          readdir_r(pDir, &entry, &pResult);
        }

      closedir(pDir);
#endif
    }

  }

}
