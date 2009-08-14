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
package com.cubrid.cubridmanager.ui.common.action;

import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.broker.task.GetBrokerConfParameterTask;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.task.GetCMConfParameterTask;
import com.cubrid.cubridmanager.core.common.task.GetCubridConfParameterTask;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.PreferenceUtil;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to show property of CUBRID node
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class PropertyAction extends
		SelectionAction {
	public static final String ID = PropertyAction.class.getName();
	private boolean isCancel = false;

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public PropertyAction(Shell shell, String text, ImageDescriptor icon) {
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
	public PropertyAction(Shell shell, ISelectionProvider provider,
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
			CubridServer server = node.getServer();
			if (!server.isConnected()) {
				return false;
			}
			ServerUserInfo userInfo = server.getServerInfo().getLoginedUserInfo();
			switch (node.getType()) {
			case SERVER:
			case DATABASE_FOLDER:
			case DATABASE:
				return true;
			case BROKER_FOLDER:
			case BROKER:
				if (userInfo == null
						|| (CasAuthType.AUTH_ADMIN != userInfo.getCasAuth() && CasAuthType.AUTH_MONITOR != userInfo.getCasAuth())) {
					return false;
				}
				return true;
			default:
			}
		}
		return false;
	}

	/**
	 * Open property dialog,view and set property
	 */
	public void run() {
		final Object[] obj = this.getSelectedObj();
		if (!isSupported(obj[0])) {
			return;
		}
		final ICubridNode node = (ICubridNode) obj[0];
		final Shell shell = getShell();
		if (node.getType() == CubridNodeType.SERVER
				|| node.getType() == CubridNodeType.DATABASE_FOLDER
				|| node.getType() == CubridNodeType.DATABASE
				|| node.getType() == CubridNodeType.BROKER_FOLDER
				|| node.getType() == CubridNodeType.BROKER) {
			TaskExecutor taskExcutor = new TaskExecutor() {
				public boolean exec(final IProgressMonitor monitor) {
					isCancel = false;
					Display display = Display.getDefault();
					if (monitor.isCanceled()) {
						isCancel = true;
						return false;
					}
					for (ITask task : taskList) {
						task.execute();
						final String msg = task.getErrorMsg();
						if (msg != null && msg.length() > 0
								&& !monitor.isCanceled()) {
							display.syncExec(new Runnable() {
								public void run() {
									CommonTool.openErrorBox(shell, msg);
								}
							});
							isCancel = true;
							return false;
						}
						if (monitor.isCanceled()) {
							isCancel = true;
							return false;
						}
						if (task instanceof GetCubridConfParameterTask) {
							GetCubridConfParameterTask getCubridConfParameterTask = (GetCubridConfParameterTask) task;
							Map<String, Map<String, String>> confParas = getCubridConfParameterTask.getConfParameters();
							node.getServer().getServerInfo().setCubridConfParaMap(
									confParas);
						}
						if (task instanceof GetBrokerConfParameterTask) {
							GetBrokerConfParameterTask getBrokerConfParameterTask = (GetBrokerConfParameterTask) task;
							Map<String, Map<String, String>> confParas = getBrokerConfParameterTask.getConfParameters();
							node.getServer().getServerInfo().setBrokerConfParaMap(
									confParas);
						}
						if (task instanceof GetCMConfParameterTask) {
							GetCMConfParameterTask getCMConfParameterTask = (GetCMConfParameterTask) task;
							Map<String, String> confParas = getCMConfParameterTask.getConfParameters();
							node.getServer().getServerInfo().setCmConfParaMap(
									confParas);
						}
					}
					return true;
				}
			};
			ServerInfo serverInfo = node.getServer().getServerInfo();
			GetCubridConfParameterTask getCubridConfParameterTask = new GetCubridConfParameterTask(
					serverInfo);
			GetBrokerConfParameterTask getBrokerConfParameterTask = new GetBrokerConfParameterTask(
					serverInfo);
			GetCMConfParameterTask getCMConfParameterTask = new GetCMConfParameterTask(
					serverInfo);
			if (node.getType() == CubridNodeType.SERVER) {
				taskExcutor.addTask(getCubridConfParameterTask);
				taskExcutor.addTask(getBrokerConfParameterTask);
				taskExcutor.addTask(getCMConfParameterTask);
			}
			if (node.getType() == CubridNodeType.DATABASE_FOLDER
					|| node.getType() == CubridNodeType.DATABASE) {
				taskExcutor.addTask(getCubridConfParameterTask);
			}
			if (node.getType() == CubridNodeType.BROKER_FOLDER
					|| node.getType() == CubridNodeType.BROKER) {
				taskExcutor.addTask(getBrokerConfParameterTask);
			}
			new ExecTaskWithProgress(taskExcutor).exec();
		}
		if (!isCancel) {
			Dialog dialog = PreferenceUtil.createPropertyDialog(getShell(),
					node);
			dialog.open();
		}
	}
}
