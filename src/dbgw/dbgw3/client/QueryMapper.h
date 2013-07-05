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

#ifndef QUERYMAPPER_H_
#define QUERYMAPPER_H_

namespace dbgw
{

  typedef std::list<std::string> _QueryNameList;

  class _Query;
  class _BoundQuery;
  class _Parameter;
  class _Connector;

  typedef boost::unordered_map<std::string, trait<_Query>::spvector,
          boost::hash<std::string>, func::compareString> _QuerySqlHashMap;

  class _QueryMapper: public _Resource,
    public _ConfigurationObject
  {
  public:
    _QueryMapper(Configuration *pConfiguration);
    virtual ~ _QueryMapper();

    void addQuery(const std::string &sqlName, trait<_Query>::sp pQuery);
    void copyFrom(const _QueryMapper &src);
    void setVersion(QueryMapperVersion version);
    void parseQuery(trait<_Connector>::sp pConnector);

  public:
    size_t size() const;
    _QueryNameList getQueryNameList() const;
    trait<_BoundQuery>::sp getQuery(const char *szSqlName, const char *szGroupName,
        const _Parameter &parameter, bool bFirstGroup = false) const;
    QueryMapperVersion getVersion() const;

  private:
    /* (sqlName => (groupName => Query)) */
    _QuerySqlHashMap m_querySqlMap;
    QueryMapperVersion m_version;
  };

}

#endif
