/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search
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

import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.socket.SocketTask;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.database.task.GetDbSizeTask;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.DbSpaceInfoList;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.cubrid.database.dialog.CopyDatabaseDialog;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * copy the database in the server
 * 
 * The development/maintenance history of the class Document applicable
 * invariants The concurrency strategy
 * 
 * @author robin 2009-3-9
 */
public class CopyDatabaseAction extends SelectionAction {

	public static final String ID = CopyDatabaseAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public CopyDatabaseAction(Shell shell,String text,ImageDescriptor icon) {
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
	public CopyDatabaseAction(Shell shell,ISelectionProvider provider,String text,ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.cubrid.action.copydatabaseaction");
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
		if (obj instanceof ISchemaNode) {
			ISchemaNode node = (ISchemaNode) obj;
			CubridDatabase database = node.getDatabase();
			DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
			if (dbUserInfo != null && dbUserInfo.isDbaAuthority() && database.isLogined()
			        && database.getRunningType() == DbRunningType.STANDALONE) {
				return true;
			}
		}
		return false;
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.jface.action.Action#run()
	 */
	public void run() {
		Object[] obj = this.getSelectedObj();
		if (!isSupported(obj[0])) {
			setEnabled(false);
			return;
		}
		ISchemaNode schemaNode = (ISchemaNode) obj[0];
		CubridDatabase database = schemaNode.getDatabase();
		if (database == null) {
			CommonTool.openErrorBox(getShell(), Messages.msgSelectDB);
			return;
		}
		GetDbSizeTask dbSizeTask = new GetDbSizeTask(database.getServer().getServerInfo());
		dbSizeTask.setDbName(database.getName());

		final CommonQueryTask<DbSpaceInfoList> dbSpaceInfotask = new CommonQueryTask<DbSpaceInfoList>(database
		        .getServer().getServerInfo(), CommonSendMsg.commonDatabaseSendMsg, new DbSpaceInfoList());
		dbSpaceInfotask.setDbName(database.getName());

		CopyDatabaseDialog dlg = new CopyDatabaseDialog(getShell());
		dlg.execTask(-1, new SocketTask[]
			{
			        dbSizeTask,
			        dbSpaceInfotask
			}, true, getShell());
		if (dbSpaceInfotask.getErrorMsg() != null || dbSizeTask.isCancel() || dbSizeTask.getErrorMsg() != null
		        || dbSpaceInfotask.isCancel())
			return;
		dlg.setDbSize(dbSizeTask.getDbSize());
		dlg.setDbSpaceInfo(dbSpaceInfotask.getResultModel());
		dlg.setDatabase(database);
		dlg.setLocalServer(database.getServer().getServerInfo().isLocalServer());
		if (dlg.open() == IDialogConstants.OK_ID) {
			ISelectionProvider provider = getSelectionProvider();
			if (provider instanceof TreeViewer) {
				TreeViewer viewer = (TreeViewer) provider;
				CommonTool.refreshNavigatorTree(viewer, database.getParent());
			}
			ActionManager.getInstance().fireSelectionChanged(getSelection());
		}
	}

}
