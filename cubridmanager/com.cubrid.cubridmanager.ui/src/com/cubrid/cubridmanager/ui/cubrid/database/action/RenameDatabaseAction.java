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
package com.cubrid.cubridmanager.ui.cubrid.database.action;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.database.dialog.RenameDatabaseDialog;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to rename database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class RenameDatabaseAction extends
		SelectionAction {

	public static final String ID = RenameDatabaseAction.class.getName();
	private DbSpaceInfoList dbSpaceInfo = null;
	private boolean isCancel = false;

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public RenameDatabaseAction(Shell shell, String text, ImageDescriptor icon) {
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
	public RenameDatabaseAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return false;
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof CubridDatabase) {
			CubridDatabase database = (CubridDatabase) obj;
			DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
			if (dbUserInfo != null && dbUserInfo.isDbaAuthority()
					&& database.isLogined()
					&& database.getRunningType() == DbRunningType.STANDALONE) {
				return true;
			}
			return false;
		}
		return false;
	}

	/**
	 * Get rename database needed information and open the RenameDatabaseDialog
	 */
	public void run() {
		Object[] obj = this.getSelectedObj();
		if (!isSupported(obj[0])) {
			setEnabled(false);
			return;
		}
		isCancel = false;
		CubridDatabase database = (CubridDatabase) obj[0];
		final RenameDatabaseDialog dialog = new RenameDatabaseDialog(getShell());
		dialog.setDatabase(database);
		final Shell shell = getShell();
		TaskExecutor taskExcutor = new TaskExecutor() {
			@SuppressWarnings("unchecked")
			public boolean exec(final IProgressMonitor monitor) {
				Display display = Display.getDefault();
				if (monitor.isCanceled()) {
					isCancel = true;
					return false;
				}
				for (ITask task : taskList) {
					task.execute();
					final String msg = task.getErrorMsg();
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(shell, msg);
							}
						});
						isCancel = true;
						return false;
					}
					if (monitor.isCanceled()) {
						isCancel = true;
						return false;
					}
					if (task instanceof CommonQueryTask) {
						dbSpaceInfo = ((CommonQueryTask<DbSpaceInfoList>) task).getResultModel();
					}
				}
				return true;
			}
		};
		dbSpaceInfo = new DbSpaceInfoList();
		CommonQueryTask<DbSpaceInfoList> task = new CommonQueryTask<DbSpaceInfoList>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, dbSpaceInfo);
		task.setDbName(database.getLabel());
		taskExcutor.addTask(task);
		new ExecTaskWithProgress(taskExcutor).exec();
		if (!isCancel) {
			dialog.setDbSpaceInfoList(dbSpaceInfo);
			if (dialog.open() == IDialogConstants.OK_ID) {
				ISelectionProvider provider = this.getSelectionProvider();
				if (provider instanceof TreeViewer) {
					TreeViewer viewer = (TreeViewer) provider;
					CommonTool.refreshNavigatorTree(viewer,
							database.getParent());
				}
			}
		}
	}

}
