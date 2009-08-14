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
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.cubrid.database.model.DatabaseInfo;
import com.cubrid.cubridmanager.core.cubrid.table.task.DelAllRecordsTask;
import com.cubrid.cubridmanager.ui.cubrid.table.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

public class DeleteTableAction extends
		SelectionAction {

	public static final String ID = DeleteTableAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DeleteTableAction(Shell shell, String text, ImageDescriptor icon) {
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
	public DeleteTableAction(Shell shell, ISelectionProvider provider,
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
		if (obj instanceof DefaultSchemaNode) {
			return true;
		}
		return false;
	}

	public void run() {

		Object[] obj = this.getSelectedObj();
		if (obj == null) {
			return;
		}

		int len = obj.length;

		for (int i = 0; i < len; i++) {
			if (!isSupported(obj[i])) {
				return;
			}
		}

		for (int i = 0; i < len; i++) {
			DefaultSchemaNode table = (DefaultSchemaNode) obj[i];
			String username = table.getDatabase().getUserName();
			if (username == null || username.length() <= 0) {
				return;
			}
			String tableName = table.getName();
			String message = Messages.bind(Messages.confirmTableDeleteWarn,
					tableName);

			if (!CommonTool.openConfirmBox(message)) {
				return;
			}

			CubridDatabase db = table.getDatabase();
			DatabaseInfo dbInfo = db.getDatabaseInfo();

			DelAllRecordsTask task = new DelAllRecordsTask(dbInfo);
			task.setTableName(tableName);
			task.setWarningMsg(null);

			TaskExecutor taskExecutor = new CommonTaskExec();
			taskExecutor.addTask(task);
			new ExecTaskWithProgress(taskExecutor).exec();
			int rowCount = task.getDeleteRecordsCount();

			if (rowCount > 1) {
				message = Messages.bind(
						Messages.resultTableDeleteInformantion2, tableName,
						rowCount);
			} else {
				message = Messages.bind(
						Messages.resultTableDeleteInformantion1, tableName,
						rowCount);
			}
			CommonTool.openInformationBox(Messages.MSG_INFORMATION, message);
		}
	}
}
