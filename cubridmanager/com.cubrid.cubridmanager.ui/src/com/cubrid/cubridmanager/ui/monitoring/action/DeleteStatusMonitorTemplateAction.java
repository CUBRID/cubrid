/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
package com.cubrid.cubridmanager.ui.monitoring.action;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.model.StatusMonitorAuthType;
import com.cubrid.cubridmanager.core.monitoring.task.DelStatusTemplateTask;
import com.cubrid.cubridmanager.ui.monitoring.Messages;
import com.cubrid.cubridmanager.ui.monitoring.editor.StatusMonitorViewPart;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This class is an action in order to listen to selection and delete relevant
 * status monitor template
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-3 created by lizhiqiang
 */
public class DeleteStatusMonitorTemplateAction extends
		SelectionAction {

	public static final String ID = DeleteStatusMonitorTemplateAction.class.getName();

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public DeleteStatusMonitorTemplateAction(Shell shell, String text,
			ImageDescriptor icon) {
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
	protected DeleteStatusMonitorTemplateAction(Shell shell,
			ISelectionProvider provider, String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
	}

	/**
	 * Deletes the selected status template
	 * 
	 */

	public void run() {		
		Object[] objs = this.getSelectedObj();
		assert(objs != null);
		List<String> nodeNames = new ArrayList<String>();
		for (Object obj : objs) {
			ICubridNode selection = (ICubridNode) obj;
			nodeNames.add(selection.getLabel());
		}
		if(!CommonTool.openConfirmBox(Messages.bind(Messages.delStatusMonitorConfirmContent, nodeNames)))
			return;
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IWorkbenchPage activePage = window.getActivePage();
		IViewPart viewPart = activePage.findView(StatusMonitorViewPart.ID);
		TaskExecutor taskExecutor = new CommonTaskExec();
		for (Object obj : objs) {
			ICubridNode selection = (ICubridNode) obj;
			if (selection.getType() == CubridNodeType.STATUS_MONITOR_TEMPLATE) {
				DelStatusTemplateTask delTsk = new DelStatusTemplateTask(
						selection.getServer().getServerInfo());
				//closes the view part
				if (null != viewPart) {
					if (viewPart.getTitle().endsWith(selection.getLabel())) {
						activePage.hideView(viewPart);
					}
				}
				delTsk.setName(selection.getName());			
				taskExecutor.addTask(delTsk);
			}
		}
		new ExecTaskWithProgress(taskExecutor).exec();
	   if(!taskExecutor.isSuccess()){
		   return;
	   }
		TreeViewer treeViewer = (TreeViewer) this.getSelectionProvider();
		CommonTool.refreshNavigatorTree(treeViewer,
				((ICubridNode) objs[0]).getParent());

	}

	/**
	 * Makes this action not support to select multi object
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return true;
	}

	/**
	 * Return whether this action support this object,if not support,this action
	 * will be disabled
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) obj;
			if (node.getType() != CubridNodeType.STATUS_MONITOR_TEMPLATE) {
				return false;
			}
			ServerUserInfo userInfo = node.getServer().getServerInfo().getLoginedUserInfo();
			if (userInfo == null
					|| StatusMonitorAuthType.AUTH_ADMIN != userInfo.getStatusMonitorAuth()) {
				return false;
			}
			return true;
		}
		return false;
	}

}
