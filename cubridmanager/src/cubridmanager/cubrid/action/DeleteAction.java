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
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TreeItem;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.ProtegoReadCert;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.DBA_CONFIRMDialog;
import cubridmanager.cubrid.dialog.DELETEDB_CONFIRMDialog;
import cubridmanager.cubrid.view.CubridView;

public class DeleteAction extends Action {
	public static AuthItem ai = null;

	public static boolean deleteBackup = false;

	public DeleteAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DeleteAction");
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
		Shell shell = new Shell();
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		if (ai.status != MainConstants.STATUS_STOP) {
			CommonTool.ErrorBox(shell, Messages.getString("ERROR.RUNNINGDATABASE"));
			return;
		}
		ClientSocket cs = new ClientSocket();

		if (cs.SendBackGround(shell, "dbname:" + CubridView.Current_db,
				"dbspaceinfo", Messages.getString("WAITING.GETTINGVOLUMEINFO"))) {
			DELETEDB_CONFIRMDialog deldlg = new DELETEDB_CONFIRMDialog(shell);
			if (deldlg.doModal()) {
				if (MainRegistry.isCertLogin()) {
					String[] ret = null;
					ProtegoReadCert reader = new ProtegoReadCert();
					ret = reader.protegoSelectCert();
					if (ret == null) {
						return;
					}
					if (!(ret[0].equals(MainRegistry.UserID))) {
						CommonTool.ErrorBox(Messages.getString("ERROR.USERDNERROR"));
						return;
					}
				} else {
					DBA_CONFIRMDialog condlg = new DBA_CONFIRMDialog(shell);
					if (!condlg.doModal())
						return;
				}

				String requestMsg = "dbname:" + CubridView.Current_db + "\n";
				if (deleteBackup)
					requestMsg += "delbackup:y\n";
				else
					requestMsg += "delbackup:n\n";

				cs = new ClientSocket();
				if (cs.SendBackGround(shell, requestMsg, "deletedb", Messages
						.getString("WAITING.DELETEDB"))) {
					MainRegistry.Authinfo_remove(CubridView.Current_db);
					MainRegistry.DBUserInfo_remove(CubridView.Current_db);
					CubridView.Current_db = "";
					ApplicationActionBarAdvisor.refreshAction.run();
					CubridView.refresh();

					TreeItem[] itemArray = new TreeItem[1];
					itemArray[0] = (TreeItem) CubridView.viewer.getTree().getItems()[0];
					CubridView.viewer.getTree().setSelection(itemArray);
					CubridView.setViewDefault();
				} else {
					CommonTool.ErrorBox(shell, cs.ErrorMsg);
					return;
				}
			}
		} else {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
	}
}
