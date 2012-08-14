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
      NO_ERROR                                  = 0,

      CONF_NOT_EXIST_NAMESPACE                  = -22100,
      CONF_NOT_EXIST_QUERY_IN_XML               = -22101,
      CONF_NOT_EXIST_ADDED_HOST                 = -22102,
      CONF_FETCH_HOST_FAIL                      = -22103,
      CONF_NOT_YET_LOADED                       = -22104,
      CONF_NOT_EXIST_VERSION                    = -22105,
      CONF_NOT_EXIST_FILE                       = -22106,

      SQL_NOT_EXIST_CONN                        = -22200,
      SQL_INVALID_SQL                           = -22201,
      SQL_NOT_EXIST_PARAM                       = -22202,
      SQL_EXECUTE_BEFORE_PREPARE                = -22203,

      VALUE_NOT_EXIST_SET                       = -22300,
      VALUE_MISMATCH_VALUE_TYPE                 = -22301,
      VALUE_INVALID_VALUE_TYPE                  = -22302,
      VALUE_INVALID_VALUE_FORMAT                = -22303,

      CLIENT_MULTISET_IGNORE_FLAG_FALSE         = -22400,
      CLIENT_INVALID_CLIENT                     = -22401,

      RESULT_NOT_ALLOWED_NEXT                   = -22500,
      RESULT_NOT_ALLOWED_GET_METADATA           = -22501,
      RESULT_NOT_ALLOWED_OPERATION              = -22502,
      RESULT_VALIDATE_FAIL                      = -22503,
      RESULT_VALIDATE_TYPE_FAIL                 = -22504,
      RESULT_VALIDATE_VALUE_FAIL                = -22505,

      INTERFACE_ERROR                           = -22600,

      XML_FAIL_CREATE_PARSER                    = -22700,
      XML_DUPLICATE_NAMESPACE                   = -22701,
      XML_DUPLICATE_SQLNAME                     = -22702,
      XML_DUPLICATE_GROUPNAME                   = -22703,
      XML_NOT_EXIST_NODE                        = -22704,
      XML_NOT_EXIST_PROPERTY                    = -22705,
      XML_INVALID_PROPERTY_VALUE                = -22706,
      XML_INVALID_SYNTAX                        = -22707,

      EXTERNAL_MUTEX_INIT_FAIL                  = -22800,
      EXTERNAL_STANDARD_ERROR                   = -22801,
      EXTERNAL_MEMORY_ALLOC_FAIL                = -22802
    };

  }

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
  };

  class InvalidValueFormatException : public DBGWException
  {
  public:
    InvalidValueFormatException(const char *szType, const char *szFormat) throw();
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
    NotExistPropertyException(const char *szNodeName,
        const char *szPropName) throw();
  };

  class InvalidPropertyValueException : public DBGWException
  {
  public:
    InvalidPropertyValueException(const char *szValue,
        const char *szCorrectValueSet) throw();
  };

  class InvalidXMLSyntaxException : public DBGWException
  {
  public:
    InvalidXMLSyntaxException(const char *szXmlErrorMessage,
        const char *szFileName, int nLine, int nCol) throw();
  };

  class MutexInitFailException : public DBGWException
  {
  public:
    MutexInitFailException() throw();
  };

  class MemoryAllocationFail : public DBGWException
  {
  public:
    MemoryAllocationFail(int nSize) throw();
  };

}
;

#endif /* DBGWERROR_H_ */
