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
package com.cubrid.cubridmanager.ui.cubrid.dbspace.action;

import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.AddVolumeDbInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAddVolumeStatusInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.task.AddVolumeDbTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.dialog.AddVolumeDialog;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * An action to add volume
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-4-16 created by lizhiqiang
 */
public class AddVolumeAction extends
		SelectionAction {

	public static final String ID = AddVolumeAction.class.getName();

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public AddVolumeAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);

	}

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	protected AddVolumeAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
	}

	/**
	 * 
	 * Creates a Dialog which is the instance of AddVolumeDialog to add a query
	 * plan
	 * 
	 */
	public void run() {

		Object[] obj = this.getSelectedObj();
		CubridDatabase database = null;
		DefaultSchemaNode selection = null;
		if (obj.length > 0 && obj[0] instanceof DefaultSchemaNode) {
			selection = (DefaultSchemaNode) obj[0];
			database = selection.getDatabase();
		}

		if (database == null) {
			CommonTool.openErrorBox(Messages.msgSelectDB);
			return;
		}
		database = selection.getDatabase();
		// Gets the status of adding volume
		GetAddVolumeStatusInfo getAddVolumeStatusInfo = new GetAddVolumeStatusInfo();
		final CommonQueryTask<GetAddVolumeStatusInfo> statusTask = new CommonQueryTask<GetAddVolumeStatusInfo>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, getAddVolumeStatusInfo);
		statusTask.setDbName(database.getLabel());
		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(statusTask);

		CommonQueryTask<DbSpaceInfoList> spaceInfoTask = null;
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		DbSpaceInfoList dbSpaceInfoList = databaseInfo.getDbSpaceInfoList();
		int pageSize = 0;
		if (null == dbSpaceInfoList) {
			dbSpaceInfoList = new DbSpaceInfoList();
			spaceInfoTask = new CommonQueryTask<DbSpaceInfoList>(
					database.getServer().getServerInfo(),
					CommonSendMsg.commonDatabaseSendMsg, dbSpaceInfoList);

			spaceInfoTask.setDbName(database.getLabel());
			taskExecutor.addTask(spaceInfoTask);
		}
		new ExecTaskWithProgress(taskExecutor).exec();
		if (spaceInfoTask != null) {
			final DbSpaceInfoList model = ((CommonQueryTask<DbSpaceInfoList>) spaceInfoTask).getResultModel();
			pageSize = model.getPagesize();
		} else {
			pageSize = dbSpaceInfoList.getPagesize();
		}

		getAddVolumeStatusInfo = statusTask.getResultModel();

		AddVolumeDialog addVolumeDialog = new AddVolumeDialog(getShell());
		addVolumeDialog.setGetAddVolumeStatusInfo(getAddVolumeStatusInfo);
		addVolumeDialog.initPara(selection);
		addVolumeDialog.setPageSize(pageSize);
		if (addVolumeDialog.open() == Dialog.OK) {
			// Adds the volumes task
			AddVolumeDbInfo addVolumeDbInfo = addVolumeDialog.getAddVolumeDbInfo();
			addVolumeDbInfo.setDbname(database.getName());
			AddVolumeDbTask addTask = new AddVolumeDbTask(
					database.getServer().getServerInfo());
			addTask.setDbname(database.getLabel());
			addTask.setVolname(addVolumeDbInfo.getVolname());
			addTask.setPurpose(addVolumeDbInfo.getPurpose());
			addTask.setPath(addVolumeDbInfo.getPath());
			addTask.setNumberofpages(addVolumeDbInfo.getNumberofpage());
			addTask.setSize_need_mb(addVolumeDbInfo.getSize_need_mb());
			// Gets the database space info
			DbSpaceInfoList dbSpaceInfo = new DbSpaceInfoList();
			final CommonQueryTask<DbSpaceInfoList> dbSpaceInfoTask = new CommonQueryTask<DbSpaceInfoList>(
					database.getServer().getServerInfo(),
					CommonSendMsg.commonDatabaseSendMsg, dbSpaceInfo);
			dbSpaceInfoTask.setDbName(database.getLabel());

			TaskExecutor taskExec = new CommonTaskExec();
			taskExec.addTask(addTask);
		
			taskExec.addTask(dbSpaceInfoTask);
			new ExecTaskWithProgress(taskExec).exec();

			if (taskExec.isSuccess()) {
				TreeViewer treeViewer = (TreeViewer) this.getSelectionProvider();
				if (selection.getType() == CubridNodeType.DBSPACE_FOLDER) {
					CommonTool.refreshNavigatorTree(treeViewer, selection);
				} else {
					CommonTool.refreshNavigatorTree(treeViewer,
							selection.getParent());
				}
			}
		}

	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections()
	 */
	public boolean allowMultiSelections() {
		return false;
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java.lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ISchemaNode) {
			ISchemaNode node = (ISchemaNode) obj;
			CubridDatabase database = node.getDatabase();
			if (database != null && database.isLogined()) {
				DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (dbUserInfo != null && dbUserInfo.isDbaAuthority())
					return true;
			}
		}
		return false;
	}

}
