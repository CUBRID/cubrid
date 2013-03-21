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

#ifndef EXCEPTION_H_
#define EXCEPTION_H_

namespace dbgw
{

  enum ErrorCode
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
    DBGW_ER_CONF_INVALID_CHARSET                      = -22110,
    DBGW_ER_CONF_NOT_EXIST_CHARSET                    = -22111,

    DBGW_ER_SQL_NOT_EXIST_OUT_PARAMETER               = -22201,
    DBGW_ER_SQL_INVALID_PARAMETER_LIST                = -22202,
    DBGW_ER_SQL_UNSUPPORTED_OPERATION                 = -22203,
    DBGW_ER_SQL_INVALID_CURSOR_POSITION               = -22204,
    DBGW_ER_SQL_FAIL_TO_BATCH_UPDATE                  = -22205,

    DBGW_ER_VALUE_MISMATCH_VALUE_TYPE                 = -22301,
    DBGW_ER_VALUE_INVALID_VALUE_TYPE                  = -22302,
    DBGW_ER_VALUE_INVALID_VALUE_FORMAT                = -22303,
    DBGW_ER_VALUE_INVALID_VALUE_SIZE                  = -22304,
    DBGW_ER_VALUE_UNSUPPORTED_LOB_OPERATION           = -22305,

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
    DBGW_ER_CLIENT_ASYNC_EXEC_TEMP_UNAVAILABLE        = -22414,

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
    DBGW_ER_EXTERNAL_DBGW_INVALID_HANDLE              = -22803,
    DBGW_ER_EXTERNAL_DBGW_NOT_ENOUGH_BUFFER           = -22804,
    DBGW_ER_EXTERNAL_COND_VAR_OPERATION_FAIL          = -22805,
    DBGW_ER_EXTERNAL_THREAD_OPERATION_FAIL            = -22806,
    DBGW_ER_EXTERNAL_CREATE_CONVERTER_FAIL            = -22807,
    DBGW_ER_EXTERNAL_CONVERT_CHARSET_FAIL             = -22808,

