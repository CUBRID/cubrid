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
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WaitingMsgBox;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.UNLOADDBDialog;
import cubridmanager.cubrid.dialog.UNLOADRESULTDialog;
import cubridmanager.cubrid.view.CubridView;

public class UnloadAction extends Action {
	public static AuthItem ai = null;

	public UnloadAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("UnloadAction");
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
		ClientSocket cs = new ClientSocket();

		if (cs.Connect()) {
			if (cs.Send(shell, "dbname:" + CubridView.Current_db
					+ "\ndbstatus:off", "classinfo")) {
				WaitingMsgBox wdlg = new WaitingMsgBox(shell);
				wdlg.run(Messages.getString("WAITING.CLASSINFO"));
				if (cs.ErrorMsg != null) {
					CommonTool.ErrorBox(shell, cs.ErrorMsg);
					return;
				}

				UNLOADDBDialog dlg = new UNLOADDBDialog(shell);
				if (dlg.doModal()) { // check dir & check files success
					if (dlg.isSchemaOnly) {
						CommonTool.MsgBox(shell, Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.SCHEMAUNLOADOK"));
					} else {
						UNLOADRESULTDialog udlg = new UNLOADRESULTDialog(shell);
						udlg.doModal();
					}
				}
			} else {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}
		} else {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
	}
}
