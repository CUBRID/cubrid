/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.logs.model.LogContentInfo;
import com.cubrid.cubridmanager.core.logs.model.LogInfo;
import com.cubrid.cubridmanager.core.logs.task.GetLogListTask;
import com.cubrid.cubridmanager.core.logs.task.ResetLogTask;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.logs.editor.LogEditorPart;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to reset admin log.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-10 created by wuyingshi
 */
public class ResetAdminLogAction extends
		SelectionAction {
	private static final Logger logger = LogUtil.getLogger(LogViewAction.class);
	public static final String ID = ResetAdminLogAction.class.getName();
	private boolean isSuccess;// If the operation is succeed,it is true,it is

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public ResetAdminLogAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param textsss
	 * @param icon
	 */
	public ResetAdminLogAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.logs.action.ResetAdminLog");
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
			if (serverUserInfo == null || !serverUserInfo.isAdmin()) {
				return false;
			}
			if (node.getType() == CubridNodeType.LOGS_BROKER_ADMIN_LOG) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Reset Admin log
	 */
	public void run() {
		if (!CommonTool.openConfirmBox(Messages.warning_resetAdminLog))
			return;
		Object[] selected = this.getSelectedObj();
		LogInfo logInfo = (LogInfo) ((DefaultCubridNode) selected[0]).getAdapter(LogInfo.class);
		ResetLogTask resetLogTask = new ResetLogTask(
				((DefaultCubridNode) selected[0]).getServer().getServerInfo());
		resetLogTask.setPath(logInfo.getPath());
		TaskExecutor taskExecutor = new AdminLogTaskExec();
		taskExecutor.addTask(resetLogTask);
		new ExecTaskWithProgress(taskExecutor).exec();
		if (isSuccess) {
			CommonTool.openInformationBox(Messages.msg_success,
					Messages.msg_deleteAllLog);
		}		
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IWorkbenchPage activePage = window.getActivePage();
		IEditorPart editor = activePage.findEditor((ICubridNode) selected[0]);

		if (null != editor) {
			//activePage.closeEditor(editor, true);
		
		Object[] obj = this.getSelectedObj();
		if (!isSupported(obj[0])) {
			setEnabled(false);
			return;
		}
		ICubridNode node = (ICubridNode) obj[0];
		logInfo = (LogInfo) node.getAdapter(LogInfo.class);
		GetLogListTask task = new GetLogListTask(
				node.getServer().getServerInfo());
		task.setPath(logInfo.getPath());
		task.setStart("1");
		task.setEnd("100");
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();

		LogContentInfo logContentInfo = (LogContentInfo) task.getLogContent();
		try {
			editor = window.getActivePage().openEditor(node, LogEditorPart.ID);
			((LogEditorPart) editor).setinfo(logContentInfo);
		} catch (PartInitException e) {
			logger.error(e.getMessage(), e);
		}
		}
	}

	/**
	 * A common type which extends the type TaakExecutor and overrides the
	 * method exec.Generally ,it can be used in an action or other type of which
	 * there is no dialog
	 */
	private class AdminLogTaskExec extends
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
