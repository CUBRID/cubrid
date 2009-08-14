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

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.logs.model.ManagerLogInfos;
import com.cubrid.cubridmanager.core.logs.task.GetManagerLogListTask;
import com.cubrid.cubridmanager.ui.logs.Messages;
import com.cubrid.cubridmanager.ui.logs.editor.LogEditorPart;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.TaskJob;
import com.cubrid.cubridmanager.ui.spi.progress.TaskJobExecutor;

/**
 * 
 * This action is responsible to view manager log.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-10 created by wuyingshi
 */
public class ManagerLogViewAction extends
		SelectionAction {

	private static final Logger logger = LogUtil.getLogger(LogViewAction.class);
	public static final String ID = ManagerLogViewAction.class.getName();
	private ICubridNode cubridNode = null;

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public ManagerLogViewAction(Shell shell, String text, ImageDescriptor icon) {
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
	public ManagerLogViewAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.logs.action.LogView");
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
			if (node.getType() == CubridNodeType.LOGS_MANAGER_ACCESS_LOG
					|| node.getType() == CubridNodeType.LOGS_MANAGER_ERROR_LOG) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Open the log editor and show log conent
	 */
	public void run() {

		final IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		if (cubridNode == null) {
			Object[] obj = this.getSelectedObj();
			if (!isSupported(obj[0])) {
				setEnabled(false);
				return;
			}
			cubridNode = (ICubridNode) obj[0];
		}
		final GetManagerLogListTask task = new GetManagerLogListTask(
				cubridNode.getServer().getServerInfo());

		TaskJobExecutor taskJobExecutor = new TaskJobExecutor() {
			public IStatus exec(IProgressMonitor monitor) {
				if (monitor.isCanceled()) {
					cubridNode = null;
					return Status.CANCEL_STATUS;
				}
				for (ITask t : taskList) {
					t.execute();
					final String msg = t.getErrorMsg();

					if (monitor.isCanceled()) {
						cubridNode = null;
						return Status.CANCEL_STATUS;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						Display.getDefault().syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(msg);
							}
						});
						cubridNode = null;
						return Status.CANCEL_STATUS;
					} else {
						Display.getDefault().syncExec(new Runnable() {
							public void run() {
								ManagerLogInfos managerLogInfos = (ManagerLogInfos) task.getLogContent();
								IEditorPart editorPart = LayoutManager.getInstance().getEditorPart(
										cubridNode);
								if (editorPart != null) {
									window.getActivePage().closeEditor(
											editorPart, false);
								}
								try {
									IEditorPart editor = window.getActivePage().openEditor(
											cubridNode, LogEditorPart.ID);
									((LogEditorPart) editor).setManagerLogInfo(
											managerLogInfos,
											cubridNode.getLabel());
								} catch (PartInitException e) {
									logger.error(e.getMessage(), e);
								}
							}
						});
					}
					if (monitor.isCanceled()) {
						cubridNode = null;
						return Status.CANCEL_STATUS;
					}
				}
				cubridNode = null;
				return Status.OK_STATUS;
			}
		};
		taskJobExecutor.addTask(task);
		TaskJob job = new TaskJob(Messages.viewLogJobName, taskJobExecutor);
		job.setUser(true);
		job.schedule();
	}

	public void setCubridNode(ICubridNode cubridNode) {
		this.cubridNode = cubridNode;
	}

}
