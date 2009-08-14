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
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAutoAddVolumeInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.task.SetAutoAddVolumeTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.cubrid.dbspace.dialog.SetAutoAddVolumeDialog;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This is an action to listen to the selection of "set auto add volume".If
 * supported,it will pop up an instance of SetAutoAddVolumeDialog
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-4-13 created by lizhiqiang
 */
public class SetAutoAddVolumeAction extends
		SelectionAction {

	public static final String ID = SetAutoAddVolumeAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public SetAutoAddVolumeAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	protected SetAutoAddVolumeAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.common.action.setautoaddvolume");

	}

	/**
	 * Creates a Dialog which is the instance of SetAutoAddVolumeDialog to set a
	 * auto adding volume
	 */
	public void run() {
		Object[] obj = this.getSelectedObj();
		DefaultSchemaNode selection = null;
		CubridDatabase database = null;
		if (obj.length > 0 && obj[0] instanceof DefaultSchemaNode) {
			selection = (DefaultSchemaNode) obj[0];
			database = selection.getDatabase();
		}
		if (database == null) {
			CommonTool.openErrorBox(Messages.msgSelectDB);
			return;
		}
		//Get the info of AutoAddVolumeInfo
		GetAutoAddVolumeInfo getAutoAddVolumeInfo = new GetAutoAddVolumeInfo();
		final CommonQueryTask<GetAutoAddVolumeInfo> autoAddVolumetask = new CommonQueryTask<GetAutoAddVolumeInfo>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, getAutoAddVolumeInfo);
		autoAddVolumetask.setDbName(database.getLabel());

		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(autoAddVolumetask);
		//Gets the page size in the database
		int pageSize = 0;
		CommonQueryTask<DbSpaceInfoList> spaceInfoTask = null;
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		DbSpaceInfoList dbSpaceInfoList = databaseInfo.getDbSpaceInfoList();
		if (null == dbSpaceInfoList) {
			dbSpaceInfoList = new DbSpaceInfoList();
			spaceInfoTask = new CommonQueryTask<DbSpaceInfoList>(
					database.getServer().getServerInfo(),
					CommonSendMsg.commonDatabaseSendMsg, dbSpaceInfoList);
			spaceInfoTask.setDbName(database.getLabel());

			taskExecutor.addTask(spaceInfoTask);

		}

		new ExecTaskWithProgress(taskExecutor).exec();
		if (!taskExecutor.isSuccess()) {
			return;
		}
		if (spaceInfoTask != null) {
			final DbSpaceInfoList model = ((CommonQueryTask<DbSpaceInfoList>) spaceInfoTask).getResultModel();
			pageSize = model.getPagesize();
		} else {
			pageSize = dbSpaceInfoList.getPagesize();
		}
		getAutoAddVolumeInfo = autoAddVolumetask.getResultModel();

		SetAutoAddVolumeDialog dialog = new SetAutoAddVolumeDialog(getShell());
		dialog.setGetAutoAddVolumeInfo(getAutoAddVolumeInfo);
		dialog.setPageSize(pageSize);

		GetAutoAddVolumeInfo returnInfo = dialog.getGetAutoAddVolumeInfo();
		if (dialog.open() == Dialog.OK) {
			ServerInfo site = database.getServer().getServerInfo();
			SetAutoAddVolumeTask setTask = new SetAutoAddVolumeTask(site);
			setTask.setDbname(database.getName());
			setTask.setData(returnInfo.getData());
			setTask.setData_warn_outofspace(returnInfo.getData_warn_outofspace());
			setTask.setData_ext_page(returnInfo.getData_ext_page());
			setTask.setIndex(returnInfo.getIndex());
			setTask.setIndex_warn_outofspace(returnInfo.getIndex_warn_outofspace());
			setTask.setIndex_ext_page(returnInfo.getIndex_ext_page());

			taskExecutor = new CommonTaskExec();
			taskExecutor.addTask(setTask);
			new ExecTaskWithProgress(taskExecutor).exec();
		}

	}

	public boolean allowMultiSelections() {
		return false;
	}

	/**
	 * Sets the condition of this action.Only when database's running type is
	 * DbRunningType.CS, this action start to work.
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof DefaultSchemaNode
				&& ((DefaultSchemaNode) obj).getType().equals(
						CubridNodeType.DBSPACE_FOLDER)) {
			CubridDatabase database = ((DefaultSchemaNode) obj).getDatabase();
			if (database != null && database.isLogined()) {
				DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (dbUserInfo != null && dbUserInfo.isDbaAuthority())
					return true;
			}
		}

		return false;
	}

}
