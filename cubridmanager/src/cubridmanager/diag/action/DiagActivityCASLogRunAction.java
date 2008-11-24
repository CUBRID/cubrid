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
import cubridmanager.diag.dialog.DiagCasRunnerConfigDialog;
import cubridmanager.diag.dialog.DiagCasRunnerResultDialog;

public class DiagActivityCASLogRunAction extends Action implements
		ISelectionListener, ActionFactory.IWorkbenchAction {
	public static String logFile = new String();
	private final IWorkbenchWindow window;
	public final static String ID = "cubridmanager.DiagActivityCASLogRun";

	public DiagActivityCASLogRunAction(String text, IWorkbenchWindow window) {
		super(text);
		this.window = window;
		setId(ID);
		setActionDefinitionId(ID);
	}

	public DiagActivityCASLogRunAction(String text, IWorkbenchWindow window,
			int style) {
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
		if (logFile.equals(""))
			return;
		DiagCasRunnerConfigDialog casrunnerconfigdialog = new DiagCasRunnerConfigDialog(
				shell);
		casrunnerconfigdialog.logfile = logFile;
		casrunnerconfigdialog.execwithFile = true;

		if (casrunnerconfigdialog.doModal() == Window.CANCEL)
			return;
		StringBuffer msg = new StringBuffer();

		msg.append("dbname:").append(casrunnerconfigdialog.dbname).append("\n");
		msg.append("brokername:").append(casrunnerconfigdialog.brokerName)
				.append("\n");
		msg.append("username:").append(casrunnerconfigdialog.userName).append(
				"\n");
		msg.append("passwd:").append(casrunnerconfigdialog.password).append(
				"\n");
		msg.append("num_thread:").append(casrunnerconfigdialog.numThread)
				.append("\n");
		msg.append("repeat_count:")
				.append(casrunnerconfigdialog.numRepeatCount).append("\n");
		msg.append("executelogfile:yes\n");
		msg.append("logfile:").append(logFile).append("\n");
		msg.append("show_queryresult:no\n");

		ClientSocket cs = new ClientSocket();
		if (cs.SendBackGround(shell, msg.toString(), "executecasrunner",
				"Executing cas runner") == false) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		/* Display result */
		DiagCasRunnerResultDialog resultDialog = new DiagCasRunnerResultDialog(
				shell);

		resultDialog
				.doModal(MainRegistry.tmpDiagExecuteCasRunnerResult.resultString);
	}
}
