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
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/SynchronizedResource.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/Connector.h"
#include "dbgw3/client/Service.h"
#include "dbgw3/client/CharsetConverter.h"
#include "dbgw3/client/Group.h"

namespace dbgw
{

  _Connector::_Connector(Configuration *pConfiguration) :
    _ConfigurationObject(pConfiguration)
  {
  }

  _Connector::~_Connector()
  {
    trait<_Service>::spvector::iterator it = m_serviceList.begin();
    for (; it != m_serviceList.end(); it++)
      {
        (*it)->timedJoin(getMaxWaitExitTimeMilSec());
      }
  }

  void _Connector::addService(trait<_Service>::sp pService)
  {
    trait<_Service>::spvector::iterator it = m_serviceList.begin();
    for (; it != m_serviceList.end(); it++)
      {
        if ((*it)->getNameSpace() == pService->getNameSpace())
          {
            DuplicateNamespaceExeception e(pService->getNameSpace(),
                pService->getFileName(), (*it)->getNameSpace());
            DBGW_LOG_ERROR(e.what());
            throw e;
          }
      }

    m_serviceList.push_back(pService);
  }

  trait<_Service>::sp _Connector::getService(const char *szNameSpace)
  {
    if (m_serviceList.empty())
      {
        NotExistNameSpaceException e;
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    trait<_Service>::spvector::const_iterator it = m_serviceList.begin();
    for (; it != m_serviceList.end(); it++)
      {
        if (szNameSpace == NULL
            || !strcmp((*it)->getNameSpace().c_str(), szNameSpace))
          {
            return *it;
          }
      }

    NotExistNameSpaceException e(szNameSpace);
    DBGW_LOG_ERROR(e.what());
    throw e;
  }

  sql::DataBaseType _Connector::getDbType(const char *szGroupName) const
  {
    trait<_Service>::spvector::const_iterator it = m_serviceList.begin();
    for (; it != m_serviceList.end(); it++)
      {
        trait<_Group>::sp pGroup = (*it)->getGroup(szGroupName);
        if (pGroup != NULL)
          {
            return pGroup->getDbType();
          }
      }

    return sql::DBGW_DB_TYPE_CUBRID;
  }

}
