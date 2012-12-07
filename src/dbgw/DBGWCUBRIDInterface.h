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
#ifndef DBGWCUBRIDINTERFACE_H
#define DBGWCUBRIDINTERFACE_H

namespace dbgw
{

  namespace db
  {

    class _CUBRIDException : public DBGWException
    {
    public:
      _CUBRIDException(const DBGWExceptionContext &context) throw();
      virtual ~ _CUBRIDException() throw();
    };

    class _CUBRIDExceptionFactory
    {
    public:
      static _CUBRIDException create(const string &errorMessage);
      static _CUBRIDException create(int nInterfaceErrorCode,
          const string &errorMessage);
      static _CUBRIDException create(int nInterfaceErrorCode,
          T_CCI_ERROR &cciError, const string &errorMessage);
    private:
      virtual ~_CUBRIDExceptionFactory();
    };

    class _DBGWCUBRIDConnection: public DBGWConnection
    {
    public:
      _DBGWCUBRIDConnection(const char *szUrl, const char *szUser,
          const char *szPassword);
      virtual ~ _DBGWCUBRIDConnection();

      virtual DBGWCallableStatementSharedPtr prepareCall(
          const char *szSql);
      virtual DBGWPreparedStatementSharedPtr prepareStatement(
          const char *szSql);

      virtual void cancel();

    protected:
      virtual void doConnect();
      virtual void doClose();
      virtual void doSetTransactionIsolation(DBGW_TRAN_ISOLATION isolation);
      virtual void doSetAutoCommit(bool bAutocommit);
      virtual void doCommit();
      virtual void doRollback();

    protected:
      virtual void *getNativeHandle() const;

    private:
      string m_url;
      string m_user;
      string m_password;
      int m_hCCIConnection;
    };

    struct _DBGWCUBRIDParameterMetaData
    {
      _DBGWCUBRIDParameterMetaData();
      _DBGWCUBRIDParameterMetaData(DBGWValueType type);

      bool unused;
      DBGWParameterMode mode;
      DBGWValueType type;
    };

    typedef vector<_DBGWCUBRIDParameterMetaData> _DBGWCUBRIDParameterMetaDataList;

    class _DBGWCUBRIDArrayParameter
    {
    public:
      _DBGWCUBRIDArrayParameter(const _DBGWParameterList &parameterList,
          DBGWValueType type, int nIndex);
      virtual ~_DBGWCUBRIDArrayParameter();

    public:
      void *toNativeArray() const;
      int *toNullIndArray() const;
      size_t size() const;

    private:
      DBGWValueType m_type;
      size_t m_size;
      vector<int> m_intList;
      vector<int64> m_longList;
      vector<float> m_floatList;
      vector<double> m_doubleList;
      vector<char *> m_cstrList;
      vector<int> m_nullIndList;
    };

    typedef boost::shared_ptr<_DBGWCUBRIDArrayParameter> _DBGWCUBRIDArrayParameterSharedPtr;
    typedef list<_DBGWCUBRIDArrayParameterSharedPtr> _DBGWCUBRIDArrayParameterList;

    class _DBGWCUBRIDStatementBase
    {
    public:
      _DBGWCUBRIDStatementBase(int hCCIConnection, const char *szSql);
      virtual ~_DBGWCUBRIDStatementBase();

      void addBatch();
      void clearBatch();
      void clearParameters();

      void prepare();
      void prepareCall();
      int execute();
      DBGWBatchResultSetSharedPtr executeArray();
      void close();

      void registerOutParameter(int nIndex, DBGWValueType type);

      void setInt(int nIndex, int nValue);
      void setLong(int nIndex, int64 lValue);
      void setChar(int nIndex, char cValue);
      void setString(int nIndex, const char *szValue);
      void setFloat(int nIndex, float fValue);
      void setDouble(int nIndex, double dValue);
      void setDate(int nIndex, const struct tm &tmValue);
      void setTime(int nIndex, const struct tm &tmValue);
      void setDateTime(int nIndex, const struct tm &tmValue);
      void setBytes(int nIndex, size_t nSize, const void *pValue);
      void setNull(int nIndex, DBGWValueType type);

    public:
      void *getNativeHandle() const;

    private:
      void setParameterMetaDataRaw(size_t nIndex, DBGWValueType type);
      void bindParameters();
      void bindArrayParameters();

