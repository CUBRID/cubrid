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

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.ISelectionListener;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.actions.ActionFactory;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.cas.CASItem;
import cubridmanager.cubrid.LogFileInfo;
import cubridmanager.diag.dialog.DiagActivityCASLogPathDialog;
import cubridmanager.diag.dialog.DiagCASLogTopConfigDialog;

public class DiagActivityAnalyzeCASLogAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {

	private final IWorkbenchWindow window;
	public static String logFile = new String("");
	public final static String ID = "cubridmanager.DiagActivityAnalyzeCASLog";

	public DiagActivityAnalyzeCASLogAction(String text, IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public DiagActivityAnalyzeCASLogAction(String text,
			IWorkbenchWindow window, int style) {
		super(text, style);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public void selectionChanged(IWorkbenchPart part, ISelection incoming) {
		setEnabled(false);
	}

	public void dispose() {
		window.getSelectionService().removeSelectionListener(this);
	}

	public void run() {
		Shell shell = new Shell();
		DiagCASLogTopConfigDialog configDialog = new DiagCASLogTopConfigDialog(
				shell);
		if (logFile.equals("")) {
			ArrayList casinfo = MainRegistry.CASinfo;
			for (int i = 0, n = casinfo.size(); i < n; i++) {
				ArrayList loginfo = ((CASItem) casinfo.get(i)).loginfo;
				for (int j = 0, m = loginfo.size(); j < m; j++) {
					if (((LogFileInfo) loginfo.get(j)).type.equals("script"))
						configDialog.targetStringList
								.add(((LogFileInfo) loginfo.get(j)).path);
				}
			}
		} else {
			configDialog.targetStringList.add(logFile);
		}

		if (configDialog.doModal() == Window.OK) {
			DiagActivityCASLogPathDialog dialog = new DiagActivityCASLogPathDialog(
					shell);

			String msg = new String();
			ClientSocket cs = new ClientSocket();

			msg = "open:logfilelist\n";
			for (int i = 0, n = configDialog.selectedStringList.size(); i < n; i++) {
				msg += "logfile:"
						+ (String) (configDialog.selectedStringList.get(i))
						+ "\n";
			}
			msg += "close:logfilelist\n";
			msg += "option_t:";
			if (configDialog.option_t)
				msg += "yes\n";
			else
				msg += "no\n";

			dialog.option_t = configDialog.option_t;

			cs.socketOwner = dialog;
			if (cs.SendBackGround(shell, msg, "analyzecaslog",
					"Analyzing cas log") == false) {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}

			dialog.filename = logFile;
			dialog.doModal();
		}
	}
}
