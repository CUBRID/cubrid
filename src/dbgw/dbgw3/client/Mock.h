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

#ifndef MOCK_H_
#define MOCK_H_

namespace dbgw
{

  enum _FAULT_TYPE
  {
    DBGW_FAULT_TYPE_NONE = 0,
    DBGW_FAULT_TYPE_EXEC_BEFORE,
    DBGW_FAULT_TYPE_EXEC_AFTER,
  };

  class _Fault
  {
  public:
    _Fault(_FAULT_TYPE type);
    _Fault(_FAULT_TYPE type, int nReturnCode);
    virtual ~_Fault() {}

    bool raiseFaultBeforeExecute();
    bool raiseFaultAfterExecute();
    virtual void setNativeError(void *pError) {}

  public:
    int getReturnCode() const;

  protected:
    virtual bool doRaiseFaultBeforeExecute() = 0;
    virtual bool doRaiseFaultAfterExecute() = 0;

  private:
    _FAULT_TYPE m_type;
    int m_nReturnCode;
  };

  /**
   * This class is singleton object.
   */
  class _MockManager
  {
  public:
    _MockManager();

    void addCCIReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode = 0, int nErrorCode = 0,
        const char *szErrorMessage = "");
    void addOCIReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode = 0, int nErrorCode = 0,
        const char *szErrorMessage = "");
    void addMySQLReturnErrorFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, int nReturnCode = 0, int nErrorCode = 0,
        const char *szErrorMessage = "");
    void addCCISleepFault(const char *szFaultFunction,
        dbgw::_FAULT_TYPE type, unsigned long ulSleepMilSec);
    void addFault(const char *szFaultFunction, trait<_Fault>::sp pFault);
    trait<_Fault>::sp getFault(const char *szFaultFunction);
    void clearFaultAll();

  public:
    static _MockManager *getInstance();

  private:
    /**
     * Don't create this class in stack memory.
     */
    virtual ~_MockManager();

  private:
    _MockManager(const _MockManager &);
    _MockManager &operator=(const _MockManager &);

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
