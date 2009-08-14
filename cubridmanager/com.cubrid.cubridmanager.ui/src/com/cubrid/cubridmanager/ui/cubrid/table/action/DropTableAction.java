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
package com.cubrid.cubridmanager.ui.cubrid.table.action;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.cubrid.table.task.DropClassTask;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

public class DropTableAction extends
		SelectionAction {
	public static final String ID = DropTableAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DropTableAction(Shell shell, String text, ImageDescriptor icon) {
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
	public DropTableAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, null, text, icon);
		this.setId(ID);

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

		if (obj instanceof DefaultSchemaNode) {
			DefaultSchemaNode table = (DefaultSchemaNode) obj;
			CubridNodeType type = table.getType();
			switch (type) {
			case USER_TABLE:
				setText(com.cubrid.cubridmanager.ui.spi.Messages.tableDropActionName);
				break;
			case USER_VIEW:
				setText(com.cubrid.cubridmanager.ui.spi.Messages.viewDropActionName);
				break;
			default:
			}
			return true;
		}
		return false;
	}

	public void run() {
		Object[] obj = this.getSelectedObj();
		if (obj == null) {
			setEnabled(false);
			return;
		}

		if (!isSupported(obj[0])) {
			setEnabled(false);
			return;
		}
		DefaultSchemaNode table = (DefaultSchemaNode) obj[0];
		CubridNodeType type = table.getType();
		String message = null;
		switch (type) {
		case USER_TABLE:
		case USER_PARTITIONED_TABLE_FOLDER:
			message = Messages.bind(Messages.dropWarnMSG, Messages.dropTable,
					table.getName());
			break;
		case USER_VIEW: //User schema/View instance
			message = Messages.bind(Messages.dropWarnMSG, Messages.dropView,
					table.getName());
			break;
		default:
		}
		boolean ret = CommonTool.openConfirmBox(message);
		if (!ret) {
			return;
		}
		DropClassTask task = new DropClassTask(
				table.getServer().getServerInfo());
		task.setDbName(table.getDatabase().getName());
		task.setVirtualClassName(table.getName());
		
		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (task.isSuccess()) {
			ISelectionProvider provider = this.getSelectionProvider();
			final TreeViewer viewer = (TreeViewer) provider;
			CommonTool.refreshNavigatorTree(viewer, table.getParent());
			ActionManager.getInstance().fireSelectionChanged(getSelection());
		} else {
			// the error information has been displayed by ExecTaskWithProgress instance
		}

	}
}