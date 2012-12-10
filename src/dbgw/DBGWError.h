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
#ifndef DBGWERROR_H_
#define DBGWERROR_H_

namespace dbgw
{

  enum DBGWErrorCode
  {
    DBGW_ER_NO_ERROR                                  = 0,

    DBGW_ER_COMMON_ARRAY_INDEX_OUT_OF_BOUNDS          = -22000,
    DBGW_ER_COMMON_NOT_EXIST_KEY                      = -22001,

    DBGW_ER_CONF_NOT_EXIST_NAMESPACE                  = -22100,
    DBGW_ER_CONF_NOT_EXIST_QUERY_IN_XML               = -22101,
    DBGW_ER_CONF_INVALID_HOST_WEIGHT                  = -22102,
    DBGW_ER_CONF_NOT_YET_LOADED                       = -22103,
    DBGW_ER_CONF_NOT_EXIST_VERSION                    = -22104,
    DBGW_ER_CONF_NOT_EXIST_FILE                       = -22105,
    DBGW_ER_CONF_NOT_EXIST_SERVICE                    = -22106,
    DBGW_ER_CONF_INVALID_PARAM_NAME                   = -22107,
    DBGW_ER_CONF_CHANGE_POOL_CONTEXT                  = -22108,
    DBGW_ER_CONF_INVALID_SQL                          = -22109,

    DBGW_ER_SQL_NOT_EXIST_OUT_PARAMETER               = -22201,
    DBGW_ER_SQL_INVALID_PARAMETER_LIST                = -22202,
    DBGW_ER_SQL_UNSUPPORTED_OPERATION                 = -22203,
    DBGW_ER_SQL_INVALID_CURSOR_POSITION               = -22204,

    DBGW_ER_VALUE_MISMATCH_VALUE_TYPE                 = -22301,
    DBGW_ER_VALUE_INVALID_VALUE_TYPE                  = -22302,
    DBGW_ER_VALUE_INVALID_VALUE_FORMAT                = -22303,
    DBGW_ER_VALUE_INVALID_VALUE_SIZE                  = -22304,

    DBGW_ER_CLIENT_CANNOT_MAKE_MULTIPLE_RESULT        = -22400,
    DBGW_ER_CLIENT_INVALID_CLIENT                     = -22401,
    DBGW_ER_CLIENT_NOT_EXIST_GROUP                    = -22402,
    DBGW_ER_CLIENT_ALREADY_IN_TRANSACTION             = -22403,
    DBGW_ER_CLIENT_NOT_IN_TRANSACTION                 = -22404,
    DBGW_ER_CLIENT_INVALID_QUERY_TYPE                 = -22405,
    DBGW_ER_CLIENT_CREATE_MAX_CONNECTION              = -22406,
    DBGW_ER_CLIENT_INVALID_OPERATION                  = -22407,
    DBGW_ER_CLIENT_VALIDATE_FAIL                      = -22408,
    DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL                 = -22409,
    DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL                = -22410,
    DBGW_ER_CLIENT_NO_MORE_DATA                       = -22411,
    DBGW_ER_CLIENT_ACCESS_DATA_BEFORE_FETCH           = -22412,
    DBGW_ER_CLIENT_EXEC_TIMEOUT                       = -22413,

    DBGW_ER_FATAL_FAILED_TO_CREATE_THREAD             = -22500,

    DBGW_ER_INTERFACE_ERROR                           = -22600,

    DBGW_ER_XML_FAIL_CREATE_PARSER                    = -22700,
    DBGW_ER_XML_DUPLICATE_NAMESPACE                   = -22701,
    DBGW_ER_XML_DUPLICATE_SQLNAME                     = -22702,
    DBGW_ER_XML_DUPLICATE_GROUPNAME                   = -22703,
    DBGW_ER_XML_NOT_EXIST_NODE                        = -22704,
    DBGW_ER_XML_NOT_EXIST_PROPERTY                    = -22705,
    DBGW_ER_XML_INVALID_PROPERTY_VALUE                = -22706,
    DBGW_ER_XML_INVALID_SYNTAX                        = -22707,
    DBGW_ER_XML_DUPLICATE_PARAM_INDEX                 = -22708,
    DBGW_ER_XML_INVALID_PARAM_INDEX                   = -22709,
    DBGW_ER_XML_DUPLICATE_RESULT_INDEX                = -22710,
    DBGW_ER_XML_INVALID_RESULT_INDEX                  = -22711,

    DBGW_ER_EXTERNAL_MUTEX_OPERATION_FAIL             = -22800,
    DBGW_ER_EXTERNAL_STANDARD_ERROR                   = -22801,
    DBGW_ER_EXTERNAL_MEMORY_ALLOC_FAIL                = -22802,
    DBGW_ER_EXTERNAL_DBGW_INVALID_HANDLE              = -22803,
    DBGW_ER_EXTERNAL_DBGW_NOT_ENOUGH_BUFFER           = -22804,
    DBGW_ER_EXTERNAL_COND_VAR_OPERATION_FAIL          = -22805,
    DBGW_ER_EXTERNAL_THREAD_OPERATION_FAIL            = -22806,

