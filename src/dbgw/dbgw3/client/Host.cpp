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

#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/client/Host.h"

namespace dbgw
{

  _Host::_Host(const std::string &url, const std::string &user,
      const std::string &password, int nWeight) :
    m_url(url), m_user(user), m_password(password), m_nWeight(nWeight)
  {
  }

  void _Host::setAltHost(const std::string &address, const std::string &port)
  {
    m_althost = "?althosts=";
    m_althost += address;
    m_althost += ":";
    m_althost += port;
  }

  std::string _Host::getUrl() const
  {
    return m_url + m_althost;
  }

  const char *_Host::getUser() const
  {
    return m_user.c_str();
  }

  const char *_Host::getPassword() const
  {
    return m_password.c_str();
  }

  int _Host::getWeight() const
  {
    return m_nWeight;
  }

}
