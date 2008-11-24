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

import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.cubrid.dialog.CREATE_CLASSDialog;
import cubridmanager.cubrid.view.CubridView;

public class NewClassAction extends Action {
	public static String newclass = "";

	public NewClassAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("NewClassAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		CREATE_CLASSDialog dlg = new CREATE_CLASSDialog(shell);
		if (dlg.doModal()) { // class or view create ok
			CubridView.myNavi.createModel();
			CubridView.viewer.refresh();
			ClientSocket cs = new ClientSocket();
			if (!cs.SendClientMessage(shell, "dbname:" + CubridView.Current_db
					+ "\nclassname:" + newclass, "class")) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}
			DBSchema.CurrentObj = newclass;
			ApplicationActionBarAdvisor.tablePropertyAction.runpage();
		}
	}
}
