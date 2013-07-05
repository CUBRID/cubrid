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

#include <iomanip>
#include <boost/lexical_cast.hpp>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/system/DBGWPorting.h"
#include "dbgw3/system/Mutex.h"
#include "dbgw3/system/ThreadEx.h"
#include "dbgw3/client/StatisticsMonitor.h"

namespace dbgw
{

  const int DEFAULT_PADDING_WIDTH = 1;
  const char *DEFAULT_PADDING = " ";

  _StatisticsItemColumnValue::_StatisticsItemColumnValue(
      _StatisticsItemColumnValueType type) :
    m_type(type)
  {
    memset(&m_value, 0, sizeof(_StatisticsItemColumnValueRaw));
  }

  _StatisticsItemColumnValue::~_StatisticsItemColumnValue()
  {
    if (m_type == DBGW_STAT_VAL_TYPE_STRING && m_value.szValue != NULL)
      {
        delete[] m_value.szValue;
      }
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator=(
      int nValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue = nValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue = (double) nValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator=(
      int64 lValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue = lValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue = (double) lValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator=(
      double dValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue = (int64) dValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue = dValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator=(
      const char *szValue)
  {
    int nLength = 0;

    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
        if (m_value.szValue != NULL)
          {
            delete[] m_value.szValue;
          }

        nLength = strlen(szValue) + 1;
        m_value.szValue = new char[nLength];
        memcpy(m_value.szValue, szValue, nLength);
        break;
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator+=(
      int nValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue += nValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue += (double) nValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator+=(
      int64 lValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue += lValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue += (double) lValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator+=(
      double dValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue += (int64) dValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue += dValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  _StatisticsItemColumnValue &_StatisticsItemColumnValue::operator-=(
      int64 lValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        m_value.lValue -= (int64) lValue;
        break;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        m_value.dValue -= lValue;
        break;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        break;
      }

    return *this;
  }

  bool _StatisticsItemColumnValue::operator<(int64 lValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        return m_value.lValue < lValue;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        return m_value.dValue < lValue;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        return false;
      }
  }

  bool _StatisticsItemColumnValue::operator<(double dValue)
  {
    switch (m_type)
      {
      case DBGW_STAT_VAL_TYPE_LONG:
        return m_value.lValue < dValue;
      case DBGW_STAT_VAL_TYPE_DOUBLE:
        return m_value.dValue < dValue;
      case DBGW_STAT_VAL_TYPE_STRING:
      default:
        return false;
      }
  }

  _StatisticsItemColumnValueType _StatisticsItemColumnValue::getType() const
  {
    return m_type;
  }

  int64 _StatisticsItemColumnValue::getLong() const
  {
    if (m_type == DBGW_STAT_VAL_TYPE_LONG)
      {
        return m_value.lValue;
      }
    else if (m_type == DBGW_STAT_VAL_TYPE_DOUBLE)
      {
        return (int64) m_value.dValue;
      }
    else
      {
        return 0;
      }
  }

  double _StatisticsItemColumnValue::getDouble() const
  {
    if (m_type == DBGW_STAT_VAL_TYPE_LONG)
      {
        return (double) m_value.lValue;
      }
    else if (m_type == DBGW_STAT_VAL_TYPE_DOUBLE)
      {
        return m_value.dValue;
      }
    else
      {
        return 0.0f;
      }
  }

  std::string _StatisticsItemColumnValue::getString() const
  {
    if (m_type == DBGW_STAT_VAL_TYPE_STRING)
      {
        return m_value.szValue;
      }
    else
      {
        return "";
      }
  }

  class _StatisticsItemColumn::Impl
  {
  public:
    Impl(trait<_StatisticsMonitor>::sp pMonitor, _StatisticsItemColumnType colType,
        _StatisticsItemColumnValueType valType, const char *szColumnName,
        int nMaxWidth, bool bIsVisible) :
      m_pMonitor(pMonitor), m_colType(colType), m_value(valType),
      m_colName(szColumnName), m_nMaxWidth(nMaxWidth),
      m_fmtflags(std::ios_base::left), m_nPrecision(0),
      m_bIsVisible(bIsVisible), m_lUpdateCount(0)
    {
      memset(m_szTmpBuffer, 0, 32);
    }

    ~Impl()
    {
    }

    void init(const char *szValue)
    {
      system::_MutexAutoLock lock(&m_mutex);

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
          m_value = szValue;
          break;
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
        case DBGW_STAT_COL_TYPE_MAX:
        default:
          break;
        }
    }

    void setRightAlign()
    {
      m_fmtflags = std::ios_base::right;
    }

    void setPrecision(int nPrecision)
    {
      m_nPrecision = nPrecision;
    }

    void writeHeader(std::stringstream &buffer)
    {
      if (m_bIsVisible)
        {
          buffer << std::setfill(' ') << std::setw(m_nMaxWidth);
          buffer << std::setiosflags(m_fmtflags);
          buffer << m_colName << " ";
          buffer << std::resetiosflags(m_fmtflags);
        }
    }

    void writeColumn(std::stringstream &buffer)
    {
      if (m_bIsVisible)
        {
          if (m_colType == DBGW_STAT_COL_TYPE_AVG)
            {
              switch (m_value.getType())
                {
                case DBGW_STAT_VAL_TYPE_LONG:
                  calcAverageBeforeWriteColumn(buffer, m_value.getLong());
                  break;
                case DBGW_STAT_VAL_TYPE_DOUBLE:
                  calcAverageBeforeWriteColumn(buffer, m_value.getDouble());
                  break;
                case DBGW_STAT_VAL_TYPE_STRING:
                default:
                  break;
                }
            }
          else
            {
              switch (m_value.getType())
                {
                case DBGW_STAT_VAL_TYPE_LONG:
                  doWriteColumn(buffer, m_value.getLong());
                  break;
                case DBGW_STAT_VAL_TYPE_DOUBLE:
                  doWriteColumn(buffer, m_value.getDouble());
                  break;
                case DBGW_STAT_VAL_TYPE_STRING:
                  doWriteColumn(buffer, m_value.getString());
                  break;
                default:
                  break;
                }
            }
        }
    }

    int getMaxWidth() const
    {
      return m_nMaxWidth;
    }

    int64 getLong() const
    {
      return m_value.getLong();
    }

    _StatisticsItemColumn::Impl &operator=(int64 lValue)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return *this;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
        case DBGW_STAT_COL_TYPE_MAX:
          m_value = lValue;
          break;
        default:
          break;
        }

      return *this;
    }

    _StatisticsItemColumn::Impl &operator=(double dValue)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return *this;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
        case DBGW_STAT_COL_TYPE_MAX:
          m_value = dValue;
          break;
        default:
          break;
        }

      return *this;
    }

    _StatisticsItemColumn::Impl &operator=(const char *szValue)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return *this;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
          m_value = szValue;
          break;
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
        case DBGW_STAT_COL_TYPE_MAX:
        default:
          break;
        }

      return *this;
    }

