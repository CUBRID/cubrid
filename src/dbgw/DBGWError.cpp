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
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWValue.h"

namespace dbgw
{

  static const int MAX_ERROR_MESSAGE_SIZE = 2048;

  __thread int g_nErrorCode;
  __thread int g_nInterfaceErrorCode;
  __thread char g_szErrorMessage[MAX_ERROR_MESSAGE_SIZE];
  __thread char g_szFormattedErrorMessage[MAX_ERROR_MESSAGE_SIZE];
  __thread bool g_bConnectionError;

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

  void setLastException(const DBGWException &exception)
  {
    g_nErrorCode = exception.getErrorCode();
    g_nInterfaceErrorCode = exception.getInterfaceErrorCode();
    setErrorMessage(g_szErrorMessage, exception.getErrorMessage());
    setErrorMessage(g_szFormattedErrorMessage, exception.what());
    g_bConnectionError = exception.isConnectionError();
  }

  void clearException()
  {
    g_nErrorCode = DBGWErrorCode::NO_ERROR;
    g_nInterfaceErrorCode = DBGWErrorCode::NO_ERROR;
    strcpy(g_szErrorMessage, "");
    strcpy(g_szFormattedErrorMessage, "");
    g_bConnectionError = false;
  }

  DBGWException getLastException()
  {
    DBGWExceptionContext context =
    {
      g_nErrorCode, g_nInterfaceErrorCode, g_szErrorMessage,
      g_szFormattedErrorMessage, g_bConnectionError
    };
    return DBGWException(context);
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

  DBGWException::DBGWException() throw()
  {
    m_context.nErrorCode = DBGWErrorCode::NO_ERROR;
    m_context.nInterfaceErrorCode = DBGWErrorCode::NO_ERROR;
    m_context.errorMessage = "";
    m_context.what = "";
    m_context.bConnectionError = false;
  }

  DBGWException::DBGWException(const DBGWExceptionContext &context) throw() :
    m_context(context)
  {
  }

  DBGWException::DBGWException(const std::exception &exception) throw() :
    std::exception(exception)
  {
    m_context.nErrorCode = DBGWErrorCode::EXTERNAL_STANDARD_ERROR;
    m_context.nInterfaceErrorCode = DBGWErrorCode::NO_ERROR;
    m_context.errorMessage = exception.what();
    m_context.what = exception.what();
    m_context.bConnectionError = false;
  }

  DBGWException::~DBGWException() throw()
  {
  }

  int DBGWException::getErrorCode() const
  {
    return m_context.nErrorCode;
  }

  const char *DBGWException::getErrorMessage() const
  {
    return m_context.errorMessage.c_str();
  }

  const char *DBGWException::what() const throw()
  {
    return m_context.what.c_str();
  }

  int DBGWException::getInterfaceErrorCode() const
  {
    return m_context.nInterfaceErrorCode;
  }

  bool DBGWException::isConnectionError() const
  {
    return m_context.bConnectionError;
  }

  void DBGWException::setConnectionError(bool bConnectionError)
  {
    m_context.bConnectionError = bConnectionError;
  }

  DBGWException DBGWExceptionFactory::create(int nErrorCode,
      const string &errorMessage)
  {
    DBGWExceptionContext context =
    { nErrorCode, DBGWErrorCode::NO_ERROR, errorMessage, "", false };

    stringstream buffer;
    buffer << "[" << context.nErrorCode << "]";
    buffer << " " << context.errorMessage;
    context.what = buffer.str();

    return DBGWException(context);
  }

  DBGWException DBGWExceptionFactory::create(int nErrorCode,
      int nInterfaceErrorCode, const string &errorMessage)
  {
    DBGWExceptionContext context =
    { nErrorCode, nInterfaceErrorCode, errorMessage, "", false };

    stringstream buffer;
    buffer << "[" << context.nErrorCode << "]";
    buffer << "[" << context.nInterfaceErrorCode << "]";
    buffer << " " << context.errorMessage;
    context.what = buffer.str();

    return DBGWException(context);
  }

  NotExistNamespaceException::NotExistNamespaceException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_NOT_EXIST_SERVICE,
            "There is no service to execute. please check your connector.xml"))
  {
  }

  NotExistNamespaceException::NotExistNamespaceException(
      const char *szNamespace) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_NOT_EXIST_NAMESPACE,
            (boost::format("The %s namespace is not exist.") % szNamespace).str()))
  {
  }

  NotExistQueryInXmlException::NotExistQueryInXmlException(
      const char *szSqlName) throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::CONF_NOT_EXIST_QUERY_IN_XML,
            (boost::format("The %s query is not exist in xml.") % szSqlName).str()))
  {
  }

  NotExistAddedHostException::NotExistAddedHostException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_NOT_EXIST_ADDED_HOST,
            "There is no added host."))
  {
  }

  FetchHostFailException::FetchHostFailException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_FETCH_HOST_FAIL,
            "Fetch host fail."))
  {
  }

  NotYetLoadedException::NotYetLoadedException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_NOT_YET_LOADED,
            "Configuration is not yet loaded."))
  {
  }

  NotExistVersionException::NotExistVersionException(int nVersion) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_NOT_EXIST_VERSION,
            (boost::format("The configuration of version %d is not exist.")
                % nVersion).str()))
  {
  }

  NotExistConfFileException::NotExistConfFileException(
      const char *szPath) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CONF_NOT_EXIST_VERSION,
            (boost::format("No such file or directory. (%s)") % szPath).str()))
  {
  }

  NotExistConnException::NotExistConnException(const char *szGroupName) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::SQL_NOT_EXIST_CONN,
            (boost::format("The %s connection group is not exist.")
                % szGroupName).str()))
  {
  }

  InvalidSqlException::InvalidSqlException(const char *szFileName,
      const char *szSqlName) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::SQL_INVALID_SQL,
            (boost::format("Cannot parse sql %s in %s.") % szSqlName
                % szFileName).str()))
  {
  }

  NotExistParamException::NotExistParamException(int nIndex) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::SQL_NOT_EXIST_PARAM,
            (boost::format("The bind parameter (index : %d) is not exist.")
                % nIndex).str()))
  {
  }

  NotExistParamException::NotExistParamException(string name) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::SQL_NOT_EXIST_PARAM,
            (boost::format("The bind parameter (key : %s) is not exist.")
                % name).str()))
  {
  }

  ExecuteBeforePrepareException::ExecuteBeforePrepareException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::SQL_EXECUTE_BEFORE_PREPARE,
            "The query is executed before prepare."))
  {
  }

  NotExistSetException::NotExistSetException(const char *szKey) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::VALUE_NOT_EXIST_SET,
            (boost::format("The value (%s key) dose not exist in set.") % szKey).str()))
  {
  }

  NotExistSetException::NotExistSetException(size_t nIndex) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::VALUE_NOT_EXIST_SET,
            (boost::format("The value (%d position) dose not exist in set.")
                % nIndex).str()))
  {
  }

  MismatchValueTypeException::MismatchValueTypeException(int orgType,
      int convType) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::VALUE_MISMATCH_VALUE_TYPE,
            (boost::format("Cannot cast %s to %s.")
                % getDBGWValueTypeString(orgType)
                % getDBGWValueTypeString(convType)).str()))
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(int type) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::VALUE_INVALID_VALUE_TYPE,
            (boost::format("The value type %d is invalid.") % type).str()))
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(
      const char *szType) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::VALUE_INVALID_VALUE_TYPE,
            (boost::format("The value type %s is invalid.") % szType).str()))
  {
  }

  InvalidValueFormatException::InvalidValueFormatException(const char *szType,
      const char *szFormat) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::VALUE_INVALID_VALUE_TYPE,
            (boost::format("The %s is not valid %s type.") % szFormat % szType).str()))
  {
  }

  MultisetIgnoreResultFlagFalseException::MultisetIgnoreResultFlagFalseException(
      const char *szSqlName) throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::CLIENT_MULTISET_IGNORE_FLAG_FALSE,
            (boost::format(
                "The 'ignore_result' flag should be set false only once in %s.")
                % szSqlName).str()))
  {
  }

  InvalidClientException::InvalidClientException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CLIENT_INVALID_CLIENT,
            "The client is invalid."))
  {
  }

  AlreadyInTransactionException::AlreadyInTransactionException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CLIENT_ALREADY_IN_TRANSACTION,
            "The client is already in transaction."))
  {
  }

  NotInTransactionException::NotInTransactionException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::CLIENT_NOT_IN_TRANSACTION,
            "The client is not in transaction."))
  {
  }

  NotAllowedNextException::NotAllowedNextException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::RESULT_NOT_ALLOWED_NEXT,
            "The next() operation is allowed only select query."))
  {
  }

  NotAllowedGetMetadataException::NotAllowedGetMetadataException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::RESULT_NOT_ALLOWED_GET_METADATA,
            "Only Select query is able to make metadata list."))
  {
  }


  NotAllowedOperationException::NotAllowedOperationException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::RESULT_NOT_ALLOWED_OPERATION,
            "You should call next() first."))
  {
  }

  NotAllowedOperationException::NotAllowedOperationException(
      const char *szOperation, const char *szQueryType) throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::RESULT_NOT_ALLOWED_OPERATION,
            (boost::format(
                "The %s operation is only allowed for query type %s.")
                % szOperation % szQueryType).str()))
  {
  }

  ValidateFailException::ValidateFailException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::RESULT_VALIDATE_FAIL,
            "The result type of lhs is different from that of rhs."))
  {
  }

  ValidateFailException::ValidateFailException(
      const DBGWException &exception) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::RESULT_VALIDATE_FAIL,
            (boost::format("Some of group is failed to execute query. %s")
                % exception.what()).str()))
  {
  }

  ValidateFailException::ValidateFailException(int lhs, int rhs) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::RESULT_VALIDATE_FAIL,
            (boost::format(
                "The affected row count / select row count of lhs is different from that of rhs. %d != %d")
                % lhs % rhs).str()))
  {
  }

  ValidateTypeFailException::ValidateTypeFailException(const char *szName,
      const string &lhs, const char *szLhsType, const string &rhs,
      const char *szRhsType) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::RESULT_VALIDATE_TYPE_FAIL,
            (boost::format(
                "The %s's type of lhs is different from that of rhs. %s (%s) != %s (%s)")
                % szName % lhs % szLhsType % rhs % szRhsType).str()))
  {
  }

  ValidateValueFailException::ValidateValueFailException(const char *szName,
      const string &lhs) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::RESULT_VALIDATE_FAIL,
            (boost::format(
                "The %s's value of lhs is different from that of rhs. %s != NULL")
                % szName % lhs).str()))
  {
  }

  ValidateValueFailException::ValidateValueFailException(const char *szName,
      const string &lhs, const string &rhs) throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::RESULT_VALIDATE_VALUE_FAIL,
            (boost::format(
                "The %s's value of lhs is different from that of rhs. %s != %s")
                % szName % lhs % rhs).str()))
  {
  }

  NoMoreDataException::NoMoreDataException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::RESULT_NO_MORE_DATA, "There is no more data."))
  {
  }

  CreateFailParserExeception::CreateFailParserExeception(
      const char *szFileName) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_FAIL_CREATE_PARSER,
            (boost::format("Cannot create xml parser from %s.") % szFileName).str()))
  {
  }

  DuplicateNamespaceExeception::DuplicateNamespaceExeception(
      const string &nameSpace, const string &fileNameNew,
      const string &fileNameOld) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_DUPLICATE_NAMESPACE,
            (boost::format("The namspace %s in %s is already exist in %s.")
                % nameSpace % fileNameNew % fileNameOld).str()))
  {
  }

  DuplicateSqlNameException::DuplicateSqlNameException(const char *szSqlName,
      const char *szFileNameNew, const char *szFileNameOld) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_DUPLICATE_SQLNAME,
            (boost::format("The %s in %s is already exist in %s.") % szSqlName
                % szFileNameNew % szFileNameOld).str()))
  {
  }

  DuplicateGroupNameException::DuplicateGroupNameException(
      const string &groupName, const string &fileNameNew,
      const string &fileNameOld) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_DUPLICATE_GROUPNAME,
            (boost::format("The %s in %s is already exist in %s.") % groupName
                % fileNameNew % fileNameOld).str()))
  {
  }

  NotExistNodeInXmlException::NotExistNodeInXmlException(const char *szNodeName,
      const char *szXmlFile) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_NOT_EXIST_NODE,
            (boost::format("The %s node is not exist in %s.") % szNodeName
                % szXmlFile).str()))
  {
  }

  NotExistPropertyException::NotExistPropertyException(const char *szNodeName,
      const char *szPropName) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_NOT_EXIST_PROPERTY,
            (boost::format("Cannot find %s property of %s node.") % szPropName
                % szNodeName).str()))
  {
  }

  InvalidPropertyValueException::InvalidPropertyValueException(
      const char *szValue, const char *szCorrectValueSet) throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::XML_INVALID_PROPERTY_VALUE,
            (boost::format("The value of property %s have to be [%s].")
                % szValue % szCorrectValueSet).str()))
  {
  }

  InvalidXMLSyntaxException::InvalidXMLSyntaxException(
      const char *szXmlErrorMessage, const char *szFileName, int nLine,
      int nCol) throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::XML_INVALID_SYNTAX,
            (boost::format("%s in %s, line %d, column %d") % szXmlErrorMessage
                % szFileName % nLine % nCol).str()))
  {
  }

  MutexInitFailException::MutexInitFailException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(DBGWErrorCode::EXTERNAL_MUTEX_INIT_FAIL,
            "Failed to init mutex object."))
  {
  }

  MemoryAllocationFail::MemoryAllocationFail(int nSize) throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::EXTERNAL_MEMORY_ALLOC_FAIL,
            (boost::format("Failed to allocate memory size (%d).") % nSize).str()))
  {
  }

  InvalidHandleException::InvalidHandleException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::EXTERNAL_DBGW_INVALID_HANDLE, "The handle is invalid."))
  {
  }

  NotEnoughBufferException::NotEnoughBufferException() throw() :
    DBGWException(
        DBGWExceptionFactory::create(
            DBGWErrorCode::EXTERNAL_DBGW_NOT_ENOUGH_BUFFER, "Not enough buffer memory."))
  {
  }

}
