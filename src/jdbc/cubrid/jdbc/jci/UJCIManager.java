/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.net.Socket;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Hashtable;

abstract public class UJCIManager {
	// static Vector connectionList;
	static String sysCharsetName;
	static Hashtable<UUrlHostKey, UUrlCache> url_cache_table;
	static ArrayList<UUrlCache> url_cache_remove_list;
	static JdbcCacheWorker CACHE_Manager;
	static boolean result_cache_enable = true;

	static {
		// connectionList = new Vector();
		sysCharsetName = System.getProperty("file.encoding");
		url_cache_table = new Hashtable<UUrlHostKey, UUrlCache>(10);
		url_cache_remove_list = new ArrayList<UUrlCache>(10);

		try {
			CACHE_Manager = new JdbcCacheWorker();
			CACHE_Manager.setDaemon(true);
			CACHE_Manager.setContextClassLoader(null);
			CACHE_Manager.start();
		} catch (Exception e) {
			e.printStackTrace();
			result_cache_enable = false;
		}
	}

	public static UConnection connect(String ip, int port, String name,
			String user, String passwd, String url)
			throws java.sql.SQLException {
		UClientSideConnection connection;

		connection = new UClientSideConnection(ip, port, name, user, passwd, url);
		// connectionList.add(connection);
		return connection;
	}

	public static UConnection connect(ArrayList<String> aConList, String name,
			String user, String passwd, String url)
			throws java.sql.SQLException {
		UClientSideConnection connection;

		connection = new UClientSideConnection(aConList, name, user, passwd, url);
		// connectionList.add(connection);
		return connection;
	}

	static UUrlCache getUrlCache(UUrlHostKey key) {
		UUrlCache url_cache;
		url_cache = url_cache_table.get(key);
		if (url_cache != null)
			return url_cache;

		synchronized (url_cache_table) {
			url_cache = url_cache_table.get(key);
			if (url_cache == null) {
				url_cache = new UUrlCache();
				url_cache_table.put(key, url_cache);
				synchronized (url_cache_remove_list) {
					url_cache_remove_list.add(url_cache);
				}
			}
		}

		return url_cache;
	}

	public static UConnection connectServerSide() throws SQLException {
		Thread curThread = Thread.currentThread();
		Socket s = (Socket) UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread",
				"getSocket", null, curThread, null);
		return new UServerSideConnection(s, curThread);
	}

	/*
	 * delete the UConnection object from connection list
	 * 
	 * synchronized static boolean deleteInList (UConnection element) { if
	 * (connectionList.contains(element)==false) return false;
	 * 
	 * return connectionList.remove(element); }
	 */
}

class JdbcCacheWorker extends Thread {
	public void run() {
		while (true) {
			try {
				long curTime = System.currentTimeMillis();
				synchronized (UJCIManager.url_cache_remove_list) {
					for (int i = 0; i < UJCIManager.url_cache_remove_list.size(); i++) {
						UUrlCache uc = (UUrlCache) UJCIManager.url_cache_remove_list.get(i);
						uc.remove_expired_stmt(curTime);
					}
				}
			} catch (Exception e) {
			}

			try {
				Thread.sleep(1000);
			} catch (InterruptedException e) {
			}
		}
	}
}
