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

#include <jansson.h>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/JsonParser.h"

namespace dbgw
{

  trait<_JsonNode>::sp _JsonParser::load(const std::string &jsonString)
  {
    json_error_t jsonError;

    json_t *pJsonRoot = json_loads(jsonString.c_str(), 0, &jsonError);
    if (pJsonRoot == NULL)
      {
        JsonDecodeFailException e(jsonError.line, jsonError.text);
        DBGW_LOG_ERROR(e.what());
        throw e;
      }

    trait<_JsonNode>::sp pJsonNode(new _JsonNode(pJsonRoot, true));
    return pJsonNode;
  }

  class _JsonNode::Impl
  {
  public:
    Impl(json_t *pJsonNode, bool bNeedCleanup) :
      m_pJsonNode(pJsonNode), m_bNeedCleanup(bNeedCleanup)
    {
    }

    ~Impl()
    {
      if (m_pJsonNode && m_bNeedCleanup)
        {
          json_decref(m_pJsonNode);
        }
    }

    std::string getString(const char *szKey) const
    {
      json_t *pJsonObj = NULL;

      pJsonObj = json_object_get(m_pJsonNode, szKey);
      if (pJsonObj == NULL)
        {
          JsonNotExistKeyException e(szKey);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (json_is_string(pJsonObj) == false)
        {
          JsonMismatchTypeException e(szKey, "string");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      const char *szValue = json_string_value(pJsonObj);
      if (szValue == NULL)
        {
          return "";
        }
      else
        {
          return szValue;
        }
    }

    trait<_JsonNode>::sp getJsonNode(const char *szKey)
    {
      json_t *pJsonObj = NULL;

      pJsonObj = json_object_get(m_pJsonNode, szKey);
      if (pJsonObj == NULL)
        {
          JsonNotExistKeyException e(szKey);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (json_is_object(pJsonObj) == false)
        {
          JsonMismatchTypeException e(szKey, "object");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<_JsonNode>::sp pJsonNode(new _JsonNode(pJsonObj, false));
      return pJsonNode;
    }

    trait<_JsonNodeList>::sp getJsonNodeList(const char *szKey)
    {
      json_t *pJsonObj = NULL;

      pJsonObj = json_object_get(m_pJsonNode, szKey);
      if (pJsonObj == NULL)
        {
          JsonNotExistKeyException e(szKey);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      if (json_is_array(pJsonObj) == false)
        {
          JsonMismatchTypeException e(szKey, "array");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<_JsonNodeList>::sp pJsonNode(new _JsonNodeList(pJsonObj, false));
      return pJsonNode;
    }

    const json_t *getNativeHandle() const
    {
      return m_pJsonNode;
    }

  private:
    json_t *m_pJsonNode;
    bool m_bNeedCleanup;
  };

  _JsonNode::_JsonNode(json_t *pJsonNode, bool bNeedCleanup) :
    m_pImpl(new Impl(pJsonNode, bNeedCleanup))
  {
  }

  _JsonNode::~_JsonNode()
  {
    if (m_pImpl)
      {
        delete m_pImpl;
      }
  }

  std::string _JsonNode::getString(const char *szKey) const
  {
    return m_pImpl->getString(szKey);
  }

  trait<_JsonNode>::sp _JsonNode::getJsonNode(const char *szKey)
  {
    return m_pImpl->getJsonNode(szKey);
  }

  trait<_JsonNodeList>::sp _JsonNode::getJsonNodeList(const char *szKey)
  {
    return m_pImpl->getJsonNodeList(szKey);
  }

  const json_t *_JsonNode::getNativeHandle() const
  {
    return m_pImpl->getNativeHandle();
  }

  _JsonNodeList::_JsonNodeList(json_t *pJsonNode, bool bNeedCleanup) :
    _JsonNode(pJsonNode, bNeedCleanup)
  {
  }

  size_t _JsonNodeList::size() const
  {
    return json_array_size(getNativeHandle());
  }

  trait<_JsonNode>::sp _JsonNodeList::at(size_t nIndex)
  {
    json_t *pJsonObj = json_array_get(getNativeHandle(), nIndex);
    trait<_JsonNode>::sp pJsonNode(new _JsonNode(pJsonObj, false));
    return pJsonNode;
  }

}
