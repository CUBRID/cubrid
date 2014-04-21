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

#include "dbgw3/sql/mysql/MySQLCommon.h"
#include "dbgw3/sql/mysql/MySQLException.h"
#include "dbgw3/sql/mysql/MySQLValue.h"
#include "dbgw3/sql/mysql/MySQLResultSetMetaData.h"

namespace dbgw
{

  namespace sql
  {

    enum_field_types getMySQLType(ValueType type)
    {
      switch (type)
        {
        case DBGW_VAL_TYPE_INT:
          return MYSQL_TYPE_LONG;
        case DBGW_VAL_TYPE_LONG:
          return MYSQL_TYPE_LONGLONG;
        case DBGW_VAL_TYPE_FLOAT:
          return MYSQL_TYPE_FLOAT;
        case DBGW_VAL_TYPE_DOUBLE:
          return MYSQL_TYPE_DOUBLE;
        case DBGW_VAL_TYPE_CHAR:
          return MYSQL_TYPE_STRING;
        case DBGW_VAL_TYPE_BLOB:
        case DBGW_VAL_TYPE_BYTES:
          return MYSQL_TYPE_BLOB;
        case DBGW_VAL_TYPE_DATE:
        case DBGW_VAL_TYPE_TIME:
        case DBGW_VAL_TYPE_DATETIME:
        case DBGW_VAL_TYPE_CLOB:
        case DBGW_VAL_TYPE_STRING:
        default:
          return MYSQL_TYPE_VARCHAR;
        }
    }

    _MySQLBindList::_MySQLBindList() :
      m_pBindList(NULL)
    {
    }

    _MySQLBindList::~_MySQLBindList()
    {
      clear();
    }

    void _MySQLBindList::init(const _ValueSet &param)
    {
      clear();

      m_pBindList = new MYSQL_BIND[param.size()];
      memset(m_pBindList, 0, sizeof(MYSQL_BIND) * param.size());

      const Value *pValue = NULL;
      for (size_t i = 0; i < param.size(); i++)
        {
          pValue = param.getValue(i);
          if (pValue == NULL)
            {
              throw getLastException();
            }

          if (pValue->isNull())
            {
              m_pBindList[i].buffer_type = MYSQL_TYPE_NULL;
              m_pBindList[i].buffer = NULL;
              m_pBindList[i].buffer_length = 0;
            }
          else
            {
              m_pBindList[i].buffer_type = getMySQLType(pValue->getType());
              m_pBindList[i].buffer = pValue->getVoidPtr();
              m_pBindList[i].buffer_length = pValue->getLength();
            }
        }
    }

    void _MySQLBindList::clear()
    {
      if (m_pBindList != NULL)
        {
          delete[] m_pBindList;
          m_pBindList = NULL;
        }
    }

    MYSQL_BIND *_MySQLBindList::get()
    {
      return m_pBindList;
    }

    _MySQLDefine::_MySQLDefine(const _MySQLResultSetMetaDataRaw &md,
        MYSQL_BIND &bind) :
      m_bind(bind), m_pBuffer(NULL), m_pConvertedBuffer(NULL), m_indicator(0),
      m_ulLength(0)
    {
      memset(&m_linkedValue, 0, sizeof(_ExternelSource));

      m_bind.buffer_type = md.mysqlType;
      m_bind.buffer_length = md.length;
      m_bind.length = &m_ulLength;
      m_bind.is_null = &m_indicator;

      m_linkedValue.type = md.type;

      switch (md.type)
        {
        case DBGW_VAL_TYPE_INT:
          m_bind.buffer = (void *) &m_linkedValue.value.n;
          break;
        case DBGW_VAL_TYPE_LONG:
          m_bind.buffer = (void *) &m_linkedValue.value.l;
          break;
        case DBGW_VAL_TYPE_FLOAT:
          m_bind.buffer = (void *) &m_linkedValue.value.f;
          break;
        case DBGW_VAL_TYPE_DOUBLE:
          m_bind.buffer = (void *) &m_linkedValue.value.d;
          break;
        case DBGW_VAL_TYPE_CHAR:
        case DBGW_VAL_TYPE_STRING:
        case DBGW_VAL_TYPE_BYTES:
          m_pBuffer = new char[md.length];
          m_bind.buffer = (void *) m_pBuffer;
          m_linkedValue.value.p = m_pBuffer;
          break;
        case DBGW_VAL_TYPE_TIME:
          m_pBuffer = new char[md.length];
          m_bind.buffer = (void *) m_pBuffer;
          /* hh:mi:ss */
          m_pConvertedBuffer = new char[9];
          m_linkedValue.value.p = m_pConvertedBuffer;
          m_linkedValue.length = 9;
          break;
        case DBGW_VAL_TYPE_DATE:
          m_pBuffer = new char[md.length];
          m_bind.buffer = (void *) m_pBuffer;
          /* yyyy-mm-dd */
          m_pConvertedBuffer = new char[11];
          m_linkedValue.value.p = m_pConvertedBuffer;
          m_linkedValue.length = 11;
          break;
        case DBGW_VAL_TYPE_DATETIME:
          m_pBuffer = new char[md.length];
          m_bind.buffer = (void *) m_pBuffer;
          /* yyyy-mm-dd hh:mi:ss */
          m_pConvertedBuffer = new char[20];
          m_linkedValue.value.p = m_pConvertedBuffer;
          m_linkedValue.length = 20;
          break;
        case DBGW_VAL_TYPE_CLOB:
        case DBGW_VAL_TYPE_BLOB:
        default:
          break;
        }
    }

