/*
 * Copyright (C) 2009 NHN Corporation. All rights reserved by NHN.
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
package com.cubrid.cubridmanager.ui.cubrid.jobauto.action;

import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.model.AddEditType;
import com.cubrid.cubridmanager.core.cubrid.jobauto.model.BackupPlanInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.database.Messages;
import com.cubrid.cubridmanager.ui.cubrid.jobauto.dialog.EditBackupPlanDialog;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultSchemaNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * 
 * 
 * This is an action to listen to adding backup plan selection event and open an
 * instance of EditBackupPlanDialog class
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-3-13 created by lizhiqiang
 */
public class AddBackupPlanAction extends
		SelectionAction {

	public static final String ID = AddBackupPlanAction.class.getName();
	private String iconPath = "icons/navigator/auto_backup_item.png";

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public AddBackupPlanAction(Shell shell, String text, ImageDescriptor icon) {
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
	protected AddBackupPlanAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
	}

	/**
	 * 
	 * Creates a Dialog which is the instance of EditBackupPlanDialog to add a
	 * query plan
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
		assert (null != selection);
		if (database == null) {
			CommonTool.openErrorBox(Messages.msgSelectDB);
			return;
		}
		TreeViewer treeViewer = (TreeViewer) this.getSelectionProvider();
		EditBackupPlanDialog editBackupPlanDlg = new EditBackupPlanDialog(
				getShell());
		editBackupPlanDlg.setOperation(AddEditType.ADD);
		editBackupPlanDlg.initPara(selection);

		if (editBackupPlanDlg.open() == Dialog.OK) {
			BackupPlanInfo backupPlanInfo = editBackupPlanDlg.getBackupPlanInfo();
			String newBackupId = backupPlanInfo.getBackupid();
			DefaultSchemaNode newNode = new DefaultSchemaNode(newBackupId,
					newBackupId, iconPath);
			newNode.setContainer(false);
			newNode.setType(CubridNodeType.BACKUP_PLAN);
			newNode.setModelObj(backupPlanInfo);
			selection.addChild(newNode);
			treeViewer.add(selection, newNode);
		}

	}

	/**
	 * Sets this action support to select multi-object
	 * 
	 * @see org.eclipse.jface.action.IAction.ISelectionAction
	 * @return boolean
	 */
	public boolean allowMultiSelections() {
		return false;
	}

	/**
	 * Sets this action support this object
	 * 
	 * @see org.eclipse.jface.action.IAction.ISelectionAction
	 * @param obj
	 * @return boolean
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ISchemaNode) {
			ISchemaNode node = (ISchemaNode) obj;
			CubridDatabase database = node.getDatabase();
			if (node.getType() == CubridNodeType.BACKUP_PLAN_FOLDER
					&& database != null && database.isLogined()) {
				DbUserInfo dbUserInfo = database.getDatabaseInfo().getAuthLoginedDbUserInfo();
				if (dbUserInfo != null && dbUserInfo.isDbaAuthority())
					return true;
			}
		}
		return false;
	}

}