    DBGW_ER_OLD_DBGW_INTERFACE_ERROR                  = -22900
  };

  struct DBGWExceptionContext
  {
    int nErrorCode;
    int nInterfaceErrorCode;
    string errorMessage;
    string what;
    bool bConnectionError;
  };

  class DBGWException : public exception
  {
  public:
    DBGWException() throw();
    DBGWException(const DBGWExceptionContext &context) throw();
    DBGWException(const std::exception &exception) throw();
    virtual ~ DBGWException() throw();

    void clear();

  public:
    int getErrorCode() const;
    const char *getErrorMessage() const;
    const char *what() const throw();
    int getInterfaceErrorCode() const;
    bool isConnectionError() const;

  protected:
    void setConnectionError(bool bConnectionError);

  private:
    DBGWExceptionContext m_context;
  };

  class DBGWExceptionFactory
  {
  public:
    static DBGWException create(int nErrorCode, const string &errorMessage);

  private:
    virtual ~DBGWExceptionFactory();
  };

  extern void setLastException(const DBGWException &exception);
  extern void clearException();
  extern DBGWException getLastException();
  extern int getLastErrorCode();
  extern int getLastInterfaceErrorCode();
  extern const char *getLastErrorMessage();
  extern const char *getFormattedErrorMessage();

  class ArrayIndexOutOfBoundsException : public DBGWException
  {
  public:
    ArrayIndexOutOfBoundsException(int nIndex, const char *szArrayName) throw();
  };

  class NotExistKeyException : public DBGWException
  {
  public:
    NotExistKeyException(const char *szKey, const char *szArrayName) throw();
  };

  class NotExistNameSpaceException : public DBGWException
  {
  public:
    NotExistNameSpaceException() throw();
    NotExistNameSpaceException(const char *szNamespace) throw();
  };

  class NotExistQueryInXmlException : public DBGWException
  {
  public:
    NotExistQueryInXmlException(const char *szSqlName) throw();
  };

  class InvalidHostWeightException : public DBGWException
  {
  public:
    InvalidHostWeightException() throw();
  };

  class NotYetLoadedException : public DBGWException
  {
  public:
    NotYetLoadedException() throw();
  };

  class NotExistVersionException : public DBGWException
  {
  public:
    NotExistVersionException(int nVersion) throw();
  };

  class NotExistConfFileException : public DBGWException
  {
  public:
    NotExistConfFileException(const char *szPath) throw();
  };

  class InvalidParamNameException : public DBGWException
  {
  public:
    InvalidParamNameException(const char *szName) throw();
  };

  class ChangePoolContextException : public DBGWException
  {
  public:
    ChangePoolContextException(const char *szContext0ame, int nModifiedValue,
        const char *szDescription) throw();
  };

  class InvalidSqlException : public DBGWException
  {
  public:
    InvalidSqlException(const char *szFileName, const char *szSqlName) throw();
  };

  class NotExistOutParameterException : public DBGWException
  {
  public:
    NotExistOutParameterException() throw();
    NotExistOutParameterException(size_t nIndex) throw();
  };

  class InvalidParameterListException : public DBGWException
  {
  public:
    InvalidParameterListException() throw();
  };

  class UnsupportedOperationException : public DBGWException
  {
  public:
    UnsupportedOperationException() throw();
  };

  class InvalidCursorPositionException : public DBGWException
  {
  public:
    InvalidCursorPositionException() throw();
  };

  class MismatchValueTypeException : public DBGWException
  {
  public:
    MismatchValueTypeException(int orgType, int convType) throw();
    MismatchValueTypeException(int orgType, const string &orgValue,
        int convType) throw();
  };

  class InvalidValueTypeException : public DBGWException
  {
  public:
    InvalidValueTypeException(int type) throw();
    InvalidValueTypeException(const char *szType) throw();
    InvalidValueTypeException(int type,
        const char *szExpectedType) throw();
    InvalidValueTypeException(const char *szFileName, const char *szType) throw();
  };

  class InvalidValueFormatException : public DBGWException
  {
  public:
    InvalidValueFormatException(const char *szType, const char *szFormat) throw();
  };

  class InvalidValueSizeException : public DBGWException
  {
  public:
    InvalidValueSizeException(int nSize) throw();
  };

  class CannotMakeMulipleResultException : public DBGWException
  {
  public:
    CannotMakeMulipleResultException(const char *szSqlName) throw();
  };

  class InvalidClientException : public DBGWException
  {
  public:
    InvalidClientException() throw();
  };

