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
#ifndef JSONPARSER_H_
#define JSONPARSER_H_

namespace dbgw
{

  class _JsonNode;
  class _JsonNodeList;

  class _JsonParser
  {
  public:
    static trait<_JsonNode>::sp load(const std::string &jsonString);
  };

  class _JsonNode
  {
  public:
    _JsonNode(json_t *pJsonNode, bool bNeedCleanup);
    virtual ~_JsonNode();

    std::string getString(const char *szKey) const;
    trait<_JsonNode>::sp getJsonNode(const char *szKey);
    trait<_JsonNodeList>::sp getJsonNodeList(const char *szKey);

  protected:
    const json_t *getNativeHandle() const;

  protected:
    class Impl;
    Impl *m_pImpl;
  };

  class _JsonNodeList : public _JsonNode
  {
  public:
    _JsonNodeList(json_t *pJsonNode, bool bNeedCleanup);
    virtual ~_JsonNodeList() {}

    size_t size() const;
    trait<_JsonNode>::sp at(size_t nIndex);
  };

}

#endif /* JSONPARSER_H_ */
