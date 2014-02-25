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

#ifndef SERVICE_H_
#define SERVICE_H_

namespace dbgw
{

  class _Connector;
  struct _ExecutorPoolContext;
  class _Group;

  class _Service : public system::_ThreadEx,
    public _ConfigurationObject
  {
  public:
    static long DEFAULT_WAIT_TIME_MILSEC();

  public:
    _Service(const _Connector &connector, const std::string &fileName,
        const std::string &nameSpace, const std::string &description,
        bool m_bNeedValidation[], int nValidateRatio,
        long lWaitTimeMilSec);
    virtual ~ _Service();

    void addGroup(trait<_Group>::sp pGroup);
    void initGroup(_ExecutorPoolContext &poolContext);
    trait<_Executor>::splist getExecutorList();

  public:
    void setForceValidateResult();

  public:
    bool needValidation(sql::StatementType type) const;
    const std::string &getFileName() const;
    const std::string &getNameSpace() const;
    bool empty() const;
    long getWaitTimeMilSec() const;
    trait<_Group>::sp getGroup(const char *szGroupName) const;

  private:
    _Service(const _Service &);
    _Service &operator=(const _Service &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