    DBGW_ER_OLD_DBGW_INTERFACE_ERROR                  = -22900
  };

  struct _ExceptionContext
  {
    int nErrorCode;
    int nInterfaceErrorCode;
    std::string errorMessage;
    std::string what;
    bool bConnectionError;
  };

  class Exception : public std::exception
  {
  public:
    Exception() throw();
    Exception(const _ExceptionContext &context) throw();
    Exception(const std::exception &exception) throw();
    virtual ~ Exception() throw();

    void clear();

  public:
    int getErrorCode() const;
    const char *getErrorMessage() const;
    virtual const char *what() const throw();
    int getInterfaceErrorCode() const;
    bool isConnectionError() const;

  protected:
    void setConnectionError(bool bConnectionError);

  private:
    _ExceptionContext m_context;
  };

  class ExceptionFactory
  {
  public:
    static Exception create(int nErrorCode, const std::string &errorMessage);

  private:
    virtual ~ExceptionFactory();
  };

  extern void setLastException(const Exception &exception);
  extern void clearException();
  extern Exception getLastException();
  extern int getLastErrorCode();
  extern int getLastInterfaceErrorCode();
  extern const char *getLastErrorMessage();
  extern const char *getFormattedErrorMessage();

  class ArrayIndexOutOfBoundsException : public Exception
  {
  public:
    ArrayIndexOutOfBoundsException(int nIndex, const char *szArrayName) throw();
  };

  class NotExistKeyException : public Exception
  {
  public:
    NotExistKeyException(const char *szKey, const char *szArrayName) throw();
  };

  class NotExistNameSpaceException : public Exception
  {
  public:
    NotExistNameSpaceException() throw();
    NotExistNameSpaceException(const char *szNamespace) throw();
  };

  class NotExistQueryInXmlException : public Exception
  {
  public:
    NotExistQueryInXmlException(const char *szSqlName) throw();
  };

  class InvalidHostWeightException : public Exception
  {
  public:
    InvalidHostWeightException() throw();
  };

  class NotYetLoadedException : public Exception
  {
  public:
    NotYetLoadedException() throw();
  };

  class NotExistVersionException : public Exception
  {
  public:
    NotExistVersionException(int nVersion) throw();
  };

  class NotExistConfFileException : public Exception
  {
  public:
    NotExistConfFileException(const char *szPath) throw();
  };

  class InvalidParamNameException : public Exception
  {
  public:
    InvalidParamNameException(const char *szName) throw();
  };

  class ChangePoolContextException : public Exception
  {
  public:
    ChangePoolContextException(const char *szContext0ame, int nModifiedValue,
        const char *szDescription) throw();
  };

  class InvalidSqlException : public Exception
  {
  public:
    InvalidSqlException(const char *szFileName, const char *szSqlName) throw();
  };

  class NotExistOutParameterException : public Exception
  {
  public:
    NotExistOutParameterException() throw();
    NotExistOutParameterException(size_t nIndex) throw();
  };

  class InvalidParameterListException : public Exception
  {
  public:
    InvalidParameterListException() throw();
  };

  class UnsupportedOperationException : public Exception
  {
  public:
    UnsupportedOperationException() throw();
  };

  class InvalidCursorPositionException : public Exception
  {
  public:
    InvalidCursorPositionException() throw();
  };

  class BatchUpdateException : public Exception
  {
  public:
    BatchUpdateException(const std::vector<int> &updateCountList) throw();
    virtual ~BatchUpdateException() throw() {}

  public:
    const std::vector<int> &getUpdateCounts();

  private:
    std::vector<int> m_updateCountList;
  };

  class MismatchValueTypeException : public Exception
  {
  public:
    MismatchValueTypeException(int orgType, int convType) throw();
    MismatchValueTypeException(int orgType, const std::string &orgValue,
        int convType) throw();
  };

  class InvalidValueTypeException : public Exception
  {
  public:
    InvalidValueTypeException(int type) throw();
    InvalidValueTypeException(const char *szType) throw();
    InvalidValueTypeException(int type,
        const char *szExpectedType) throw();
    InvalidValueTypeException(const char *szFileName, const char *szType) throw();
  };

  class InvalidValueFormatException : public Exception
  {
  public:
    InvalidValueFormatException(const char *szType, const char *szFormat) throw();
  };

  class InvalidValueSizeException : public Exception
  {
  public:
    InvalidValueSizeException(int nSize) throw();
  };

  class UnsupportedLobOperationException : public Exception
  {
  public:
    UnsupportedLobOperationException() throw();
  };

  class CannotMakeMulipleResultException : public Exception
  {
  public:
    CannotMakeMulipleResultException(const char *szSqlName) throw();
  };

  class InvalidClientException : public Exception
  {
  public:
    InvalidClientException() throw();
  };

  class NotExistGroupException : public Exception
  {
  public:
    NotExistGroupException(const char *szSqlName) throw();
  };

  class AlreadyInTransactionException : public Exception
  {
  public:
    AlreadyInTransactionException() throw();
  };

  class NotInTransactionException : public Exception
  {
  public:
    NotInTransactionException() throw();
  };

  class InvalidQueryTypeException : public Exception
  {
  public:
    InvalidQueryTypeException() throw();
  };

  class CreateMaxConnectionException : public Exception
  {
  public:
    CreateMaxConnectionException(int nSize) throw();
  };

  class InvalidClientOperationException : public Exception
  {
  public:
    InvalidClientOperationException() throw();
  };

  class ValidateFailException : public Exception
  {
  public:
    ValidateFailException() throw();
    ValidateFailException(const Exception &exception) throw();
    ValidateFailException(int lhs, int rhs) throw();
  };

  class ValidateTypeFailException : public Exception
  {
  public:
    ValidateTypeFailException(const std::string &name, const std::string &lhs,
        const char *szLhsType, const std::string &rhs,
        const char *szRhsType) throw();
  };

  class ValidateValueFailException : public Exception
  {
  public:
    ValidateValueFailException(const std::string &name,
        const std::string &lhs) throw();
    ValidateValueFailException(const std::string &name, const std::string &lhs,
        const std::string &rhs) throw();
  };

  class NoMoreDataException : public Exception
  {
  public:
    NoMoreDataException() throw();
  };

  class AccessDataBeforeFetchException : public Exception
  {
  public:
    AccessDataBeforeFetchException() throw();
  };

  class FailedToCreateThreadException : public Exception
  {
  public:
    FailedToCreateThreadException(const char *szThread) throw();
  };

  class ExecuteTimeoutExecption : public Exception
  {
  public:
    ExecuteTimeoutExecption(unsigned long ulWaitTimeMilSec) throw();
  };

  class ExecuteAsyncTempUnavailableException : public Exception
  {
  public:
    ExecuteAsyncTempUnavailableException() throw();
  };

  class InvalidClientWorkerThreadException : public Exception
  {
  public:
    InvalidClientWorkerThreadException() throw();
  };

  class CreateFailParserExeception : public Exception
  {
  public:
    CreateFailParserExeception(const char *szFileName) throw();
  };

  class DuplicateNamespaceExeception : public Exception
  {
  public:
    DuplicateNamespaceExeception(const std::string &nameSpace,
        const std::string &fileNameNew, const std::string &fileNameOld) throw();
  };

  class DuplicateSqlNameException : public Exception
  {
  public:
    DuplicateSqlNameException(const char *szSqlName, const char *szFileNameNew,
        const char *szFileNameOld) throw();
  };

  class DuplicateGroupNameException : public Exception
  {
  public:
    DuplicateGroupNameException(const std::string &groupName,
        const std::string &fileNameNew, const std::string &fileNameOld) throw();
  };

  class NotExistNodeInXmlException : public Exception
  {
  public:
    NotExistNodeInXmlException(const char *szNodeName,
        const char *szXmlFile) throw();
  };

  class NotExistPropertyException : public Exception
  {
  public:
    NotExistPropertyException(const char *szFileName, const char *szNodeName,
        const char *szPropName) throw();
  };

  class InvalidPropertyValueException : public Exception
  {
  public:
    InvalidPropertyValueException(const char *szFileName, const char *szValue,
        const char *szCorrectValueSet) throw();
  };

  class InvalidXMLSyntaxException : public Exception
  {
  public:
    InvalidXMLSyntaxException(const char *szXmlErrorMessage,
        const char *szFileName, int nLine, int nCol) throw();
  };

  class DuplicateParamIndexException : public Exception
  {
  public:
    DuplicateParamIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class InvalidParamIndexException : public Exception
  {
  public:
    InvalidParamIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class DuplicateResultIndexException : public Exception
  {
  public:
    DuplicateResultIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class InvalidResultIndexException : public Exception
  {
  public:
    InvalidResultIndexException(const char *szFileName, const char *szSqlName,
        int nIndex) throw();
  };

  class MutexOperationFailException : public Exception
  {
  public:
    MutexOperationFailException(const char *szOp, int nStatus) throw();
  };

  class InvalidHandleException : public Exception
  {
  public:
    InvalidHandleException() throw();
  };

  class NotEnoughBufferException : public Exception
  {
  public:
    NotEnoughBufferException() throw();
  };

  class CondVarOperationFailException : public Exception
  {
  public:
    CondVarOperationFailException(const char *szOp, int nStatus) throw();
  };

  class ThreadOperationFailException : public Exception
  {
  public:
    ThreadOperationFailException(const char *szOp, int nStatus) throw();
  };

  class InvalidCharsetException : public Exception
  {
  public:
    InvalidCharsetException() throw();
  };

  class CreateConverterFailException : public Exception
  {
  public:
    CreateConverterFailException() throw();
  };

  class ConvertCharsetFailException : public Exception
  {
  public:
    ConvertCharsetFailException() throw();
  };

  class NotExistCharsetException : public Exception
  {
  public:
    NotExistCharsetException() throw();
  };

}
;

#endif