    private:
      int m_hCCIConnection;
      int m_hCCIRequest;
      string m_sql;
      _DBGWParameter m_parameter;
      _DBGWParameterList m_parameterList;
      _DBGWCUBRIDArrayParameterList m_arrayParameterList;
      _DBGWCUBRIDParameterMetaDataList m_metaDataRawList;
    };

    class _DBGWCUBRIDPreparedStatement : public DBGWPreparedStatement
    {
    public:
      _DBGWCUBRIDPreparedStatement(DBGWConnectionSharedPtr pConnection,
          const char *szSql);
      virtual ~ _DBGWCUBRIDPreparedStatement();

      virtual bool addBatch();
      virtual bool clearBatch();
      virtual bool clearParameters();

      virtual DBGWResultSetSharedPtr executeQuery();
      virtual int executeUpdate();
      virtual DBGWBatchResultSetSharedPtr executeBatch();

      virtual bool setInt(int nIndex, int nValue);
      virtual bool setLong(int nIndex, int64 lValue);
      virtual bool setChar(int nIndex, char cValue);
      virtual bool setCString(int nIndex, const char *szValue);
      virtual bool setFloat(int nIndex, float fValue);
      virtual bool setDouble(int nIndex, double dValue);
      virtual bool setDate(int nIndex, const struct tm &tmValue);
      virtual bool setTime(int nIndex, const struct tm &tmValue);
      virtual bool setDateTime(int nIndex, const struct tm &tmValue);
      virtual bool setBytes(int nIndex, size_t nSize, const void *pValue);
      virtual bool setNull(int nIndex, DBGWValueType type);

    public:
      virtual void *getNativeHandle() const;

    protected:
      virtual void doClose();

    private:
      _DBGWCUBRIDStatementBase m_baseStatement;
    };

    struct _DBGWCUBRIDResultSetMetaDataRaw
    {
      _DBGWCUBRIDResultSetMetaDataRaw();

      bool unused;
      string columnName;
      DBGWValueType columnType;
    };

    typedef vector<_DBGWCUBRIDResultSetMetaDataRaw>
    _DBGWCUBRIDResultSetMetaDataRawList;

    class _DBGWCUBRIDCallableStatement : public DBGWCallableStatement
    {
    public:
      _DBGWCUBRIDCallableStatement(DBGWConnectionSharedPtr pConnection,
          const char *szSql);
      virtual ~_DBGWCUBRIDCallableStatement();

      virtual bool addBatch();
      virtual bool clearBatch();
      virtual bool clearParameters();

      virtual DBGWResultSetSharedPtr executeQuery();
      virtual DBGWBatchResultSetSharedPtr executeBatch();
      virtual int executeUpdate();

      virtual bool registerOutParameter(size_t nIndex, DBGWValueType type);

      virtual bool setInt(int nIndex, int nValue);
      virtual bool setLong(int nIndex, int64 lValue);
      virtual bool setChar(int nIndex, char cValue);
      virtual bool setCString(int nIndex, const char *szValue);
      virtual bool setFloat(int nIndex, float fValue);
      virtual bool setDouble(int nIndex, double dValue);
      virtual bool setDate(int nIndex, const struct tm &tmValue);
      virtual bool setTime(int nIndex, const struct tm &tmValue);
      virtual bool setDateTime(int nIndex, const struct tm &tmValue);
      virtual bool setBytes(int nIndex, size_t nSize, const void *pValue);
      virtual bool setNull(int nIndex, DBGWValueType type);

    public:
      virtual bool getType(int nIndex, DBGWValueType *pType) const;
      virtual bool getInt(int nIndex, int *pValue) const;
      virtual bool getCString(int nIndex, char **pValue) const;
      virtual bool getLong(int nIndex, int64 *pValue) const;
      virtual bool getChar(int nIndex, char *pValue) const;
      virtual bool getFloat(int nIndex, float *pValue) const;
      virtual bool getDouble(int nIndex, double *pValue) const;
      virtual bool getDateTime(int nIndex, struct tm *pValue) const;
      virtual bool getBytes(int nIndex, size_t *pSize, char **pValue) const;
      virtual const DBGWValue *getValue(int nIndex) const;

    public:
      virtual void *getNativeHandle() const;

    protected:
      virtual void doClose();

    private:
      bool isExistOutParameter() const;

