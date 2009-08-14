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
package com.cubrid.cubridmanager.ui.cubrid.database.control;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.IDialogConstants;
import org.eclipse.jface.wizard.Wizard;
import org.eclipse.jface.wizard.WizardDialog;
import org.eclipse.swt.widgets.Display;

import com.cubrid.cubridmanager.core.common.ITask;
import com.cubrid.cubridmanager.core.common.task.CommonSendMsg;
import com.cubrid.cubridmanager.core.common.task.CommonTaskName;
import com.cubrid.cubridmanager.core.common.task.CommonUpdateTask;
import com.cubrid.cubridmanager.core.cubrid.database.model.UserSendObj;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckDirTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.CheckFileTask;
import com.cubrid.cubridmanager.core.cubrid.database.task.CreateDbTask;
import com.cubrid.cubridmanager.core.cubrid.dbspace.model.GetAutoAddVolumeInfo;
import com.cubrid.cubridmanager.core.cubrid.dbspace.task.SetAutoAddVolumeTask;
import com.cubrid.cubridmanager.core.cubrid.user.task.UpdateAddUserTask;
import com.cubrid.cubridmanager.ui.cubrid.database.dialog.CreateDirDialog;
import com.cubrid.cubridmanager.ui.cubrid.database.dialog.OverrideFileDialog;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.progress.ExecTaskWithProgress;
import com.cubrid.cubridmanager.ui.spi.progress.TaskExecutor;

