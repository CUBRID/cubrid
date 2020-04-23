/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

package cubrid.jdbc.jci;

import java.io.DataOutputStream;
import java.io.IOException;
import java.net.Socket;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

import cubrid.jdbc.net.BrokerHandler;

public class UUnreachableHostList {
	private static final String HEALTH_CHECK_DUMMY_DB = "___health_check_dummy_db___";
	private static final int CAS_INFO_SIZE = 4;

	private static UUnreachableHostList instance = null;
	private List<String> unreachableHosts;
	private boolean useSSL = true;

	private UUnreachableHostList() {
		unreachableHosts = new CopyOnWriteArrayList<String>();
	}

	public synchronized static UUnreachableHostList getInstance() {
		if (instance == null) {
			instance = new UUnreachableHostList();
		}

		return instance;
	}

	public boolean contains(String host) {
		return unreachableHosts.contains(host);
	}

	public synchronized void add(String host) {
		if (!unreachableHosts.contains(host)) {
			unreachableHosts.add(host);
		}
	}

	public void remove(String host) {
		unreachableHosts.remove(host);
	}

	public void checkReachability(int timeout) {
		String ip;
		int port;

		if (unreachableHosts == null) {
			return;
		}

		for (String host : unreachableHosts) {
			ip = host.split(":")[0];
			port = Integer.parseInt(host.split(":")[1]);

			try {
				checkHostAlive(ip, port, timeout);
				remove(host);
			} catch (UJciException e) {
				// do nothing
			} catch (IOException e) {
				// do nothing
			}
		}
	}

	private void checkHostAlive(String ip, int port, int timeout)
			throws IOException, UJciException {
		Socket toBroker = null;
		byte[] serverInfo;
		byte[] casInfo;
		String dummyUrl = "jdbc:cubrid:" + ip + ":" + port + ":"
				+ HEALTH_CHECK_DUMMY_DB + "::********:";
		UTimedDataInputStream is = null;
		DataOutputStream os = null;

		long startTime = System.currentTimeMillis();

		try {
			toBroker = BrokerHandler.connectBroker(ip, port, useSSL, timeout);
			if (timeout > 0) {
				timeout -= (System.currentTimeMillis() - startTime);
				if (timeout <= 0) {
					throw new UJciException(UErrorCode.ER_TIMEOUT);
				}
			}

			is = new UTimedDataInputStream(toBroker.getInputStream(), ip, port,
					timeout);
			os = new DataOutputStream(toBroker.getOutputStream());
			serverInfo = UConnection.createDBInfo(HEALTH_CHECK_DUMMY_DB, "",
					"", dummyUrl);

			// send db info
			os.write(serverInfo);
			os.flush();

			// receive header
			int dataLength = is.readInt();
			casInfo = new byte[CAS_INFO_SIZE];
			is.readFully(casInfo);
			if (dataLength < 0) {
				throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
			}

		} finally {
			if (is != null)
				is.close();
			if (os != null)
				os.close();
			if (toBroker != null)
				toBroker.close();
		}
	}

	public void setUseSSL(boolean useSSL) {
		this.useSSL = useSSL;
	}
}
