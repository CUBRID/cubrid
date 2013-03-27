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

#ifndef MYSQLBIND_H_
#define MYSQLBIND_H_

namespace dbgw
{

  namespace sql
  {

    class MySQLResultSetMetaData;
    class _MySQLResultSetMetaDataRaw;

    class _MySQLBindList
    {
    public:
      _MySQLBindList();
      virtual ~_MySQLBindList();

      void init(const _ValueSet &param);
      void clear();
      MYSQL_BIND *get();

    private:
      MYSQL_BIND *m_pBindList;
    };

    class _MySQLDefine
    {
    public:
      _MySQLDefine(const _MySQLResultSetMetaDataRaw &md, MYSQL_BIND &bind);
      virtual ~_MySQLDefine();

      const _ExternelSource &getExternalSource();

    private:
      MYSQL_BIND &m_bind;
      char *m_pBuffer;
      char *m_pConvertedBuffer;
      my_bool m_indicator;
      unsigned long m_ulLength;
      _ExternelSource m_linkedValue;
    };

    class _MySQLDefineList
    {
    public:
      _MySQLDefineList();
      virtual ~_MySQLDefineList();

      void init(trait<MySQLResultSetMetaData>::sp pResultSetMeta);
      MYSQL_BIND *get();
      const _ExternelSource &getExternalSource(size_t nIndex);

    private:
      trait<_MySQLDefine>::spvector m_defineList;
      MYSQL_BIND *m_pBindList;
    };

  }

}

#endif