/**
 * 
 * This wizard is provided for creating database
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CreateDatabaseWizard extends
		Wizard {
	private GeneralInfoPage generalInfoPage = null;
	private VolumeInfoPage volumeInfoPage = null;
	private SetAutoAddVolumeInfoPage setAutoAddVolumeInfoPage = null;
	private DatabaseInfoPage databaseInfoPage = null;
	private SetDbaPasswordPage setDbaPasswordPage = null;

	private CubridServer server = null;
	private boolean isCanFinished = true;
	private boolean isOverride = true;

	/**
	 * The constructor
	 */
	public CreateDatabaseWizard(CubridServer server) {
		setWindowTitle(com.cubrid.cubridmanager.ui.cubrid.database.Messages.titleCreateDbDialog);
		this.server = server;
	}

	@Override
	public void addPages() {
		generalInfoPage = new GeneralInfoPage(server);
		addPage(generalInfoPage);
		volumeInfoPage = new VolumeInfoPage(server);
		addPage(volumeInfoPage);
		setAutoAddVolumeInfoPage = new SetAutoAddVolumeInfoPage();
		addPage(setAutoAddVolumeInfoPage);
		setDbaPasswordPage = new SetDbaPasswordPage();
		addPage(setDbaPasswordPage);
		databaseInfoPage = new DatabaseInfoPage();
		addPage(databaseInfoPage);
		WizardDialog dialog = (WizardDialog) getContainer();
		dialog.addPageChangedListener(volumeInfoPage);
		dialog.addPageChangedListener(setAutoAddVolumeInfoPage);
		dialog.addPageChangedListener(setDbaPasswordPage);
		dialog.addPageChangedListener(databaseInfoPage);
	}

	public boolean canFinish() {
		return getContainer().getCurrentPage() == databaseInfoPage;
	}

	/**
	 * Called when user clicks Finish
	 * 
	 * @return boolean
	 */
	public boolean performFinish() {
		isCanFinished = true;
		isOverride = true;
		TaskExecutor taskExcutor = new TaskExecutor() {
			public boolean exec(final IProgressMonitor monitor) {
				Display display = getShell().getDisplay();
				if (monitor.isCanceled()) {
					return false;
				}
				for (ITask task : taskList) {
					if (task instanceof CreateDbTask) {
						CreateDbTask createDbTask = (CreateDbTask) task;
						createDbTask.setOverwriteConfigFile(isOverride);
					}
					task.execute();
					final String msg = task.getErrorMsg();
					if (monitor.isCanceled()) {
						return false;
					}
					if (msg != null && msg.length() > 0
							&& !monitor.isCanceled()) {
						display.syncExec(new Runnable() {
							public void run() {
								CommonTool.openErrorBox(getShell(), msg);
							}
						});
						return false;
					}
					if (task instanceof CheckDirTask) {
						CheckDirTask checkDirTask = (CheckDirTask) task;
						final String[] dirs = checkDirTask.getNoExistDirectory();
						if (dirs != null && dirs.length > 0) {
							display.syncExec(new Runnable() {
								public void run() {
									CreateDirDialog dialog = new CreateDirDialog(
											getShell());
									dialog.setDirs(dirs);
									if (dialog.open() != IDialogConstants.OK_ID) {
										isCanFinished = false;
									}
								}
							});
						}
					} else if (task instanceof CheckFileTask) {
						CheckFileTask checkFileTask = (CheckFileTask) task;
						final String[] files = checkFileTask.getExistFiles();
						if (files != null && files.length > 0) {
							display.syncExec(new Runnable() {
								public void run() {
									OverrideFileDialog dialog = new OverrideFileDialog(
											getShell());
									dialog.setFiles(files);
									if (dialog.open() != IDialogConstants.OK_ID) {
										isOverride = false;
									}
								}
							});
						}
					}
					if (!isCanFinished) {
						return false;
					}
					if (monitor.isCanceled()) {
						return false;
					}
				}
				return true;
			}
		};
		CheckDirTask checkDirTask = new CheckDirTask(server.getServerInfo());
		CheckFileTask checkFileTask = new CheckFileTask(server.getServerInfo());
		CreateDbTask createDbTask = new CreateDbTask(server.getServerInfo());

		List<String> checkedDirsList = new ArrayList<String>();
		List<String> checkedFilesList = new ArrayList<String>();

		String databaseName = generalInfoPage.getDatabaseName();
		String pageSize = generalInfoPage.getPageSize();
		String generalPageNum = generalInfoPage.getGenericPageNum();
		String generalVolumePath = generalInfoPage.getGenericVolumePath();
		// add checked directory(general volume path)
		addVolumePath(checkedDirsList, generalVolumePath);

		String logPageNum = generalInfoPage.getLogPageNum();
		String logVolumePath = generalInfoPage.getLogVolumePath();
		// add checked directory(log volume path)
		addVolumePath(checkedDirsList, logVolumePath);
		List<Map<String, String>> volumeList = volumeInfoPage.getVolumeList();
		for (int i = 0; i < volumeList.size(); i++) {
			Map<String, String> map = volumeList.get(i);
			String volumeName = map.get("0");
			String volumePath = map.get("3");
			// add checked directory(additional volume path)
			addVolumePath(checkedDirsList, volumePath);
			// add checked file(additional volume)
			addVolumePath(checkedFilesList, volumePath
					+ server.getServerInfo().getPathSeparator() + volumeName);
		}
		String[] dirs = new String[checkedDirsList.size()];
		checkDirTask.setDirectory(checkedDirsList.toArray(dirs));

		String[] files = new String[checkedFilesList.size()];
		checkFileTask.setFile(checkedFilesList.toArray(files));

		createDbTask.setDbName(databaseName);
		createDbTask.setPageSize(pageSize);
		createDbTask.setNumPage(generalPageNum);
		createDbTask.setGeneralVolumePath(generalVolumePath);
		createDbTask.setLogSize(logPageNum);
		createDbTask.setLogVolumePath(logVolumePath);
		createDbTask.setExVolumes(volumeList);

		taskExcutor.addTask(checkDirTask);
		taskExcutor.addTask(checkFileTask);
		taskExcutor.addTask(createDbTask);
		//add set auto added volume
		GetAutoAddVolumeInfo returnInfo = setAutoAddVolumeInfoPage.getAutoAddVolumeInfo();
		if (returnInfo != null) {
			SetAutoAddVolumeTask setTask = new SetAutoAddVolumeTask(
					server.getServerInfo());
			setTask.setDbname(databaseName);
			setTask.setData(returnInfo.getData());
			setTask.setData_warn_outofspace(returnInfo.getData_warn_outofspace());
			setTask.setData_ext_page(returnInfo.getData_ext_page());
			setTask.setIndex(returnInfo.getIndex());
			setTask.setIndex_warn_outofspace(returnInfo.getIndex_warn_outofspace());
			setTask.setIndex_ext_page(returnInfo.getIndex_ext_page());
			taskExcutor.addTask(setTask);
		}
		//start database
		CommonUpdateTask startDbTask = new CommonUpdateTask(
				CommonTaskName.START_DB_TASK_NAME, server.getServerInfo(),
				CommonSendMsg.commonDatabaseSendMsg);
		startDbTask.setDbName(databaseName);
		taskExcutor.addTask(startDbTask);
		//set dba password
		UpdateAddUserTask updateUserTask = new UpdateAddUserTask(
				server.getServerInfo(), false);
		UserSendObj userSendObj = new UserSendObj();
		userSendObj.setDbname(databaseName);
		userSendObj.setUsername("dba");
		String password = setDbaPasswordPage.getPassword();
		userSendObj.setUserpass(password);
		updateUserTask.setUserSendObj(userSendObj);
		taskExcutor.addTask(updateUserTask);

		new ExecTaskWithProgress(taskExcutor).exec(true, true);
		return isCanFinished;
	}

	/**
	 * 
	 * Add volume path to list
	 * 
	 * @param checkedList
	 * @param volumePath
	 */
	private void addVolumePath(List<String> checkedList, String volumePath) {
		boolean isExist = false;
		for (int i = 0; i < checkedList.size(); i++) {
			String volPath = checkedList.get(i);
			if (volumePath.equals(volPath)) {
				isExist = true;
				break;
			}
		}
		if (!isExist) {
			checkedList.add(volumePath);
		}
	}
}
