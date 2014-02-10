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

#include <curl/curl.h>
#include <jansson.h>
#include "dbgw3/Common.h"
#include "dbgw3/Exception.h"
#include "dbgw3/Logger.h"
#include "dbgw3/NClavisClient.h"
#include "dbgw3/JsonParser.h"
#include "dbgw3/system/Mutex.h"

namespace dbgw
{

  class _NClavisGlobal
  {
  public:
    ~_NClavisGlobal()
    {
      cleanup();
    }

    void init()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_bIsCleanup == false)
        {
          return;
        }

      m_bIsCleanup = false;

      curl_global_init(CURL_GLOBAL_ALL);
    }

    void cleanup()
    {
      system::_MutexAutoLock lock(&m_mutex);

      if (m_bIsCleanup)
        {
          return;
        }

      m_bIsCleanup = true;

      curl_global_cleanup();
    }

    void lock()
    {
      m_mutex.lock();
    }

    void unlock()
    {
      m_mutex.unlock();
    }

    static trait<_NClavisGlobal>::sp getInstance()
    {
      static trait<_NClavisGlobal>::sp m_pInstance;
      static system::_Mutex mutex;

      system::_MutexAutoLock lock(&mutex);

      if (m_pInstance == NULL)
        {
          m_pInstance = trait<_NClavisGlobal>::sp(new _NClavisGlobal());
        }

      return m_pInstance;
    }

  private:
    _NClavisGlobal() :
      m_bIsCleanup(false)
    {
      curl_global_init(CURL_GLOBAL_ALL);
    }

    bool m_bIsCleanup;
    system::_Mutex m_mutex;
  };

  class _NClavisClient::Impl
  {
  public:
    Impl() :
      m_pCurl(NULL)
    {
      int nResult = CURLE_OK;

      trait<_NClavisGlobal>::sp pGlobal = _NClavisGlobal::getInstance();
      pGlobal->lock();
      m_pCurl = curl_easy_init();
      pGlobal->unlock();

      setCurlOption(CURLOPT_VERBOSE, "CURLOPT_VERBOSE", 0L);
      setCurlOption(CURLOPT_SSL_VERIFYHOST, "CURLOPT_SSL_VERIFYHOST", 0L);
      setCurlOption(CURLOPT_SSL_VERIFYPEER, "CURLOPT_SSL_VERIFYPEER", 0L);
      setCurlOption(CURLOPT_NOPROGRESS, "CURLOPT_NOPROGRESS", 1L);

      nResult = curl_easy_setopt(m_pCurl, CURLOPT_WRITEFUNCTION,
          Impl::curlWriteFunction);
      if (CURLE_OK != nResult)
        {
          NClavisOptionException e(nResult, "CURLOPT_WRITEFUNCTION");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      nResult = curl_easy_setopt(m_pCurl, CURLOPT_WRITEDATA, this);
      if (CURLE_OK != nResult)
        {
          NClavisOptionException e(nResult, "CURLOPT_WRITEDATA");
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
    }

    ~Impl()
    {
      if (m_pCurl)
        {
          trait<_NClavisGlobal>::sp pGlobal = _NClavisGlobal::getInstance();
          pGlobal->lock();
          curl_easy_cleanup(m_pCurl);
          pGlobal->unlock();
        }
    }

    void setAuthTypeMac(const char *szMacAddr)
    {
      int nResult = CURLE_OK;

      std::stringstream macAddr;
      macAddr << "AUTH_MAC_ADDR: " << szMacAddr;
      szMacAddr = macAddr.str().c_str();

      struct curl_slist *pHeader = NULL;
      pHeader = curl_slist_append(pHeader, szMacAddr);
      nResult = curl_easy_setopt(m_pCurl, CURLOPT_HTTPHEADER, pHeader);
      if (CURLE_OK != nResult)
        {
          NClavisOptionException e(nResult, "CURLOPT_HTTPHEADER", szMacAddr);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      DBGW_LOGF_DEBUG("CURL OPTION : %s = %s", "CURLOPT_HTTPHEADER", szMacAddr);
    }

    void setAuthTypeKeyStore(const char *szCertFile,
        const char *szCertPasswd)
    {
      setCurlOption(CURLOPT_SSLCERT, "CURLOPT_SSLCERT", szCertFile);
      setCurlOption(CURLOPT_SSLCERTTYPE, "CURLOPT_SSLCERTTYPE", "P12");
      setCurlOption(CURLOPT_SSLKEY, "CURLOPT_SSLKEY", szCertFile);
      setCurlOption(CURLOPT_SSLKEYTYPE, "CURLOPT_SSLKEYTYPE", "P12");
      setCurlOption(CURLOPT_SSLKEYPASSWD, "CURLOPT_SSLKEYPASSWD", szCertPasswd);
      setCurlOption(CURLOPT_SSL_VERIFYHOST, "CURLOPT_SSL_VERIFYHOST", 1L);
    }

    void request(const char *szUrl, long lTimeout)
    {
      int nResult = CURLE_OK;

      m_response.str("");

      setCurlOption(CURLOPT_URL, "CURLOPT_URL", szUrl);

      if (lTimeout > 0)
        {
          setCurlOption(CURLOPT_TIMEOUT, "CURLOPT_TIMEOUT", lTimeout);
        }

      nResult = curl_easy_perform(m_pCurl);
      if (CURLE_OK != nResult)
        {
          NClavisException e(nResult);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      parseResponse();
    }

    std::string getDbName() const
    {
      return m_dbConnectionInfoName;
    }

    std::string getDbUrl() const
    {
      return m_dbConnectionUrl;
    }

    std::string getDbId() const
    {
      return m_dbConnectionId;
    }

    std::string getDbPassword() const
    {
      return m_dbConnectionPassword;
    }

    static size_t curlWriteFunction(void *p, size_t nSize, size_t nMem,
        Impl *pImpl)
    {
      pImpl->appendCurlResponse((const char *) p);

      return (nSize * nMem);
    }

    void appendCurlResponse(const char *szResponse)
    {
      m_response << szResponse;
    }

    void parseResponse()
    {
      trait<_JsonNode>::sp pJsonRoot = _JsonParser::load(m_response.str());
      trait<_JsonNode>::sp pJsonResult = pJsonRoot->getJsonNode("result");
      if (pJsonResult->getString("status") == "FAIL")
        {
          NClavisException e(m_response.str().c_str());
          DBGW_LOG_ERROR(e.what());
          throw e;
        }

      trait<_JsonNodeList>::sp pJsonSecureDataList =
          pJsonResult->getJsonNodeList("secureData");
      trait<_JsonNode>::sp pJsonSecureData = pJsonSecureDataList->at(0);
      m_dbConnectionInfoName = pJsonSecureData->getString(
          "dbConnectionInfoName");
      m_dbConnectionId = pJsonSecureData->getString("dbConnectionId");
      m_dbConnectionPassword = pJsonSecureData->getString(
          "dbConnectionPassword");
      m_dbConnectionUrl = pJsonSecureData->getString(
          "dbConnectionUrl");
    }

    void setCurlOption(CURLoption option, const char *szOptionName,
        long lValue)
    {
      int nResult = CURLE_OK;

      nResult = curl_easy_setopt(m_pCurl, option, lValue);
      if (CURLE_OK != nResult)
        {
          NClavisOptionException e(nResult, szOptionName, lValue);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      DBGW_LOGF_DEBUG("CURL OPTION : %s = %d", szOptionName, lValue);
    }

    void setCurlOption(CURLoption option, const char *szOptionName,
        const char *szValue)
    {
      int nResult = CURLE_OK;

      nResult = curl_easy_setopt(m_pCurl, option, szValue);
      if (CURLE_OK != nResult)
        {
          NClavisOptionException e(nResult, szOptionName, szValue);
          DBGW_LOG_ERROR(e.what());
          throw e;
        }
      DBGW_LOGF_DEBUG("CURL OPTION : %s = %s", szOptionName, szValue);
    }

  private:
    CURL *m_pCurl;
    std::stringstream m_response;
    std::string m_dbConnectionInfoName;
    std::string m_dbConnectionId;
    std::string m_dbConnectionPassword;
    std::string m_dbConnectionUrl;
  };

  _NClavisClient::_NClavisClient() :
    m_pImpl(new Impl())
  {
  }

  _NClavisClient::~_NClavisClient()
  {
  }

  void _NClavisClient::setAuthTypeMac(const char *szMacAddr)
  {
    m_pImpl->setAuthTypeMac(szMacAddr);
  }

  void _NClavisClient::setAuthTypeKeyStore(const char *szCertFile,
      const char *szCertPasswd)
  {
    m_pImpl->setAuthTypeKeyStore(szCertFile, szCertPasswd);
  }

  void _NClavisClient::request(const char *szUrl, long lTimeout)
  {
    m_pImpl->request(szUrl, lTimeout);
  }

  std::string _NClavisClient::getDbName() const
  {
    return m_pImpl->getDbName();
  }

  std::string _NClavisClient::getDbUrl() const
  {
    return m_pImpl->getDbUrl();
  }

  std::string _NClavisClient::getDbId() const
  {
    return m_pImpl->getDbId();
  }

  std::string _NClavisClient::getDbPassword() const
  {
    return m_pImpl->getDbPassword();
  }

}
