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
package com.cubrid.cubridmanager.ui.spi;

import java.io.ByteArrayInputStream;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.Preferences;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.ServerManager;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.xml.IXMLMemento;
import com.cubrid.cubridmanager.core.common.xml.XMLMemento;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.event.ICubridNodeChangedListener;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.loader.CubridServerLoader;

/**
 * 
 * This class is for managing all CUBRID Node in navigator tree
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridNodeManager {

	private static final Logger logger = LogUtil.getLogger(CubridNodeManager.class);
	private final String SERVER_XML_CONTENT = "CUBRID_SERVERS";
	private static CubridNodeManager instance = null;
	private List<CubridServer> serverList = null;
	private static boolean initialized = false;
	private List<ICubridNodeChangedListener> cubridNodeChangeListeners = new ArrayList<ICubridNodeChangedListener>();

	private CubridNodeManager() {
	}

	/**
	 * Return the only CUBRID Node manager
	 * 
	 * @return CubridNodeManager
	 */
	public synchronized static CubridNodeManager getInstance() {
		if (instance == null) {
			instance = new CubridNodeManager();
		}
		return instance;
	}

	/**
	 * 
	 * Init the CUBRID Node manager
	 * 
	 */
	protected synchronized void init() {
		if (initialized) {
			return;
		}
		serverList = new ArrayList<CubridServer>();
		loadSevers();
		initialized = true;
	}

	/**
	 * 
	 * Load added host from plugin preference
	 * 
	 */
	protected synchronized void loadSevers() {
		Preferences preference = CubridManagerUIPlugin.getDefault().getPluginPreferences();
		String xmlString = preference.getString(SERVER_XML_CONTENT);
		boolean isHasLocalHost = false;
		if (xmlString != null && xmlString.length() > 0) {
			try {
				ByteArrayInputStream in = new ByteArrayInputStream(
						xmlString.getBytes("UTF-8"));
				IXMLMemento memento = XMLMemento.loadMemento(in);
				IXMLMemento[] children = memento.getChildren("host");
				for (int i = 0; i < children.length; i++) {
					String id = children[i].getString("id");
					String name = children[i].getString("name");
					if (name.equals("localhost")) {
						isHasLocalHost = true;
					}
					String address = children[i].getString("address");
					String port = children[i].getString("port");
					String user = children[i].getString("user");
					ServerInfo serverInfo = new ServerInfo();
					serverInfo.setServerName(name);
					serverInfo.setHostAddress(address);
					serverInfo.setHostMonPort(Integer.parseInt(port));
					serverInfo.setHostJSPort(Integer.parseInt(port) + 1);
					serverInfo.setUserName(user);
					serverInfo.setUserPassword("");
					CubridServer server = new CubridServer(id, name,
							"icons/navigator/host.png",
							"icons/navigator/host_connected.png");
					server.setServerInfo(serverInfo);
					server.setType(CubridNodeType.SERVER);
					server.setLoader(new CubridServerLoader());
					serverList.add(server);
				}
			} catch (Exception e) {
				logger.error(e.getMessage());
			}
		}
		if (!isHasLocalHost) {
			String id = "localhost";
			String name = "localhost";
			int port = 8001;
			String userName = "admin";
			ServerInfo serverInfo = new ServerInfo();
			serverInfo.setServerName(name);
			serverInfo.setHostAddress(name);
			serverInfo.setHostMonPort(port);
			serverInfo.setHostJSPort(port + 1);
			serverInfo.setUserName(userName);
			serverInfo.setUserPassword("");
			CubridServer server = new CubridServer(id, name,
					"icons/navigator/host.png",
					"icons/navigator/host_connected.png");
			server.setServerInfo(serverInfo);
			server.setType(CubridNodeType.SERVER);
			server.setLoader(new CubridServerLoader());
			serverList.add(0, server);
		}
	}

	/**
	 * 
	 * Save added server to plugin preference
	 * 
	 */
	public synchronized void saveServers() {
		if (!initialized) {
			init();
		}
		try {
			XMLMemento memento = XMLMemento.createWriteRoot("hosts");
			Iterator<CubridServer> iterator = serverList.iterator();
			while (iterator.hasNext()) {
				CubridServer server = (CubridServer) iterator.next();
				IXMLMemento child = memento.createChild("host");
				child.putString("id", server.getId());
				child.putString("name", server.getLabel());
				child.putString("port", String.valueOf(server.getMonPort()));
				child.putString("address", server.getHostAddress());
				child.putString("user", server.getUserName());
			}
			String xmlString = memento.saveToString();
			Preferences prefs = CubridManagerUIPlugin.getDefault().getPluginPreferences();
			prefs.setValue(SERVER_XML_CONTENT, xmlString);
			CubridManagerUIPlugin.getDefault().savePluginPreferences();
		} catch (Exception e) {
			logger.error(e.getMessage());
		}
	}

	/**
	 * 
	 * Add server
	 * 
	 * @param server
	 */
	public synchronized void addServer(CubridServer server) {
		if (!initialized) {
			init();
		}
		if (server != null) {
			ServerManager.getInstance().addServer(
					server.getServerInfo().getHostAddress(),
					server.getServerInfo().getHostMonPort(),
					server.getServerInfo());
			serverList.add(server);
			saveServers();
			fireCubridNodeChanged(new CubridNodeChangedEvent(server,
					CubridNodeChangedEventType.NODE_ADD));
		}
	}

	/**
	 * 
	 * Remove server
	 * 
	 * @param server
	 */
	public synchronized void removeServer(CubridServer server) {
		if (!initialized) {
			init();
		}
		if (server != null) {
			ServerManager.getInstance().removeServer(
					server.getServerInfo().getHostAddress(),
					server.getServerInfo().getHostMonPort());
			serverList.remove(server);
			saveServers();
			fireCubridNodeChanged(new CubridNodeChangedEvent(server,
					CubridNodeChangedEventType.NODE_REMOVE));
		}
	}

	/**
	 * 
	 * Remove all servers
	 * 
	 */
	public synchronized void removeAllServer() {
		if (!initialized) {
			init();
		}
		for (int i = 0; i < serverList.size(); i++) {
			CubridServer server = serverList.get(i);
			ServerManager.getInstance().removeServer(
					server.getServerInfo().getHostAddress(),
					server.getServerInfo().getHostMonPort());
			serverList.remove(server);
			fireCubridNodeChanged(new CubridNodeChangedEvent(server,
					CubridNodeChangedEventType.NODE_REMOVE));
		}
		saveServers();
	}

	/**
	 * 
	 * Get server by id
	 * 
	 * @param id
	 * @return
	 */
	public CubridServer getServer(String id) {
		if (!initialized) {
			init();
		}
		for (int i = 0; i < serverList.size(); i++) {
			CubridServer server = serverList.get(i);
			if (server.getId().equals(id)) {
				return server;
			}
		}
		return null;
	}

	/**
	 * 
	 * Get All servers
	 * 
	 * @return
	 */
	public List<CubridServer> getAllServer() {
		if (!initialized) {
			init();
		}
		return serverList;
	}

	/**
	 * 
	 * Add CUBRID node object changed listener
	 * 
	 * @param listener
	 */
	public void addCubridNodeChangeListener(ICubridNodeChangedListener listener) {
		if (!cubridNodeChangeListeners.contains(listener))
			cubridNodeChangeListeners.add(listener);
	}

	/**
	 * 
	 * Remove CUBRID node object changed listener
	 * 
	 * @param listener
	 */
	public void removeCubridNodeChangeListener(
			ICubridNodeChangedListener listener) {
		cubridNodeChangeListeners.remove(listener);
	}

	/**
	 * 
	 * Fire CUBRID node object changed event to all added listeners
	 * 
	 * @param event
	 */
	public void fireCubridNodeChanged(final CubridNodeChangedEvent event) {
		for (int i = 0; i < cubridNodeChangeListeners.size(); ++i) {
			final ICubridNodeChangedListener listener = (ICubridNodeChangedListener) cubridNodeChangeListeners.get(i);
			Display.getDefault().asyncExec(new Runnable() {
				public void run() {
					listener.nodeChanged(event);
				}
			});

		}
	}

	/**
	 * 
	 * Return whether this server has been existed and exclude this server
	 * 
	 * @param serverName
	 * @param server
	 * @return
	 */
	public boolean isContainedByName(String serverName, CubridServer server) {
		if (!initialized) {
			init();
		}
		for (int i = 0; i < serverList.size(); i++) {
			CubridServer serv = serverList.get(i);
			if (server != null && server.getId().equals(serv.getId())) {
				continue;
			}
			if (serv.getLabel().equals(serverName)) {
				return true;
			}
		}
		return false;
	}

	/**
	 * 
	 * Return whether this server has been existed and exclude this server
	 * 
	 * @param address
	 * @param server
	 * @return
	 */
	public boolean isContainedByHostAddress(String address, String port,
			CubridServer server) {
		if (!initialized) {
			init();
		}
		for (int i = 0; i < serverList.size(); i++) {
			CubridServer serv = serverList.get(i);
			if (server != null && server.getId().equals(serv.getId())) {
				continue;
			}
			ServerInfo serverInfo = serv.getServerInfo();
			if (serverInfo.getHostAddress().equals(address)
					&& serverInfo.getHostMonPort() == Integer.parseInt(port)) {
				return true;
			}
		}
		return false;
	}
}
