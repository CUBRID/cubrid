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

package com.cubrid.cubridmanager.ui.logs.action;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.logs.model.LogInfo;
import com.cubrid.cubridmanager.core.logs.task.DelLogTask;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to RemoveLogAction
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-10 created by wuyingshi
 */
public class RemoveLogAction extends
		SelectionAction {

	public static final String ID = RemoveLogAction.class.getName();
	private boolean isSuccess;// If the operation is succeed,it is true,it is

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public RemoveLogAction(Shell shell, String text, ImageDescriptor icon) {
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
	public RemoveLogAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.logs.action.RemoveLog");
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
			ServerUserInfo serverUserInfo = node.getServer().getServerInfo().getLoginedUserInfo();
			if (node.getType() == CubridNodeType.BROKER_SQL_LOG) {
				if (serverUserInfo == null
						|| serverUserInfo.getCasAuth() != CasAuthType.AUTH_ADMIN) {
					return false;
				}
				return true;
			} else if (node.getType() == CubridNodeType.LOGS_BROKER_ACCESS_LOG
					|| node.getType() == CubridNodeType.LOGS_BROKER_ERROR_LOG
					|| node.getType() == CubridNodeType.LOGS_SERVER_DATABASE_LOG) {
				if (serverUserInfo == null || !serverUserInfo.isAdmin()) {
					return false;
				}
				return true;
			}
		}
		return false;
	}

	/**
	 * Delete log file
	 */
	public void run() {
		if (!CommonTool.openConfirmBox(Messages.warning_removeLog))
			return;
		Object[] selected = this.getSelectedObj();
		String lastDBLog = "";
		if (((DefaultCubridNode) selected[0]).getId().indexOf("Logs/Server") >= 0) {
			String[] path = new String[((DefaultCubridNode) selected[0]).getParent().getChildren().size()];
			//get last file name
			LogInfo currLogFile;
			for (int i = 0, len = path.length; i < len; i++) {
				currLogFile = ((LogInfo) (((DefaultCubridNode) selected[0]).getParent().getChildren().get(
						i).getAdapter(LogInfo.class)));
				if (lastDBLog.compareTo(currLogFile.getPath()) < 0) {
					lastDBLog = currLogFile.getPath();
				}
			}
			//get last file name

		}
		LogInfo logInfo = (LogInfo) ((DefaultCubridNode) selected[0]).getAdapter(LogInfo.class);
		if (logInfo.getPath().equals(lastDBLog)) {
			return;
		}
		DelLogTask delLogTask = new DelLogTask(
				((DefaultCubridNode) selected[0]).getServer().getServerInfo());
		delLogTask.setPath(logInfo.getPath());
		delLogTask.setOpen("files");
		delLogTask.setClose("files");
		TaskExecutor taskExecutor = new DelLogTaskExec();
		taskExecutor.addTask(delLogTask);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (isSuccess) {
			TreeViewer treeViewer = (TreeViewer) this.getSelectionProvider();
			DefaultCubridNode delNode = ((DefaultCubridNode) selected[0]);
			ICubridNode parentNode = delNode.getParent();
			parentNode.removeChild(delNode);
			treeViewer.remove(delNode);

			IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
			if (window == null) {
				return;
			}
			IWorkbenchPage activePage = window.getActivePage();
			IEditorPart editor = activePage.findEditor((ICubridNode) selected[0]);

			if (null != editor) {
				activePage.closeEditor(editor, true);
			}
		}
	}

	/**
	 * A common type which extends the type TaakExecutor and overrides the
	 * method exec.Generally ,it can be used in an action or other type of which
	 * there is no dialog
	 */
	private class DelLogTaskExec extends
			TaskExecutor {

		/**
		 * Override method
		 * 
		 * @param monitor
		 * @return
		 */

		public boolean exec(final IProgressMonitor monitor) {
			isSuccess = true;
			Display display = Display.getDefault();

			if (monitor.isCanceled()) {
				isSuccess = false;
				return isSuccess;
			}

			for (ITask task : taskList) {
				task.execute();
				final String msg = task.getErrorMsg();
				if (monitor.isCanceled()) {
					return false;
				}
				if (msg != null && msg.length() > 0 && !monitor.isCanceled()) {
					isSuccess = false;
					display.syncExec(new Runnable() {
						public void run() {
							CommonTool.openErrorBox(getShell(), msg);
						}
					});
					isSuccess = false;
					return isSuccess;
				}
				if (monitor.isCanceled()) {
					isSuccess = false;
					return isSuccess;
				}
			}

			return isSuccess;
		}
	}
}
