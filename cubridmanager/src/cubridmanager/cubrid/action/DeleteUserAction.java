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

package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.view.DBUsers;
import cubridmanager.cubrid.view.CubridView;

public class DeleteUserAction extends Action {
	public DeleteUserAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteUserAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (DBUsers.Current_select.length() < 1) {
			return;
		}
		if (DBUsers.Current_select.equals("dba")
				|| DBUsers.Current_select.equals("public")) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
					.getString("ERROR.IDCANNOTBEDELETED"));
			return;
		}
		if (CommonTool.WarnYesNo(DBUsers.Current_select + " "
				+ Messages.getString("WARNYESNO.DELETEUSER")) != SWT.YES) {
			return;
		}
		ClientSocket cs = new ClientSocket();
		String msg = "dbname:" + CubridView.Current_db + "\nusername:"
				+ DBUsers.Current_select;
		if (!cs.SendBackGround(Application.mainwindow.getShell(), msg,
				"deleteuser", Messages.getString("WAIT.DELETE"))) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
			return;
		}
		cs = new ClientSocket();
		if (!cs.SendClientMessage(Application.mainwindow.getShell(), "dbname:"
				+ CubridView.Current_db, "userinfo")) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), cs.ErrorMsg);
		}
		WorkView.DeleteView(DBUsers.ID);
		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();

		ApplicationActionBarAdvisor.refreshAction.run();
		if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID) {
			CubridView.myNavi.SelectDB_UpdateView(CubridView.Current_db);
		} else if (MainRegistry.Current_Navigator == MainConstants.NAVI_CAS) {
			CASView.myNavi.SelectBroker_UpdateView(CASView.Current_broker);
		}
	}
}
