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

#ifndef STATISTICSMONITOR_H_
#define STATISTICSMONITOR_H_

namespace dbgw
{

  enum _StatisticsItemColumnType
  {
    DBGW_STAT_COL_TYPE_STATIC = 0,
    DBGW_STAT_COL_TYPE_ADD,
    DBGW_STAT_COL_TYPE_AVG,
    DBGW_STAT_COL_TYPE_MAX
  };

  enum _StatisticsItemColumnValueType
  {
    DBGW_STAT_VAL_TYPE_LONG = 0,
    DBGW_STAT_VAL_TYPE_DOUBLE,
    DBGW_STAT_VAL_TYPE_STRING
  };

  class _StatisticsMonitor;
  class _StatisticsGroup;

  class _StatisticsItem;
  typedef boost::unordered_map<std::string, _StatisticsItem *,
          boost::hash<std::string>, func::compareString> _StatisticsItemHashMap;

  class _StatisticsItemColumn;
  typedef std::vector<_StatisticsItemColumn *> _StatisticsItemColumnList;

  union _StatisticsItemColumnValueRaw
  {
    int64 lValue;
    double dValue;
    char *szValue;
  };

  class _StatisticsItemColumnValue
  {
  public:
    _StatisticsItemColumnValue(_StatisticsItemColumnValueType type);
    virtual ~_StatisticsItemColumnValue();

    _StatisticsItemColumnValue &operator=(int nValue);
    _StatisticsItemColumnValue &operator=(int64 lValue);
    _StatisticsItemColumnValue &operator=(double dValue);
    _StatisticsItemColumnValue &operator=(const char *szValue);

    _StatisticsItemColumnValue &operator+=(int nValue);
    _StatisticsItemColumnValue &operator+=(int64 lValue);
    _StatisticsItemColumnValue &operator+=(double dValue);

    _StatisticsItemColumnValue &operator-=(int64 lValue);

    bool operator<(int64 lValue);
    bool operator<(double dValue);

  public:
    _StatisticsItemColumnValueType getType() const;
    int64 getLong() const;
    double getDouble() const;
    const char *getCString() const;

  private:
    _StatisticsItemColumnValueType m_type;
    _StatisticsItemColumnValueRaw m_value;
  };

  class _StatisticsItemColumn
  {
  public:
    _StatisticsItemColumn(trait<_StatisticsMonitor>::sp pMonitor,
        _StatisticsItemColumnType colType,
        _StatisticsItemColumnValueType valType, const char *szColumnName,
        int nMaxWidth, bool bIsVisible = true);
    virtual ~_StatisticsItemColumn();

    _StatisticsItemColumn &init(const char *szValue);

    void setRightAlign();
    void setPrecision(int nPrecision);

    void writeHeader(std::stringstream &buffer);
    void writeColumn(std::stringstream &buffer);

  public:
    int getMaxWidth() const;
    int64 getLong() const;

  public:
    _StatisticsItemColumn &operator=(int64 lValue);
    _StatisticsItemColumn &operator=(double dValue);
    _StatisticsItemColumn &operator=(const char *szValue);
    _StatisticsItemColumn &operator+=(int64 lValue);
    _StatisticsItemColumn &operator+=(double dValue);
    _StatisticsItemColumn &operator-=(int64 lValue);
    _StatisticsItemColumn &operator++(int);
    _StatisticsItemColumn &operator--(int);

  private:
    void calcAverageBeforeWriteColumn(std::stringstream &buffer, int64 lValue);
    void calcAverageBeforeWriteColumn(std::stringstream &buffer, double dValue);

    void doWriteColumn(std::stringstream &buffer, int64 lValue);
    void doWriteColumn(std::stringstream &buffer, double dValue);
    void doWriteColumn(std::stringstream &buffer, const std::string &value);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _StatisticsItem
  {
  public:
    _StatisticsItem(const char *szPrefix);
    virtual ~_StatisticsItem();

    void addColumn(_StatisticsItemColumn *pColumn);
    void writeHeader(std::stringstream &buffer);
    void writeItem(std::stringstream &buffer);
    void removeAfterWriteItem();

  public:
    bool needRemove() const;
    _StatisticsItemColumn &operator[](int nIndex) const;

  private:
    void writePrefix(std::stringstream &buffer);

  private:
    _StatisticsItemColumnList m_colList;
    std::string m_prefix;
    int m_nTotalWidth;
    bool m_bNeedRemove;
  };

  class _StatisticsGroup
  {
  public:
    _StatisticsGroup(bool bNeedForceWriteHeader = false);
    virtual ~_StatisticsGroup();

    void addItem(const std::string &key, _StatisticsItem *pItem);
    void removeItem(const std::string &key);
    void clearItem();
    void writeGroup(std::stringstream &buffer);

  private:
    class Impl;
    Impl *m_pImpl;
  };

  class _StatisticsMonitor : public system::_ThreadEx
  {
  public:
    static const char *DEFAULT_LOG_PATH();
    static unsigned long DEFAULT_LOG_INTERVAL_MILSEC();
    static int DEFAULT_MAX_FILE_SIZE_KBYTES();
    static int DEFAULT_MAX_BACKUP_COUNT();
    static int DBGW_STAT_TYPE_QUERY();
    static int DBGW_STAT_TYPE_CONNECTION();
    static int DBGW_STAT_TYPE_WORKER();
    static int DBGW_STAT_TYPE_STATEMENT();
    static int DBGW_STAT_TYPE_ALL();

  public:
    _StatisticsMonitor();
    virtual ~_StatisticsMonitor();

    _StatisticsGroup *getQueryStatGroup();
    _StatisticsGroup *getConnPoolStatGroup();
    _StatisticsGroup *getWorkerStatGroup();
    _StatisticsGroup *getProxyPoolStatGroup();

    void init(const char *szLogPath, int nStatTYpe,
        unsigned long ulLogIntervalMilSec, int nMaxFileSizeKBytes,
        int nMaxBackupCount);
    void clear();

  public:
    bool isRunning() const;

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif
