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
package com.cubrid.cubridmanager.ui.broker.editor;

import java.util.ArrayList;
import java.util.List;

import org.apache.log4j.Logger;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.MouseAdapter;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.broker.model.BrokerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfoList;
import com.cubrid.cubridmanager.core.broker.model.BrokerInfos;
import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.task.CommonQueryTask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.broker.Messages;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSetting;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSettingManager;
import com.cubrid.cubridmanager.ui.spi.CubridViewPart;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.model.CubridBrokerFolder;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * A editor part which is responsible for showing the status of all the brokers
 * 
 * BlocksStatusEditor Description
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-18 created by lizhiqiang
 */
public class BrokerEnvStatusView extends
		CubridViewPart {

	private static final Logger logger = LogUtil.getLogger(BrokerEnvStatusView.class);
	public static final String ID = "com.cubrid.cubridmanager.ui.broker.editor.BrokerEnvStatusView";

	private TableViewer tableViewer;
	private Composite composite;

	private String tblBrokerName = Messages.tblBrokerName;
	private String tblBrokerStatus = Messages.tblBrokerStatus;
	private String tblBrokerProcess = Messages.tblBrokerProcess;
	private String tblPort = Messages.tblPort;
	private String tblServer = Messages.tblServer;
	private String tblQueue = Messages.tblQueue;
	private String tblLongTran = Messages.tblLongTran;
	private String tblLongQuery = Messages.tblLongQuery;
	private String tblErrQuery = Messages.tblErrQuery;
	private String tblRequest = Messages.tblRequest;
	private String tblTps = Messages.tblTps;
	private String tblQps = Messages.tblQps;
	private String headTitel = Messages.envHeadTitel;
	private boolean runflag = false;
	private boolean isRunning = true;
	private List<BrokerInfo> oldBrokerInfoList;
	private boolean isFirstLoaded = true;

	public void init(IViewSite site) throws PartInitException {
		super.init(site);
		setPartName(headTitel);
		if (null != cubridNode
				&& cubridNode.getType() == CubridNodeType.BROKER_FOLDER) {
			CubridBrokerFolder brokerFolderNode = (CubridBrokerFolder) cubridNode;
			if (brokerFolderNode != null && brokerFolderNode.isRunning()) {
				this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker_service_started.png"));
			} else {
				this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker_group.png"));
			}
			runflag = brokerFolderNode.isRunning();
		}
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	public void createPartControl(Composite parent) {
		FillLayout flayout = new FillLayout();
		composite = new Composite(parent, SWT.NONE);
		composite.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite.setLayout(flayout);

		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(composite, CubridManagerHelpContextIDs.brokerEnvStatusView);

		createTable();
		new StatusUpdate().start();
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		tableViewer = new TableViewer(composite, SWT.FULL_SELECTION);
		tableViewer.getTable().setHeaderVisible(true);
		tableViewer.getTable().setLinesVisible(true);

		TableLayout tlayout = new TableLayout();
		for (int i = 0; i < 12; i++) {
			tlayout.addColumnData(new ColumnWeightData(10, 40, true));
		}
		tableViewer.getTable().setLayout(tlayout);
		tableViewer.getTable().addMouseListener(new MouseAdapter() {
			public void mouseDoubleClick(MouseEvent e) {
				int index = -1;
				if ((index = tableViewer.getTable().getSelectionIndex()) >= 0) {
					TableItem tableItem = tableViewer.getTable().getItem(index);
					String brokename = tableItem.getText(0).trim();
					ICubridNode input = null;
					for (ICubridNode node : cubridNode.getChildren()) {
						if (node.getLabel().equalsIgnoreCase(brokename)) {
							input = node;
							break;
						}
					}
					LayoutManager.getInstance().setCurrentSelectedNode(input);
					IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
					if (null == window) {
						return;
					}
					IWorkbenchPage activePage = window.getActivePage();
					IViewPart viewPart = window.getActivePage().findView(
							BrokerStatusView.ID);
					if (null != viewPart) {
						activePage.hideView(viewPart);
					}
					try {
						activePage.showView(BrokerStatusView.ID);
					} catch (PartInitException e1) {
						logger.error(e1.getMessage(), e1);
					}

				}
			}
		});

		TableColumn tblColumn = new TableColumn(tableViewer.getTable(),
				SWT.LEFT);
		tblColumn.setText(tblBrokerName);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblBrokerStatus);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblBrokerProcess);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblPort);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblServer);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblQueue);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblRequest);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblTps);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblQps);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblLongTran);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblLongQuery);
		tblColumn = new TableColumn(tableViewer.getTable(), SWT.LEFT);
		tblColumn.setText(tblErrQuery);

		tableViewer.setContentProvider(new BrokersStatusContentProvider());
		tableViewer.setLabelProvider(new BrokersStatusLabelProvider());
	}

	@Override
	public void setFocus() {

	}

	/**
	 * Response to cubrid node changes
	 */
	public void nodeChanged(CubridNodeChangedEvent e) {
		ICubridNode eventNode = e.getCubridNode();
		if (eventNode == null || this.cubridNode == null) {
			return;
		}
		//if it is not in the same host,return
		if (!eventNode.getServer().getId().equals(
				this.cubridNode.getServer().getId())) {
			return;
		}
		//if changed node is not broker folder or server,return
		CubridNodeType type = eventNode.getType();
		if (type != CubridNodeType.BROKER_FOLDER
				&& type != CubridNodeType.SERVER) {
			return;
		}
		if (type == CubridNodeType.SERVER) {
			String id = eventNode.getId();
			CubridBrokerFolder currentNode = (CubridBrokerFolder) eventNode.getChild(id);
			this.cubridNode = currentNode;
		}
		if (this.cubridNode == null
				|| !((CubridBrokerFolder) eventNode).isRunning()) {
			setRunflag(false);
			this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker_group.png"));
		} else {
			setRunflag(true);
			this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker_service_started.png"));
		}
		refresh(true, false);
	}

	/**
	 * Refreshes this view
	 * 
	 */
	public void refresh(boolean isUpdateTable, boolean isRefreshChanged) {
		ServerInfo site = cubridNode.getServer().getServerInfo();
		BrokerInfos brokerInfos = new BrokerInfos();
		final CommonQueryTask<BrokerInfos> task = new CommonQueryTask<BrokerInfos>(
				site, CommonSendMsg.commonSimpleSendMsg, brokerInfos);
		task.execute();
		brokerInfos = task.getResultModel();
		List<BrokerInfo> newBrokerInfoList = null;
		if (null != brokerInfos) {
			BrokerInfoList list = brokerInfos.getBorkerInfoList();
			if (list != null && list.getBrokerInfoList() != null) {
				newBrokerInfoList = list.getBrokerInfoList();
			}
		}
		List<BrokerInfo> changedBrokerInfoList = new ArrayList<BrokerInfo>();
		for (int i = 0; newBrokerInfoList != null
				&& i < newBrokerInfoList.size(); i++) {
			BrokerInfo newBrokerInfo = newBrokerInfoList.get(i);
			BrokerInfo changedBrokerInfo = newBrokerInfo.clone();
			for (int j = 0; oldBrokerInfoList != null
					&& j < oldBrokerInfoList.size(); j++) {
				BrokerInfo oldBrokerInfo = oldBrokerInfoList.get(j);
				if (newBrokerInfo.getName().equalsIgnoreCase(
						oldBrokerInfo.getName())) {
					long newTran = StringUtil.intValue(newBrokerInfo.getTran());
					long newQuery = StringUtil.intValue(newBrokerInfo.getQuery());
					long newLongTran = StringUtil.longValue(newBrokerInfo.getLong_tran());
					long newLongQuery = StringUtil.longValue(newBrokerInfo.getLong_query());
					long newErrQuery = StringUtil.intValue(newBrokerInfo.getError_query());

					long oldTran = StringUtil.intValue(oldBrokerInfo.getTran());
					long oldQuery = StringUtil.intValue(oldBrokerInfo.getQuery());
					long oldLongTran = StringUtil.longValue(oldBrokerInfo.getLong_tran());
					long oldLongQuery = StringUtil.longValue(oldBrokerInfo.getLong_query());
					long oldErrQuery = StringUtil.intValue(oldBrokerInfo.getError_query());

					long changedTran = newTran - oldTran;
					long changedQuery = newQuery - oldQuery;
					long changedLongTran = newLongTran - oldLongTran;
					long changedLongQuery = newLongQuery - oldLongQuery;
					long changedErrQuery = newErrQuery - oldErrQuery;

					changedBrokerInfo.setTran(String.valueOf(changedTran > 0 ? changedTran
							: 0));
					changedBrokerInfo.setQuery(String.valueOf(changedQuery > 0 ? changedQuery
							: 0));
					changedBrokerInfo.setLong_tran(String.valueOf(changedLongTran > 0 ? changedLongTran
							: 0));
					changedBrokerInfo.setLong_query(String.valueOf(changedLongQuery > 0 ? changedLongQuery
							: 0));
					changedBrokerInfo.setError_query(String.valueOf(changedErrQuery > 0 ? changedErrQuery
							: 0));
					break;
				}
			}
			changedBrokerInfoList.add(changedBrokerInfo);
		}
		oldBrokerInfoList = newBrokerInfoList;
		if (isUpdateTable) {
			if (isRefreshChanged)
				tableViewer.setInput(changedBrokerInfoList);
			else
				tableViewer.setInput(oldBrokerInfoList);
			tableViewer.refresh();
		}
	}

	/**
	 * A inner class which extends the Thread and calls the refresh method
	 * 
	 * @author lizhiqiang
	 * @version 1.0 - 2009-5-30 created by lizhiqiang
	 */
	private class StatusUpdate extends
			Thread {
		public void run() {
			int count = 0;
			while (isRunning) {
				String serverName = cubridNode.getServer().getLabel();
				BrokerIntervalSetting brokerIntervalSetting = BrokerIntervalSettingManager.getInstance().getBrokerIntervalSetting(
						serverName, cubridNode.getLabel());
				final int term = Integer.parseInt(brokerIntervalSetting.getInterval());
				final int timeCount = count;
				if (getRunflag() && brokerIntervalSetting.isOn() && term > 0) {
					isFirstLoaded = false;
					Display.getDefault().asyncExec(new Runnable() {
						public void run() {
							if (composite != null && !composite.isDisposed()) {
								refresh(timeCount % term == 0, true);
							}
						}
					});

					try {
						if (count % term == 0) {
							count = 0;
						}
						count++;
						Thread.sleep(1000);
					} catch (Exception e) {
						logger.error(e.getMessage());
					}
				} else {
					if (isFirstLoaded) {
						isFirstLoaded = false;
						Display.getDefault().asyncExec(new Runnable() {
							public void run() {
								if (composite != null
										&& !composite.isDisposed()) {
									refresh(true, false);
								}
							}
						});

					}
					try {
						Thread.sleep(1000);
					} catch (Exception e) {
						logger.error(e.getMessage(), e);
					}
				}
			}

		}

	}

	/*
	 * Gets the value of runflag
	 * 
	 * @return
	 */
	private synchronized boolean getRunflag() {
		return runflag;
	}

	/*
	 * Sets the value of runflag
	 * 
	 * @return
	 */
	private synchronized void setRunflag(boolean runflag) {
		this.runflag = runflag;
	}

	@Override
	public void dispose() {
		runflag = false;
		isRunning = false;
		tableViewer = null;
		super.dispose();
	}

}
