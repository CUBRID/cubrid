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
package com.cubrid.cubridmanager.ui.cubrid.serial.action;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.model.SerialInfo;
import com.cubrid.cubridmanager.core.cubrid.serial.task.DeleteSerialTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.serial.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to delete serial
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-8 created by pangqiren
 */
public class DeleteSerialAction extends
		SelectionAction {

	public static final String ID = DeleteSerialAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DeleteSerialAction(Shell shell, String text, ImageDescriptor icon) {
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
	public DeleteSerialAction(Shell shell, ISelectionProvider provider,
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
		if (obj instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) obj;
			if (node.getType() == CubridNodeType.SERIAL) {
				ISchemaNode schemaNode = (ISchemaNode) node;
				CubridDatabase database = schemaNode.getDatabase();
				if (database == null || !database.isLogined()
						|| database.getRunningType() != DbRunningType.CS) {
					return false;
				}
				DbUserInfo userInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (userInfo != null && userInfo.isDbaAuthority()) {
					return true;
				}
				SerialInfo serialInfo = (SerialInfo) node.getAdapter(SerialInfo.class);
				if (serialInfo != null
						&& userInfo != null
						&& userInfo.getName().equalsIgnoreCase(
								serialInfo.getOwner())) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Delete the selected serials
	 */
	public void run() {
		Object[] objArr = this.getSelectedObj();
		if (objArr == null || objArr.length <= 0) {
			return;
		}
		final List<String> serialNameList = new ArrayList<String>();
		String serialNames = "";
		for (int i = 0; objArr != null && i < objArr.length; i++) {
			if (!isSupported(objArr[i])) {
				setEnabled(false);
				return;
			}
			ISchemaNode schemaNode = (ISchemaNode) objArr[i];
			serialNames += schemaNode.getLabel();
			if (i != objArr.length - 1) {
				serialNames += ",";
			}
			serialNameList.add(schemaNode.getLabel());
		}
		boolean isDelete = CommonTool.openConfirmBox(getShell(), Messages.bind(
				Messages.msgConfirmDelSerial, serialNames));
		if (!isDelete) {
			return;
		}
		final Shell shell = getShell();
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = Display.getDefault();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					if (task instanceof DeleteSerialTask) {
						DeleteSerialTask deleteSerialTask = (DeleteSerialTask) task;
						String[] serialNames = new String[serialNameList.size()];
						deleteSerialTask.deleteSerial(serialNameList.toArray(serialNames));
					}
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
				}
				return true;
			}
		};
		ISchemaNode schemaNode = (ISchemaNode) objArr[0];
		CubridDatabase database = schemaNode.getDatabase();
		DatabaseInfo databaseInfo = database.getDatabaseInfo();
		DeleteSerialTask deleteSerialTask = new DeleteSerialTask(databaseInfo);
		taskExcutor.addTask(deleteSerialTask);
		new ExecTaskWithProgress(taskExcutor).exec();
		ISelectionProvider provider = this.getSelectionProvider();
		ICubridNode parent = schemaNode.getParent();
		if (provider instanceof TreeViewer) {
			TreeViewer viewer = (TreeViewer) provider;
			if (parent.getLoader() != null) {
				parent.getLoader().setLoaded(false);
			}
			viewer.refresh(parent);
		}
	}
}