    void operator+=(int64 lValue)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
          m_value += lValue;
          break;
        case DBGW_STAT_COL_TYPE_MAX:
        default:
          break;
        }
    }

    void operator+=(double dValue)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
          m_value += dValue;
          break;
        case DBGW_STAT_COL_TYPE_MAX:
        default:
          break;
        }
    }

    void operator-=(int64 lValue)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;

      switch (m_colType)
        {
        case DBGW_STAT_COL_TYPE_STATIC:
        case DBGW_STAT_COL_TYPE_ADD:
        case DBGW_STAT_COL_TYPE_AVG:
          m_value -= lValue;
          break;
        case DBGW_STAT_COL_TYPE_MAX:
        default:
          break;
        }
    }

    void operator++(int)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;
      if (m_colType == DBGW_STAT_COL_TYPE_ADD
          || m_colType == DBGW_STAT_COL_TYPE_AVG)
        {
          m_value += 1;
        }
    }

    void operator--(int)
    {
      if (m_pMonitor->isRunning() == false)
        {
          return;
        }

      system::_MutexAutoLock lock(&m_mutex);

      m_lUpdateCount++;
      if (m_colType == DBGW_STAT_COL_TYPE_ADD
          || m_colType == DBGW_STAT_COL_TYPE_AVG)
        {
          m_value += -1;
        }
    }

    void calcAverageBeforeWriteColumn(
        std::stringstream &buffer, int64 lValue)
    {
      if (lValue <= 0)
        {
          doWriteColumn(buffer, (int64) 0);
        }
      else
        {
          doWriteColumn(buffer, (double) lValue / m_lUpdateCount);
        }
    }

    void calcAverageBeforeWriteColumn(
        std::stringstream &buffer, double dValue)
    {
      if (dValue <= 0)
        {
          doWriteColumn(buffer, 0.0f);
        }
      else
        {
          doWriteColumn(buffer, dValue / m_lUpdateCount);
        }
    }

    void doWriteColumn(std::stringstream &buffer,
        int64 lValue)
    {
      doWriteColumn(buffer, boost::lexical_cast<std::string>(lValue));
    }

    void doWriteColumn(std::stringstream &buffer,
        double dValue)
    {
      sprintf(m_szTmpBuffer, "%.*lf", m_nPrecision, dValue);
      doWriteColumn(buffer, m_szTmpBuffer);
    }

    void doWriteColumn(std::stringstream &buffer,
        const std::string &value)
    {
      buffer << std::setfill(' ') << std::setw(m_nMaxWidth);
      buffer << std::setiosflags(m_fmtflags);
      if (value.length() > (size_t) m_nMaxWidth)
        {
          buffer << value.substr(0, m_nMaxWidth);
        }
      else
        {
          buffer << value << DEFAULT_PADDING;
        }
      buffer << std::resetiosflags(m_fmtflags);
    }

  private:
    system::_Mutex m_mutex;
    trait<_StatisticsMonitor>::sp m_pMonitor;
    _StatisticsItemColumnType m_colType;
    _StatisticsItemColumnValue m_value;
    std::string m_colName;
    int m_nMaxWidth;
    std::ios_base::fmtflags m_fmtflags;
    int m_nPrecision;
    bool m_bIsVisible;
    int64 m_lUpdateCount;
    char m_szTmpBuffer[32];
  };

  _StatisticsItemColumn::_StatisticsItemColumn(
      trait<_StatisticsMonitor>::sp pMonitor, _StatisticsItemColumnType colType,
      _StatisticsItemColumnValueType valType, const char *szColumnName,
      int nMaxWidth, bool bIsVisible) :
    m_pImpl(new Impl(pMonitor, colType, valType, szColumnName, nMaxWidth,
        bIsVisible))
  {
  }

  _StatisticsItemColumn::~_StatisticsItemColumn()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  _StatisticsItemColumn &_StatisticsItemColumn::init(
      const char *szValue)
  {
    m_pImpl->init(szValue);
    return *this;
  }

  void _StatisticsItemColumn::setRightAlign()
  {
    m_pImpl->setRightAlign();
  }

  void _StatisticsItemColumn::setPrecision(int nPrecision)
  {
    m_pImpl->setPrecision(nPrecision);
  }

  void _StatisticsItemColumn::writeHeader(std::stringstream &buffer)
  {
    m_pImpl->writeHeader(buffer);
  }

  void _StatisticsItemColumn::writeColumn(std::stringstream &buffer)
  {
    m_pImpl->writeColumn(buffer);
  }

  int _StatisticsItemColumn::getMaxWidth() const
  {
    return m_pImpl->getMaxWidth();
  }

  int64 _StatisticsItemColumn::getLong() const
  {
    return m_pImpl->getLong();
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator=(int64 lValue)
  {
    *m_pImpl = lValue;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator=(double dValue)
  {
    *m_pImpl = dValue;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator=(
      const char *szValue)
  {
    *m_pImpl = szValue;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator+=(int64 lValue)
  {
    *m_pImpl += lValue;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator+=(
      double dValue)
  {
    *m_pImpl += dValue;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator-=(int64 lValue)
  {
    *m_pImpl += lValue;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator++(int)
  {
    (*m_pImpl)++;
    return *this;
  }

  _StatisticsItemColumn &_StatisticsItemColumn::operator--(int)
  {
    (*m_pImpl)--;
    return *this;
  }

  void _StatisticsItemColumn::calcAverageBeforeWriteColumn(
      std::stringstream &buffer, int64 lValue)
  {
    m_pImpl->calcAverageBeforeWriteColumn(buffer, lValue);
  }

  void _StatisticsItemColumn::calcAverageBeforeWriteColumn(
      std::stringstream &buffer, double dValue)
  {
    m_pImpl->calcAverageBeforeWriteColumn(buffer, dValue);
  }

  void _StatisticsItemColumn::doWriteColumn(std::stringstream &buffer,
      int64 lValue)
  {
    doWriteColumn(buffer, boost::lexical_cast<std::string>(lValue));
  }

  void _StatisticsItemColumn::doWriteColumn(std::stringstream &buffer,
      double dValue)
  {
    m_pImpl->doWriteColumn(buffer, dValue);
  }

  void _StatisticsItemColumn::doWriteColumn(std::stringstream &buffer,
      const std::string &value)
  {
    m_pImpl->doWriteColumn(buffer, value);
  }

  _StatisticsItem::_StatisticsItem(const char *szPrefix) :
    m_prefix(szPrefix), m_nTotalWidth(0), m_bNeedRemove(false)
  {
  }

  _StatisticsItem::~_StatisticsItem()
  {
    _StatisticsItemColumnList::iterator it = m_colList.begin();
    for (; it != m_colList.end(); it++)
      {
        if ((*it) != NULL)
          {
            delete(*it);
          }
      }

    m_colList.clear();
  }

  void _StatisticsItem::addColumn(_StatisticsItemColumn *pColumn)
  {
    m_colList.push_back(pColumn);
    m_nTotalWidth += pColumn->getMaxWidth() + DEFAULT_PADDING_WIDTH;
  }

  _StatisticsItemColumn &_StatisticsItem::getColumn(int nIndex)
  {
    return *(m_colList[nIndex]);
  }

  void _StatisticsItem::writeHeader(std::stringstream &buffer)
  {
    struct timeval tp;
    struct tm cal;

    gettimeofday(&tp, NULL);
    time_t t = tp.tv_sec;

    localtime_r(&t, &cal);
    cal.tm_year += 1900;
    cal.tm_mon += 1;

    char szBuffer[128];
    snprintf(szBuffer, 128, "%d-%02d-%02d %02d:%02d:%02d.%03d",
        cal.tm_year, cal.tm_mon, cal.tm_mday, cal.tm_hour, cal.tm_min,
        cal.tm_sec, (int) tp.tv_usec / 1000);

    writePrefix(buffer);
    buffer << "\n";

    writePrefix(buffer);
    buffer << "  " << szBuffer;
    buffer << "\n";

    writePrefix(buffer);
    for (int i = 0; i < m_nTotalWidth - DEFAULT_PADDING_WIDTH; i++)
      {
        buffer << "=";
      }
    buffer << "\n";

    writePrefix(buffer);
    _StatisticsItemColumnList::iterator it = m_colList.begin();
    for (int i = 0; it != m_colList.end(); it++, i++)
      {
        (*it)->writeHeader(buffer);
      }
    buffer << "\n";

    writePrefix(buffer);
    for (int i = 0; i < m_nTotalWidth - DEFAULT_PADDING_WIDTH; i++)
      {
        buffer << "=";
      }
    buffer << "\n";
  }

  void _StatisticsItem::writeItem(std::stringstream &buffer)
  {
    writePrefix(buffer);
    _StatisticsItemColumnList::iterator it = m_colList.begin();
    for (int i = 0; it != m_colList.end(); it++, i++)
      {
        (*it)->writeColumn(buffer);
      }
    buffer << "\n";
  }

  void _StatisticsItem::removeAfterWriteItem()
  {
    m_bNeedRemove = true;
  }

  bool _StatisticsItem::needRemove() const
  {
    return m_bNeedRemove;
  }

  void _StatisticsItem::writePrefix(std::stringstream &buffer)
  {
    buffer << m_prefix << "|";
  }

  class _StatisticsGroup::Impl
  {
  public:
    Impl(bool bNeedForceWriteHeader) :
      m_nPrintCount(0), m_bNeedForceWriteHeader(bNeedForceWriteHeader)
    {
    }

    ~Impl()
    {
    }

    void addItem(const std::string &key,
        trait<_StatisticsItem>::sp pItem)
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_itemMap[key] = pItem;
    }

    void removeItem(const std::string &key)
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_itemMap.erase(key);
    }

    void clearItem()
    {
      system::_MutexAutoLock lock(&m_mutex);

      m_itemMap.clear();
    }

    void writeGroup(std::stringstream &buffer)
    {
      system::_MutexAutoLock lock(&m_mutex);

      _StatisticsItemHashMap::iterator it = m_itemMap.begin();
      if (m_bNeedForceWriteHeader)
        {
          /**
           * write header every first row.
           */
          while (it != m_itemMap.end())
            {
              if (it == m_itemMap.begin())
                {
                  it->second->writeHeader(buffer);
                }

              it->second->writeItem(buffer);

              if (it->second->needRemove())
                {
                  m_itemMap.erase(it++);
                }
              else
                {
                  it++;
                }
            }
        }
      else
        {
          /**
           * write header every 40 row.
           */
          while (it != m_itemMap.end())
            {
              if (m_nPrintCount % 40 == 0)
                {
                  it->second->writeHeader(buffer);
                }

              it->second->writeItem(buffer);

              if (it->second->needRemove())
                {
                  m_itemMap.erase(it++);
                }
              else
                {
                  it++;
                }

              m_nPrintCount++;
            }
        }
    }

  private:
    system::_Mutex m_mutex;
    _StatisticsItemHashMap m_itemMap;
    int m_nPrintCount;
    bool m_bNeedForceWriteHeader;
  };

  _StatisticsGroup::_StatisticsGroup(bool bNeedForceWriteHeader) :
    m_pImpl(new Impl(bNeedForceWriteHeader))
  {
  }

  _StatisticsGroup::~_StatisticsGroup()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  void _StatisticsGroup::addItem(const std::string &key,
      trait<_StatisticsItem>::sp pItem)
  {
    m_pImpl->addItem(key, pItem);
  }

  void _StatisticsGroup::removeItem(const std::string &key)
  {
    m_pImpl->removeItem(key);
  }

  void _StatisticsGroup::clearItem()
  {
    m_pImpl->clearItem();
  }

  void _StatisticsGroup::writeGroup(std::stringstream &buffer)
  {
    m_pImpl->writeGroup(buffer);
  }

  class _StatisticsMonitor::Impl
  {
  public:
    Impl(_StatisticsMonitor *pSelf) :
      m_pSelf(pSelf), m_queryStatGroup(false), m_connPoolStatGroup(false),
      m_workerStatGroup(true), m_proxyPoolStatGroup(false), m_logger(NULL),
      m_nStatType(DBGW_STAT_TYPE_ALL()),
      m_ulLogIntervalMilSec(10)
    {
    }

    ~Impl()
    {
    }

    _StatisticsGroup *getQueryStatGroup()
    {
      return &m_queryStatGroup;
    }

    _StatisticsGroup *getConnPoolStatGroup()
    {
      return &m_connPoolStatGroup;
    }

    _StatisticsGroup *getWorkerStatGroup()
    {
      return &m_workerStatGroup;
    }

    _StatisticsGroup *getProxyPoolStatGroup()
    {
      return &m_proxyPoolStatGroup;
    }

    void init(const char *szLogPath,
        int nStatTYpe, unsigned long ulLogIntervalMilSec, int nMaxFileSizeKBytes,
        int nMaxBackupCount)
    {
      if (strcmp(m_logPath.c_str(), szLogPath) != 0)
        {
          clear();
        }

      m_logPath = szLogPath;
      m_nStatType = nStatTYpe;
      m_ulLogIntervalMilSec = ulLogIntervalMilSec;

      Logger logger = cci_log_get(szLogPath);
      cci_log_use_default_prefix(logger, false);
      cci_log_use_default_newline(logger, false);
      cci_log_change_max_file_size_appender(logger, nMaxFileSizeKBytes,
          nMaxBackupCount);

      system::_MutexAutoLock lock(&m_mutex);
      m_logger = logger;
    }

    void clear()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_logger != NULL)
        {
          m_logger = NULL;
          cci_log_remove(m_logPath.c_str());
        }

      m_logPath = "";
    }

    bool isRunning()
    {
      system::_MutexAutoLock lock(&m_mutex);
      return m_logger != NULL;
    }

    void writeStatistics()
    {
      m_buffer.str("");

      if (isRunning() == false)
        {
          return;
        }

      if (m_nStatType & DBGW_STAT_TYPE_CONNECTION())
        {
          m_connPoolStatGroup.writeGroup(m_buffer);
        }

      if (m_nStatType & DBGW_STAT_TYPE_QUERY())
        {
          m_queryStatGroup.writeGroup(m_buffer);
        }

      if (m_nStatType & DBGW_STAT_TYPE_WORKER())
        {
          m_workerStatGroup.writeGroup(m_buffer);
        }

      if (m_nStatType & DBGW_STAT_TYPE_STATEMENT())
        {
          m_proxyPoolStatGroup.writeGroup(m_buffer);
        }

      system::_MutexAutoLock lock(&m_mutex);
      if (m_logger != NULL)
        {
          CCI_LOG_INFO(m_logger, m_buffer.str().c_str());
        }
    }

    unsigned long getLogIntervalMilSec()
    {
      return m_ulLogIntervalMilSec;
    }

    static void run(const system::_ThreadEx *pThread)
    {
      if (pThread == NULL)
        {
          FailedToCreateThreadException e("statistics logger");
          DBGW_LOG_ERROR(e.what());
          return;
        }

      _StatisticsMonitor::Impl *pMonitorImpl =
          ((_StatisticsMonitor *) pThread)->m_pImpl;

      unsigned long ulLogIntervalMilSec;

      while (pThread->isRunning())
        {
          ulLogIntervalMilSec = pMonitorImpl->getLogIntervalMilSec();

          pMonitorImpl->writeStatistics();

          if (ulLogIntervalMilSec > 0)
            {
              if (pThread->sleep(ulLogIntervalMilSec) == false)
                {
                  break;
                }
            }
        }
    }

  private:
    _StatisticsMonitor *m_pSelf;
    _StatisticsGroup m_queryStatGroup;
    _StatisticsGroup m_connPoolStatGroup;
    _StatisticsGroup m_workerStatGroup;
    _StatisticsGroup m_proxyPoolStatGroup;

    Logger m_logger;
    system::_Mutex m_mutex;
    std::string m_logPath;
    int m_nStatType;
    unsigned long m_ulLogIntervalMilSec;
    std::stringstream m_buffer;
  };

  const char *_StatisticsMonitor::DEFAULT_LOG_PATH()
  {
    return "log/dbgw_statistics.log";
  }

  unsigned long _StatisticsMonitor::DEFAULT_LOG_INTERVAL_MILSEC()
  {
    return 1 * 1000;
  }

  int _StatisticsMonitor::DEFAULT_MAX_FILE_SIZE_KBYTES()
  {
    return 1000 * 1024;
  }

  int _StatisticsMonitor::DEFAULT_MAX_BACKUP_COUNT()
  {
    return 5;
  }

  int _StatisticsMonitor::DBGW_STAT_TYPE_QUERY()
  {
    return 0x00000001;
  }

  int _StatisticsMonitor::DBGW_STAT_TYPE_CONNECTION()
  {
    return 0x00000002;
  }

  int _StatisticsMonitor::DBGW_STAT_TYPE_WORKER()
  {
    return 0x00000004;
  }

  int _StatisticsMonitor::DBGW_STAT_TYPE_STATEMENT()
  {
    return 0x00000008;
  }

  int _StatisticsMonitor::DBGW_STAT_TYPE_ALL()
  {
    return DBGW_STAT_TYPE_QUERY() | DBGW_STAT_TYPE_CONNECTION()
        | DBGW_STAT_TYPE_STATEMENT() | DBGW_STAT_TYPE_WORKER();
  }

  _StatisticsMonitor::_StatisticsMonitor() :
    system::_ThreadEx(Impl::run), m_pImpl(new Impl(this))
  {
  }

  _StatisticsMonitor::~_StatisticsMonitor()
  {
    if (m_pImpl != NULL)
      {
        delete m_pImpl;
      }
  }

  _StatisticsGroup *_StatisticsMonitor::getQueryStatGroup()
  {
    return m_pImpl->getQueryStatGroup();
  }

  _StatisticsGroup *_StatisticsMonitor::getConnPoolStatGroup()
  {
    return m_pImpl->getConnPoolStatGroup();
  }

  _StatisticsGroup *_StatisticsMonitor::getWorkerStatGroup()
  {
    return m_pImpl->getWorkerStatGroup();
  }

  _StatisticsGroup *_StatisticsMonitor::getProxyPoolStatGroup()
  {
    return m_pImpl->getProxyPoolStatGroup();
  }

  void _StatisticsMonitor::init(const char *szLogPath,
      int nStatTYpe, unsigned long ulLogIntervalMilSec, int nMaxFileSizeKBytes,
      int nMaxBackupCount)
  {
    m_pImpl->init(szLogPath, nStatTYpe, ulLogIntervalMilSec,
        nMaxFileSizeKBytes, nMaxBackupCount);
  }

  void _StatisticsMonitor::clear()
  {
    m_pImpl->clear();
  }

  bool _StatisticsMonitor::isRunning() const
  {
    return m_pImpl->isRunning();
  }

}
