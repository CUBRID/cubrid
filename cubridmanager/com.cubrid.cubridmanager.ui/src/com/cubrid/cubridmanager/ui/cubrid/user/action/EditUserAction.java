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
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllClassListTask;
import com.cubrid.cubridmanager.core.cubrid.table.task.GetAllPartitionClassTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.ui.cubrid.user.Messages;
import com.cubrid.cubridmanager.ui.cubrid.user.dialog.EditUserDialog;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * Edit the user
 * 
 * @author robin 2009-3-18
 */
public class EditUserAction extends
		SelectionAction {

	public static final String ID = EditUserAction.class.getName();
	private static final Logger logger = LogUtil.getLogger(EditUserAction.class);

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 */

	public EditUserAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 */
	public EditUserAction(Shell shell, ISelectionProvider provider,
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
		if (obj instanceof DefaultCubridNode
				&& ((DefaultCubridNode) obj).getType().equals(
						CubridNodeType.USER))
			return true;
		return false;
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see org.eclipse.jface.action.Action#run()
	 */
	public void run() {
		Shell shell = getShell();
		Object[] obj = this.getSelectedObj();
		CubridDatabase database = null;
		DefaultCubridNode node = null;
		if (obj.length > 0
				&& obj[0] instanceof DefaultCubridNode
				&& ((DefaultCubridNode) obj[0]).getType().equals(
						CubridNodeType.USER)) {
			node = (DefaultCubridNode) obj[0];
			database = ((ISchemaNode) node.getParent()).getDatabase();
		}
		if (database == null || node == null) {
			CommonTool.openErrorBox(getShell(), Messages.msgSelectDB);
			return;
		}

		final CommonQueryTask<DbUserInfoList> userTask = new CommonQueryTask<DbUserInfoList>(
				database.getServer().getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg, new DbUserInfoList());
		userTask.setDbName(database.getName());

		final GetAllClassListTask classInfoTask = new GetAllClassListTask(
				database.getDatabaseInfo());
		final GetAllPartitionClassTask partitionTask = new GetAllPartitionClassTask(
				database.getDatabaseInfo());

		EditUserDialog dlg = new EditUserDialog(shell);
		dlg.execTask(-1,
				new ITask[] { userTask, classInfoTask, partitionTask }, true,
				shell);
		if (userTask.getErrorMsg() != null
				|| classInfoTask.getErrorMsg() != null
				|| partitionTask.getErrorMsg() != null || userTask.isCancel()
				|| classInfoTask.isCancel() || partitionTask.isCancel())
			return;
		DbUserInfoList userListInfo = userTask.getResultModel();

		dlg.setUserListInfo(userListInfo);

		dlg.setDatabase(database);
		dlg.setUserName(node.getName());
		dlg.setPartitionClassMap(partitionTask.getPartitionClassMap());
		dlg.setNewFlag(false);
		try {
			if (dlg.open() == IDialogConstants.OK_ID) {

				ISelectionProvider provider = getSelectionProvider();

				if (provider instanceof TreeViewer) {
					TreeViewer treeViewer = (TreeViewer) provider;
					if (database.getDatabaseInfo().getAuthLoginedDbUserInfo().getName().equalsIgnoreCase(
							node.getName())) {
						CommonTool.openInformationBox(Messages.titleLogout,
								Messages.msgLogoutInfomation);
						database.setLogined(false);
						LayoutManager.closeAllEditorAndViewInDatabase(database);
						database.removeAllChild();
						treeViewer.refresh(database, true);
						CubridNodeManager.getInstance().fireCubridNodeChanged(
								new CubridNodeChangedEvent(
										database,
										CubridNodeChangedEventType.DATABASE_LOGOUT));
					} else {

						CommonTool.refreshNavigatorTree(treeViewer,
								node.getParent());
						setEnabled(false);
					}
				}
				ActionManager.getInstance().fireSelectionChanged(getSelection());
			}
		} catch (Exception e) {
			logger.error(e.getMessage(), e);
		}

	}

}
