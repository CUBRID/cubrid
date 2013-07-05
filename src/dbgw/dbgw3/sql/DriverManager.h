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

#ifndef DRIVERMANAGER_H_
#define DRIVERMANAGER_H_

namespace dbgw
{

  namespace sql
  {

    enum DataBaseType
    {
      DBGW_DB_TYPE_CUBRID = 0,
      DBGW_DB_TYPE_MYSQL,
      DBGW_DB_TYPE_ORACLE
    };

    class Connection;

    const char *getDbTypeString(DataBaseType dbType);

    class DriverManager
    {
    public:
      static trait<Connection>::sp getConnection(const char *szUrl,
          DataBaseType dbType = DBGW_DB_TYPE_CUBRID);
      static trait<Connection>::sp getConnection(const char *szUrl,
          const char *szUser, const char *szPassword,
          DataBaseType dbType = DBGW_DB_TYPE_CUBRID);
    };

  }

}

#endif
