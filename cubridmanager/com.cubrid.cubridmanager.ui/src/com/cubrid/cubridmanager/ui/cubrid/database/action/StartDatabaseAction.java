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

import java.util.HashSet;
import java.util.Set;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to start database in C/S mode
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class StartDatabaseAction extends
		SelectionAction {

	public static final String ID = StartDatabaseAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public StartDatabaseAction(Shell shell, String text, ImageDescriptor icon) {
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
	public StartDatabaseAction(Shell shell, ISelectionProvider provider,
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
		return true;
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ISchemaNode) {
			ISchemaNode node = (ISchemaNode) obj;
			CubridDatabase database = node.getDatabase();
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
	 * Start database and refresh navigator
	 */
	public void run() {
		Object[] objArr = this.getSelectedObj();
		if (objArr == null || objArr.length <= 0) {
			setEnabled(false);
			return;
		}
		Set<CubridDatabase> databaseSet = new HashSet<CubridDatabase>();
		for (int i = 0; objArr != null && i < objArr.length; i++) {
			if (!isSupported(objArr[i])) {
				setEnabled(false);
				return;
			}
			ISchemaNode schemaNode = (ISchemaNode) objArr[i];
			CubridDatabase database = schemaNode.getDatabase();
			databaseSet.add(database);
		}
		final Object[] dbObjectArr = new Object[databaseSet.size()];
		databaseSet.toArray(dbObjectArr);
		ISelectionProvider provider = getSelectionProvider();
		final Shell shell = getShell();
		if (provider instanceof TreeViewer && dbObjectArr.length > 0) {
			final TreeViewer viewer = (TreeViewer) provider;
			TaskExecutor taskExcutor = new TaskExecutor() {
				public boolean exec(final IProgressMonitor monitor) {
					Display display = Display.getDefault();
					if (monitor.isCanceled()) {
						return false;
					}
					for (int i = 0; i < taskList.size(); i++) {
						ISchemaNode node = (ISchemaNode) dbObjectArr[i];
						final CubridDatabase database = node.getDatabase();
						if (!isSupported(database)) {
							continue;
						}
						ITask task = taskList.get(i);
						task.execute();
						final String msg = task.getErrorMsg();
						if (msg != null && msg.length() > 0
								&& !monitor.isCanceled()) {
							display.syncExec(new Runnable() {
								public void run() {
									CommonTool.openErrorBox(shell, msg);
								}
							});
							return false;
						}
						if (monitor.isCanceled()) {
							return false;
						}
						database.removeAllChild();
						if (database.getLoader() != null) {
							database.getLoader().setLoaded(false);
						}
						database.setRunningType(DbRunningType.CS);
						display.syncExec(new Runnable() {
							public void run() {
								viewer.refresh(database, true);
							}
						});
						if (monitor.isCanceled()) {
							return false;
						}
					}
					return true;
				}
			};
			for (int i = 0; i < dbObjectArr.length; i++) {
				ISchemaNode node = (ISchemaNode) dbObjectArr[i];
				CubridDatabase database = node.getDatabase();
				if (!isSupported(database)) {
					setEnabled(false);
					return;
				}
				CommonUpdateTask task = new CommonUpdateTask(
						CommonTaskName.START_DB_TASK_NAME,
						database.getServer().getServerInfo(),
						CommonSendMsg.commonDatabaseSendMsg);
				task.setDbName(database.getLabel());
				taskExcutor.addTask(task);
			}
			new ExecTaskWithProgress(taskExcutor).exec();
			ActionManager.getInstance().fireSelectionChanged(getSelection());
		}
	}

}
