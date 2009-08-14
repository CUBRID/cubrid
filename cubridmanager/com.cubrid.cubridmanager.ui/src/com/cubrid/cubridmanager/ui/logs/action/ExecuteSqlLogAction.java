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

import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.CasAuthType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.logs.model.GetExecuteCasRunnerResultInfo;
import com.cubrid.cubridmanager.core.logs.model.LogInfo;
import com.cubrid.cubridmanager.core.logs.task.GetExecuteCasRunnerResultTask;
import com.cubrid.cubridmanager.ui.logs.dialog.CasRunnerConfigDialog;
import com.cubrid.cubridmanager.ui.logs.dialog.CasRunnerResultDialog;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to execute sql log.
 * 
 * @author wuyingshi
 * @version 1.0 - 2009-3-10 created by wuyingshi
 */
public class ExecuteSqlLogAction extends
		SelectionAction {

	public static final String ID = ExecuteSqlLogAction.class.getName();
	private boolean isCancel = false;

	/**
	 * The Constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public ExecuteSqlLogAction(Shell shell, String text,
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
	public ExecuteSqlLogAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId("com.cubrid.cubridmanager.ui.logs.action.ExecuteSqlLogAction");
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
			if (serverUserInfo == null
					|| serverUserInfo.getCasAuth() != CasAuthType.AUTH_ADMIN) {
				return false;
			}
			if (node.getType() == CubridNodeType.BROKER_SQL_LOG) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Open dialog
	 */
	public void run() {

		final Display display = getShell().getDisplay();
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				isCancel = false;
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
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						isCancel = true;
						return false;
					}
					if (monitor.isCanceled()) {
						isCancel = true;
						return false;
					}
				}
				return true;
			}
		};

		Object[] obj = this.getSelectedObj();
		DefaultCubridNode node = null;
		LogInfo logInfo = null;
		node = (DefaultCubridNode) obj[0];
		String targetBroker = node.getParent().getParent().getLabel();
		logInfo = (LogInfo) node.getAdapter(LogInfo.class);
		List<String> allDatabaseList = node.getServer().getServerInfo().getAllDatabaseList();
		BrokerInfos brokerInfos = node.getServer().getServerInfo().getBrokerInfos();

		CasRunnerConfigDialog casRunnerConfigDialog = new CasRunnerConfigDialog(
				getShell());
		casRunnerConfigDialog.setBrokerInfos(brokerInfos);
		casRunnerConfigDialog.setAllDatabaseList(allDatabaseList);
		casRunnerConfigDialog.setLogInfo(logInfo);
		casRunnerConfigDialog.setTargetBroker(targetBroker);
		casRunnerConfigDialog.setExecwithFile(true);
		casRunnerConfigDialog.create();

		if (casRunnerConfigDialog.open() == Dialog.OK) {
			final GetExecuteCasRunnerResultTask task = new GetExecuteCasRunnerResultTask(
					((DefaultCubridNode) obj[0]).getServer().getServerInfo());
			task.setBrokerName(CasRunnerConfigDialog.getBrokerName());
			task.setUserName(CasRunnerConfigDialog.getUserName());
			task.setPasswd(CasRunnerConfigDialog.getPassword());
			task.setNumThread(CasRunnerConfigDialog.getNumThread());
			task.setRepeatCount(casRunnerConfigDialog.getNumRepeatCount());
			String isShowqueryresult = "";
			if (casRunnerConfigDialog.isShowqueryresult()) {
				isShowqueryresult = "yes";
			} else {
				isShowqueryresult = "no";
			}
			task.setShowQueryResult(isShowqueryresult);
			String isShowqueryplan = "";
			if (casRunnerConfigDialog.isShowqueryplan()) {
				isShowqueryplan = "yes";
			} else {
				isShowqueryplan = "no";
			}
			task.setShowQueryResult(isShowqueryplan);
			task.setDbName(CasRunnerConfigDialog.getDbname());
			task.setExecuteLogFile("yes");
			task.setLogFile(logInfo.getPath());
			taskExcutor.addTask(task);
			new ExecTaskWithProgress(taskExcutor).exec();
			if (isCancel) {
				return;
			}
			GetExecuteCasRunnerResultInfo getExecuteCasRunnerResultInfo = (GetExecuteCasRunnerResultInfo) task.getContent();
			StringBuffer result = new StringBuffer("");
			for (int i = 0, n = getExecuteCasRunnerResultInfo.getResult().size(); i < n; i++) {
				result.append(getExecuteCasRunnerResultInfo.getResult().get(i)
						+ "\n");
			}
			CasRunnerResultDialog casRunnerResultDialog = new CasRunnerResultDialog(
					getShell());
			casRunnerResultDialog.setResult(result);
			if (casRunnerResultDialog.open() == Dialog.OK) {
			}
		}
	}

}
