/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.test.spi;

import junit.framework.TestCase;

import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;

/**
 * Test SiteManager class
 * 
 * @author pangqiren 2009-3-3
 */
public class CubridNodeManagerTest extends TestCase {

	/**
	 * Test method for getInstance() and addServer and getServer and
	 * getAllServer and removeServer and removeAllServer
	 * 
	 */
	public void testAll() {
		CubridNodeManager manager = CubridNodeManager.getInstance();
		if (manager == null)
			fail("testGetInstance() failed.");
		CubridServer server = new CubridServer("192.168.0.1", "localhost", "","");
		manager.addServer(server);
		if (manager.getAllServer().size() != 1)
			fail("testAddServer() fail");
		CubridServer server1 = manager.getServer(server.getId());
		if (server1 == null) {
			fail("testGetServer() fail");
		}
		if (manager.getAllServer().size() != 1) {
			fail("testGetAllServer() fail");
		}
		manager.removeServer(server);
		if (manager.getServer(server.getId()) != null) {
			fail("testRemoveServer() fail");
		}
		manager.addServer(server);
		manager.removeAllServer();
		if (manager.getAllServer().size() != 0) {
			fail("testRemoveAllServer() fail");
		}
		manager.addServer(server);
	}
}
