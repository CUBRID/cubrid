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

#include <boost/format.hpp>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Lob.h"
#include "dbgw3/Value.h"

namespace dbgw
{

  static const int MAX_ERROR_MESSAGE_SIZE = 2048;

  __thread int g_nErrorCode;
  __thread int g_nInterfaceErrorCode;
  __thread char g_szErrorMessage[MAX_ERROR_MESSAGE_SIZE];
  __thread char g_szFormattedErrorMessage[MAX_ERROR_MESSAGE_SIZE];
  __thread bool g_bConnectionError;
  __thread int g_nCount = 0;

  static void setErrorMessage(char *szTarget, const char *szErrorMessage)
  {
    if (szErrorMessage == NULL)
      {
        strcpy(szTarget, "");
      }
    else
      {
        int nErrorMessageSize = strlen(szErrorMessage);
        if (nErrorMessageSize > MAX_ERROR_MESSAGE_SIZE - 1)
          {
            nErrorMessageSize = MAX_ERROR_MESSAGE_SIZE - 1;
          }
        memcpy(szTarget, szErrorMessage, nErrorMessageSize);
        szTarget[nErrorMessageSize] = '\0';
      }
  }

  void setLastException(const Exception &exception)
  {
    g_nErrorCode = exception.getErrorCode();
    g_nInterfaceErrorCode = exception.getInterfaceErrorCode();
    setErrorMessage(g_szErrorMessage, exception.getErrorMessage());
    setErrorMessage(g_szFormattedErrorMessage, exception.what());
    g_bConnectionError = exception.isConnectionError();
  }

  void clearException()
  {
    g_nErrorCode = DBGW_ER_NO_ERROR;
    g_nInterfaceErrorCode = DBGW_ER_NO_ERROR;
    strcpy(g_szErrorMessage, "");
    strcpy(g_szFormattedErrorMessage, "");
    g_bConnectionError = false;
  }

  Exception getLastException()
  {
    _ExceptionContext context =
    {
      g_nErrorCode, g_nInterfaceErrorCode, g_szErrorMessage,
      g_szFormattedErrorMessage, g_bConnectionError
    };
    return Exception(context);
  }

  int getLastErrorCode()
  {
    return g_nErrorCode;
  }

  int getLastInterfaceErrorCode()
  {
    return g_nInterfaceErrorCode;
  }

  const char *getLastErrorMessage()
  {
    return g_szErrorMessage;
  }

  const char *getFormattedErrorMessage()
  {
    return g_szFormattedErrorMessage;
  }

  Exception::Exception() throw()
  {
    clear();
  }

  Exception::Exception(const _ExceptionContext &context) throw() :
    m_context(context)
  {
  }

  Exception::Exception(const std::exception &exception) throw() :
    std::exception(exception)
  {
    m_context.nErrorCode = DBGW_ER_EXTERNAL_STANDARD_ERROR;
    m_context.nInterfaceErrorCode = DBGW_ER_NO_ERROR;
    m_context.errorMessage = exception.what();
    m_context.what = exception.what();
    m_context.bConnectionError = false;
  }

  Exception::~Exception() throw()
  {
  }

  void Exception::clear()
  {
    m_context.nErrorCode = DBGW_ER_NO_ERROR;
    m_context.nInterfaceErrorCode = DBGW_ER_NO_ERROR;
    m_context.errorMessage = "";
    m_context.what = "";
    m_context.bConnectionError = false;
  }

  int Exception::getErrorCode() const
  {
    return m_context.nErrorCode;
  }

  const char *Exception::getErrorMessage() const
  {
    return m_context.errorMessage.c_str();
  }

  const char *Exception::what() const throw()
  {
    return m_context.what.c_str();
  }

  int Exception::getInterfaceErrorCode() const
  {
    return m_context.nInterfaceErrorCode;
  }

  bool Exception::isConnectionError() const
  {
    return m_context.bConnectionError;
  }

  void Exception::setConnectionError(bool bConnectionError)
  {
    m_context.bConnectionError = bConnectionError;
  }