    _MySQLDefine::~_MySQLDefine()
    {
      if (m_pBuffer != NULL)
        {
          delete[] m_pBuffer;
        }

      if (m_pConvertedBuffer != NULL)
        {
          delete[] m_pConvertedBuffer;
        }
    }

    const _ExternelSource &_MySQLDefine::getExternalSource()
    {
      m_linkedValue.length = m_ulLength;
      m_linkedValue.isNull = m_indicator == 1 ? true : false;

      if (m_linkedValue.isNull)
        {
          return m_linkedValue;
        }

      if (m_linkedValue.type == DBGW_VAL_TYPE_TIME)
        {
          MYSQL_TIME *pMySQLTime = (MYSQL_TIME *) m_pBuffer;

          sprintf(m_pConvertedBuffer, "%02d:%02d:%02d", pMySQLTime->hour,
              pMySQLTime->minute, pMySQLTime->second);
        }
      else if (m_linkedValue.type == DBGW_VAL_TYPE_DATE)
        {
          MYSQL_TIME *pMySQLTime = (MYSQL_TIME *) m_pBuffer;

          sprintf(m_pConvertedBuffer, "%04d-%02d-%02d", pMySQLTime->year,
              pMySQLTime->month, pMySQLTime->day);
        }
      else if (m_linkedValue.type == DBGW_VAL_TYPE_DATETIME)
        {
          MYSQL_TIME *pMySQLTime = (MYSQL_TIME *) m_pBuffer;

          sprintf(m_pConvertedBuffer, "%04d-%02d-%02d %02d:%02d:%02d",
              pMySQLTime->year, pMySQLTime->month, pMySQLTime->day,
              pMySQLTime->hour, pMySQLTime->minute, pMySQLTime->second);
        }

      return m_linkedValue;
    }

    _MySQLDefineList::_MySQLDefineList() :
      m_pBindList(NULL)
    {
    }

    _MySQLDefineList::~_MySQLDefineList()
    {
      if (m_pBindList != NULL)
        {
          delete[] m_pBindList;
        }
    }

    void _MySQLDefineList::init(trait<MySQLResultSetMetaData>::sp pResultSetMeta)
    {
      if (m_pBindList != NULL)
        {
          delete[] m_pBindList;
        }

      m_pBindList = new MYSQL_BIND[pResultSetMeta->getColumnCount()];
      memset(m_pBindList, 0,
          sizeof(MYSQL_BIND) * pResultSetMeta->getColumnCount());
      m_defineList.clear();

      for (size_t i = 0; i < pResultSetMeta->getColumnCount(); i++)
        {
          const _MySQLResultSetMetaDataRaw &md = pResultSetMeta->getMetaData(i);
          trait<_MySQLDefine>::sp pDefine(new _MySQLDefine(md, m_pBindList[i]));
          m_defineList.push_back(pDefine);
        }
    }

    MYSQL_BIND *_MySQLDefineList::get()
    {
      return m_pBindList;
    }

    const _ExternelSource &_MySQLDefineList::getExternalSource(size_t nIndex)
    {
      return m_defineList[nIndex]->getExternalSource();
    }

  }

}
