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

#ifndef CUBRIDSTATEMENTBASE_H_
#define CUBRIDSTATEMENTBASE_H_

namespace dbgw
{

  namespace sql
  {

    struct _CUBRIDParameterMetaData;
    typedef std::vector<_CUBRIDParameterMetaData> _CUBRIDParameterMetaDataList;

    class _CUBRIDArrayParameter
    {
    public:
      _CUBRIDArrayParameter(const _ParameterList &parameterList,
          ValueType type, int nIndex);
      virtual ~_CUBRIDArrayParameter() {}

      void makeNativeArray();

    public:
      void *toNativeArray() const;
      int *toNullIndArray() const;

    private:
      void doMakeIntNativeArray(const _Parameter &parameter);
      void doMakeLongNativeArray(const _Parameter &parameter);
      void doMakeFloatNativeArray(const _Parameter &parameter);
      void doMakeDoubleNativeArray(const _Parameter &parameter);
      void doMakeCStringNativeArray(const _Parameter &parameter);
      void doMakeBytesArray(const _Parameter &parameter);

    private:
      const _ParameterList &m_parameterList;
      ValueType m_type;
      int m_nIndex;
      size_t m_size;
      std::vector<int> m_intList;
      std::vector<int64> m_longList;
      std::vector<float> m_floatList;
      std::vector<double> m_doubleList;
      std::vector<const char *> m_cstrList;
      std::vector<int> m_nullIndList;
      std::vector<T_CCI_BIT> m_bitList;
    };

    struct _CUBRIDParameterMetaData
    {
      _CUBRIDParameterMetaData();
      _CUBRIDParameterMetaData(ValueType type);

      bool unused;
      ParameterMode mode;
      ValueType type;
    };

    class _CUBRIDStatementBase
    {
    public:
      _CUBRIDStatementBase(int hCCIConnection, const char *szSql);
      _CUBRIDStatementBase(int hCCIConnection, int hCCIRequest);
      virtual ~_CUBRIDStatementBase();

      void addBatch();
      void clearBatch();
      void clearParameters();

      void prepare();
      void prepareCall();
      int execute();
      std::vector<int> executeArray();
      void close();

      void registerOutParameter(int nIndex, ValueType type);

      void setInt(int nIndex, int nValue);
      void setLong(int nIndex, int64 lValue);
      void setChar(int nIndex, char cValue);
      void setString(int nIndex, const char *szValue);
      void setFloat(int nIndex, float fValue);
      void setDouble(int nIndex, double dValue);
      void setDate(int nIndex, const char *szValue);
      void setDate(int nIndex, const struct tm &tmValue);
      void setTime(int nIndex, const char *szValue);
      void setTime(int nIndex, const struct tm &tmValue);
      void setDateTime(int nIndex, const char *szValue);
      void setDateTime(int nIndex, const struct tm &tmValue);
      void setBytes(int nIndex, size_t nSize, const void *pValue);
      void setNull(int nIndex, ValueType type);

    public:
      void *getNativeHandle() const;

    private:
      void setParameterMetaDataRaw(size_t nIndex, ValueType type);
      void bindParameters();
      void bindArrayParameters();

    private:
      int m_hCCIConnection;
      int m_hCCIRequest;
      std::string m_sql;
      _Parameter m_parameter;
      _ParameterList m_parameterList;
      trait<_CUBRIDArrayParameter>::spvector m_arrayParameterList;
      _CUBRIDParameterMetaDataList m_metaDataRawList;
    };

  }

}

#endif
