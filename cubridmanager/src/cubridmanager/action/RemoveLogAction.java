/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
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

package cubridmanager.action;

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.view.CASLogs;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBLogs;

public class RemoveLogAction extends Action {
	public RemoveLogAction(String text, String img) {
		super(text);

		// The id is used to refer to the action in a menu or toolbar
		setId("RemoveLogAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		boolean fileCountIsZero = true;
		StringBuffer targetFile = new StringBuffer("open:files\n");
		StringBuffer waitMessage = new StringBuffer("");
		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			ArrayList logFiles = LogFileInfo
					.DBLogInfo_get(CubridView.Current_db);
			LogFileInfo log;
			if (CubridView.Current_select.equals(DBLogs.ID)) {
				byte status = ((AuthItem) MainRegistry
						.Authinfo_find(CubridView.Current_db)).status;
				if (status == MainConstants.STATUS_START) {
					String lastDBLogFile = getLastDBLog(logFiles);
					for (int i = 0; i < logFiles.size(); i++) {
						log = (LogFileInfo) logFiles.get(i);
						if (!log.filename.equals(lastDBLogFile)) {
							targetFile.append("path:");
							targetFile.append(log.path);
							targetFile.append("\n");
							fileCountIsZero = false;
						}
					}
				} else if (status == MainConstants.STATUS_STOP) {
					for (int i = 0; i < logFiles.size(); i++) {
						log = (LogFileInfo) logFiles.get(i);
						targetFile.append("path:");
						targetFile.append(log.path);
						targetFile.append("\n");
						fileCountIsZero = false;
					}
				}
				waitMessage.append(Messages.getString("WAIT.REMOVEALLERRLOG"));
			} else if (CubridView.Current_select.equals(DBLogs.OBJ)) {
				for (int i = 0; i < logFiles.size(); i++) {
					log = (LogFileInfo) logFiles.get(i);
					if (log.filename.equals(DBLogs.Current_select)) {
						targetFile.append("path:");
						targetFile.append(log.path);
						targetFile.append("\n");
						fileCountIsZero = false;
					}
				}
				waitMessage.append(Messages.getString("WAIT.REMOVEERRLOG"));
				waitMessage.append("\n");
				waitMessage.append(DBLogs.Current_select);
			}
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			ArrayList logFiles = LogFileInfo
					.BrokerLog_get(CASView.Current_broker);
			LogFileInfo log;
			if (CASView.Current_select.equals(CASLogs.LOGS_ACCESS)) {
				for (int i = 0; i < logFiles.size(); i++) {
					log = (LogFileInfo) logFiles.get(i);
					if (log.type.equals("access")) {
						targetFile.append("path:");
						targetFile.append(log.path);
						targetFile.append("\n");
						fileCountIsZero = false;
					}
				}
				waitMessage.append(Messages
						.getString("WAIT.REMOVEALLACCESSLOG"));
			} else if (CASView.Current_select.equals(CASLogs.LOGS_ERROR)) {
				for (int i = 0; i < logFiles.size(); i++) {
					log = (LogFileInfo) logFiles.get(i);
					if (log.type.equals("error")) {
						targetFile.append("path:");
						targetFile.append(log.path);
						targetFile.append("\n");
						fileCountIsZero = false;
					}
				}
				waitMessage.append(Messages.getString("WAIT.REMOVEALLERRLOG"));
			} else if (CASView.Current_select.equals(CASLogs.LOGS_SCRIPT)) {
				for (int i = 0; i < logFiles.size(); i++) {
					log = (LogFileInfo) logFiles.get(i);
					if (log.type.equals("script")) {
						targetFile.append("path:");
						targetFile.append(log.path);
						targetFile.append("\n");
						fileCountIsZero = false;
					}
				}
				waitMessage.append(Messages
						.getString("WAIT.REMOVEALLSCRIPTLOG"));
			} else if (CASView.Current_select.equals(CASLogs.LOGSOBJ)) {
				for (int i = 0; i < logFiles.size(); i++) {
					log = (LogFileInfo) logFiles.get(i);
					if (log.type.equals("access") || log.type.equals("error")) {
						if (log.filename.equals(CASLogs.CurrentObj)) {
							targetFile.append("path:");
							targetFile.append(log.path);
							targetFile.append("\n");
							fileCountIsZero = false;
						}
					}
				}
				waitMessage.append(Messages.getString("WAIT.REMOVELOG"));
			} else if (CASView.Current_select.equals(CASLogs.SCRIPTOBJ)) {
				for (int i = 0; i < logFiles.size(); i++) {
					log = (LogFileInfo) logFiles.get(i);
					if (log.type.equals("script")) {
						if (log.filename.equals(CASLogs.CurrentObj)) {
							targetFile.append("path:");
							targetFile.append(log.path);
							targetFile.append("\n");
							fileCountIsZero = false;
						}
					}
				}
				waitMessage.append(Messages.getString("WAIT.REMOVESCRIPTLOG"));
			}
		}
		targetFile.append("close:files\n");

		if (!fileCountIsZero) {
			if (CommonTool.WarnYesNo(Messages.getString("WARN.REMOVELOG")) != SWT.YES)
				return;
			ClientSocket cs = new ClientSocket();
			Shell shell = new Shell();
			if (!cs.SendBackGround(shell, targetFile.toString(), "removelog",
					waitMessage.toString())) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}

			ApplicationActionBarAdvisor.refreshAction.run();
			if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
				CubridView.myNavi.SelectDB_UpdateView(CubridView.Current_db);
			} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
				CASView.myNavi.SelectBroker_UpdateView(CASView.Current_broker);
			}
		}
	}

	private String getLastDBLog(ArrayList logFiles) {
		String lastDBLog = "";
		LogFileInfo currLogFile;
		for (int i = 0; i < logFiles.size(); i++) {
			currLogFile = (LogFileInfo) logFiles.get(i);
			if (lastDBLog.compareTo(currLogFile.filename) < 0) {
				lastDBLog = currLogFile.filename;
			}
		}
		return lastDBLog;
	}
}
