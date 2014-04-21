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
#ifndef NCLAVISCLIENT_H_
#define NCLAVISCLIENT_H_

namespace dbgw
{

  enum _NClavisAuthType
  {
    NCLAVIS_AUTH_TYPE_IPV4 = 0x0000,
    NCLAVIS_AUTH_TYPE_KEYSTORE = 0x0001,
    NCLAVIS_AUTH_TYPE_MAC = 0x0002
  };

  class _NClavisGlobal
  {
  public:
    virtual ~_NClavisGlobal();

    static _NClavisGlobal *getInstance();
    void init();
    void cleanup();
    void lock();
    void unlock();

  private:
    _NClavisGlobal();

    class Impl;
    Impl *m_pImpl;
  };

  class _NClavisClient
  {
  public:
    _NClavisClient();
    virtual ~_NClavisClient();

    void setAuthTypeMac(const char *szMacAddr);
    void setAuthTypeKeyStore(const char *szCertFile,
        const char *szCertPasswd);
    void request(const char *szUrl, long lTimeout);

  public:
    std::string getDbName() const;
    std::string getDbUrl() const;
    std::string getDbId() const;
    std::string getDbPassword() const;

  private:
    class Impl;
    Impl *m_pImpl;
  };

}

#endif /* NCLAVISCLIENT_H_ */
