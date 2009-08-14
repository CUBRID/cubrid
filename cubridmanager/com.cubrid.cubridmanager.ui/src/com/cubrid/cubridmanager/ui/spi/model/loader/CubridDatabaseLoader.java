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

import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetDatabaseListTask;
import com.cubrid.cubridmanager.ui.common.navigator.CubridNavigatorView;
import com.cubrid.cubridmanager.ui.cubrid.database.editor.DatabaseStatusEditor;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.Messages;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.loader.jobauto.CubridJobAutoFolderLoader;
import com.cubrid.cubridmanager.ui.spi.model.loader.schema.CubridTablesFolderLoader;
import com.cubrid.cubridmanager.ui.spi.model.loader.schema.CubridViewsFolderLoader;
import com.cubrid.cubridmanager.ui.spi.model.loader.sp.CubridSPFolderLoader;

/**
 * 
 * This class is responsible to load the children of CUBRID database,these
 * children include Users,Job automation,Database space,Schema,Stored
 * procedure,Trigger folder
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDatabaseLoader extends
		CubridNodeLoader {

	private static final String USERS_FOLDER_NAME = Messages.msgUserFolderName;
	private static final String JOB_AUTO_FOLDER_NAME = Messages.msgJobAutoFolderName;
	private static final String DB_SPACE_FOLDER_NAME = Messages.msgDbSpaceFolderName;
	private static final String TABLES_FOLDER_NAME = Messages.msgTablesFolderName;
	private static final String VIEWS_FOLDER_NAME = Messages.msgViewsFolderName;
	private static final String SP_FOLDER_NAME = Messages.msgSpFolderName;
	private static final String TRIGGER_FOLDER_NAME = Messages.msgTriggerFolderName;
	private static final String SERIAL_FOLDER_NAME = Messages.msgSerialFolderName;

	public static final String USERS_FOLDER_ID = "Users";
	public static final String JOB_AUTO_FOLDER_ID = "Job automation";
	public static final String DB_SPACE_FOLDER_ID = "Database space";
	public static final String TABLES_FOLDER_ID = "Tables";
	public static final String VIEWS_FOLDER_ID = "Views";
	public static final String SP_FOLDER_ID = "Stored procedure";
	public static final String TRIGGER_FOLDER_ID = "Triggers";
	public static final String SERIAL_FOLDER_ID = "Serials";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(final ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		CubridDatabase database = (CubridDatabase) parent;
		database.getDatabaseInfo().clear();
		if (!database.isLogined()) {
			parent.removeAllChild();
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent((ICubridNode) parent,
							CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
			return;
		}
		//when refresh,firstly check whether this database exist
		ServerInfo serverInfo = parent.getServer().getServerInfo();
		final GetDatabaseListTask getDatabaseListTask = new GetDatabaseListTask(
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
					getDatabaseListTask.cancel();
				}
			}
		};
		thread.start();
		getDatabaseListTask.execute();
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		final String msg = getDatabaseListTask.getErrorMsg();
		if (!monitor.isCanceled() && msg != null && msg.trim().length() > 0) {
			parent.removeAllChild();
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(msg);
				}
			});
			setLoaded(true);
			return;
		}
		List<DatabaseInfo> databaseInfoList = getDatabaseListTask.loadDatabaseInfo();
		String databaseName = database.getLabel();
		boolean isExist = false;
		for (int i = 0; databaseInfoList != null && i < databaseInfoList.size(); i++) {
			DatabaseInfo dbInfo = databaseInfoList.get(i);
			if (dbInfo.getDbName().equalsIgnoreCase(databaseName)) {
				database.setRunningType(dbInfo.getRunningType());
				isExist = true;
			}
		}
		if (!isExist) {
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(Messages.errDatabaseNoExist);
					CommonTool.refreshNavigatorTree(
							CubridNavigatorView.getNavigatorView().getViewer(),
							parent.getParent());
				}
			});
			setLoaded(true);
			return;
		}
		// add user folder
		String userFolderId = database.getId() + NODE_SEPARATOR
				+ USERS_FOLDER_ID;
		ICubridNode userFolder = database.getChild(userFolderId);
		if (userFolder == null) {
			userFolder = new DefaultSchemaNode(userFolderId, USERS_FOLDER_NAME,
					"icons/navigator/user_group.png");
			userFolder.setType(CubridNodeType.USER_FOLDER);
			userFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridDbUsersFolderLoader();
			loader.setLevel(getLevel());
			userFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				userFolder.getChildren(monitor);
			}
			database.addChild(userFolder);
		} else {
			if (userFolder.getLoader() != null
					&& userFolder.getLoader().isLoaded()) {
				userFolder.getLoader().setLoaded(false);
				userFolder.getChildren(monitor);
			}
		}
		// add tables folder
		String tablesFolderId = database.getId() + NODE_SEPARATOR
				+ TABLES_FOLDER_ID;
		ICubridNode tablesFolder = database.getChild(tablesFolderId);
		if (tablesFolder == null) {
			tablesFolder = new DefaultSchemaNode(tablesFolderId,
					TABLES_FOLDER_NAME, "icons/navigator/schema_table.png");
			tablesFolder.setType(CubridNodeType.TABLE_FOLDER);
			tablesFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridTablesFolderLoader();
			loader.setLevel(getLevel());
			tablesFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				tablesFolder.getChildren(monitor);
			}
			database.addChild(tablesFolder);
		} else {
			if (tablesFolder.getLoader() != null
					&& tablesFolder.getLoader().isLoaded()) {
				tablesFolder.getLoader().setLoaded(false);
				tablesFolder.getChildren(monitor);
			}
		}
		// add views folder
		String viewsFolderId = database.getId() + NODE_SEPARATOR
				+ VIEWS_FOLDER_ID;
		ICubridNode viewsFolder = database.getChild(viewsFolderId);
		if (viewsFolder == null) {
			viewsFolder = new DefaultSchemaNode(viewsFolderId,
					VIEWS_FOLDER_NAME, "icons/navigator/schema_view.png");
			viewsFolder.setType(CubridNodeType.VIEW_FOLDER);
			viewsFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridViewsFolderLoader();
			loader.setLevel(getLevel());
			viewsFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				viewsFolder.getChildren(monitor);
			}
			database.addChild(viewsFolder);
		} else {
			if (viewsFolder.getLoader() != null
					&& viewsFolder.getLoader().isLoaded()) {
				viewsFolder.getLoader().setLoaded(false);
				viewsFolder.getChildren(monitor);
			}
		}
		// add triggers folder
		String tiggerFolderId = database.getId() + NODE_SEPARATOR
				+ TRIGGER_FOLDER_ID;
		ICubridNode tiggerFolder = database.getChild(tiggerFolderId);
		if (tiggerFolder == null) {
			tiggerFolder = new DefaultSchemaNode(tiggerFolderId,
					TRIGGER_FOLDER_NAME, "icons/navigator/trigger_group.png");
			tiggerFolder.setType(CubridNodeType.TRIGGER_FOLDER);
			tiggerFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridTriggerFolderLoader();
			loader.setLevel(getLevel());
			tiggerFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				tiggerFolder.getChildren(monitor);
			}
			database.addChild(tiggerFolder);
		} else {
			if (tiggerFolder.getLoader() != null
					&& tiggerFolder.getLoader().isLoaded()) {
				tiggerFolder.getLoader().setLoaded(false);
				tiggerFolder.getChildren(monitor);
			}
		}
		// add serials folder
		String serialFolderId = database.getId() + NODE_SEPARATOR
				+ SERIAL_FOLDER_ID;
		ICubridNode serialFolder = database.getChild(serialFolderId);
		if (serialFolder == null) {
			serialFolder = new DefaultSchemaNode(serialFolderId,
					SERIAL_FOLDER_NAME, "icons/navigator/serial_group.png");
			serialFolder.setType(CubridNodeType.SERIAL_FOLDER);
			serialFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridSerialFolderLoader();
			loader.setLevel(getLevel());
			serialFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				serialFolder.getChildren(monitor);
			}
			database.addChild(serialFolder);
		} else {
			if (serialFolder.getLoader() != null
					&& serialFolder.getLoader().isLoaded()) {
				serialFolder.getLoader().setLoaded(false);
				serialFolder.getChildren(monitor);
			}
		}
		// add stored procedure folder
		String spFolderId = database.getId() + NODE_SEPARATOR + SP_FOLDER_ID;
		ICubridNode spFolder = database.getChild(spFolderId);
		if (spFolder == null) {
			spFolder = new DefaultSchemaNode(spFolderId, SP_FOLDER_NAME,
					"icons/navigator/procedure_group.png");
			spFolder.setType(CubridNodeType.STORED_PROCEDURE_FOLDER);
			spFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridSPFolderLoader();
			loader.setLevel(getLevel());
			spFolder.setLoader(loader);
			database.addChild(spFolder);
		} else {
			if (spFolder.getLoader() != null && spFolder.getLoader().isLoaded()) {
				spFolder.getLoader().setLoaded(false);
				spFolder.getChildren(monitor);
			}
		}
		// add job automation folder
		String jobAutoFolderId = database.getId() + NODE_SEPARATOR
				+ JOB_AUTO_FOLDER_ID;
		ICubridNode jobAutoFolder = database.getChild(jobAutoFolderId);
		if (jobAutoFolder == null) {
			jobAutoFolder = new DefaultSchemaNode(jobAutoFolderId,
					JOB_AUTO_FOLDER_NAME, "icons/navigator/auto_group.png");
			jobAutoFolder.setType(CubridNodeType.JOB_FOLDER);
			jobAutoFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridJobAutoFolderLoader();
			loader.setLevel(getLevel());
			jobAutoFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				jobAutoFolder.getChildren(monitor);
			}
			database.addChild(jobAutoFolder);
		} else {
			if (jobAutoFolder.getLoader() != null
					&& jobAutoFolder.getLoader().isLoaded()) {
				jobAutoFolder.getLoader().setLoaded(false);
				jobAutoFolder.getChildren(monitor);
			}
		}
		// add database space folder
		String databaseSpaceFolderId = database.getId() + NODE_SEPARATOR
				+ DB_SPACE_FOLDER_ID;
		ICubridNode databaseSpaceFolder = database.getChild(databaseSpaceFolderId);
		if (databaseSpaceFolder == null) {
			databaseSpaceFolder = new DefaultSchemaNode(databaseSpaceFolderId,
					DB_SPACE_FOLDER_NAME, "icons/navigator/volume_group.png");
			databaseSpaceFolder.setType(CubridNodeType.DBSPACE_FOLDER);
			databaseSpaceFolder.setContainer(true);
			databaseSpaceFolder.setEditorId(DatabaseStatusEditor.ID);
			ICubridNodeLoader loader = new CubridDbSpaceFolderLoader();
			loader.setLevel(getLevel());
			databaseSpaceFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				databaseSpaceFolder.getChildren(monitor);
			}
			database.addChild(databaseSpaceFolder);
		} else {
			if (databaseSpaceFolder.getLoader() != null
					&& databaseSpaceFolder.getLoader().isLoaded()) {
				databaseSpaceFolder.getLoader().setLoaded(false);
				databaseSpaceFolder.getChildren(monitor);
			}
		}
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
