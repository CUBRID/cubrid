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

#include "dbgw3/sql/oracle/OracleCommon.h"
#include "dbgw3/sql/oracle/OracleParameterMetaData.h"

namespace dbgw
{

  namespace sql
  {

    _OracleParameterMetaData::_OracleParameterMetaData() :
      m_type(DBGW_VAL_TYPE_UNDEFINED), m_mode(DBGW_PARAM_MODE_NONE), m_nSize(0)
    {
    }

    void _OracleParameterMetaData::setParamType(ValueType type)
    {
      m_type = type;
    }

    void _OracleParameterMetaData::setParamMode(ParameterMode mode)
    {
      if (m_mode ==  DBGW_PARAM_MODE_NONE)
        {
          m_mode = mode;
        }
      else if (m_mode != mode)
        {
          m_mode = DBGW_PARAM_MODE_INOUT;
        }
    }

    void _OracleParameterMetaData::setReservedSize(int nSize)
    {
      m_nSize = nSize;
    }

    bool _OracleParameterMetaData::isUsed() const
    {
      return m_mode != DBGW_PARAM_MODE_NONE;
    }

    bool _OracleParameterMetaData::isInParameter() const
    {
      return m_mode == DBGW_PARAM_MODE_IN;
    }

    int _OracleParameterMetaData::getSize() const
    {
      return m_nSize;
    }

    bool _OracleParameterMetaData::isLazyBindingOutParameter() const
    {
      return m_mode == DBGW_PARAM_MODE_OUT && m_type == DBGW_VAL_TYPE_RESULTSET;
    }

    ValueType _OracleParameterMetaData::getType() const
    {
      return m_type;
    }

  }

}
