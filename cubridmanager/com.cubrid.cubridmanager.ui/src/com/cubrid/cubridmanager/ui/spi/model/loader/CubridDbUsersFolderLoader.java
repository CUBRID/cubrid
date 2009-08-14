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

import java.util.Collections;
import java.util.Comparator;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.ui.cubrid.user.editor.UserEditor;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * This class is responsible to load all children of CUBRID database users
 * folder,these children include all CUBRID database user
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDbUsersFolderLoader extends
		CubridNodeLoader {

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		CubridDatabase database = ((ISchemaNode) parent).getDatabase();
		if (database.getRunningType() == DbRunningType.STANDALONE) {
			parent.removeAllChild();
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent((ICubridNode) parent,
							CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
			return;
		}
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		DbUserInfoList dbUserInfoList = new DbUserInfoList();
		final CommonQueryTask<DbUserInfoList> task = new CommonQueryTask<DbUserInfoList>(
				parent.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, dbUserInfoList);
		task.setDbName(database.getLabel());
		Thread thread = new Thread() {
			public void run() {
				while (!monitor.isCanceled() && !isLoaded()) {
					try {
						sleep(WAIT_TIME);
					} catch (InterruptedException e) {
					}
				}
				if (monitor.isCanceled()) {
					task.cancel();
				}
			}
		};
		thread.start();
		task.execute();
		final String errorMsg = task.getErrorMsg();
		if (!monitor.isCanceled() && errorMsg != null
				&& errorMsg.trim().length() > 0) {
			parent.removeAllChild();
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(null, errorMsg);
				}
			});
			setLoaded(true);
			return;
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		parent.removeAllChild();
		dbUserInfoList = task.getResultModel();
		DbUserInfo latestDLoginedbUserInfo = databaseInfo.getAuthLoginedDbUserInfo();
		if (dbUserInfoList != null) {
			List<DbUserInfo> dbUserList = dbUserInfoList.getUserList();
			if (dbUserList != null && dbUserList.size() > 0) {
				formatUserList(dbUserList);
				for (DbUserInfo dbUserInfo : dbUserList) {
					if (dbUserInfo.getName().equals(
							latestDLoginedbUserInfo.getName())) {
						dbUserInfo.setDbaAuthority(latestDLoginedbUserInfo.isDbaAuthority());
						//databaseInfo.setAuthLoginedDbUserInfo(dbUserInfo);
					}
					String id = parent.getId() + NODE_SEPARATOR
							+ dbUserInfo.getName();
					ICubridNode dbUserInfoNode = new DefaultSchemaNode(id,
							dbUserInfo.getName(),
							"icons/navigator/user_item.png");
					dbUserInfoNode.setType(CubridNodeType.USER);
					dbUserInfoNode.setModelObj(dbUserInfo);
					dbUserInfoNode.setContainer(false);
					dbUserInfoNode.setEditorId(UserEditor.ID);
					parent.addChild(dbUserInfoNode);
				}
			}
		}
		databaseInfo.setDbUserInfoList(dbUserInfoList);
		//		Collections.sort(parent.getChildren());
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}

	private void formatUserList(List<DbUserInfo> list) {
		if (list.size() < 2)
			return;
		DbUserInfo dbaUser = null;
		DbUserInfo publicUser = null;
		for (DbUserInfo bean : list) {
			if (bean.getName().equalsIgnoreCase("public")) {
				publicUser = bean;

			}
			if (bean.getName().equalsIgnoreCase("dba")) {
				dbaUser = bean;
			}
		}
		list.remove(dbaUser);
		list.remove(publicUser);
		if (dbaUser == null || publicUser == null)
			return;
		Collections.sort(list, new Comparator<DbUserInfo>() {
			public int compare(DbUserInfo o1, DbUserInfo o2) {
				if (o1 == null || o2 == null || o1.getName() == null
						|| o2.getName() == null)
					return 0;
				int cc = o1.getName().compareToIgnoreCase(o2.getName());
				return (cc < 0 ? -1 : cc > 0 ? 1 : 0);
			}
		});
		list.add(0, publicUser);
		list.add(0, dbaUser);
	}
}
