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

package cubridmanager.diag.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.diag.view.DiagView;

public class DiagRemoveTemplateAction extends Action {
	public String templateName = "";
	public String templateType = "";
	// private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagRemoveTemplate";

	public DiagRemoveTemplateAction(String text, String tType,
			IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
		templateType = tType;
	}

	public DiagRemoveTemplateAction(String text, String tType, String tName,
			IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		setId(ID);
		setActionDefinitionId(ID);

		templateType = tType;
		templateName = tName;
	}

	public void run() {
		String msg;
		ClientSocket cs = new ClientSocket();
		msg = "name:";
		msg += templateName;
		msg += "\n";
		if (templateType == "status") {
			if (!cs.SendBackGround(Application.mainwindow.getShell(), msg,
					"removestatustemplate", "Removing status template")) {
				CommonTool.ErrorBox(cs.ErrorMsg);
			} else
				DiagView.refresh();
		} else if (templateType == "activity") {
			if (!cs.SendBackGround(Application.mainwindow.getShell(), msg,
					"removeactivitytemplate", "Removing activity template")) {
				CommonTool.ErrorBox(cs.ErrorMsg);
			} else
				DiagView.refresh();
		}
	}
}
