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
package com.cubrid.cubridmanager.ui.spi.model.loader.logs;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.logs.model.BrokerLogInfos;
import com.cubrid.cubridmanager.core.logs.model.LogInfo;
import com.cubrid.cubridmanager.core.logs.model.LogType;
import com.cubrid.cubridmanager.ui.logs.editor.LogEditorPart;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeManager;
import com.cubrid.cubridmanager.ui.spi.Messages;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEventType;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeLoader;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.DefaultCubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNodeLoader;

/**
 * 
 * This class is responsible to load all children of logs broker folder
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridBrokerLogFolderLoader extends
		CubridNodeLoader {

	private static final String ACCESS_LOG_FOLDER_NAME = Messages.msgAccessLogFolderName;
	private static final String ERROR_LOG_FOLDER_NAME = Messages.msgErrorLogFolderName;
	private static final String ADMIN_LOG_FOLDER_NAME = Messages.msgAdminLogFolderName;

	public static final String ACCESS_LOG_FOLDER_ID = "Access log";
	public static final String ERROR_LOG_FOLDER_ID = "Error log";
	public static final String ADMIN_LOG_FOLDER_ID = "Admin log";

	private CommonQueryTask<BrokerLogInfos> loadBrokrLogInfoTask = null;
	private CommonQueryTask<BrokerInfos> loadBrokersTask = null;

	/**
	 * @see ICubridNodeLoader#load(ICubridNode, IProgressMonitor)
	 */
	public synchronized void load(ICubridNode parent,
			final IProgressMonitor monitor) {
		if (isLoaded())
			return;
		// add access and error and admin log folder to broker logs folder
		String[] brokerLogIdArr = { ACCESS_LOG_FOLDER_ID, ERROR_LOG_FOLDER_ID,
				ADMIN_LOG_FOLDER_ID };
		String[] brokerLogNameArr = { ACCESS_LOG_FOLDER_NAME,
				ERROR_LOG_FOLDER_NAME, ADMIN_LOG_FOLDER_NAME };
		CubridNodeType[] brokerLogTypeArr = {
				CubridNodeType.LOGS_BROKER_ACCESS_LOG_FOLDER,
				CubridNodeType.LOGS_BROKER_ERROR_LOG_FOLDER,
				CubridNodeType.LOGS_BROKER_ADMIN_LOG_FOLDER };
		String[] iconArr = { "icons/navigator/folder.png",
				"icons/navigator/folder.png", "icons/navigator/folder.png" };
		ICubridNode[] logFoldrNodeArr = new ICubridNode[3];
		for (int i = 0; i < brokerLogNameArr.length; i++) {
			String id = parent.getId() + NODE_SEPARATOR + brokerLogIdArr[i];
			logFoldrNodeArr[i] = parent.getChild(id);
			if (logFoldrNodeArr[i] == null) {
				logFoldrNodeArr[i] = new DefaultCubridNode(id,
						brokerLogNameArr[i], iconArr[i]);
				logFoldrNodeArr[i].setType(brokerLogTypeArr[i]);
				logFoldrNodeArr[i].setContainer(true);
				if (i == 2) {
					ICubridNodeLoader loader = new CubridAdminLogFolderLoader();
					loader.setLevel(getLevel());
					logFoldrNodeArr[i].setLoader(loader);
					if (getLevel() == DEFINITE_LEVEL) {
						logFoldrNodeArr[i].getChildren(monitor);
					}
				}
				parent.addChild(logFoldrNodeArr[i]);
			} else {
				if (logFoldrNodeArr[i].getLoader() != null) {
					logFoldrNodeArr[i].getLoader().setLoaded(false);
					logFoldrNodeArr[i].getChildren(monitor);
				}
			}
		}

		ServerInfo serverInfo = parent.getServer().getServerInfo();
		BrokerInfos brokerInfos = serverInfo.getBrokerInfos();
		Thread thread = new Thread() {
			public void run() {
				while (!monitor.isCanceled() && !isLoaded()) {
					try {
						sleep(WAIT_TIME);
					} catch (InterruptedException e) {
					}
				}
				if (monitor.isCanceled()) {
					if (loadBrokersTask != null)
						loadBrokersTask.cancel();
					if (loadBrokrLogInfoTask != null)
						loadBrokrLogInfoTask.cancel();
				}
			}
		};
		thread.start();
		if (brokerInfos == null && !monitor.isCanceled()) {
			// load all borkers
			brokerInfos = new BrokerInfos();
			loadBrokersTask = new CommonQueryTask<BrokerInfos>(serverInfo,
					CommonSendMsg.commonSimpleSendMsg, brokerInfos);
			loadBrokersTask.execute();
			final String errorMsg = loadBrokersTask.getErrorMsg();
			if (!monitor.isCanceled() && errorMsg != null
					&& errorMsg.trim().length() > 0) {
				logFoldrNodeArr[0].removeAllChild();
				logFoldrNodeArr[1].removeAllChild();
				Display display = Display.getDefault();
				display.syncExec(new Runnable() {
					public void run() {
						CommonTool.openErrorBox(errorMsg);
					}
				});
				setLoaded(true);
				return;
			}
			brokerInfos = loadBrokersTask.getResultModel();
			serverInfo.setBrokerInfos(brokerInfos);
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		// load broker log file information
		List<BrokerLogInfos> brokerLogInfosList = new ArrayList<BrokerLogInfos>();
		if (brokerInfos != null) {
			BrokerInfoList list = brokerInfos.getBorkerInfoList();
			if (list != null && list.getBrokerInfoList() != null) {
				List<BrokerInfo> brokerInfoList = list.getBrokerInfoList();
				for (int i = 0; !monitor.isCanceled() && brokerInfoList != null
						&& i < brokerInfoList.size(); i++) {
					BrokerInfo brokerInfo = brokerInfoList.get(i);
					BrokerLogInfos brokerLogInfos = new BrokerLogInfos();
					loadBrokrLogInfoTask = new CommonQueryTask<BrokerLogInfos>(
							serverInfo,
							CommonSendMsg.getBrokerLogFileInfoMSGItems,
							brokerLogInfos);
					loadBrokrLogInfoTask.setBroker(brokerInfo.getName());
					loadBrokrLogInfoTask.execute();
					final String errorMsg = loadBrokrLogInfoTask.getErrorMsg();
					if (!monitor.isCanceled() && errorMsg != null
							&& errorMsg.trim().length() > 0) {
						logFoldrNodeArr[0].removeAllChild();
						logFoldrNodeArr[1].removeAllChild();
						Display display = Display.getDefault();
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(errorMsg);
							}
						});
						setLoaded(true);
						return;
					}
					brokerLogInfos = loadBrokrLogInfoTask.getResultModel();
					brokerLogInfosList.add(brokerLogInfos);
				}
			}
		}
		if (monitor.isCanceled()) {
			setLoaded(true);
			return;
		}
		logFoldrNodeArr[0].removeAllChild();
		logFoldrNodeArr[1].removeAllChild();
		for (BrokerLogInfos brokerLogInfos : brokerLogInfosList) {
			List<LogInfo> logInfoList = brokerLogInfos.getBrokerLogInfoList().getLogFileInfoList();
			if (logInfoList != null && logInfoList.size() > 0) {
				for (LogInfo logInfo : logInfoList) {
					ICubridNode logInfoNode = new DefaultCubridNode("",
							logInfo.getName(), "");
					logInfoNode.setContainer(false);
					logInfoNode.setModelObj(logInfo);
					logInfoNode.setEditorId(LogEditorPart.ID);
					if (LogType.ACCESS.getText().toLowerCase().equals(
							logInfo.getType())) {
						String id = logFoldrNodeArr[0].getId() + NODE_SEPARATOR
								+ logInfo.getName();
						logInfoNode.setId(id);
						logInfoNode.setType(CubridNodeType.LOGS_BROKER_ACCESS_LOG);
						logInfoNode.setIconPath("icons/navigator/log_item.png");
						logFoldrNodeArr[0].addChild(logInfoNode);
					} else if (LogType.ERROR.getText().toLowerCase().equals(
							logInfo.getType())) {
						String id = logFoldrNodeArr[1].getId() + NODE_SEPARATOR
								+ logInfo.getName();
						logInfoNode.setId(id);
						logInfoNode.setType(CubridNodeType.LOGS_BROKER_ERROR_LOG);
						logInfoNode.setIconPath("icons/navigator/log_item.png");
						logFoldrNodeArr[1].addChild(logInfoNode);
					}
				}
			}
		}
		Collections.sort(logFoldrNodeArr[0].getChildren());
		Collections.sort(logFoldrNodeArr[1].getChildren());
		setLoaded(true);
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent((ICubridNode) parent,
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent(logFoldrNodeArr[0],
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
		CubridNodeManager.getInstance().fireCubridNodeChanged(
				new CubridNodeChangedEvent(logFoldrNodeArr[1],
						CubridNodeChangedEventType.CONTAINER_NODE_REFRESH));
	}
}
