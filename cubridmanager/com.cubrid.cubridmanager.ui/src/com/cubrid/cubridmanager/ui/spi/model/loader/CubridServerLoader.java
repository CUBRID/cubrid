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
package com.cubrid.cubridmanager.ui.spi.model.loader;

import org.eclipse.core.runtime.IProgressMonitor;

import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.model.StatusMonitorAuthType;
import com.cubrid.cubridmanager.ui.broker.editor.BrokerEnvStatusView;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.Messages;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridBrokerFolder;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.loader.logs.CubridLogsFolderLoader;

/**
 * 
 * This loader is responsible to load the children of CUBRID Server,these
 * children include Databases,Brokers,Monitoring(Status monitor,Broker
 * monitor),Logs folder.
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridServerLoader extends
		CubridNodeLoader {

	private static final String DATABASE_FOLDER_NAME = Messages.msgDatabaseFolderName;
	private static final String BROKER_FOLDER_NAME = Messages.msgBrokersFolderName;
	private static final String STATUS_MONITORING_FOLDER_NAME = Messages.msgStatusMonitorFolderName;
	private static final String LOGS_FOLDER_NAME = Messages.msgLogsFolderName;

	public static final String DATABASE_FOLDER_ID = "Databases";
	public static final String BROKER_FOLDER_ID = "Brokers";
	public static final String STATUS_MONITORING_FOLDER_ID = "Status monitor";
	public static final String LOGS_FOLDER_ID = "Logs";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent, IProgressMonitor monitor) {
		if (isLoaded())
			return;
		CubridServer server = parent.getServer();
		if (!server.isConnected()) {
			parent.removeAllChild();
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent((ICubridNode) parent,
							CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
			return;
		}
		ServerType serverType = server.getServerInfo().getServerType();
		// add database folder
		if (serverType == ServerType.BOTH || serverType == ServerType.DATABASE) {
			String databaseFolderId = parent.getId() + NODE_SEPARATOR
					+ DATABASE_FOLDER_ID;
			ICubridNode databaseFolder = parent.getChild(databaseFolderId);
			if (databaseFolder == null) {
				databaseFolder = new DefaultCubridNode(databaseFolderId,
						DATABASE_FOLDER_NAME,
						"icons/navigator/database_group.png");
				databaseFolder.setType(CubridNodeType.DATABASE_FOLDER);
				databaseFolder.setContainer(true);
				ICubridNodeLoader loader = new CubridDatabasesFolderLoader();
				loader.setLevel(getLevel());
				databaseFolder.setLoader(loader);
				if (getLevel() == DEFINITE_LEVEL) {
					databaseFolder.getChildren(monitor);
				}
				parent.addChild(databaseFolder);
			} else {
				if (databaseFolder.getLoader() != null
						&& databaseFolder.getLoader().isLoaded()) {
					databaseFolder.getLoader().setLoaded(false);
					databaseFolder.getChildren(monitor);
				}
			}
		}
		ServerUserInfo userInfo = parent.getServer().getServerInfo().getLoginedUserInfo();
		// add broker folder
		if (serverType == ServerType.BOTH || serverType == ServerType.BROKER) {
			if (userInfo != null
					&& (CasAuthType.AUTH_ADMIN == userInfo.getCasAuth() || CasAuthType.AUTH_MONITOR == userInfo.getCasAuth())) {
				String brokerFolderId = parent.getId() + NODE_SEPARATOR
						+ BROKER_FOLDER_ID;
				ICubridNode brokerFolder = parent.getChild(brokerFolderId);
				if (brokerFolder == null) {
					brokerFolder = new CubridBrokerFolder(brokerFolderId,
							BROKER_FOLDER_NAME,
							"icons/navigator/broker_group.png");
					((CubridBrokerFolder) brokerFolder).setStartedIconPath("icons/navigator/broker_service_started.png");
					brokerFolder.setContainer(true);
					brokerFolder.setViewId(BrokerEnvStatusView.ID);
					ICubridNodeLoader loader = new CubridBrokersFolderLoader();
					loader.setLevel(getLevel());
					brokerFolder.setLoader(loader);
					if (getLevel() == DEFINITE_LEVEL) {
						brokerFolder.getChildren(monitor);
					}
					parent.addChild(brokerFolder);
				} else {
					if (brokerFolder.getLoader() != null
							&& brokerFolder.getLoader().isLoaded()) {
						brokerFolder.getLoader().setLoaded(false);
						brokerFolder.getChildren(monitor);
					}
				}
			}
		}
		// add status monitor folder to monitoring folder
		if (userInfo != null
				&& (StatusMonitorAuthType.AUTH_ADMIN == userInfo.getStatusMonitorAuth() || StatusMonitorAuthType.AUTH_MONITOR == userInfo.getStatusMonitorAuth())) {
			String statusMonitroingId = parent.getId() + NODE_SEPARATOR
					+ STATUS_MONITORING_FOLDER_ID;
			ICubridNode statusMonitoringFolder = parent.getChild(statusMonitroingId);
			if (statusMonitoringFolder == null) {
				statusMonitoringFolder = new DefaultCubridNode(
						statusMonitroingId, STATUS_MONITORING_FOLDER_NAME,
						"icons/navigator/status_group.png");
				statusMonitoringFolder.setType(CubridNodeType.STATUS_MONITOR_FOLDER);
				statusMonitoringFolder.setContainer(true);
				ICubridNodeLoader loader = new CubridStatusMonitorFolderLoader();
				loader.setLevel(getLevel());
				statusMonitoringFolder.setLoader(loader);
				if (getLevel() == DEFINITE_LEVEL) {
					statusMonitoringFolder.getChildren(monitor);
				}
				parent.addChild(statusMonitoringFolder);
			} else {
				if (statusMonitoringFolder.getLoader() != null
						&& statusMonitoringFolder.getLoader().isLoaded()) {
					statusMonitoringFolder.getLoader().setLoaded(false);
					statusMonitoringFolder.getChildren(monitor);
				}
			}
		}

		// add logs folder
		String logsFolderId = parent.getId() + NODE_SEPARATOR + LOGS_FOLDER_ID;
		ICubridNode logsFolder = parent.getChild(logsFolderId);
		if (logsFolder == null) {
			logsFolder = new DefaultCubridNode(logsFolderId, LOGS_FOLDER_NAME,
					"icons/navigator/log_group_big.png");
			logsFolder.setType(CubridNodeType.LOGS_FOLDER);
			logsFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridLogsFolderLoader();
			loader.setLevel(getLevel());
			logsFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				logsFolder.getChildren(monitor);
			}
			parent.addChild(logsFolder);
		} else {
			if (logsFolder.getLoader() != null
					&& logsFolder.getLoader().isLoaded()) {
				logsFolder.getLoader().setLoaded(false);
				logsFolder.getChildren(monitor);
			}
		}

		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}

}
