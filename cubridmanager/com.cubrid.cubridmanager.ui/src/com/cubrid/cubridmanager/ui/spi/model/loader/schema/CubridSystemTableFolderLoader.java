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
 * This class is responsible to load the children of CUBRID system tables folder
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-14 created by pangqiren
 */
public class CubridSystemTableFolderLoader extends
		CubridNodeLoader {

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
			database.getDatabaseInfo().setSysTableInfoList(null);
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
		List<ClassInfo> allClassInfoList = task.getSchema(false, true);
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
		parent.removeAllChild();
		if (allClassInfoList != null) {
			for (ClassInfo classInfo : allClassInfoList) {
				String id = parent.getId() + NODE_SEPARATOR
						+ classInfo.getClassName();
				ICubridNode classNode = new DefaultSchemaNode(id,
						classInfo.getClassName(),
						"icons/navigator/schema_table_item.png");
				classNode.setType(CubridNodeType.SYSTEM_TABLE);
				classNode.setEditorId(SchemaEditor.ID);
				classNode.setContainer(false);
				classNode.setModelObj(classInfo);
				parent.addChild(classNode);
			}
		}
		database.getDatabaseInfo().setSysTableInfoList(allClassInfoList);
		Collections.sort(parent.getChildren());
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
