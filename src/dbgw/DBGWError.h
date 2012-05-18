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

  namespace DBGWErrorCode
  {

    enum Enum
    {
      NO_ERROR 								= 0,

      CONF_NOT_EXIST_NAMESPACE 				= -100,
      CONF_NOT_EXIST_QUERY_IN_XML 			= -101,
      CONF_NOT_EXIST_ADDED_HOST 			= -102,
      CONF_FETCH_HOST_FAIL 					= -103,
      CONF_NOT_YET_LOADED 					= -104,
      CONF_NOT_EXIST_VERSION 				= -105,

      SQL_NOT_EXIST_CONN 					= -200,
      SQL_INVALID_SQL 						= -201,
      SQL_NOT_EXIST_PARAM 					= -202,
      SQL_EXECUTE_BEFORE_PREPARE 			= -203,

      VALUE_NOT_EXIST_SET 					= -300,
      VALUE_MISMATCH_VALUE_TYPE 			= -301,
      VALUE_INVALID_VALUE_TYPE 				= -302,

      CLIENT_MULTISET_IGNORE_FLAG_FALSE 	= -400,

      RESULT_NOT_ALLOWED_NEXT 				= -500,
      RESULT_NOT_ALLOWED_GET_METADATA 		= -501,
      RESULT_NOT_ALLOWED_OPERATION 			= -502,
      RESULT_VALIDATE_FAIL 					= -503,

      INTERFACE_ERROR 						= -600,

      XML_FAIL_CREATE_PARSER 				= -700,
      XML_DUPLICATE_NAMESPACE 				= -701,
      XML_DUPLICATE_SQLNAME 				= -702,
      XML_DUPLICATE_GROUPNAME 				= -703,
      XML_NOT_EXIST_NODE 					= -704,
      XML_NOT_EXIST_PROPERTY 				= -705,
      XML_INVALID_PROPERTY_VALUE 			= -706,
      XML_INVALID_SYNTAX 					= -707,

      MUTEX_INIT_FAIL 						= -800
    };

  }

  class DBGWException: public exception
  {
  public:
    DBGWException() throw();
    DBGWException(int nErrorCode) throw();
    DBGWException(int nErrorCode, const string &errorMessage) throw();
    DBGWException(int nErrorCode, const boost::format &fmt) throw();
    DBGWException(const DBGWException &exception) throw();
    virtual ~ DBGWException() throw();

  public:
    int getErrorCode() const;
    const char *getErrorMessage() const;
    const char *what() const throw();

  protected:
    void createErrorMessage();
    virtual void doCreateErrorMessage();

  protected:
    int m_nErrorCode;
    int m_nInterfaceErrorCode;
    string m_errorMessage;
    string m_what;
  };

  class DBGWInterfaceException: public DBGWException
  {
  public:
    DBGWInterfaceException() throw();
    DBGWInterfaceException(const string &errorMessage) throw();
    DBGWInterfaceException(int nInterfaceErrorCode) throw();
    DBGWInterfaceException(int nInterfaceErrorCode,
        const string &errorMessage) throw();
    DBGWInterfaceException(int nInterfaceErrorCode,
        const boost::format &fmt) throw();
    DBGWInterfaceException(const DBGWException &exception) throw();
    DBGWInterfaceException(const DBGWInterfaceException &exception) throw();

  public:
    int getInterfaceErrorCode() const;

  protected:
    int m_nInterfaceErrorCode;
  };

  extern void setLastException(const DBGWInterfaceException &exception);
  extern void clearException();
  extern DBGWInterfaceException getLastException();
  extern int getLastErrorCode();
  extern int getLastInterfaceErrorCode();
  extern const char *getLastErrorMessage();
  extern const char *getFormattedErrorMessage();

  class NotExistNamespaceException: public DBGWException
  {
  public:
    NotExistNamespaceException(const char *szNamespace) throw();
  };

  class NotExistQueryInXmlException: public DBGWException
  {
  public:
    NotExistQueryInXmlException(const char *szSqlName) throw();
  };

  class NotExistAddedHostException: public DBGWException
  {
  public:
    NotExistAddedHostException() throw();
  };

  class FetchHostFailException: public DBGWException
  {
  public:
    FetchHostFailException() throw();
  };

  class NotYetLoadedException: public DBGWException
  {
  public:
    NotYetLoadedException() throw();
  };

  class NotExistVersionException: public DBGWException
  {
  public:
    NotExistVersionException(int nVersion) throw();
  };

  class NotExistConnException: public DBGWException
  {
  public:
    NotExistConnException(const char *szGroupName) throw();
  };

  class InvalidSqlException: public DBGWException
  {
  public:
    InvalidSqlException(const char *szFileName, const char *szSqlName) throw();
  };

  class NotExistParamException: public DBGWException
  {
  public:
    NotExistParamException(int nIndex) throw();
    NotExistParamException(string name) throw();
  };

  class ExecuteBeforePrepareException: public DBGWException
  {
  public:
    ExecuteBeforePrepareException() throw();
  };

  class NotExistSetException: public DBGWException
  {
  public:
    NotExistSetException(const char *szKey) throw();
    NotExistSetException(size_t nIndex) throw();
  };

  class MismatchValueTypeException: public DBGWException
  {
  public:
    MismatchValueTypeException(int orgType, int convType) throw();
  };

  class InvalidValueTypeException: public DBGWException
  {
  public:
    InvalidValueTypeException(int type) throw();
    InvalidValueTypeException(const char *szType) throw();
  };

  class MultisetIgnoreResultFlagFalseException: public DBGWException
  {
  public:
    MultisetIgnoreResultFlagFalseException(const char *szSqlName) throw();
  };

  class NotAllowedNextException: public DBGWException
  {
  public:
    NotAllowedNextException() throw();
  };

  class NotAllowedGetMetadataException: public DBGWException
  {
  public:
    NotAllowedGetMetadataException() throw();
  };

  class NotAllowedOperationException: public DBGWException
  {
  public:
    NotAllowedOperationException(const char *szOperation,
        const char *szQueryType) throw();
  };

  class ValidateFailException: public DBGWException
  {
  public:
    ValidateFailException() throw();
    ValidateFailException(int lhs, int rhs) throw();
    ValidateFailException(const char *szName, const string &lhs) throw();
    ValidateFailException(const char *szName, const string &lhs,
        const string &rhs) throw();
  };

  class CreateFailParserExeception: public DBGWException
  {
  public:
    CreateFailParserExeception(const char *szFileName) throw();
  };

  class DuplicateNamespaceExeception: public DBGWException
  {
  public:
    DuplicateNamespaceExeception(const string &nameSpace,
        const string &fileNameNew, const string &fileNameOld) throw();
  };

  class DuplicateSqlNameException: public DBGWException
  {
  public:
    DuplicateSqlNameException(const char *szSqlName, const char *szFileNameNew,
        const char *szFileNameOld) throw();
  };

  class DuplicateGroupNameException: public DBGWException
  {
  public:
    DuplicateGroupNameException(const string &groupName,
        const string &fileNameNew, const string &fileNameOld) throw();
  };

  class NotExistNodeInXmlException: public DBGWException
  {
  public:
    NotExistNodeInXmlException(const char *szNodeName,
        const char *szXmlFile) throw();
  };

  class NotExistPropertyException: public DBGWException
  {
  public:
    NotExistPropertyException(const char *szNodeName,
        const char *szPropName) throw();
  };

  class InvalidPropertyValueException: public DBGWException
  {
  public:
    InvalidPropertyValueException(const char *szValue,
        const char *szCorrectValueSet) throw();
  };

  class MutexInitFailException: public DBGWException
  {
  public:
    MutexInitFailException() throw();
  };

}
;

#endif /* DBGWERROR_H_ */
