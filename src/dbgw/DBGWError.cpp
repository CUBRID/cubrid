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
#include "DBGWCommon.h"
#include "DBGWError.h"
#include "DBGWValue.h"

namespace dbgw
{

  __thread DBGWInterfaceException *g_pException;

  void setLastException(const DBGWInterfaceException &exception)
  {
    if (g_pException != NULL)
      {
        delete g_pException;
      }

    g_pException = new DBGWInterfaceException(exception);
  }

  void clearException()
  {
    if (g_pException != NULL)
      {
        delete g_pException;
      }
    g_pException = NULL;
  }

  DBGWInterfaceException getLastException()
  {
    if (g_pException == NULL)
      {
        return DBGWInterfaceException();
      }
    else
      {
        return DBGWInterfaceException(*g_pException);;
      }
  }

  int getLastErrorCode()
  {
    if (g_pException == NULL)
      {
        return DBGWErrorCode::NO_ERROR;
      }

    return g_pException->getErrorCode();
  }

  int getLastInterfaceErrorCode()
  {
    if (g_pException == NULL)
      {
        return DBGWErrorCode::NO_ERROR;
      }

    return g_pException->getInterfaceErrorCode();
  }

  const char *getLastErrorMessage()
  {
    if (g_pException == NULL)
      {
        return "";
      }

    return g_pException->getErrorMessage();
  }

  const char *getFormattedErrorMessage()
  {
    if (g_pException == NULL)
      {
        return "";
      }

    return g_pException->what();
  }

  DBGWException::DBGWException() throw() :
    m_nErrorCode(DBGWErrorCode::NO_ERROR)
  {
  }

  DBGWException::DBGWException(int nErrorCode) throw() :
    m_nErrorCode(nErrorCode)
  {
    createErrorMessage();
  }

  DBGWException::DBGWException(int nErrorCode, const string &errorMessage) throw() :
    m_nErrorCode(nErrorCode), m_errorMessage(errorMessage)
  {
    createErrorMessage();
  }

  DBGWException::DBGWException(int nErrorCode, const boost::format &fmt) throw() :
    m_nErrorCode(nErrorCode), m_errorMessage(fmt.str())
  {
    createErrorMessage();
  }

  DBGWException::DBGWException(const DBGWException &exception) throw() :
    std::exception(exception), m_nErrorCode(exception.m_nErrorCode),
    m_errorMessage(exception.m_errorMessage), m_what(exception.m_what)
  {
  }

  DBGWException::DBGWException(const std::exception &exception) throw() :
    std::exception(exception),
    m_nErrorCode(DBGWErrorCode::EXTERNAL_STANDARD_ERROR),
    m_errorMessage(exception.what()), m_what(exception.what())
  {
  }

  DBGWException::~DBGWException() throw()
  {
  }

  int DBGWException::getErrorCode() const
  {
    return m_nErrorCode;
  }

  const char *DBGWException::getErrorMessage() const
  {
    return m_errorMessage.c_str();
  }

  const char *DBGWException::what() const throw()
  {
    return m_what.c_str();
  }

  void DBGWException::createErrorMessage()
  {
    doCreateErrorMessage();
  }

  void DBGWException::doCreateErrorMessage()
  {
    stringstream buffer;
    buffer << "[" << m_nErrorCode << "] " << m_errorMessage;
    m_what = buffer.str();
  }

  DBGWInterfaceException::DBGWInterfaceException() throw() :
    DBGWException(), m_nInterfaceErrorCode(DBGWErrorCode::NO_ERROR)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(const string &errorMessage) throw() :
    DBGWException(DBGWErrorCode::INTERFACE_ERROR, errorMessage),
    m_nInterfaceErrorCode(DBGWErrorCode::NO_ERROR)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(int nInterfaceErrorCode) throw() :
    DBGWException(DBGWErrorCode::INTERFACE_ERROR),
    m_nInterfaceErrorCode(nInterfaceErrorCode)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(int nInterfaceErrorCode,
      const string &errorMessage) throw() :
    DBGWException(DBGWErrorCode::INTERFACE_ERROR, errorMessage),
    m_nInterfaceErrorCode(nInterfaceErrorCode)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(int nInterfaceErrorCode,
      const boost::format &fmt) throw() :
    DBGWException(DBGWErrorCode::INTERFACE_ERROR, fmt),
    m_nInterfaceErrorCode(nInterfaceErrorCode)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(const DBGWException &exception) throw() :
    DBGWException(exception), m_nInterfaceErrorCode(DBGWErrorCode::NO_ERROR)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(
      const DBGWInterfaceException &exception) throw() :
    DBGWException(exception),
    m_nInterfaceErrorCode(exception.m_nInterfaceErrorCode)
  {
  }

  DBGWInterfaceException::DBGWInterfaceException(
      const std::exception &exception) throw() :
    DBGWException(exception), m_nInterfaceErrorCode(DBGWErrorCode::NO_ERROR)
  {
  }

  int DBGWInterfaceException::getInterfaceErrorCode() const
  {
    return m_nInterfaceErrorCode;
  }