  class NotExistGroupException : public DBGWException
  {
  public:
    NotExistGroupException(const char *szSqlName) throw();
  };

  class AlreadyInTransactionException : public DBGWException
  {
  public:
    AlreadyInTransactionException() throw();
  };

  class NotInTransactionException : public DBGWException
  {
  public:
    NotInTransactionException() throw();
  };

  class InvalidQueryTypeException : public DBGWException
  {
  public:
    InvalidQueryTypeException() throw();
  };

  class CreateMaxConnectionException : public DBGWException
  {
  public:
    CreateMaxConnectionException(int nSize) throw();
  };

  class InvalidClientOperationException : public DBGWException
  {
  public:
    InvalidClientOperationException() throw();
  };

  class ValidateFailException : public DBGWException
  {
  public:
    ValidateFailException() throw();
    ValidateFailException(const DBGWException &exception) throw();
    ValidateFailException(int lhs, int rhs) throw();
  };

  class ValidateTypeFailException : public DBGWException
  {
  public:
    ValidateTypeFailException(const char *szName, const string &lhs,
        const char *szLhsType, const string &rhs, const char *szRhsType) throw();
  };

  class ValidateValueFailException : public DBGWException
  {
  public:
    ValidateValueFailException(const char *szName, const string &lhs) throw();
    ValidateValueFailException(const char *szName, const string &lhs,
        const string &rhs) throw();
  };

  class NoMoreDataException : public DBGWException
  {
  public:
    NoMoreDataException() throw();
  };

  class AccessDataBeforeFetchException : public DBGWException
  {
  public:
    AccessDataBeforeFetchException() throw();
  };

  class FailedToCreateThreadException : public DBGWException
  {
  public:
    FailedToCreateThreadException(const char *szThread) throw();
  };

  class ExecuteTimeoutExecption : public DBGWException
  {
  public:
    ExecuteTimeoutExecption(long lWaitTimeMilSec) throw();
  };

  class InvalidClientWorkerThreadException : public DBGWException
  {
  public:
    InvalidClientWorkerThreadException() throw();
  };

  class CreateFailParserExeception : public DBGWException
  {
  public:
    CreateFailParserExeception(const char *szFileName) throw();
  };

  class DuplicateNamespaceExeception : public DBGWException
  {
  public:
    DuplicateNamespaceExeception(const string &nameSpace,
        const string &fileNameNew, const string &fileNameOld) throw();
  };

  class DuplicateSqlNameException : public DBGWException
  {
  public:
    DuplicateSqlNameException(const char *szSqlName, const char *szFileNameNew,
        const char *szFileNameOld) throw();
  };

  class DuplicateGroupNameException : public DBGWException
  {
  public:
    DuplicateGroupNameException(const string &groupName,
        const string &fileNameNew, const string &fileNameOld) throw();
  };

  class NotExistNodeInXmlException : public DBGWException
  {
  public:
    NotExistNodeInXmlException(const char *szNodeName,
        const char *szXmlFile) throw();
  };

  class NotExistPropertyException : public DBGWException
  {
  public:
    NotExistPropertyException(const char *szFileName, const char *szNodeName,
        const char *szPropName) throw();
  };

  class InvalidPropertyValueException : public DBGWException
  {
  public:
    InvalidPropertyValueException(const char *szFileName, const char *szValue,
        const char *szCorrectValueSet) throw();
  };

  class InvalidXMLSyntaxException : public DBGWException
  {
  public:
    InvalidXMLSyntaxException(const char *szXmlErrorMessage,
        const char *szFileName, int nLine, int nCol) throw();
  };

  class DuplicateParamIndexException : public DBGWException
  {
  public:
    DuplicateParamIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class InvalidParamIndexException : public DBGWException
  {
  public:
    InvalidParamIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class DuplicateResultIndexException : public DBGWException
  {
  public:
    DuplicateResultIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class InvalidResultIndexException : public DBGWException
  {
  public:
    InvalidResultIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class MutexOperationFailException : public DBGWException
  {
  public:
    MutexOperationFailException(const char *szOp, int nStatus) throw();
  };

  class MemoryAllocationFail : public DBGWException
  {
  public:
    MemoryAllocationFail(int nSize) throw();
  };

  class InvalidHandleException : public DBGWException
  {
  public:
    InvalidHandleException() throw();
  };

  class NotEnoughBufferException : public DBGWException
  {
  public:
    NotEnoughBufferException() throw();
  };

  class CondVarOperationFailException : public DBGWException
  {
  public:
    CondVarOperationFailException(const char *szOp, int nStatus) throw();
  };

  class ThreadOperationFailException : public DBGWException
  {
  public:
    ThreadOperationFailException(const char *szOp, int nStatus) throw();
  };

}
;

#endif /* DBGWERROR_H_ */
