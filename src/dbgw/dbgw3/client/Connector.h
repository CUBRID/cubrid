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

#ifndef CONNECTOR_H_
#define CONNECTOR_H_

namespace dbgw
{

  class _Service;

  class _Connector: public _Resource,
    public _ConfigurationObject
  {
  public:
    _Connector(Configuration *pConfiguration);
    virtual ~ _Connector();

    void addService(trait<_Service>::sp pService);
    trait<_Service>::sp getService(const char *szNameSpace);

  private:
    /* (namespace => _Service) */
    trait<_Service>::spvector m_serviceList;
  };

}

#endif
