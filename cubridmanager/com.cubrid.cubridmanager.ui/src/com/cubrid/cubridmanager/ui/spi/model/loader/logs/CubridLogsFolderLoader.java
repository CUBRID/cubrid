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
package com.cubrid.cubridmanager.ui.spi.model.loader.logs;

import org.eclipse.core.runtime.IProgressMonitor;

import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.ui.logs.editor.LogEditorPart;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.Messages;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;

/**
 * 
 * This class is responsible to load all children of logs folder
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridLogsFolderLoader extends
		CubridNodeLoader {

	private static final String LOGS_BROKER_FOLDER_NAME = Messages.msgLogsBrokerFolderName;
	private static final String LOGS_MANAGER_FOLDER_NAME = Messages.msgLogsManagerFolderName;
	private static final String LOGS_SERVER_FOLDER_NAME = Messages.msgLogsServerFolderName;
	private static final String ACCESS_LOG_FOLDER_NAME = Messages.msgAccessLogFolderName;
	private static final String ERROR_LOG_FOLDER_NAME = Messages.msgErrorLogFolderName;

	public static final String LOGS_BROKER_FOLDER_ID = "Broker";
	public static final String LOGS_MANAGER_FOLDER_ID = "Manager";
	public static final String LOGS_SERVER_FOLDER_ID = "Server";
	public static final String ACCESS_LOG_FOLDER_ID = "Access log";
	public static final String ERROR_LOG_FOLDER_ID = "Error log";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		ServerType serverType = parent.getServer().getServerInfo().getServerType();
		// add broker logs folder to logs folder
		if (serverType == ServerType.BOTH || serverType == ServerType.BROKER) {
			String brokerLogsFolderId = parent.getId() + NODE_SEPARATOR
					+ LOGS_BROKER_FOLDER_ID;
			ICubridNode brokerLogsFolder = parent.getChild(brokerLogsFolderId);
			if (brokerLogsFolder == null) {
				brokerLogsFolder = new DefaultCubridNode(brokerLogsFolderId,
						LOGS_BROKER_FOLDER_NAME,
						"icons/navigator/log_broker_group.png");
				brokerLogsFolder.setType(CubridNodeType.LOGS_BROKER_FOLDER);
				brokerLogsFolder.setContainer(true);
				ICubridNodeLoader loader = new CubridBrokerLogFolderLoader();
				loader.setLevel(getLevel());
				brokerLogsFolder.setLoader(loader);
				if (getLevel() == DEFINITE_LEVEL) {
					brokerLogsFolder.getChildren(monitor);
				}
				parent.addChild(brokerLogsFolder);
			} else {
				if (brokerLogsFolder.getLoader() != null
						&& brokerLogsFolder.getLoader().isLoaded()) {
					brokerLogsFolder.getLoader().setLoaded(false);
					brokerLogsFolder.getChildren(monitor);
				}
			}
		}
		// add manager logs folder to logs folder
		if (serverType == ServerType.BOTH || serverType == ServerType.DATABASE) {
			String managerLogsFolderId = parent.getId() + NODE_SEPARATOR
					+ LOGS_MANAGER_FOLDER_ID;
			ICubridNode managerLogsFolder = parent.getChild(managerLogsFolderId);
			if (managerLogsFolder == null) {
				managerLogsFolder = new DefaultCubridNode(managerLogsFolderId,
						LOGS_MANAGER_FOLDER_NAME,
						"icons/navigator/log_manager_group.png");
				managerLogsFolder.setType(CubridNodeType.LOGS_MANAGER_FOLDER);
				managerLogsFolder.setContainer(true);
				parent.addChild(managerLogsFolder);
			}
			// add access and error log folder to manager logs folder
			String[] managerLogIdArr = { ACCESS_LOG_FOLDER_ID,
					ERROR_LOG_FOLDER_ID };
			String[] managerLogNameArr = { ACCESS_LOG_FOLDER_NAME,
					ERROR_LOG_FOLDER_NAME };
			CubridNodeType[] managerLogTypeArr = {
					CubridNodeType.LOGS_MANAGER_ACCESS_LOG,
					CubridNodeType.LOGS_MANAGER_ERROR_LOG, };
			String[] iconsArr = { "icons/navigator/log_item.png",
					"icons/navigator/log_item.png" };
			for (int i = 0; i < managerLogNameArr.length; i++) {
				String id = parent.getId() + NODE_SEPARATOR
						+ managerLogIdArr[i];
				ICubridNode logFolder = parent.getChild(id);
				if (logFolder == null) {
					logFolder = new DefaultCubridNode(id, managerLogNameArr[i],
							iconsArr[i]);
					logFolder.setType(managerLogTypeArr[i]);
					logFolder.setEditorId(LogEditorPart.ID);
					logFolder.setContainer(false);
					managerLogsFolder.addChild(logFolder);
				}
			}
			//add database server log folder
			String serverLogsFolderId = parent.getId() + NODE_SEPARATOR
					+ LOGS_SERVER_FOLDER_ID;
			ICubridNode serverLogsFolder = parent.getChild(serverLogsFolderId);
			if (serverLogsFolder == null) {
				serverLogsFolder = new DefaultCubridNode(serverLogsFolderId,
						LOGS_SERVER_FOLDER_NAME,
						"icons/navigator/log_db_group.png");
				serverLogsFolder.setType(CubridNodeType.LOGS_SERVER_FOLDER);
				serverLogsFolder.setContainer(true);
				ICubridNodeLoader loader = new CubridDatabaseLogFolderLoader();
				loader.setLevel(getLevel());
				serverLogsFolder.setLoader(loader);
				if (getLevel() == DEFINITE_LEVEL) {
					serverLogsFolder.getChildren(monitor);
				}
				parent.addChild(serverLogsFolder);
			} else {
				if (serverLogsFolder.getLoader() != null
						&& serverLogsFolder.getLoader().isLoaded()) {
					serverLogsFolder.getLoader().setLoaded(false);
					serverLogsFolder.getChildren(monitor);
				}
			}
		}
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
