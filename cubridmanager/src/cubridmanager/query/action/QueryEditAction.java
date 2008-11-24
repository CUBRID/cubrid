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

package cubridmanager.query.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.WorkView;
import cubridmanager.query.view.QueryEditor;
import cubridmanager.Application;
import cubridmanager.MainRegistry;
import cubridmanager.MainConstants;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.dialog.ActiveDatabaseSelectDialog;

public class QueryEditAction extends Action {
	boolean needActiveDataBaseSelectDialog = true;

	public QueryEditAction(String text, String img, boolean needDBSelectDlg) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("QueryEditAction");
		// Associate the action with a pre-defined command, to allow key
		// bindings.
		setActionDefinitionId("QueryEditAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		needActiveDataBaseSelectDialog = needDBSelectDlg;
		setToolTipText(text);
	}

	public void run() {
		AuthItem aurec = null;
		DBUserInfo selUserInfo = null;

		aurec = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (needActiveDataBaseSelectDialog) {
			Shell sh = new Shell();
			ActiveDatabaseSelectDialog dlg = null;
			if (CubridView.Current_db.length() > 0)
				dlg = new ActiveDatabaseSelectDialog(sh, CubridView.Current_db);
			else
				dlg = new ActiveDatabaseSelectDialog(sh);
			selUserInfo = dlg.doModal();
		} else {
			if (MainRegistry.Current_Navigator == MainConstants.NAVI_CUBRID
					&& CubridView.Current_db.length() > 0) {
				if (aurec.dbuser != null && aurec.dbuser.length() > 0)
					selUserInfo = MainRegistry.getDBUserInfo(aurec.dbname);
				else {
					Shell sh = new Shell(Application.mainwindow.getShell());
					ActiveDatabaseSelectDialog dlg = new ActiveDatabaseSelectDialog(
							sh, CubridView.Current_db);
					selUserInfo = dlg.doModal();
				}
			} else {
				Shell sh = new Shell(Application.mainwindow.getShell());
				ActiveDatabaseSelectDialog dlg = new ActiveDatabaseSelectDialog(
						sh);
				selUserInfo = dlg.doModal();
			}
		}
		if (selUserInfo == null)
			return;
		WorkView.SetView(QueryEditor.ID, selUserInfo, null);
	}

}
