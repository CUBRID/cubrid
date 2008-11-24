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

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBTriggers;

import org.eclipse.swt.widgets.Shell;

public class DropTriggerAction extends Action {
	public DropTriggerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("DropTriggerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		if (DBTriggers.Current_select.length() <= 0)
			return;
		if (CommonTool.WarnYesNo(Messages.getString("WARNYESNO.DROPTRIGGER")) != SWT.YES)
			return;

		Shell dlgShell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "dbname:" + CubridView.Current_db
				+ "\ntriggername:" + DBTriggers.Current_select, "droptrigger",
				Messages.getString("WAITING.DELTRIGGER"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}

		CommonTool.MsgBox(dlgShell, Messages.getString("MSG.SUCCESS"), Messages
				.getString("MSG.DELTRIGGERSUCCESS"));

		cs = new ClientSocket();
		if (!cs.SendClientMessage(dlgShell, "dbname:" + CubridView.Current_db,
				"gettriggerinfo")) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return;
		}
		WorkView.DeleteView(DBTriggers.ID);
		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();
	}
}
