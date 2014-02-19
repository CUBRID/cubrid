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

#include "dbgw3/Common.h"
#include "dbgw3/Logger.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/GlobalOnce.h"
#if defined(USE_NCLAVIS)
#include "dbgw3/NClavisClient.h"
#endif /* defined(USE_NCLAVIS) */
#if defined(DBGW_MYSQL)
#include "dbgw3/sql/mysql/MySQLCommon.h"
#include "dbgw3/sql/mysql/MySQLConnection.h"
#endif /* defined(DBGW_MYSQL) */

namespace dbgw
{

  system::_Mutex g_mutex;

  void initializeGlobalOnce(void)
  {
    static bool bInitialized = false;

    system::_MutexAutoLock lock(&g_mutex);

    if (bInitialized)
      {
        return;
      }

    bInitialized = true;

#if defined(USE_NCLAVIS)
    _NClavisGlobal::getInstance();
#endif /* defined(USE_NCLAVIS) */

#if defined(DBGW_MYSQL)
    sql::_MySQLGlobal::getInstance();
#endif /* defined(DBGW_MYSQL) */
  }

}