  NotExistNamespaceException::NotExistNamespaceException(const char *szNamespace) throw() :
    DBGWException(
        DBGWErrorCode::CONF_NOT_EXIST_NAMESPACE,
        boost::format("The %s namespace is not exist.")
        % szNamespace)
  {
  }

  NotExistQueryInXmlException::NotExistQueryInXmlException(const char *szSqlName) throw() :
    DBGWException(
        DBGWErrorCode::CONF_NOT_EXIST_QUERY_IN_XML,
        boost::format("The %s query is not exist in xml.")
        % szSqlName)
  {
  }

  NotExistAddedHostException::NotExistAddedHostException() throw() :
    DBGWException(DBGWErrorCode::CONF_NOT_EXIST_ADDED_HOST,
        "There is no added host.")
  {
  }

  FetchHostFailException::FetchHostFailException() throw() :
    DBGWException(DBGWErrorCode::CONF_FETCH_HOST_FAIL, "Fetch host fail.")
  {
  }

  NotYetLoadedException::NotYetLoadedException() throw() :
    DBGWException(DBGWErrorCode::CONF_NOT_YET_LOADED,
        "Configuration is not yet loaded.")
  {
  }

  NotExistVersionException::NotExistVersionException(int nVersion) throw() :
    DBGWException(
        DBGWErrorCode::CONF_NOT_EXIST_VERSION,
        (boost::format(
            "The configuration of version %d is not exist.")
            % nVersion).str())
  {

  }

  NotExistConnException::NotExistConnException(const char *szGroupName) throw() :
    DBGWException(
        DBGWErrorCode::SQL_NOT_EXIST_CONN,
        boost::format("The %s connection group is not exist.")
        % szGroupName)
  {
  }

  InvalidSqlException::InvalidSqlException(const char *szFileName,
      const char *szSqlName) throw() :
    DBGWException(
        DBGWErrorCode::SQL_INVALID_SQL,
        boost::format("Cannot parse sql %s in %s.") % szSqlName
        % szFileName)
  {
  }

  NotExistParamException::NotExistParamException(int nIndex) throw() :
    DBGWException(
        DBGWErrorCode::SQL_NOT_EXIST_PARAM,
        boost::format(
            "The bind parameter (index : %d) is not exist.")
        % nIndex)
  {
  }

  NotExistParamException::NotExistParamException(string name) throw() :
    DBGWException(
        DBGWErrorCode::SQL_NOT_EXIST_PARAM,
        boost::format("The bind parameter (key : %s) is not exist.")
        % name)
  {
  }

  ExecuteBeforePrepareException::ExecuteBeforePrepareException() throw() :
    DBGWException(DBGWErrorCode::SQL_EXECUTE_BEFORE_PREPARE,
        "The query is executed before prepare.")
  {
  }

  NotExistSetException::NotExistSetException(const char *szKey) throw() :
    DBGWException(DBGWErrorCode::VALUE_NOT_EXIST_SET,
        boost::format("The %s key is not exist in set.") % szKey)
  {
  }

  NotExistSetException::NotExistSetException(size_t nIndex) throw() :
    DBGWException(
        DBGWErrorCode::VALUE_NOT_EXIST_SET,
        boost::format(
            "The value of position %d is not exist in set.")
        % nIndex)
  {
  }

  MismatchValueTypeException::MismatchValueTypeException(int orgType,
      int convType) throw() :
    DBGWException(
        DBGWErrorCode::VALUE_MISMATCH_VALUE_TYPE,
        boost::format("Cannot cast %s to %s.")
        % getDBGWValueTypeString(orgType)
        % getDBGWValueTypeString(convType))
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(int type) throw() :
    DBGWException(DBGWErrorCode::VALUE_INVALID_VALUE_TYPE,
        boost::format("The value type %d is invalid.") % type)
  {
  }

  InvalidValueTypeException::InvalidValueTypeException(const char *szType) throw() :
    DBGWException(DBGWErrorCode::VALUE_INVALID_VALUE_TYPE,
        boost::format("The value type %s is invalid.") % szType)
  {
  }

  InvalidValueFormatException::InvalidValueFormatException(const char *szType,
      const char *szFormat) throw() :
    DBGWException(DBGWErrorCode::VALUE_INVALID_VALUE_TYPE,
        boost::format("The %s is not valid %s type.") % szFormat % szType)
  {
  }

  MultisetIgnoreResultFlagFalseException::MultisetIgnoreResultFlagFalseException(
      const char *szSqlName) throw() :
    DBGWException(
        DBGWErrorCode::CLIENT_MULTISET_IGNORE_FLAG_FALSE,
        boost::format(
            "The 'ignore_result' flag should be set false only once in %s.")
        % szSqlName)
  {
  }

  InvalidClientException::InvalidClientException() throw() :
    DBGWException(DBGWErrorCode::CLIENT_INVALID_CLIENT,
        "The client is invalid.")
  {
  }

  NotAllowedNextException::NotAllowedNextException() throw() :
    DBGWException(DBGWErrorCode::RESULT_NOT_ALLOWED_NEXT,
        "The next() operation is allowed only select query.")
  {
  }

