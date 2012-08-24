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

namespace dbgw
{

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

    class PosixDirectory : public Directory
    {
    public:
      PosixDirectory(const char *szPath);
      virtual ~PosixDirectory();

    public:
      bool isDirectory() const;
      void getFileFullPathList(DBGWStringList &fileNameList);
    };

  }

}

#endif				/* DBGWLogger.h */
