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
#ifndef DBGWDATABASEINTERFACE_H
#define DBGWDATABASEINTERFACE_H

namespace dbgw
{

  namespace db
  {

    typedef enum
    {
      DBGW_TRAN_UNKNOWN = 0,
      DBGW_TRAN_READ_UNCOMMITED,
      DBGW_TRAN_READ_COMMITED,
      DBGW_TRAN_REPEATABLE_READ,
      DBGW_TRAN_SERIALIZABLE
    } DBGW_TRAN_ISOLATION;

    enum DBGWDataBaseType
    {
      DBGW_DB_TYPE_CUBRID = 0,
      DBGW_DB_TYPE_MYSQL,
      DBGW_DB_TYPE_ORACLE
    };

    enum DBGWStatementType
    {
      DBGW_STMT_TYPE_UNDEFINED = -1,
      DBGW_STMT_TYPE_SELECT = 0,
      DBGW_STMT_TYPE_PROCEDURE,
      DBGW_STMT_TYPE_UPDATE,
      DBGW_STMT_TYPE_SIZE
    };

    enum DBGWBatchExecuteStatus
    {
      DBGW_BATCH_EXEC_SUCCESS,
      DBGW_BATCH_EXEC_FAIL
    };

    class DBGWConnection;
    typedef boost::shared_ptr<DBGWConnection> DBGWConnectionSharedPtr;

    class DBGWStatement;
    typedef boost::shared_ptr<DBGWStatement> DBGWStatementSharedPtr;

    class DBGWPreparedStatement;
    typedef boost::shared_ptr<DBGWPreparedStatement> DBGWPreparedStatementSharedPtr;

    class DBGWCallableStatement;
    typedef boost::shared_ptr<DBGWCallableStatement> DBGWCallableStatementSharedPtr;

    class DBGWResultSet;
    typedef boost::shared_ptr<DBGWResultSet> DBGWResultSetSharedPtr;

    class DBGWResultSetMetaData;
    typedef boost::shared_ptr<DBGWResultSetMetaData> DBGWResultSetMetaDataSharedPtr;

    class DBGWBatchResultSet;
    typedef boost::shared_ptr<DBGWBatchResultSet> DBGWBatchResultSetSharedPtr;

    class DBGWDriverManager
    {
    public:
      static DBGWConnectionSharedPtr getConnection(const char *szUrl,
          DBGWDataBaseType dbType = DBGW_DB_TYPE_CUBRID);
      static DBGWConnectionSharedPtr getConnection(const char *szUrl,
          const char *szUser, const char *szPassword,
          DBGWDataBaseType dbType = DBGW_DB_TYPE_CUBRID);
    };

    /**
     * External access class.
     */
    class DBGWConnection : public boost::enable_shared_from_this<DBGWConnection>
    {
    public:
      DBGWConnection(const char *szUrl, const char *szUser,
          const char *szPassword);
      virtual ~ DBGWConnection();

      bool connect();
      bool close();

      virtual DBGWCallableStatementSharedPtr prepareCall(
          const char *szSql) = 0;
      virtual DBGWPreparedStatementSharedPtr prepareStatement(
          const char *szSql) = 0;

      bool setTransactionIsolation(DBGW_TRAN_ISOLATION isolation);
      bool setAutoCommit(bool bAutocommit);
      bool commit();
      bool rollback();
      virtual void cancel() = 0;

    public:
      bool getAutoCommit() const;
      DBGW_TRAN_ISOLATION getTransactionIsolation() const;
      bool isClosed() const;
      virtual void *getNativeHandle() const = 0;

    protected:
      virtual void doConnect() = 0;
      virtual void doClose() = 0;
      virtual void doSetTransactionIsolation(DBGW_TRAN_ISOLATION isolation) = 0;
      virtual void doSetAutoCommit(bool bAutocommit) = 0;
      virtual void doCommit() = 0;
      virtual void doRollback() = 0;

