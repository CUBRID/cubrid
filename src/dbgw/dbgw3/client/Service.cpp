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

#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random/mersenne_twister.hpp>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/Value.h"
#include "dbgw3/ValueSet.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/sql/DatabaseInterface.h"
#include "dbgw3/sql/ResultSetMetaData.h"
#include "dbgw3/client/ConfigurationObject.h"
#include "dbgw3/client/Resource.h"
#include "dbgw3/client/Connector.h"
#include "dbgw3/client/ClientResultSet.h"
#include "dbgw3/client/Executor.h"
#include "dbgw3/client/Service.h"
#include "dbgw3/client/CharsetConverter.h"
#include "dbgw3/client/Group.h"

namespace dbgw
{

  typedef boost::uniform_int<int> Distributer;
  typedef boost::variate_generator<boost::mt19937 &, Distributer> Generator;

  static boost::mt19937 g_base(std::time(0));
  static Generator g_generator(g_base, Distributer(0, 99));

  class _Service::Impl
  {
  public:
    Impl(const std::string &fileName, const std::string &nameSpace,
        const std::string &description, bool bNeedValidation[],
        int nValidateRatio) :
      m_fileName(fileName), m_nameSpace(nameSpace), m_description(description),
      m_nValidateRatio(nValidateRatio),
      m_ulTimeBetweenEvictionRunsMillis(
          _ExecutorPoolContext::DEFAULT_TIME_BETWEEN_EVICTION_RUNS_MILLIS())
    {
      memcpy(m_bNeedValidation, bNeedValidation, sql::DBGW_STMT_TYPE_SIZE);

      if (m_nValidateRatio < 0)
        {
          m_nValidateRatio = 0;
        }
      else if (m_nValidateRatio > 100)
        {
          m_nValidateRatio = 100;
        }
    }

    ~Impl()
    {
    }

    void addGroup(trait<_Group>::sp pGroup)
    {
      for (trait<_Group>::spvector::iterator it = m_groupList.begin(); it
          != m_groupList.end(); it++)
        {
          if ((*it)->getName() == pGroup->getName())
            {
              DuplicateGroupNameException e(pGroup->getName(),
                  pGroup->getFileName(), (*it)->getName());
              DBGW_LOG_ERROR(e.what());
              throw e;
            }
        }

      m_groupList.push_back(pGroup);
    }

    void initGroup(_ExecutorPoolContext &poolContext)
    {
      m_ulTimeBetweenEvictionRunsMillis =
          poolContext.timeBetweenEvictionRunsMillis;

      if (poolContext.maxIdle > poolContext.maxActive)
        {
          poolContext.maxIdle = poolContext.maxActive;

          ChangePoolContextException e("maxIdle", poolContext.maxIdle,
              "maxIdle > maxActive");
          DBGW_LOG_WARN(e.what());
        }

      if (poolContext.minIdle > poolContext.maxIdle)
        {
          poolContext.minIdle = poolContext.maxIdle;

          ChangePoolContextException e("minIdle", poolContext.minIdle,
              "minIdle > maxIdle");
          DBGW_LOG_WARN(e.what());
        }

      if ((int) poolContext.initialSize < poolContext.minIdle)
        {
          poolContext.initialSize = poolContext.minIdle;

          ChangePoolContextException e("initialSize", poolContext.initialSize,
              "initialSize < minIdle");
          DBGW_LOG_WARN(e.what());
        }
      else if ((int) poolContext.initialSize > poolContext.maxIdle)
        {
          poolContext.initialSize = poolContext.maxIdle;

          ChangePoolContextException e("initialSize", poolContext.initialSize,
              "initialSize > maxIdle");
          DBGW_LOG_WARN(e.what());
        }

      for (trait<_Group>::spvector::iterator it = m_groupList.begin(); it
          != m_groupList.end(); it++)
        {
          if ((*it)->isInactivate() == true)
            {
              continue;
            }

          (*it)->initPool(poolContext);
        }
    }

