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

import java.io.File;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Shell;

import cubrid.upa.UpaClient;
import cubrid.upa.UpaException;
import cubrid.upa.UpaUserInfo;
import cubridmanager.Application;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.dialog.PROPAGE_ProtegoUserManagementDialog;
import cubridmanager.dialog.ProtegoUserAddDialog;
import cubridmanager.dialog.ProtegoUserAddResultDialog;

public class ProtegoUserAddAction extends Action {
	public boolean bulkAdd = false;

	Shell shell = new Shell();

	public ProtegoUserAddAction(String text) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserAddAction");
	}

	public ProtegoUserAddAction(String text, boolean addFromFile) {
		super(text);
		bulkAdd = addFromFile;
		// The id is used to refer to the action in a menu or toolbar
		setId("ProtegoUserAddAction");
	}

	public void run() {
		if (bulkAdd) {
			FileDialog dlg = new FileDialog(Application.mainwindow.getShell(),
					SWT.OPEN | SWT.APPLICATION_MODAL);
			dlg.setFilterExtensions(new String[] { "*.txt", "*.*" });
			dlg.setFilterNames(new String[] { "Txt file", "All file" });
			File curdir = new File(".");
			try {
				dlg.setFilterPath(curdir.getCanonicalPath());
			} catch (Exception e) {
				dlg.setFilterPath(".");
			}

			String fileName = dlg.open();
			if (fileName == null)
				return;

			UpaUserInfo[] failedList = null;
			try {
				failedList = UpaClient.admUserAddFile(
						ProtegoUserManagementAction.dlg.upaKey, fileName);
			} catch (UpaException ee) {
				CommonTool.ErrorBox(Application.mainwindow.getShell(), Messages
						.getString("TEXT.FAILEDADDUSERAUTH"));
				return;
			}

			if (failedList != null && failedList.length != 0) {
				ProtegoUserAddResultDialog addResultDialog = new ProtegoUserAddResultDialog(
						shell);
				addResultDialog.failedList = failedList;
				addResultDialog.doModal();
			}
		} else {
			ProtegoUserAddDialog dlg = new ProtegoUserAddDialog(shell);
			dlg.doModal();
		}

		PROPAGE_ProtegoUserManagementDialog.refresh();
	}
}