  Exception ExceptionFactory::create(int nErrorCode,
      const std::string &errorMessage)
  {
    _ExceptionContext context =
    { nErrorCode, DBGW_ER_NO_ERROR, errorMessage, "", false };

    std::stringstream buffer;
    buffer << "[" << context.nErrorCode << "]";
    buffer << " " << context.errorMessage;
    context.what = buffer.str();

    return Exception(context);
  }

  ArrayIndexOutOfBoundsException::ArrayIndexOutOfBoundsException(int nIndex,
      const char *szArrayName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_COMMON_ARRAY_INDEX_OUT_OF_BOUNDS,
            (boost::format("Array index (%d) is out of bounds in %s.")
                % nIndex % szArrayName).str()))

  {
  }

  NotExistKeyException::NotExistKeyException(const char *szKey,
      const char *szArrayName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_COMMON_NOT_EXIST_KEY,
            (boost::format("The key (%s) does not exist in %s.")
                % szKey % szArrayName).str()))
  {
  }

  NotExistNameSpaceException::NotExistNameSpaceException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_NOT_EXIST_SERVICE,
            "There is no service to execute. please check your connector.xml"))
  {
  }

  NotExistNameSpaceException::NotExistNameSpaceException(
      const char *szNamespace) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_NOT_EXIST_NAMESPACE,
            (boost::format("The %s namespace is not exist.")
                % szNamespace).str()))
  {
  }

  NotExistQueryInXmlException::NotExistQueryInXmlException(
      const char *szSqlName) throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CONF_NOT_EXIST_QUERY_IN_XML,
            (boost::format("There is no '%s' query in querymap")
                % szSqlName).str()))
  {
  }

  InvalidHostWeightException::InvalidHostWeightException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_INVALID_HOST_WEIGHT,
            "The host weight must be greater than 0."))
  {
  }

  NotYetLoadedException::NotYetLoadedException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_NOT_YET_LOADED,
            "Configuration is not yet loaded."))
  {
  }

  NotExistVersionException::NotExistVersionException(int nVersion) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_NOT_EXIST_VERSION,
            (boost::format("The configuration of version %d is not exist.")
                % nVersion).str()))
  {
  }

  NotExistConfFileException::NotExistConfFileException(
      const char *szPath) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_NOT_EXIST_FILE,
            (boost::format("No such file or directory. (%s)") % szPath).str()))
  {
  }

  InvalidParamNameException::InvalidParamNameException(
      const char *szName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_INVALID_PARAM_NAME,
            (boost::format("You can't use this parameter name %s.")
                % szName).str()))
  {
  }

  ChangePoolContextException::ChangePoolContextException(
      const char *szContext0ame, int nModifiedValue,
      const char *szDescription) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_CHANGE_POOL_CONTEXT,
            (boost::format("Pool parameter value '%s' has been changed to %d."
                " (%s)")
                % szContext0ame % nModifiedValue % szDescription).str()))
  {
  }

  InvalidSqlException::InvalidSqlException(const char *szFileName,
      const char *szSqlName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_INVALID_SQL,
            (boost::format("Cannot parse sql %s (%s).") % szSqlName
                % szFileName).str()))
  {
  }

  NotExistOutParameterException::NotExistOutParameterException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_SQL_NOT_EXIST_OUT_PARAMETER,
            "There is no out bind parameter."))
  {
  }

  NotExistOutParameterException::NotExistOutParameterException(
      size_t nIndex) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_SQL_NOT_EXIST_OUT_PARAMETER,
            (boost::format("There is no out bind parameter. (index : %d)")
                % nIndex).str()))
  {
  }

  InvalidParameterListException::InvalidParameterListException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_SQL_INVALID_PARAMETER_LIST, "The parameter list is invalid."))
  {
  }

  UnsupportedOperationException::UnsupportedOperationException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_SQL_UNSUPPORTED_OPERATION, "This operation is unsupported."))
  {
  }

  InvalidCursorPositionException::InvalidCursorPositionException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_SQL_INVALID_CURSOR_POSITION, "Invalid cursor position."))
  {
  }

  BatchUpdateException::BatchUpdateException(
      const std::vector<int> &updateCountList) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_SQL_FAIL_TO_BATCH_UPDATE,
            "Failed to execute batch update query.")),
    m_updateCountList(updateCountList)
  {
  }

  const std::vector<int> &BatchUpdateException::getUpdateCounts()
  {
    return m_updateCountList;
  }

  MismatchValueTypeException::MismatchValueTypeException(int orgType,
      int convType) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_MISMATCH_VALUE_TYPE,
            (boost::format("Cannot cast %s to %s.")
                % getValueTypeString(orgType)
                % getValueTypeString(convType)).str()))
  {
  }

  MismatchValueTypeException::MismatchValueTypeException(int orgType,
      const std::string &orgValue, int convType) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_MISMATCH_VALUE_TYPE,
            (boost::format("Cannot cast %s (%s) to %s.")
                % getValueTypeString(orgType) % orgValue
                % getValueTypeString(convType)).str()))
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(int type) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_INVALID_VALUE_TYPE,
            (boost::format("The value type %d is invalid.") % type).str()))
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(
      const char *szFileName, const char *szType) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_INVALID_VALUE_TYPE,
            (boost::format("The value type %s is not supported (%s).")
                % szType % szFileName).str()))
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(int type,
      const char *szExpectedType) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_INVALID_VALUE_TYPE,
            (boost::format("The value type (%s) must be one of types (%s).")
                % getValueTypeString(type) % szExpectedType).str()))
  {
  }

  InvalidValueFormatException::InvalidValueFormatException(const char *szType,
      const char *szFormat) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_INVALID_VALUE_FORMAT,
            (boost::format("The %s is not valid %s type.") % szFormat
                % szType).str()))
  {
  }

  InvalidValueSizeException::InvalidValueSizeException(int nSize) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_VALUE_INVALID_VALUE_SIZE,
            (boost::format("The %d size is invalid value size") % nSize).str()))
  {
  }

  UnsupportedLobOperationException::UnsupportedLobOperationException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_VALUE_UNSUPPORTED_LOB_OPERATION,
            "This operation is only allowed for lob type."))
  {
  }

  CannotMakeMulipleResultException::CannotMakeMulipleResultException(
      const char *szSqlName) throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CLIENT_CANNOT_MAKE_MULTIPLE_RESULT,
            (boost::format("Cannot make multiple result set for query (%s).")
                % szSqlName).str()))
  {
  }

  InvalidClientException::InvalidClientException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_INVALID_CLIENT,
            "The client is invalid."))
  {
  }

  NotExistGroupException::NotExistGroupException(const char *szSqlName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_NOT_EXIST_GROUP,
            (boost::format("There is no group (%s).")
                % szSqlName).str()))
  {
  }

  AlreadyInTransactionException::AlreadyInTransactionException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_ALREADY_IN_TRANSACTION,
            "The client is already in transaction."))
  {
  }

  NotInTransactionException::NotInTransactionException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_NOT_IN_TRANSACTION,
            "The client is not in transaction."))
  {
  }

  InvalidQueryTypeException::InvalidQueryTypeException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_INVALID_QUERY_TYPE,
            "Cannot use select / procedure statement when execute array"))
  {
  }

  CreateMaxConnectionException::CreateMaxConnectionException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_CREATE_MAX_CONNECTION,
            ("Cannot create connection anymore.")))
  {
  }

  CreateMaxConnectionException::CreateMaxConnectionException(int nSize) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_CREATE_MAX_CONNECTION,
            (boost::format("Cannot create more than %s connections.")
                % nSize).str()))
  {
  }

  InvalidClientOperationException::InvalidClientOperationException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_INVALID_OPERATION,
            "This operation is invalid."))
  {
  }

  ValidateFailException::ValidateFailException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_VALIDATE_FAIL,
            "The result type of lhs is different from that of rhs."))
  {
  }

  ValidateFailException::ValidateFailException(
      const Exception &exception) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_VALIDATE_FAIL,
            (boost::format("Some of group is failed to execute query. %s")
                % exception.what()).str()))
  {
  }

  ValidateFailException::ValidateFailException(int lhs, int rhs) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_VALIDATE_FAIL,
            (boost::format(
                "The affected row count / select row count of lhs"
                " is different from that of rhs. %d != %d")
                % lhs % rhs).str()))
  {
  }

  ValidateTypeFailException::ValidateTypeFailException(const std::string &name,
      const std::string &lhs, const char *szLhsType, const std::string &rhs,
      const char *szRhsType) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_VALIDATE_TYPE_FAIL,
            (boost::format(
                "The %s's type of lhs is different from that"
                " of rhs. %s (%s) != %s (%s)")
                % name % lhs % szLhsType % rhs % szRhsType).str()))
  {
  }

  ValidateValueFailException::ValidateValueFailException(
      const std::string &name, const std::string &lhs) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_VALIDATE_FAIL,
            (boost::format(
                "The %s's value of lhs is different from that of rhs."
                " %s != NULL")
                % name % lhs).str()))
  {
  }

  ValidateValueFailException::ValidateValueFailException(
      const std::string &name, const std::string &lhs,
      const std::string &rhs) throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CLIENT_VALIDATE_VALUE_FAIL,
            (boost::format(
                "The %s's value of lhs is different from that of rhs. %s != %s")
                % name % lhs % rhs).str()))
  {
  }

  NoMoreDataException::NoMoreDataException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CLIENT_NO_MORE_DATA, "There is no more data."))
  {
  }

  AccessDataBeforeFetchException::AccessDataBeforeFetchException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CLIENT_ACCESS_DATA_BEFORE_FETCH,
            "You must call next() before get data."))
  {
  }

  FailedToCreateThreadException::FailedToCreateThreadException(
      const char *szThread) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_FATAL_FAILED_TO_CREATE_THREAD,
            (boost::format("Failed to create %s thread.") % szThread).str()))
  {
  }

  ExecuteTimeoutExecption::ExecuteTimeoutExecption(
      unsigned long ulWaitTimeMilSec) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_EXEC_TIMEOUT,
            (boost::format("Timeout occurred. (%lu msec)")
                % ulWaitTimeMilSec).str()))
  {
  }

  ExecuteAsyncTempUnavailableException::ExecuteAsyncTempUnavailableException() throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CLIENT_ASYNC_EXEC_TEMP_UNAVAILABLE,
            "async execution is temporarily unavailable."))
  {
  }

  CreateFailParserExeception::CreateFailParserExeception(
      const char *szFileName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_FAIL_CREATE_PARSER,
            (boost::format("Cannot create xml parser from %s.")
                % szFileName).str()))
  {
  }

  DuplicateNamespaceExeception::DuplicateNamespaceExeception(
      const std::string &nameSpace, const std::string &fileNameNew,
      const std::string &fileNameOld) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_DUPLICATE_NAMESPACE,
            (boost::format("The namspace %s in %s is already exist in %s.")
                % nameSpace % fileNameNew % fileNameOld).str()))
  {
  }

  DuplicateSqlNameException::DuplicateSqlNameException(const char *szSqlName,
      const char *szFileNameNew, const char *szFileNameOld) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_DUPLICATE_SQLNAME,
            (boost::format("The %s in %s is already exist in %s.") % szSqlName
                % szFileNameNew % szFileNameOld).str()))
  {
  }

  DuplicateGroupNameException::DuplicateGroupNameException(
      const std::string &groupName, const std::string &fileNameNew,
      const std::string &fileNameOld) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_DUPLICATE_GROUPNAME,
            (boost::format("The %s in %s is already exist in %s.") % groupName
                % fileNameNew % fileNameOld).str()))
  {
  }

  NotExistNodeInXmlException::NotExistNodeInXmlException(const char *szNodeName,
      const char *szXmlFile) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_NOT_EXIST_NODE,
            (boost::format("The %s node is not exist in %s.") % szNodeName
                % szXmlFile).str()))
  {
  }

  NotExistPropertyException::NotExistPropertyException(const char *szFileName,
      const char *szNodeName, const char *szPropName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_NOT_EXIST_PROPERTY,
            (boost::format("Cannot find %s property of %s node (%s).")
                % szPropName % szNodeName % szFileName).str()))
  {
  }

  InvalidPropertyValueException::InvalidPropertyValueException(
      const char *szFileName, const char *szValue,
      const char *szCorrectValueSet) throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_XML_INVALID_PROPERTY_VALUE,
            (boost::format("The value of property %s have to be [%s] (%s).")
                % szValue % szCorrectValueSet % szFileName).str()))
  {
  }

  InvalidXMLSyntaxException::InvalidXMLSyntaxException(
      const char *szXmlErrorMessage, const char *szFileName, int nLine,
      int nCol) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_INVALID_SYNTAX,
            (boost::format("%s in %s, line %d, column %d") % szXmlErrorMessage
                % szFileName % nLine % nCol).str()))
  {
  }

  DuplicateParamIndexException::DuplicateParamIndexException(
      const char *szFileName, const char *szSqlName, int nIndex) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_DUPLICATE_PARAM_INDEX,
            (boost::format(
                "Duplicate index '%d' for parameter node of %s. (%s)")
                % nIndex % szSqlName % szFileName).str()))
  {
  }

  InvalidParamIndexException::InvalidParamIndexException(const char *szFileName,
      const char *szSqlName, int nIndex) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_INVALID_PARAM_INDEX,
            (boost::format(
                "The index %d of paramter node of %s is out of bounds. (%s)")
                % nIndex % szSqlName % szFileName).str()))
  {
  }

  DuplicateResultIndexException::DuplicateResultIndexException(
      const char *szFileName, const char *szSqlName, int nIndex) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_DUPLICATE_RESULT_INDEX,
            (boost::format(
                "Duplicate index '%d' for result node of %s. (%s)")
                % nIndex % szSqlName % szFileName).str()))
  {
  }

  InvalidResultIndexException::InvalidResultIndexException(
      const char *szFileName, const char *szSqlName, int nIndex) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_INVALID_RESULT_INDEX,
            (boost::format(
                "The index %d of paramter node of %s is out of bounds. (%s)")
                % nIndex % szSqlName % szFileName).str()))
  {
  }

  FailedToParseXmlException::FailedToParseXmlException(
      const char *szFileName) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_XML_FAILED_TO_PARSE,
            (boost::format(
                "Failed to parse xml. (%s).") % szFileName).str()))
  {
  }

  MutexOperationFailException::MutexOperationFailException(const char *szOp,
      int nStatus) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_EXTERNAL_MUTEX_OPERATION_FAIL,
            (boost::format("Failed to %s mutex. (status : %d)") % szOp
                % nStatus).str()))
  {
  }

  InvalidHandleException::InvalidHandleException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_EXTERNAL_DBGW_INVALID_HANDLE, "The handle is invalid."))
  {
  }

  NotEnoughBufferException::NotEnoughBufferException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_EXTERNAL_DBGW_NOT_ENOUGH_BUFFER,
            "Not enough buffer memory."))
  {
  }

  CondVarOperationFailException::CondVarOperationFailException(const char *szOp,
      int nStatus) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_EXTERNAL_COND_VAR_OPERATION_FAIL,
            (boost::format("Failed to %s condition variable. (status : %d)")
                % szOp % nStatus).str()))
  {
  }

  ThreadOperationFailException::ThreadOperationFailException(const char *szOp,
      int nStatus) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_EXTERNAL_THREAD_OPERATION_FAIL,
            (boost::format("Failed to %s thread. (status : %d)") % szOp
                % nStatus).str()))
  {
  }

  InvalidCharsetException::InvalidCharsetException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CONF_INVALID_CHARSET, "The character set name is invalid."))
  {
  }

  CreateConverterFailException::CreateConverterFailException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_EXTERNAL_CREATE_CONVERTER_FAIL,
            "Failed to create converter."))
  {
  }

  ConvertCharsetFailException::ConvertCharsetFailException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_EXTERNAL_CONVERT_CHARSET_FAIL,
            "Failed to convert character set."))
  {
  }

  NotExistCharsetException::NotExistCharsetException() throw() :
    Exception(
        ExceptionFactory::create(
            DBGW_ER_CONF_NOT_EXIST_CHARSET, "Either client or DB "
            "character set does not exist."))
  {
  }

  InvalidDbTypeException::InvalidDbTypeException(const char *szDbType) throw() :
    Exception(
        ExceptionFactory::create(DBGW_ER_CONF_INVALID_DB_TYPE,
            (boost::format("This library is not supported %s database type.") %
                szDbType).str()))
  {
  }

}