  NotAllowedGetMetadataException::NotAllowedGetMetadataException() throw() :
    DBGWException(DBGWErrorCode::RESULT_NOT_ALLOWED_GET_METADATA,
        "Only Select query is able to make metadata list.")
  {
  }

  NotAllowedOperationException::NotAllowedOperationException(
      const char *szOperation, const char *szQueryType) throw() :
    DBGWException(
        DBGWErrorCode::RESULT_NOT_ALLOWED_OPERATION,
        boost::format(
            "The %s operation is only allowed for query type %s.")
        % szOperation % szQueryType)
  {
  }

  ValidateFailException::ValidateFailException() throw() :
    DBGWException(DBGWErrorCode::RESULT_VALIDATE_FAIL,
        "The result type is different.")
  {
  }

  ValidateFailException::ValidateFailException(int lhs, int rhs) throw() :
    DBGWException(
        DBGWErrorCode::RESULT_VALIDATE_FAIL,
        (boost::format(
            "The affected row count is different each other. %d != %d")
            % lhs % rhs).str())
  {
  }

  ValidateTypeFailException::ValidateTypeFailException(const char *szName,
      const string &lhs, const char *szLhsType, const string &rhs,
      const char *szRhsType) throw() :
    DBGWException(
        DBGWErrorCode::RESULT_VALIDATE_TYPE_FAIL,
        (boost::format(
            "The %s's type is different each other. %s (%s) != %s (%s)")
            % szName % lhs % szLhsType % rhs % szRhsType).str())
  {
  }

  ValidateValueFailException::ValidateValueFailException(const char *szName,
      const string &lhs) throw() :
    DBGWException(
        DBGWErrorCode::RESULT_VALIDATE_FAIL,
        (boost::format(
            "The %s's value is different each other. %s != NULL")
            % szName % lhs).str())
  {
  }

  ValidateValueFailException::ValidateValueFailException(const char *szName,
      const string &lhs, const string &rhs) throw() :
    DBGWException(
        DBGWErrorCode::RESULT_VALIDATE_VALUE_FAIL,
        (boost::format("The %s's value is different each other. %s != %s")
            % szName % lhs % rhs).str())
  {
  }

  CreateFailParserExeception::CreateFailParserExeception(const char *szFileName) throw() :
    DBGWException(
        DBGWErrorCode::XML_FAIL_CREATE_PARSER,
        boost::format("Cannot create xml parser from %s.")
        % szFileName)
  {
  }

  DuplicateNamespaceExeception::DuplicateNamespaceExeception(
      const string &nameSpace, const string &fileNameNew,
      const string &fileNameOld) throw() :
    DBGWException(
        DBGWErrorCode::XML_DUPLICATE_NAMESPACE,
        boost::format(
            "The namspace %s in %s is already exist in %s.")
        % nameSpace % fileNameNew % fileNameOld)
  {
  }

  DuplicateSqlNameException::DuplicateSqlNameException(const char *szSqlName,
      const char *szFileNameNew, const char *szFileNameOld) throw() :
    DBGWException(
        DBGWErrorCode::XML_DUPLICATE_SQLNAME,
        boost::format("The %s in %s is already exist in %s.")
        % szSqlName % szFileNameNew % szFileNameOld)
  {
  }

  DuplicateGroupNameException::DuplicateGroupNameException(
      const string &groupName, const string &fileNameNew,
      const string &fileNameOld) throw() :
    DBGWException(
        DBGWErrorCode::XML_DUPLICATE_GROUPNAME,
        boost::format("The %s in %s is already exist in %s.")
        % groupName % fileNameNew % fileNameOld)
  {
  }

  NotExistNodeInXmlException::NotExistNodeInXmlException(const char *szNodeName,
      const char *szXmlFile) throw() :
    DBGWException(
        DBGWErrorCode::XML_NOT_EXIST_NODE,
        boost::format("The %s node is not exist in %s.")
        % szNodeName % szXmlFile)
  {
  }

  NotExistPropertyException::NotExistPropertyException(const char *szNodeName,
      const char *szPropName) throw() :
    DBGWException(
        DBGWErrorCode::XML_NOT_EXIST_PROPERTY,
        boost::format("Cannot find %s property of %s node.")
        % szPropName % szNodeName)
  {
  }

  InvalidPropertyValueException::InvalidPropertyValueException(
      const char *szValue, const char *szCorrectValueSet) throw() :
    DBGWException(
        DBGWErrorCode::XML_INVALID_PROPERTY_VALUE,
        boost::format("The value of property %s have to be [%s].")
        % szValue % szCorrectValueSet)
  {
  }

  InvalidXMLSyntaxException::InvalidXMLSyntaxException(
      const char *szXmlErrorMessage, const char *szFileName, int nLine, int nCol) throw() :
    DBGWException(DBGWErrorCode::XML_INVALID_SYNTAX,
        boost::format("%s in %s, line %d, column %d") % szXmlErrorMessage
        % szFileName % nLine % nCol)
  {
  }

  MutexInitFailException::MutexInitFailException() throw() :
    DBGWException(DBGWErrorCode::EXTERNAL_MUTEX_INIT_FAIL,
        "Failed to init mutex object.")
  {
  }

}
