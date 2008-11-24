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

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Cursor;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.WaitingMsgBox;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cas.view.BrokerList;
import cubridmanager.cubrid.view.DatabaseStatus;
import cubridmanager.ApplicationActionBarAdvisor;

public class StopServerAction extends Action {
	public StopServerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("StopServerAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
	}

	public void run() {
		String WaitMsg = null;
		String Cmds = null;
		String Msgs = null;

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			if (CubridView.Current_db.length() <= 0)
				return;
			AuthItem authrec = MainRegistry
					.Authinfo_find(CubridView.Current_db);
			if (authrec != null && authrec.status != MainConstants.STATUS_START)
				return;
			if (CommonTool
					.WarnYesNo(Messages.getString("WARNYESNO.CUBRIDSTOP")) != SWT.YES)
				return;

			WaitMsg = Messages.getString("WAIT.CUBRIDSTOP");
			authrec.status = MainConstants.STATUS_STOP;
			authrec.setinfo = false;
			Cmds = "stopdb";
			Msgs = "dbname:" + CubridView.Current_db;
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			if (!MainRegistry.IsCASStart)
				return;
			if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.CASSTOP")) != SWT.YES)
				return;
			WaitMsg = Messages.getString("WAIT.CASSTOP");
			Cmds = "stopbroker";
			Msgs = "";
			MainRegistry.AddedBrokers.clear();
			MainRegistry.DeletedBrokers.clear();
		} else
			return;
		ClientSocket cs = new ClientSocket();
		if (cs.Connect()) {
			if (!cs.Send(Application.mainwindow.getShell(), Msgs, Cmds)) {
				CommonTool.ErrorBox(Application.mainwindow.getShell(),
						cs.ErrorMsg);
				MainRegistry.IsConnected = false;
			}
		} else {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			MainRegistry.IsConnected = false;
		}
		WaitingMsgBox dlg = new WaitingMsgBox(Application.mainwindow.getShell());
		dlg.run(WaitMsg);
		if (cs.ErrorMsg != null) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			return;
		}

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			AuthItem authrec = MainRegistry
					.Authinfo_find(CubridView.Current_db);
			authrec.status = MainConstants.STATUS_STOP;
			authrec.setinfo = false;
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			Shell psh = Application.mainwindow.getShell();
			psh.update();
			psh.setEnabled(false);
			psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_WAIT));
			try { // for CAS refresh waiting
				Thread.sleep(2000);
			} catch (Exception e) {
			}
			psh.setEnabled(true);
			psh.setCursor(new Cursor(psh.getDisplay(), SWT.CURSOR_ARROW));
		}

		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			CubridView.myNavi.SelectDB_UpdateView(CubridView.Current_db);
		}

		Application.mainwindow.getShell().update();
		ApplicationActionBarAdvisor.refreshAction.run();
	}
}
