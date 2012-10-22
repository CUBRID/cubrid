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

    DBGW_ER_CONF_NOT_EXIST_NAMESPACE                  = -22100,
    DBGW_ER_CONF_NOT_EXIST_QUERY_IN_XML               = -22101,
    DBGW_ER_CONF_NOT_EXIST_ADDED_HOST                 = -22102,
    DBGW_ER_CONF_FETCH_HOST_FAIL                      = -22103,
    DBGW_ER_CONF_NOT_YET_LOADED                       = -22104,
    DBGW_ER_CONF_NOT_EXIST_VERSION                    = -22105,
    DBGW_ER_CONF_NOT_EXIST_FILE                       = -22106,
    DBGW_ER_CONF_NOT_EXIST_SERVICE                    = -22107,
    DBGW_ER_CONF_INVALID_PARAM_NAME                   = -22108,
    DBGW_ER_CONF_CHANGE_POOL_CONTEXT                  = -22109,
    DBGW_ER_CONF_FAILED_TO_CREATE_EVICTOR_THREAD      = -22110,

    DBGW_ER_SQL_NOT_EXIST_CONN                        = -22200,
    DBGW_ER_SQL_INVALID_SQL                           = -22201,
    DBGW_ER_SQL_NOT_EXIST_PARAM                       = -22202,
    DBGW_ER_SQL_EXECUTE_BEFORE_PREPARE                = -22203,
    DBGW_ER_SQL_NOT_EXIST_PROPERTY                    = -22204,

    DBGW_ER_VALUE_NOT_EXIST_SET                       = -22300,
    DBGW_ER_VALUE_MISMATCH_VALUE_TYPE                 = -22301,
    DBGW_ER_VALUE_INVALID_VALUE_TYPE                  = -22302,
    DBGW_ER_VALUE_INVALID_VALUE_FORMAT                = -22303,
    DBGW_ER_VALUE_INVALID_SIZE                        = -22304,

    DBGW_ER_CLIENT_MULTISET_IGNORE_FLAG_FALSE         = -22400,
    DBGW_ER_CLIENT_INVALID_CLIENT                     = -22401,
    DBGW_ER_CLIENT_NOT_EXIST_GROUP                    = -22402,
    DBGW_ER_CLIENT_ALREADY_IN_TRANSACTION             = -22403,
    DBGW_ER_CLIENT_NOT_IN_TRANSACTION                 = -22404,
    DBGW_ER_CLIENT_INVALID_PARAMETER_LIST             = -22405,
    DBGW_ER_CLIENT_EXECUTE_SELECT_IN_BATCH            = -22406,
    DBGW_ER_CLIENT_EXECUTE_PROCEDURE_IN_BATCH         = -22407,
    DBGW_ER_CLIENT_CREATE_MAX_CONNECTION              = -22408,

    DBGW_ER_RESULT_NOT_ALLOWED_NEXT                   = -22500,
    DBGW_ER_RESULT_NOT_ALLOWED_GET_METADATA           = -22501,
    DBGW_ER_RESULT_NOT_ALLOWED_OPERATION              = -22502,
    DBGW_ER_RESULT_VALIDATE_FAIL                      = -22503,
    DBGW_ER_RESULT_VALIDATE_TYPE_FAIL                 = -22504,
    DBGW_ER_RESULT_VALIDATE_VALUE_FAIL                = -22505,
    DBGW_ER_RESULT_NO_MORE_DATA                       = -22506,

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
    DBGW_ER_XML_NOT_EXIST_RESULT                      = -22710,
    DBGW_ER_XML_DUPLICATE_RESULT_INDEX                = -22711,
    DBGW_ER_XML_INVALID_RESULT_INDEX                  = -22712,

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
    static DBGWException create(int nErrorCode, int nInterfaceErrorCode,
        const string &errorMessage);

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

  class NotExistNamespaceException : public DBGWException
  {
  public:
    NotExistNamespaceException() throw();
    NotExistNamespaceException(const char *szNamespace) throw();
  };

  class NotExistQueryInXmlException : public DBGWException
  {
  public:
    NotExistQueryInXmlException(const char *szSqlName) throw();
  };

  class NotExistAddedHostException : public DBGWException
  {
  public:
    NotExistAddedHostException() throw();
  };

  class FetchHostFailException : public DBGWException
  {
  public:
    FetchHostFailException() throw();
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

  class FailedToCreateEvictorException : public DBGWException
  {
  public:
    FailedToCreateEvictorException() throw();
  };

  class NotExistConnException : public DBGWException
  {
  public:
    NotExistConnException(const char *szGroupName) throw();
  };

  class InvalidSqlException : public DBGWException
  {
  public:
    InvalidSqlException(const char *szFileName, const char *szSqlName) throw();
  };

  class NotExistParamException : public DBGWException
  {
  public:
    NotExistParamException(int nIndex) throw();
    NotExistParamException(string name) throw();
  };

  class ExecuteBeforePrepareException : public DBGWException
  {
  public:
    ExecuteBeforePrepareException() throw();
  };

  class SQLNotExistPropertyException : public DBGWException
  {
  public:
    SQLNotExistPropertyException(const char *szName) throw();
  };

  class NotExistSetException : public DBGWException
  {
  public:
    NotExistSetException(const char *szKey) throw();
    NotExistSetException(size_t nIndex) throw();
  };

  class MismatchValueTypeException : public DBGWException
  {
  public:
    MismatchValueTypeException(int orgType, int convType) throw();
  };

  class InvalidValueTypeException : public DBGWException
  {
  public:
    InvalidValueTypeException(int type) throw();
    InvalidValueTypeException(const char *szType) throw();
    InvalidValueTypeException(int type,
        const char *szExpectedType) throw();
    InvalidValueTypeException(const char *szFileName, const char *szType) throw();
    InvalidValueTypeException(int firstType, int anotherType) throw();
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

  class MultisetIgnoreResultFlagFalseException : public DBGWException
  {
  public:
    MultisetIgnoreResultFlagFalseException(const char *szSqlName) throw();
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

  class CreateMaxConnectionException : public DBGWException
  {
  public:
    CreateMaxConnectionException(int nSize) throw();
  };

  class NotAllowedNextException : public DBGWException
  {
  public:
    NotAllowedNextException() throw();
  };

  class NotAllowedGetMetadataException : public DBGWException
  {
  public:
    NotAllowedGetMetadataException() throw();
  };

  class NotAllowedOperationException : public DBGWException
  {
  public:
    NotAllowedOperationException() throw();
    NotAllowedOperationException(const char *szOperation,
        const char *szQueryType) throw();
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

  class InvalidParameterListException : public DBGWException
  {
  public:
    InvalidParameterListException() throw();
  };

  class ExecuteSelectInBatchException : public DBGWException
  {
  public:
    ExecuteSelectInBatchException() throw();
  };

  class ExecuteProcedureInBatchException : public DBGWException
  {
  public:
    ExecuteProcedureInBatchException() throw();
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
