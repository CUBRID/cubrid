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

package com.cubrid.cubridmanager.ui.cubrid.user.action;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllPartitionClassTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.ui.cubrid.user.dialog.EditUserDialog;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * This action is responsible to add the user
 * 
 * @author robin 2009-3-18
 */
public class AddUserAction extends
		SelectionAction {

	public static final String ID = AddUserAction.class.getName();
	private static final Logger logger = LogUtil.getLogger(AddUserAction.class);

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public AddUserAction(Shell shell, String text, ImageDescriptor enabledIcon,
			ImageDescriptor disabledIcon) {
		this(shell, null, text, enabledIcon, disabledIcon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public AddUserAction(Shell shell, ISelectionProvider provider, String text,
			ImageDescriptor enabledIcon, ImageDescriptor disabledIcon) {
		super(shell, provider, text, enabledIcon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
		this.setDisabledImageDescriptor(disabledIcon);
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
			if (database != null
					&& database.getRunningType() == DbRunningType.CS
					&& database.isLogined()) {
				DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (dbUserInfo != null && dbUserInfo.isDbaAuthority())
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
		ISchemaNode node = (ISchemaNode) obj[0];
		CubridDatabase database = node.getDatabase();

		final CommonQueryTask<DbUserInfoList> userTask = new CommonQueryTask<DbUserInfoList>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, new DbUserInfoList());
		userTask.setDbName(database.getName());
		final GetAllClassListTask classInfoTask = new GetAllClassListTask(
				database.getDatabaseInfo());

		final GetAllPartitionClassTask partitionTask = new GetAllPartitionClassTask(
				database.getDatabaseInfo());

		EditUserDialog dlg = new EditUserDialog(getShell());
		dlg.execTask(-1,
				new ITask[] { userTask, classInfoTask, partitionTask }, true,
				getShell());
		if (userTask.getErrorMsg() != null
				|| partitionTask.getErrorMsg() != null
				|| classInfoTask.getErrorMsg() != null || userTask.isCancel()
				|| partitionTask.isCancel() || classInfoTask.isCancel())
			return;
		DbUserInfoList userListInfo = userTask.getResultModel();
		dlg.setUserListInfo(userListInfo);
		dlg.setPartitionClassMap(partitionTask.getPartitionClassMap());
		dlg.setDatabase(database);
		dlg.setNewFlag(true);
		try {
			if (dlg.open() == IDialogConstants.OK_ID) {
				ISelectionProvider provider = getSelectionProvider();
				if (provider instanceof TreeViewer) {
					TreeViewer treeViewer = (TreeViewer) provider;
					CommonTool.refreshNavigatorTree(treeViewer, node);
					setEnabled(false);
				}
				ActionManager.getInstance().fireSelectionChanged(getSelection());
			}
		} catch (Exception e) {
			logger.error(e.getMessage(), e);
		}

	}
}
