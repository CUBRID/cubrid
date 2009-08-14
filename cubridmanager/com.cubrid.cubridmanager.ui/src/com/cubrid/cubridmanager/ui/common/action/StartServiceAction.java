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

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.broker.task.StartBrokerEnvTask;
import com.cubrid.cubridmanager.core.broker.task.StartBrokerTask;
import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.model.ConfConstants;
import com.cubrid.cubridmanager.core.common.model.OnOffType;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.model.ServerType;
import com.cubrid.cubridmanager.core.common.model.ServerUserInfo;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.common.task.GetCubridConfParameterTask;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This action is responsible to start service
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-6 created by pangqiren
 */
public class StartServiceAction extends
		SelectionAction {

	public static final String ID = StartServiceAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public StartServiceAction(Shell shell, String text,
			ImageDescriptor enabledIcon, ImageDescriptor disabledIcon) {
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
	public StartServiceAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor enabledIcon,
			ImageDescriptor disabledIcon) {
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
		if (obj instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) obj;
			CubridServer server = node.getServer();
			ServerInfo serverInfo = server.getServerInfo();
			ServerType serverType = serverInfo != null ? serverInfo.getServerType()
					: null;
			if (serverType == null || serverType == ServerType.BROKER) {
				return false;
			}
			if (server != null && server.isConnected() && serverInfo != null) {
				ServerUserInfo userInfo = serverInfo.getLoginedUserInfo();
				if (userInfo != null && userInfo.isAdmin()) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Open start service dialog and set configuration information
	 */
	public void run() {
		Object[] obj = this.getSelectedObj();
		if (obj == null || obj.length <= 0 || !isSupported(obj[0])) {
			setEnabled(false);
			return;
		}
		ISelectionProvider provider = this.getSelectionProvider();
		final TreeViewer viewer = (TreeViewer) provider;
		ICubridNode node = (ICubridNode) obj[0];
		final CubridServer server = node.getServer();
		final Shell shell = getShell();
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = Display.getDefault();
				if (monitor.isCanceled()) {
					return false;
				}
				List<ITask> otherTaskList = new ArrayList<ITask>();
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
						return false;
					}
					if (monitor.isCanceled()) {
						return false;
					}
					if (task instanceof GetCubridConfParameterTask) {
						GetCubridConfParameterTask getCubridConfParameterTask = (GetCubridConfParameterTask) task;
						Map<String, Map<String, String>> confParas = getCubridConfParameterTask.getConfParameters();
						String services = "";
						String servers = "";
						Map<String, String> map = confParas.get(ConfConstants.service_section_name);
						if (map != null) {
							services = map.get(ConfConstants.service);
							servers = map.get(ConfConstants.server);
						}
						if (services != null && services.indexOf("server") >= 0) {
							if (servers != null && servers.length() > 0) {
								String[] databases = servers.split(",");
								for (int i = 0; i < databases.length; i++) {
									CommonUpdateTask commonTask = new CommonUpdateTask(
											CommonTaskName.START_DB_TASK_NAME,
											server.getServerInfo(),
											CommonSendMsg.commonDatabaseSendMsg);
									commonTask.setDbName(databases[i]);
									otherTaskList.add(commonTask);
								}
							}
						}
						if (services != null && services.indexOf("broker") >= 0) {
							BrokerInfos brokerInfos = server.getServerInfo().getBrokerInfos();
							if (brokerInfos != null
									&& OnOffType.ON.getText().equalsIgnoreCase(
											brokerInfos.getBrokerstatus())) {
								BrokerInfoList brokerInfoList = brokerInfos.getBorkerInfoList();
								if (brokerInfoList != null) {
									List<BrokerInfo> list = brokerInfoList.getBrokerInfoList();
									for (int i = 0; list != null
											&& i < list.size(); i++) {
										BrokerInfo brokerInfo = list.get(i);
										if (OnOffType.OFF.getText().equalsIgnoreCase(
												brokerInfo.getState())) {
											StartBrokerTask startBrokerTask = new StartBrokerTask(
													server.getServerInfo());
											startBrokerTask.setBname(brokerInfo.getName());
											otherTaskList.add(startBrokerTask);
										}
									}
								}
							} else {
								StartBrokerEnvTask startBrokerEnvTask = new StartBrokerEnvTask(
										server.getServerInfo());
								otherTaskList.add(startBrokerEnvTask);
							}
						}
					}
				}
				for (ITask task : otherTaskList) {
					task.execute();
					final String msg = task.getErrorMsg();
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(shell, msg);
							}
						});
					}
					if (monitor.isCanceled()) {
						return false;
					}
				}
				if (otherTaskList.size() > 0) {
					display.syncExec(new Runnable() {
						public void run() {
							CommonTool.refreshNavigatorTree(viewer, server);
						}
					});
				}
				return true;
			}
		};
		ServerInfo serverInfo = server.getServerInfo();
		GetCubridConfParameterTask task = new GetCubridConfParameterTask(
				serverInfo);
		taskExcutor.addTask(task);
		new ExecTaskWithProgress(taskExcutor).exec();
	}
}
