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

package com.cubrid.cubridmanager.ui.cubrid.procedure.action;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.DbRunningType;
import com.cubrid.cubridmanager.core.cubrid.sp.model.SPInfo;
import com.cubrid.cubridmanager.core.cubrid.sp.task.CommonSQLExcuterTask;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.ui.cubrid.procedure.Messages;
import com.cubrid.cubridmanager.ui.cubrid.procedure.dialog.EditProcedureDialog;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * This action is responsible to delete procedure
 * 
 * @author robin 2009-3-18
 */
public class DeleteProcedureAction extends
		SelectionAction {

	public static final String ID = DeleteProcedureAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DeleteProcedureAction(Shell shell, String text, ImageDescriptor icon) {
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
	public DeleteProcedureAction(Shell shell, ISelectionProvider provider,
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
		if (obj instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) obj;
			if (node.getType() == CubridNodeType.STORED_PROCEDURE_PROCEDURE) {
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
				SPInfo spInfo = (SPInfo) node.getAdapter(SPInfo.class);
				if (spInfo != null
						&& userInfo != null
						&& userInfo.getName().equalsIgnoreCase(
								spInfo.getOwner())) {
					return true;
				}
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
		if (!CommonTool.openConfirmBox(getShell(),
				Messages.msgSureDropProcedure))
			return;
		Shell shell = getShell();
		Object[] obj = this.getSelectedObj();
		CubridDatabase database = null;
		DefaultCubridNode node = null;
		if (obj.length > 0
				&& obj[0] instanceof DefaultCubridNode
				&& ((DefaultCubridNode) obj[0]).getType().equals(
						CubridNodeType.STORED_PROCEDURE_PROCEDURE)) {
			node = (DefaultCubridNode) obj[0];
			database = ((ISchemaNode) node.getParent()).getDatabase();
		}
		if (database == null || node == null) {
			CommonTool.openErrorBox(shell, Messages.errSelectProcedure);
			return;
		}

		CommonSQLExcuterTask task = new CommonSQLExcuterTask(
				database.getDatabaseInfo());
		String sql = " drop procedure " + node.getName();
		task.addSqls(sql);
		EditProcedureDialog dlg = new EditProcedureDialog(shell);
		dlg.connect(-1, new ITask[] { task }, false, shell);
		if (task.getErrorMsg() != null || task.isCancel())
			return;
		if (task.getErrorMsg() == null) {
			CommonTool.openInformationBox(shell, Messages.titleSuccess,
					Messages.msgDeleteProcedureSuccess);
			ISelectionProvider provider = getSelectionProvider();
			if (provider instanceof TreeViewer) {
				TreeViewer treeViewer = (TreeViewer) provider;
				CommonTool.refreshNavigatorTree(treeViewer, node.getParent());
				setEnabled(false);
			}
			ActionManager.getInstance().fireSelectionChanged(getSelection());
		} else {
			return;
		}
	}

}