    private:
      _DBGWCUBRIDStatementBase m_baseStatement;
      _DBGWCUBRIDResultSetMetaDataRawList m_metaDataRawList;
      DBGWResultSetSharedPtr m_pOutParamResult;
    };

    class _DBGWCUBRIDResultSetMetaData : public DBGWResultSetMetaData
    {
    public:
      _DBGWCUBRIDResultSetMetaData(int hCCIRequest);
      _DBGWCUBRIDResultSetMetaData(
          const _DBGWCUBRIDResultSetMetaDataRawList &metaDataRawList);
      virtual ~_DBGWCUBRIDResultSetMetaData();

    public:
      virtual size_t getColumnCount() const;
      virtual bool getColumnName(size_t nIndex, const char **szName) const;
      virtual bool getColumnType(size_t nIndex, DBGWValueType *pType) const;
      const _DBGWCUBRIDResultSetMetaDataRaw &getColumnInfo(size_t nIndex) const;

    private:
      _DBGWCUBRIDResultSetMetaDataRawList m_metaDataRawList;
    };

    typedef boost::shared_ptr<_DBGWCUBRIDResultSetMetaData>
    _DBGWCUBRIDResultSetMetaDataSharedPtr;

    class _DBGWCUBRIDResultSet : public DBGWResultSet
    {
    public:
      /**
       * this constructor called by DBGWCUBRIDStatement, _DBGWCUBRIDPreparedStatement.
       */
      _DBGWCUBRIDResultSet(DBGWStatementSharedPtr pStatement, int nRowCount);
      /**
       * this constructor called by DBGWCUBRIDCallableStatement.
       */
      _DBGWCUBRIDResultSet(DBGWStatementSharedPtr pStatement,
          const _DBGWCUBRIDResultSetMetaDataRawList &metaDataRawList);
      virtual ~_DBGWCUBRIDResultSet();

      virtual bool isFirst();
      virtual bool first();
      virtual bool next();

    public:
      virtual int getRowCount() const;
      virtual bool getType(int nIndex, DBGWValueType *pType) const;
      virtual bool getInt(int nIndex, int *pValue) const;
      virtual bool getCString(int nIndex, char **pValue) const;
      virtual bool getLong(int nIndex, int64 *pValue) const;
      virtual bool getChar(int nIndex, char *pValue) const;
      virtual bool getFloat(int nIndex, float *pValue) const;
      virtual bool getDouble(int nIndex, double *pValue) const;
      virtual bool getDateTime(int nIndex, struct tm *pValue) const;
      virtual bool getBytes(int nIndex, size_t *pSize, char **pValue) const;
      virtual const DBGWValue *getValue(int nIndex) const;
      virtual DBGWResultSetMetaDataSharedPtr getMetaData() const;

    protected:
      virtual void doClose();

    private:
      void makeResultSet();
      void getResultSetIntColumn(size_t nIndex,
          const _DBGWCUBRIDResultSetMetaDataRaw &md);
      void getResultSetLongColumn(size_t nIndex,
          const _DBGWCUBRIDResultSetMetaDataRaw &md);
      void getResultSetFloatColumn(size_t nIndex,
          const _DBGWCUBRIDResultSetMetaDataRaw &md);
      void getResultSetDoubleColumn(size_t nIndex,
          const _DBGWCUBRIDResultSetMetaDataRaw &md);
      void getResultSetBytesColumn(size_t nIndex,
          const _DBGWCUBRIDResultSetMetaDataRaw &md);
      void getResultSetStringColumn(size_t nIndex,
          const _DBGWCUBRIDResultSetMetaDataRaw &md);
      void doMakeResultSet(size_t nIndex, const char *szKey, DBGWValueType type,
          void *pValue, bool bNull, int nLength);

    private:
      int m_hCCIRequest;
      int m_nRowCount;
      int m_nFetchRowCount;
      T_CCI_CURSOR_POS m_cursorPos;
      _DBGWValueSet m_resultSet;
      _DBGWCUBRIDResultSetMetaDataSharedPtr m_pResultSetMetaData;
      _DBGWCUBRIDResultSetMetaDataRawList m_metaDataRawList;
    };

    class _DBGWCUBRIDBatchResultSet : public DBGWBatchResultSet
    {
    public:
      _DBGWCUBRIDBatchResultSet(size_t nSize,
          const T_CCI_QUERY_RESULT *pQueryResult);
      virtual ~_DBGWCUBRIDBatchResultSet();
    };

  }

}

#endif