    private:
      bool m_bConnected;
      bool m_bClosed;
      bool m_bAutoCommit;
      DBGW_TRAN_ISOLATION m_isolation;
    };

    /**
     * External access class.
     */
    class DBGWStatement : public boost::enable_shared_from_this<DBGWPreparedStatement>
    {
    public:
      DBGWStatement(DBGWConnectionSharedPtr pConnection);
      virtual ~DBGWStatement();

      bool close();

      virtual DBGWResultSetSharedPtr executeQuery(const char *szSql) = 0;
      virtual int executeUpdate(const char *szSql) = 0;
      virtual DBGWBatchResultSetSharedPtr executeBatch() = 0;

    public:
      DBGWConnectionSharedPtr getConnection() const;
      virtual void *getNativeHandle() const = 0;

    protected:
      virtual void doClose() = 0;

    private:
      bool m_bClosed;
      DBGWConnectionSharedPtr m_pConnection;
    };

    enum DBGWParameterMode
    {
      DBGW_PARAM_MODE_IN = 0,
      DBGW_PARAM_MODE_INOUT,
      DBGW_PARAM_MODE_OUT
    };

    /**
     * External access class.
     */
    class DBGWPreparedStatement : public DBGWStatement
    {
    public:
      DBGWPreparedStatement(DBGWConnectionSharedPtr pConnection);
      virtual ~ DBGWPreparedStatement();

      virtual bool addBatch() = 0;
      virtual bool clearBatch() = 0;
      virtual bool clearParameters() = 0;

      virtual DBGWResultSetSharedPtr executeQuery() = 0;
      virtual int executeUpdate() = 0;
      virtual DBGWBatchResultSetSharedPtr executeBatch() = 0;

      virtual bool setInt(int nIndex, int nValue) = 0;
      virtual bool setLong(int nIndex, int64 lValue) = 0;
      virtual bool setChar(int nIndex, char cValue) = 0;
      virtual bool setCString(int nIndex, const char *szValue) = 0;
      virtual bool setFloat(int nIndex, float fValue) = 0;
      virtual bool setDouble(int nIndex, double dValue) = 0;
      virtual bool setDate(int nIndex, const struct tm &tmValue) = 0;
      virtual bool setTime(int nIndex, const struct tm &tmValue) = 0;
      virtual bool setDateTime(int nIndex, const struct tm &tmValue) = 0;
      virtual bool setBytes(int nIndex, size_t nSize, const void *pValue) = 0;
      virtual bool setNull(int nIndex, DBGWValueType type) = 0;

    private:
      /**
       * prepared statement doesn't use these methods.
       */
      virtual DBGWResultSetSharedPtr executeQuery(const char *szSql);
      virtual int executeUpdate(const char *szSql);
    };

    /**
     * External access class.
     */
    class DBGWCallableStatement : public DBGWPreparedStatement
    {
    public:
      DBGWCallableStatement(DBGWConnectionSharedPtr pConnection);
      virtual ~DBGWCallableStatement();

      virtual DBGWResultSetSharedPtr executeQuery() = 0;
      virtual int executeUpdate() = 0;

      virtual bool registerOutParameter(size_t nIndex, DBGWValueType type) = 0;

      virtual bool setInt(int nIndex, int nValue) = 0;
      virtual bool setLong(int nIndex, int64 lValue) = 0;
      virtual bool setChar(int nIndex, char cValue) = 0;
      virtual bool setCString(int nIndex, const char *szValue) = 0;
      virtual bool setFloat(int nIndex, float fValue) = 0;
      virtual bool setDouble(int nIndex, double dValue) = 0;
      virtual bool setDate(int nIndex, const struct tm &tmValue) = 0;
      virtual bool setTime(int nIndex, const struct tm &tmValue) = 0;
      virtual bool setDateTime(int nIndex, const struct tm &tmValue) = 0;
      virtual bool setBytes(int nIndex, size_t nSize, const void *pValue) = 0;
      virtual bool setNull(int nIndex, DBGWValueType type) = 0;

