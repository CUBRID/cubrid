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
import org.eclipse.swt.widgets.MessageBox;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.PROPAGE_ProtegoAPIDManagementDialog;

public class ProtegoAPIDDeleteAction extends Action {
	public ProtegoAPIDDeleteAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoMTUserAddAction");
	}

	public void run() {
		ArrayList usrInfo = null;
		usrInfo = PROPAGE_ProtegoAPIDManagementDialog.getSelectedUserInfo();
		if ((usrInfo == null) || (usrInfo.size() == 0))
			return;

		try {
			MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
					SWT.ICON_QUESTION | SWT.YES | SWT.NO);
			mb.setText(Messages.getString("TITLE.REMOVEAPID"));
			mb.setMessage(Messages.getString("TEXT.CONFIRMAPIDREMOVE"));
			int state = mb.open();
			if (state == SWT.YES) {
				for (int i = 0; i < usrInfo.size(); i++) {
					UpaClient.admAppCmd(ProtegoUserManagementAction.dlg.upaKey,
							UpaClient.UPA_USER_APID_DEL, (UpaUserInfo) usrInfo
									.get(i));
				}
			}
		} catch (UpaException ee) {
			CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
					.getString("TEXT.FAILEDREMOVEAPID"));
			return;
		}

		PROPAGE_ProtegoAPIDManagementDialog.refresh();
	}
}
