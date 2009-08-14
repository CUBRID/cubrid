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

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.task.GetCMUserListTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetDatabaseListTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.database.editor.DatabaseStatusEditor;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;

/**
 * 
 * This class is responsible to load the children of CUBRID databases
 * folder,these children include all CUBRID database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDatabasesFolderLoader extends
		CubridNodeLoader {

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		ServerInfo serverInfo = parent.getServer().getServerInfo();
		final GetDatabaseListTask getDatabaseListTask = new GetDatabaseListTask(
				serverInfo);
		final GetCMUserListTask getUserInfoTask = new GetCMUserListTask(
				serverInfo);
		Thread thread = new Thread() {
			public void run() {
				while (!monitor.isCanceled() && !isLoaded()) {
					try {
						sleep(WAIT_TIME);
					} catch (InterruptedException e) {
					}
				}
				if (monitor.isCanceled()) {
					getUserInfoTask.cancel();
					getDatabaseListTask.cancel();
				}
			}
		};
		thread.start();
		getUserInfoTask.execute();
		final String msg1 = getUserInfoTask.getErrorMsg();
		if (!monitor.isCanceled() && msg1 != null && msg1.trim().length() > 0) {
			parent.removeAllChild();
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(msg1);
				}
			});
			setLoaded(true);
			return;
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		List<ServerUserInfo> serverUserInfoList = getUserInfoTask.getServerUserInfoList();
		List<DatabaseInfo> oldDatabaseInfoList = null;
		if (serverInfo.getLoginedUserInfo() != null) {
			oldDatabaseInfoList = serverInfo.getLoginedUserInfo().getDatabaseInfoList();
		}
		for (int i = 0; serverUserInfoList != null
				&& i < serverUserInfoList.size(); i++) {
			ServerUserInfo userInfo = serverUserInfoList.get(i);
			if (userInfo != null
					&& userInfo.getUserName().equals(serverInfo.getUserName())) {
				serverInfo.setLoginedUserInfo(userInfo);
				break;
			}
		}
		getDatabaseListTask.execute();
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		final String msg2 = getDatabaseListTask.getErrorMsg();
		if (!monitor.isCanceled() && msg2 != null && msg2.trim().length() > 0) {
			parent.removeAllChild();
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(msg2);
				}
			});
			setLoaded(true);
			return;
		}
		List<DatabaseInfo> databaseInfoList = getDatabaseListTask.loadDatabaseInfo();
		List<ICubridNode> oldNodeList = new ArrayList<ICubridNode>();
		oldNodeList.addAll(parent.getChildren());
		parent.removeAllChild();
		ServerUserInfo userInfo = serverInfo.getLoginedUserInfo();
		List<DatabaseInfo> authorDatabaseList = userInfo != null ? userInfo.getDatabaseInfoList()
				: null;
		filterDatabaseList(databaseInfoList, authorDatabaseList);
		for (int i = 0; authorDatabaseList != null
				&& i < authorDatabaseList.size() && !monitor.isCanceled(); i++) {
			DatabaseInfo databaseInfo = authorDatabaseList.get(i);
			DatabaseInfo newDatabaseInfo = getDatabaseInfo(databaseInfoList,
					databaseInfo.getDbName());
			if (newDatabaseInfo != null) {
				databaseInfo.setDbDir(newDatabaseInfo.getDbDir());
				databaseInfo.setRunningType(newDatabaseInfo.getRunningType());
				if (oldDatabaseInfoList != null) {
					newDatabaseInfo = getDatabaseInfo(oldDatabaseInfoList,
							databaseInfo.getDbName());
					if (newDatabaseInfo != null) {
						DbUserInfo dbUserInfo = newDatabaseInfo.getAuthLoginedDbUserInfo();
						if (dbUserInfo != null
								&& databaseInfo.getAuthLoginedDbUserInfo() != null) {
							databaseInfo.getAuthLoginedDbUserInfo().setNoEncryptPassword(
									dbUserInfo.getNoEncryptPassword());
						}
					}
				}
			} else {
				continue;
			}
			String name = databaseInfo.getDbName();
			String id = parent.getId() + NODE_SEPARATOR + name;
			ICubridNode databaseNode = isContained(oldNodeList, id);
			if (databaseNode == null) {
				databaseNode = new CubridDatabase(id, databaseInfo.getDbName());
				CubridDatabase database = (CubridDatabase) databaseNode;
				database.setStartAndLoginIconPath("icons/navigator/database_start_connected.png");
				database.setStartAndLogoutIconPath("icons/navigator/database_start_disconnected.png");
				database.setStopAndLogoutIconPath("icons/navigator/database_stop_disconnected.png");
				database.setStopAndLoginIconPath("icons/navigator/database_stop_connected.png");
				ICubridNodeLoader loader = new CubridDatabaseLoader();
				loader.setLevel(getLevel());
				databaseNode.setLoader(loader);
				if (getLevel() == DEFINITE_LEVEL) {
					databaseNode.getChildren(monitor);
				}
				databaseNode.setEditorId(DatabaseStatusEditor.ID);
				((CubridDatabase) databaseNode).setDatabaseInfo(databaseInfo);
				databaseNode.setContainer(true);
			} else {
				databaseInfo.setLogined(((CubridDatabase) databaseNode).isLogined());
				((CubridDatabase) databaseNode).setDatabaseInfo(databaseInfo);
				if (databaseNode.getLoader() != null
						&& databaseNode.getLoader().isLoaded()) {
					databaseNode.getLoader().setLoaded(false);
					databaseNode.getChildren(monitor);
				}
			}
			parent.addChild(databaseNode);
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

	private DatabaseInfo getDatabaseInfo(List<DatabaseInfo> databaseInfoList,
			String dbName) {
		for (int i = 0; databaseInfoList != null && i < databaseInfoList.size(); i++) {
			DatabaseInfo databaseInfo = databaseInfoList.get(i);
			if (databaseInfo.getDbName().equals(dbName)) {
				return databaseInfo;
			}
		}
		return null;
	}

	private void filterDatabaseList(List<DatabaseInfo> databaseInfoList,
			List<DatabaseInfo> authorDatabaseList) {
		List<DatabaseInfo> deletedDbList = new ArrayList<DatabaseInfo>();
		for (int i = 0; authorDatabaseList != null
				&& i < authorDatabaseList.size(); i++) {
			DatabaseInfo databaseInfo = authorDatabaseList.get(i);
			if (databaseInfo != null
					&& getDatabaseInfo(databaseInfoList,
							databaseInfo.getDbName()) == null) {
				deletedDbList.add(databaseInfo);
			}
		}
		if (authorDatabaseList != null)
			authorDatabaseList.removeAll(deletedDbList);
	}
}