    void evictUnsuedExecutor()
    {
      if (m_groupList.empty())
        {
          return;
        }

      for (trait<_Group>::spvector::iterator it = m_groupList.begin(); it
          != m_groupList.end(); it++)
        {
          (*it)->evictUnsuedExecutor();
        }
    }

    trait<_Executor>::splist getExecutorList()
    {
      trait<_Executor>::splist executorList;

      for (trait<_Group>::spvector::iterator it = m_groupList.begin(); it
          != m_groupList.end(); it++)
        {
          if ((*it)->isInactivate() == true)
            {
              continue;
            }

          try
            {
              executorList.push_back((*it)->getExecutor());
            }
          catch (Exception &)
            {
              if ((*it)->isIgnoreResult() == false)
                {
                  throw;
                }

              clearException();
            }
        }

      return executorList;
    }

    void setForceValidateResult()
    {
      memset(&m_bNeedValidation, 1, sizeof(m_bNeedValidation));
      m_nValidateRatio = 100;
    }

    bool needValidation(sql::StatementType type) const
    {
      if (type == sql::DBGW_STMT_TYPE_UNDEFINED
          || m_bNeedValidation[type] == false)
        {
          return false;
        }

      int nRandom = g_generator();
      return nRandom < m_nValidateRatio;
    }

    const std::string &getFileName() const
    {
      return m_fileName;
    }

    const std::string &getNameSpace() const
    {
      return m_nameSpace;
    }

    bool empty() const
    {
      return m_groupList.empty();
    }

    static void run(const system::_ThreadEx *pThread)
    {
      if (pThread == NULL)
        {
          FailedToCreateThreadException e("timer");
          DBGW_LOG_ERROR(e.what());
          return;
        }

      _Service::Impl *pServiceImpl = ((_Service *) pThread)->m_pImpl;
      unsigned long ulTimeBetweenEvictionRunsMillis =
          pServiceImpl->getTimeBetweenEvictionRunsMillis();

      while (pThread->isRunning())
        {
          pServiceImpl->evictUnsuedExecutor();

          if (ulTimeBetweenEvictionRunsMillis > 0)
            {
              if (pThread->sleep(ulTimeBetweenEvictionRunsMillis) == false)
                {
                  break;
                }
            }
        }
    }

  private:
    unsigned long getTimeBetweenEvictionRunsMillis()
    {
      return m_ulTimeBetweenEvictionRunsMillis;
    }

  private:
    std::string m_fileName;
    std::string m_nameSpace;
    std::string m_description;
    bool m_bNeedValidation[sql::DBGW_STMT_TYPE_SIZE];
    int m_nValidateRatio;
    unsigned long m_ulTimeBetweenEvictionRunsMillis;
    /* (groupName => Group) */
    trait<_Group>::spvector m_groupList;
  };

  _Service::_Service(const _Connector &connector,
      const std::string &fileName, const std::string &nameSpace,
      const std::string &description, bool bNeedValidation[],
      int nValidateRatio) :
    system::_ThreadEx(Impl::run), _ConfigurationObject(connector),
    m_pImpl(new Impl(fileName, nameSpace, description,
        bNeedValidation, nValidateRatio))
  {
  }

  _Service::~_Service()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _Service::addGroup(trait<_Group>::sp pGroup)
  {
    m_pImpl->addGroup(pGroup);
  }

  void _Service::initGroup(_ExecutorPoolContext &poolContext)
  {
    m_pImpl->initGroup(poolContext);
  }

  trait<_Executor>::splist _Service::getExecutorList()
  {
    return m_pImpl->getExecutorList();
  }

  void _Service::setForceValidateResult()
  {
    m_pImpl->setForceValidateResult();
  }

  bool _Service::needValidation(sql::StatementType type) const
  {
    return m_pImpl->needValidation(type);
  }

  const std::string &_Service::getFileName() const
  {
    return m_pImpl->getFileName();
  }

  const std::string &_Service::getNameSpace() const
  {
    return m_pImpl->getNameSpace();
  }

  bool _Service::empty() const
  {
    return m_pImpl->empty();
  }

}
