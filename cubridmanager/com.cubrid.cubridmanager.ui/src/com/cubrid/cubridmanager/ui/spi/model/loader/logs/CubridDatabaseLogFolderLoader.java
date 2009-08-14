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

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;

/**
 * 
 * This class is responsible to load all database children of CUBRID logs server
 * folder
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDatabaseLogFolderLoader extends
		CubridNodeLoader {

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		ServerInfo serverInfo = parent.getServer().getServerInfo();
		ServerUserInfo userInfo = serverInfo.getLoginedUserInfo();
		List<DatabaseInfo> databaseInfoList = userInfo.getDatabaseInfoList();
		if (databaseInfoList == null || databaseInfoList.size() <= 0) {
			setLoaded(true);
			return;
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		List<ICubridNode> oldNodeList = new ArrayList<ICubridNode>();
		oldNodeList.addAll(parent.getChildren());
		parent.removeAllChild();
		for (int i = 0; i < databaseInfoList.size() && !monitor.isCanceled(); i++) {
			DatabaseInfo databaseInfo = databaseInfoList.get(i);
			String id = parent.getId() + NODE_SEPARATOR
					+ databaseInfo.getDbName();
			ICubridNode logDatabaseNode = isContained(oldNodeList, id);
			if (logDatabaseNode == null) {
				logDatabaseNode = new DefaultCubridNode(id,
						databaseInfo.getDbName(), "icons/navigator/folder.png");
				logDatabaseNode.setType(CubridNodeType.LOGS_SERVER_DATABASE_FOLDER);
				logDatabaseNode.setModelObj(databaseInfo);
				logDatabaseNode.setContainer(true);
				ICubridNodeLoader loader = new CubridDatabaseLogLoader();
				loader.setLevel(getLevel());
				logDatabaseNode.setLoader(loader);
				if (getLevel() == DEFINITE_LEVEL) {
					logDatabaseNode.getChildren(monitor);
				}
			} else {
				if (logDatabaseNode.getLoader() != null
						&& logDatabaseNode.getLoader().isLoaded()) {
					logDatabaseNode.getLoader().setLoaded(false);
					logDatabaseNode.getChildren(monitor);
				}
			}
			parent.addChild(logDatabaseNode);
		}
		Collections.sort(parent.getChildren());
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}

	private ICubridNode isContained(List<ICubridNode> nodeList, String id) {
		for (int i = 0; nodeList != null && i < nodeList.size(); i++) {
			ICubridNode node = nodeList.get(i);
			if (node.getId().equals(id)) {
				return node;
			}
		}
		return null;
	}
}
