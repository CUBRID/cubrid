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
package com.cubrid.cubridmanager.ui.spi.model.loader.schema;

import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.model.ClassInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;
import com.cubrid.cubridmanager.ui.cubrid.table.editor.SchemaEditor;
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
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * This class is responsible to load the children of CUBRID tables folder
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-19 created by pangqiren
 */
public class CubridTablesFolderLoader extends
		CubridNodeLoader {

	private static final String SYSTEM_TABLE_FOLDER_NAME = Messages.msgSystemTableFolderName;
	public static final String SYSTEM_TABLE_FOLDER_ID = "System Tables";

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		CubridDatabase database = ((ISchemaNode) parent).getDatabase();
		if (!database.isLogined()
				|| database.getRunningType() == DbRunningType.STANDALONE) {
			database.getDatabaseInfo().setUserTableInfoList(null);
			database.getDatabaseInfo().setSysTableInfoList(null);
			database.getDatabaseInfo().setPartitionedTableMap(null);
			database.getDatabaseInfo().clearSchemas();
			parent.removeAllChild();
			CubridNodeManager.getInstance().fireCubridNodeChanged(
					new CubridNodeChangedEvent((ICubridNode) parent,
							CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
			return;
		}
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		final GetAllClassListTask task = new GetAllClassListTask(databaseInfo);
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
		List<ClassInfo> allClassInfoList = task.getSchema(true, true);
		final String errorMsg = task.getErrorMsg();
		if (!monitor.isCanceled() && errorMsg != null
				&& errorMsg.trim().length() > 0) {
			parent.removeAllChild();
			Display display = Display.getDefault();
			display.syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(errorMsg);
				}
			});
			setLoaded(true);
			return;
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		// add system table folder
		String systemTableFolderId = parent.getId() + NODE_SEPARATOR
				+ SYSTEM_TABLE_FOLDER_ID;
		ICubridNode systemTableFolder = parent.getChild(systemTableFolderId);
		if (systemTableFolder == null) {
			systemTableFolder = new DefaultSchemaNode(systemTableFolderId,
					SYSTEM_TABLE_FOLDER_NAME, "icons/navigator/folder.png");
			systemTableFolder.setType(CubridNodeType.SYSTEM_TABLE_FOLDER);
			systemTableFolder.setContainer(true);
			ICubridNodeLoader loader = new CubridSystemTableFolderLoader();
			loader.setLevel(getLevel());
			systemTableFolder.setLoader(loader);
			if (getLevel() == DEFINITE_LEVEL) {
				systemTableFolder.getChildren(monitor);
			}
			parent.addChild(systemTableFolder);
		} else {
			if (systemTableFolder.getLoader() != null
					&& systemTableFolder.getLoader().isLoaded()) {
				systemTableFolder.getLoader().setLoaded(false);
				systemTableFolder.getChildren(monitor);
			}
		}
		parent.removeAllChild();
		if (allClassInfoList != null) {
			for (ClassInfo classInfo : allClassInfoList) {
				String id = parent.getId() + NODE_SEPARATOR
						+ classInfo.getClassName();
				ICubridNode classNode = new DefaultSchemaNode(id,
						classInfo.getClassName(),
						"icons/navigator/schema_table_item.png");
				classNode.setType(CubridNodeType.USER_TABLE);
				classNode.setEditorId(SchemaEditor.ID);
				classNode.setContainer(false);
				classNode.setModelObj(classInfo);
				if (classInfo.isPartitionedClass()) {
					classNode.setType(CubridNodeType.USER_PARTITIONED_TABLE_FOLDER);
					classNode.setIconPath("icons/navigator/schema_table_partition.png");
					classNode.setContainer(true);
					ICubridNodeLoader loader = new CubridPartitionedTableLoader();
					loader.setLevel(getLevel());
					classNode.setLoader(loader);
					if (getLevel() == DEFINITE_LEVEL) {
						classNode.getChildren(monitor);
					}
				}
				parent.addChild(classNode);
			}
		}
		database.getDatabaseInfo().setUserTableInfoList(allClassInfoList);
		database.getDatabaseInfo().clearSchemas();
		Collections.sort(parent.getChildren());
		parent.getChildren().add(0, systemTableFolder);
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));

	}
}
