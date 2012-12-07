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
#ifndef DBGWMOCK_H_
#define DBGWMOCK_H_

namespace dbgw
{

  typedef enum
  {
    CCI_FAULT_TYPE_NONE = 0,
    CCI_FAULT_TYPE_EXEC_BEFORE,
    CCI_FAULT_TYPE_EXEC_AFTER,
  } _CCI_FAULT_TYPE;

  class _CCIFault
  {
  public:
    _CCIFault(_CCI_FAULT_TYPE type, int nReturnCode);
    virtual ~_CCIFault();

    bool raiseFaultBeforeExecute(T_CCI_ERROR *pCCIError);
    bool raiseFaultAfterExecute(T_CCI_ERROR *pCCIError);

  public:
    int getReturnCode() const;

  protected:
    virtual bool doRaiseFaultBeforeExecute(T_CCI_ERROR *pCCIError) = 0;
    virtual bool doRaiseFaultAfterExecute(T_CCI_ERROR *pCCIError) = 0;

  private:
    _CCI_FAULT_TYPE m_type;
    int m_nReturnCode;
  };

  class _CCIFaultReturnError : public _CCIFault
  {
  public:
    _CCIFaultReturnError(_CCI_FAULT_TYPE type, int nReturnCode,
        int nErrorCode, string errorMessage);
    virtual ~_CCIFaultReturnError();

  protected:
    virtual bool doRaiseFaultBeforeExecute(T_CCI_ERROR *pCCIError);
    virtual bool doRaiseFaultAfterExecute(T_CCI_ERROR *pCCIError);

  private:
    int m_nErrorCode;
    string m_errorMessage;
  };

  class _CCIFaultSleep : public _CCIFault
  {
  public:
    _CCIFaultSleep(_CCI_FAULT_TYPE type, unsigned long ulSleepMilSec);
    virtual ~_CCIFaultSleep();

  protected:
    virtual bool doRaiseFaultBeforeExecute(T_CCI_ERROR *pCCIError);
    virtual bool doRaiseFaultAfterExecute(T_CCI_ERROR *pCCIError);

  private:
    unsigned long m_ulSleepMilSec;
  };

  typedef boost::shared_ptr<_CCIFault> _CCIFaultSharedPtr;

  typedef list<_CCIFaultSharedPtr> _CCIFaultRawList;

  class _CCIFaultList
  {
  public:
    _CCIFaultList();
    virtual ~_CCIFaultList();

    void addFault(_CCIFaultSharedPtr pFault);
    _CCIFaultSharedPtr getFault();

  private:
    _CCIFaultRawList m_faultList;
  };

  typedef boost::shared_ptr<_CCIFaultList> _CCIFaultListSharedPtr;

  typedef boost::unordered_map<string, _CCIFaultListSharedPtr,
          boost::hash<string>, dbgwStringCompareFunc> _CCIFaultListMap;

  /**
   * This class is singleton object.
   */
  class _CCIMockManager
  {
  public:
    _CCIMockManager();

    void addReturnErrorFault(const char *szFaultFunction, _CCI_FAULT_TYPE type,
        int nReturnCode = 0, int nErrorCode = 0, const char *szErrorMessage = "");
    void addSleepFault(const char *szFaultFunction, _CCI_FAULT_TYPE type,
        unsigned long ulSleepMilSec);
    _CCIFaultSharedPtr getFault(const char *szFaultFunction);
    void clearFaultAll();

  public:
    static _CCIMockManager *getInstance();

  private:
    /**
     * Don't create this class in stack memory.
     */
    virtual ~_CCIMockManager();

  private:
    static _CCIMockManager *m_pInstance;
    _CCIFaultListMap m_faultListMap;
    system::_MutexSharedPtr m_pMutex;
  };

  extern int cci_mock_connect_with_url(char *url, char *user, char *password);
  extern int cci_mock_prepare(int con_handle, char *sql_stmt, char flag,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_execute(int req_handle, char flag, int max_col_size,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_execute_array(int req_h_id, T_CCI_QUERY_RESULT **qr,
      T_CCI_ERROR *err_buf);
  extern int cci_mock_set_autocommit(int con_handle,
      CCI_AUTOCOMMIT_MODE autocommit_mode);
  extern int cci_mock_end_tran(int con_h_id, char type, T_CCI_ERROR *err_buf);

}

#endif
