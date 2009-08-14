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
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.broker.model.ApplyServerInfo;
import com.cubrid.cubridmanager.core.broker.model.BrokerStatusInfos;
import com.cubrid.cubridmanager.core.broker.model.JobInfo;
import com.cubrid.cubridmanager.core.broker.task.GetBrokerStatusInfosTask;
import com.cubrid.cubridmanager.core.broker.task.RestartBrokerTask;
import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.broker.Messages;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSetting;
import com.cubrid.cubridmanager.ui.broker.editor.internal.BrokerIntervalSettingManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridViewPart;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.model.CubridBroker;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;
import com.cubrid.cubridmanager.ui.spi.progress.CommonTaskExec;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * A editor part which is responsible for showing the status of all the brokers
 * 
 * BlocksStatusEditor Description
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-18 created by lizhiqiang
 */
public class BrokerStatusView extends
		CubridViewPart {

	private static final Logger logger = LogUtil.getLogger(BrokerStatusView.class);
	public static final String ID = "com.cubrid.cubridmanager.ui.broker.editor.BrokerStatusView";
	private TableViewer asTableViewer;
	private TableViewer jobTableViewer;
	private Composite composite;

	private List<ApplyServerInfo> asinfoLst;
	private List<JobInfo> jobinfoLst;
	private String tblAsId = Messages.tblAsId;
	private String tblAsProcess = Messages.tblAsProcess;
	private String tblAsQps = Messages.tblAsQps;
	private String tblAsLqs = Messages.tblAsLqs;
	private String tblAsDb = Messages.tblAsDb;
	private String tblAsHost = Messages.tblAsHost;
	private String tblAsLct = Messages.tblAsLct;
	private String tblAsSize = Messages.tblAsSize;
	private String tblAsStatus = Messages.tblAsStatus;
	private String tblAsLastAccess = Messages.tblAsLastAccess;
	private String tblAsCur = Messages.tblAsCur;

	private String jobTblTitle = Messages.jobTblTitle;
	private String tblJobId = Messages.tblJobId;
	private String tblJobPriority = Messages.tblJobPriority;
	private String tblJobAddress = Messages.tblJobAddress;
	private String tblJobTime = Messages.tblJobTime;
	private String tblJobRequest = Messages.tblJobRequest;
	private String headTitel = Messages.headTitel;

	private CubridBroker brokerNode;
	private boolean runflag = false;
	private String nodeName;
	private boolean isRunning = true;
	private boolean isFirstLoaded = true;
	private List<ApplyServerInfo> oldAsInfoLst;

	@Override
	public void init(IViewSite site) throws PartInitException {
		super.init(site);
		initValue();
		setPartName(headTitel + nodeName);
		assert (null != brokerNode);
		if (brokerNode.isRunning()) {
			this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker_started.png"));
		} else {
			this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker.png"));
		}
		runflag = brokerNode.isRunning();
	}

	/**
	 * Initializes the parameters of this view
	 */
	public void initValue() {
		if (null == getCubridNode()
				|| getCubridNode().getType() != CubridNodeType.BROKER)
			return;
		brokerNode = (CubridBroker) getCubridNode();
		nodeName = brokerNode.getLabel().trim();

		ServerInfo site = brokerNode.getServer().getServerInfo();
		BrokerStatusInfos brokerStatusInfos = new BrokerStatusInfos();
		final GetBrokerStatusInfosTask<BrokerStatusInfos> task = new GetBrokerStatusInfosTask<BrokerStatusInfos>(
				site, CommonSendMsg.getBrokerStatusItems, brokerStatusInfos);
		task.setBname(nodeName);

		TaskExecutor taskExecutor = new CommonTaskExec();
		taskExecutor.addTask(task);
		new ExecTaskWithProgress(taskExecutor).exec();

		if (!taskExecutor.isSuccess()) {
			return;
		}

		brokerStatusInfos = task.getResultModel();
		if (brokerStatusInfos != null) {
			asinfoLst = brokerStatusInfos.getAsinfo();
			jobinfoLst = brokerStatusInfos.getJobinfo();
		}

	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	public void createPartControl(Composite parent) {
		composite = new Composite(parent, SWT.NONE);
		composite.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		GridLayout layout = new GridLayout();
		composite.setLayout(layout);

		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(composite, CubridManagerHelpContextIDs.brokerStatusView);

		createAsTable(composite);
		createJobTable(composite);
		new StatusUpdate().start();
	}

	/*
	 * This method initializes As table
	 * 
	 */
	private void createAsTable(Composite comp) {
		Composite asComposite = new Composite(comp, SWT.NONE);
		GridData gd_as = new GridData(SWT.FILL, SWT.FILL, true, true);
		asComposite.setLayoutData(gd_as);
		asComposite.setLayout(new GridLayout());
		final Label label = new Label(asComposite, SWT.CENTER);
		label.setText(nodeName);
		asTableViewer = new TableViewer(asComposite, SWT.FULL_SELECTION
				| SWT.BORDER);
		asTableViewer.getTable().setHeaderVisible(true);
		asTableViewer.getTable().setLinesVisible(true);
		asTableViewer.getTable().setLayoutData(gd_as);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(20, 20, true));
		tlayout.addColumnData(new ColumnWeightData(20, 20, true));
		tlayout.addColumnData(new ColumnWeightData(20, 20, true));
		tlayout.addColumnData(new ColumnWeightData(20, 20, true));
		tlayout.addColumnData(new ColumnWeightData(25, 25, true));
		tlayout.addColumnData(new ColumnWeightData(20, 20, true));
		tlayout.addColumnData(new ColumnWeightData(30, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 50, true));
		tlayout.addColumnData(new ColumnWeightData(70, 70, true));
		tlayout.addColumnData(new ColumnWeightData(70, 70, true));
		tlayout.addColumnData(new ColumnWeightData(150, 150, true));
		asTableViewer.getTable().setLayout(tlayout);

		TableColumn tblColumn = new TableColumn(asTableViewer.getTable(),
				SWT.CENTER);
		tblColumn.setText(tblAsId);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsProcess);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsQps);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsLqs);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsSize);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsStatus);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsDb);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsHost);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsLastAccess);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsLct);
		tblColumn = new TableColumn(asTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblAsCur);

		asTableViewer.setContentProvider(new ApplyServerContentProvider());
		asTableViewer.setLabelProvider(new ApplyServerLabelProvider());
		asTableViewer.setInput(asinfoLst);

		MenuManager menuManager = new MenuManager();
		menuManager.setRemoveAllWhenShown(true);
		menuManager.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				IStructuredSelection selection = (IStructuredSelection) asTableViewer.getSelection();
				ApplyServerInfo as = (ApplyServerInfo) (selection.toArray()[0]);
				RestartAction restartAcion = new RestartAction(as.getAs_id());
				manager.add(restartAcion);
			}
		});
		Menu contextMenu = menuManager.createContextMenu(asTableViewer.getControl());
		asTableViewer.getControl().setMenu(contextMenu);

	}

	/*
	 * * This method initializes Job table
	 *
	 */
	private void createJobTable(Composite comp) {
		Composite jobComposite = new Composite(comp, SWT.NONE);
		GridData gd_job = new GridData(SWT.FILL, SWT.FILL, true, true);
		jobComposite.setLayoutData(gd_job);
		jobComposite.setLayout(new GridLayout());

		final Label label = new Label(jobComposite, SWT.CENTER);
		label.setText(jobTblTitle);
		jobTableViewer = new TableViewer(jobComposite, SWT.FULL_SELECTION
				| SWT.BORDER);
		jobTableViewer.getTable().setHeaderVisible(true);
		jobTableViewer.getTable().setLinesVisible(true);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		tlayout.addColumnData(new ColumnWeightData(10, 50, true));
		jobTableViewer.getTable().setLayout(tlayout);
		jobTableViewer.getTable().setLayoutData(gd_job);

		TableColumn tblColumn = new TableColumn(jobTableViewer.getTable(),
				SWT.CENTER);
		tblColumn.setText(tblJobId);
		tblColumn = new TableColumn(jobTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblJobPriority);
		tblColumn = new TableColumn(jobTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblJobAddress);
		tblColumn = new TableColumn(jobTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblJobTime);
		tblColumn = new TableColumn(jobTableViewer.getTable(), SWT.CENTER);
		tblColumn.setText(tblJobRequest);

		jobTableViewer.setContentProvider(new JobContentProvider());
		jobTableViewer.setLabelProvider(new JobLabelProvider());
		jobTableViewer.setInput(jobinfoLst);
	}

	/**
	 * An action that is an inner class in order to execute restarting a certain
	 * server
	 * 
	 * @author lizhiqiang
	 * @version 1.0 - 2009-5-22 created by lizhiqiang
	 */
	private class RestartAction extends
			Action {
		private String server;

		public RestartAction(String server) {
			setText(Messages.bind(Messages.restartBrokerServerTip, server));
			this.server = server;
		}

		public void run() {
			if (!CommonTool.openConfirmBox(Messages.bind(
					Messages.restartBrokerServerMsg, server))) {
				return;
			}
			ServerInfo site = brokerNode.getServer().getServerInfo();
			RestartBrokerTask task = new RestartBrokerTask(site);
			task.setBname(brokerNode.getLabel());
			task.setAsnum(server);

			TaskExecutor taskExecutor = new CommonTaskExec();
			taskExecutor.addTask(task);
			new ExecTaskWithProgress(taskExecutor).exec();

			if (!taskExecutor.isSuccess()) {
				return;
			}

			initValue();

			asTableViewer.refresh();
			jobTableViewer.refresh();

		}
	}

	/**
	 * Refresh this view
	 * 
	 */
	public void refresh() {
		ServerInfo site = brokerNode.getServer().getServerInfo();
		BrokerStatusInfos brokerStatusInfos = new BrokerStatusInfos();
		final GetBrokerStatusInfosTask<BrokerStatusInfos> task = new GetBrokerStatusInfosTask<BrokerStatusInfos>(
				site, CommonSendMsg.getBrokerStatusItems, brokerStatusInfos);
		task.setBname(nodeName);
		task.execute();

		if (!task.isSuccess()) {
			return;
		}

		brokerStatusInfos = task.getResultModel();

		asinfoLst = brokerStatusInfos.getAsinfo();
		jobinfoLst = brokerStatusInfos.getJobinfo();

		asTableViewer.setInput(asinfoLst);
		asTableViewer.refresh();
		jobTableViewer.setInput(jobinfoLst);
		jobTableViewer.refresh();
	}

	public void refresh(boolean isUpdateTable, boolean isRefreshChanged) {
		ServerInfo site = brokerNode.getServer().getServerInfo();
		BrokerStatusInfos brokerStatusInfos = new BrokerStatusInfos();
		final GetBrokerStatusInfosTask<BrokerStatusInfos> task = new GetBrokerStatusInfosTask<BrokerStatusInfos>(
				site, CommonSendMsg.getBrokerStatusItems, brokerStatusInfos);
		task.setBname(nodeName);
		task.execute();

		if (!task.isSuccess()) {
			return;
		}
		brokerStatusInfos = task.getResultModel();

		List<ApplyServerInfo> newAsInfoLst = null;
		if (null != brokerStatusInfos) {
			newAsInfoLst = brokerStatusInfos.getAsinfo();
		}
		List<ApplyServerInfo> changedAsInfoLst = new ArrayList<ApplyServerInfo>();
		for (int i = 0; newAsInfoLst != null && i < newAsInfoLst.size(); i++) {
			ApplyServerInfo newAsInfo = newAsInfoLst.get(i);
			ApplyServerInfo changedAsInfo = newAsInfo.clone();
			for (int j = 0; oldAsInfoLst != null && j < oldAsInfoLst.size(); j++) {
				ApplyServerInfo oldAsInfo = oldAsInfoLst.get(j);
				if (newAsInfo.getAs_id().equalsIgnoreCase(oldAsInfo.getAs_id())) {
					long newQuery = StringUtil.intValue(newAsInfo.getAs_num_query());
					long newTran = StringUtil.intValue(newAsInfo.getAs_num_tran());
					long newLongQuery = StringUtil.longValue(newAsInfo.getAs_long_query());
					long newLongTran = StringUtil.longValue(newAsInfo.getAs_long_tran());
					long newErrQuery = StringUtil.intValue(newAsInfo.getAs_error_query());

					long oldQuery = StringUtil.intValue(oldAsInfo.getAs_num_query());
					long oldTran = StringUtil.intValue(oldAsInfo.getAs_num_tran());
					long oldLongQuery = StringUtil.longValue(oldAsInfo.getAs_long_query());
					long oldLongTran = StringUtil.longValue(oldAsInfo.getAs_long_tran());
					long oldErrQuery = StringUtil.intValue(oldAsInfo.getAs_error_query());

					long changedQuery = newQuery - oldQuery;
					long changedTran = newTran - oldTran;
					long changedLongTran = newLongTran - oldLongTran;
					long changedLongQuery = newLongQuery - oldLongQuery;
					long changedErrQuery = newErrQuery - oldErrQuery;

					changedAsInfo.setAs_num_query(String.valueOf(changedQuery > 0 ? changedQuery
							: 0));
					changedAsInfo.setAs_num_tran(String.valueOf(changedTran > 0 ? changedTran
							: 0));
					changedAsInfo.setAs_long_tran(String.valueOf(changedLongTran > 0 ? changedLongTran
							: 0));
					changedAsInfo.setAs_long_query(String.valueOf(changedLongQuery > 0 ? changedLongQuery
							: 0));
					changedAsInfo.setAs_error_query(String.valueOf(changedErrQuery > 0 ? changedErrQuery
							: 0));
					break;
				}
			}
			changedAsInfoLst.add(changedAsInfo);
		}
		oldAsInfoLst = newAsInfoLst;
		if (isUpdateTable) {
			if (isRefreshChanged) {
				asTableViewer.setInput(changedAsInfoLst);
			} else {
				asTableViewer.setInput(oldAsInfoLst);
			}
		}
		asTableViewer.refresh();

		jobinfoLst = brokerStatusInfos.getJobinfo();
		jobTableViewer.setInput(jobinfoLst);
		jobTableViewer.refresh();
	}

	/**
	 * Response to node changes
	 */
	public void nodeChanged(CubridNodeChangedEvent e) {
		ICubridNode eventNode = e.getCubridNode();
		if (eventNode == null || brokerNode == null) {
			return;
		}
		//if it is not in the same host,return
		if (!eventNode.getServer().getId().equals(
				brokerNode.getServer().getId())) {
			return;
		}
		CubridNodeType type = eventNode.getType();
		if (type != CubridNodeType.BROKER_FOLDER) {
			return;
		}
		String id = brokerNode.getId();
		CubridBroker currentNode = (CubridBroker) eventNode.getChild(id);
		this.brokerNode = currentNode;
		if (currentNode == null || !currentNode.isRunning()) {
			setRunflag(false);
			this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker.png"));
			if (asTableViewer != null && asTableViewer.getTable() != null
					&& !asTableViewer.getTable().isDisposed())
				asTableViewer.getTable().removeAll();
			if (jobTableViewer != null && jobTableViewer.getTable() != null
					&& !jobTableViewer.getTable().isDisposed()) {
				jobTableViewer.getTable().removeAll();
			}
		} else {
			setRunflag(true);
			this.setTitleImage(CubridManagerUIPlugin.getImage("icons/navigator/broker_started.png"));
			refresh(true, false);
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
				String serverName = brokerNode.getServer().getLabel();
				BrokerIntervalSetting brokerIntervalSetting = BrokerIntervalSettingManager.getInstance().getBrokerIntervalSetting(
						serverName, brokerNode.getLabel());
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
						logger.error(e.getMessage());
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
		isRunning = false;
		runflag = false;
		asTableViewer = null;
		super.dispose();
	}

	@Override
	public void setFocus() {

	}
}