    public:
      virtual bool getType(int nIndex, DBGWValueType *pType) const = 0;
      virtual bool getInt(int nIndex, int *pValue) const = 0;
      virtual bool getCString(int nIndex, char **pValue) const = 0;
      virtual bool getLong(int nIndex, int64 *pValue) const = 0;
      virtual bool getChar(int nIndex, char *pValue) const = 0;
      virtual bool getFloat(int nIndex, float *pValue) const = 0;
      virtual bool getDouble(int nIndex, double *pValue) const = 0;
      virtual bool getDateTime(int nIndex, struct tm *pValue) const = 0;
      virtual bool getBytes(int nIndex, size_t *pSize, char **pValue) const = 0;
      virtual const DBGWValue *getValue(int nIndex) const = 0;
    };

    /**
     * External access class.
     */
    class DBGWResultSetMetaData
    {
    public:
      virtual ~DBGWResultSetMetaData();

    public:
      virtual size_t getColumnCount() const = 0;
      virtual bool getColumnName(size_t nIndex, const char **szName) const = 0;
      virtual bool getColumnType(size_t nIndex, DBGWValueType *pType) const = 0;
    };

    /**
     * External access class.
     */
    class DBGWResultSet
    {
    public:
      DBGWResultSet(DBGWStatementSharedPtr pStatement);
      virtual ~DBGWResultSet();

      bool close();
      virtual bool isFirst() = 0;
      virtual bool first() = 0;
      virtual bool next() = 0;

    public:
      virtual int getRowCount() const = 0;
      virtual bool getType(int nIndex, DBGWValueType *pType) const = 0;
      virtual bool getInt(int nIndex, int *pValue) const = 0;
      virtual bool getCString(int nIndex, char **pValue) const = 0;
      virtual bool getLong(int nIndex, int64 *pValue) const = 0;
      virtual bool getChar(int nIndex, char *pValue) const = 0;
      virtual bool getFloat(int nIndex, float *pValue) const = 0;
      virtual bool getDouble(int nIndex, double *pValue) const = 0;
      virtual bool getDateTime(int nIndex, struct tm *pValue) const = 0;
      virtual bool getBytes(int nIndex, size_t *pSize, char **pValue) const = 0;
      virtual const DBGWValue *getValue(int nIndex) const = 0;
      virtual DBGWResultSetMetaDataSharedPtr getMetaData() const = 0;

    protected:
      virtual void doClose() = 0;

    private:
      bool m_bClosed;
      DBGWStatementSharedPtr m_pStatement;
    };

    struct DBGWBatchResultSetRaw
    {
      int affectedRow;
      int errorCode;
      string errorMessage;
      DBGWStatementType queryType;
    };

    typedef vector<DBGWBatchResultSetRaw> DBGWBatchResultSetRawList;

    /**
     * External access class.
     */
    class DBGWBatchResultSet
    {
    public:
      DBGWBatchResultSet(size_t nSize);
      virtual ~DBGWBatchResultSet();

    public:
      size_t size() const;
      DBGWBatchExecuteStatus getExecuteStatus() const;
      bool getAffectedRow(size_t nIndex, int *pAffectedRow) const;
      bool getErrorCode(size_t nIndex, int *pErrorCode) const;
      bool getErrorMessage(size_t nIndex, const char **pErrorMessage) const;
      bool getStatementType(size_t nIndex,
          DBGWStatementType *pStatementType) const;

    protected:
      void setExecuteStatus(DBGWBatchExecuteStatus executeStatus);
      void addBatchResultSetRaw(
          const DBGWBatchResultSetRaw &batchResultSetRaw);

    private:
      size_t m_nSize;
      DBGWBatchExecuteStatus m_executeStatus;
      DBGWBatchResultSetRawList m_batchResultSetRawList;
    };

  }

}

#endif
