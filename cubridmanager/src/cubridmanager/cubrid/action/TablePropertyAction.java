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

import java.util.ArrayList;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE1Dialog;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE2Dialog;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE3Dialog;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE4Dialog;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;

public class TablePropertyAction extends Action {
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="5,8"
	private TabFolder tabFolder = null;
	private Composite composite = null;
	private Composite composite2 = null;
	private Composite composite3 = null;
	private Composite composite4 = null;
	private Button IDCANCEL = null;
	AuthItem authrec = null;
	public static ArrayList sinfo = null;
	public static SchemaInfo si = null;

	public TablePropertyAction(String text, String img) {
		super(text);

		// The id is used to refer to the action in a menu or toolbar
		setId("TablePropertyAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		runpage();
	}

	private boolean getclass() {
		sinfo = null;
		si = null;
		for (int i = 0, n = MainRegistry.Authinfo.size(); i < n; i++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(i);
			if (authrec.dbname.equals(CubridView.Current_db)) {
				sinfo = authrec.Schema;
				for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
					si = (SchemaInfo) sinfo.get(ai);
					if (si.name.equals(DBSchema.CurrentObj)) {
						return true;
					}
				}
			}
		}
		return false;
	}

	public void runpage() {
		if (DBSchema.CurrentObj == null)
			return;
		if (!getclass())
			return;

		sShell = new Shell(Application.mainwindow.getShell(),
				SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 1;
		sShell.setLayout(gridLayout);
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(sShell, "dbname:" + CubridView.Current_db
				+ "\nclassname:" + DBSchema.CurrentObj, "class")) {
			CommonTool.ErrorBox(sShell, cs.ErrorMsg);
			return;
		}
		tabFolder = new TabFolder(sShell, SWT.NONE);

		String title;
		if (si.virtual.equals("normal")) {
			if (si.type.equals("user")) {
				title = Messages.getString("TITLE.CLASSINFO_EDIT");
			}
			else {
				title = Messages.getString("TITLE.CLASSINFO");
			}
			sShell.setText(title);

			PROPPAGE_CLASS_PAGE1Dialog part1 = new PROPPAGE_CLASS_PAGE1Dialog(
					sShell);
			PROPPAGE_CLASS_PAGE2Dialog part2 = new PROPPAGE_CLASS_PAGE2Dialog(
					sShell);
			PROPPAGE_CLASS_PAGE3Dialog part3 = new PROPPAGE_CLASS_PAGE3Dialog(
					sShell);

			composite = part1.SetTabPart(tabFolder);
			composite2 = part2.SetTabPart(tabFolder);
			composite3 = part3.SetTabPart(tabFolder);

			TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
			tabItem.setControl(composite);
			tabItem.setText(Messages
					.getString("TITLE.PROPPAGE_CLASS_PAGE1DIALOG"));
			tabItem = new TabItem(tabFolder, SWT.NONE);
			tabItem.setControl(composite2);
			tabItem.setText(Messages
					.getString("TITLE.PROPPAGE_CLASS_PAGE2DIALOG"));
			tabItem = new TabItem(tabFolder, SWT.NONE);
			tabItem.setControl(composite3);
			tabItem.setText(Messages
					.getString("TITLE.PROPPAGE_CLASS_PAGE3DIALOG"));
		} else if (si.virtual.equals("view")) {
			if (si.type.equals("user")) {
				title = Messages.getString("TITLE.VIRTUALCLASSINFO_EDIT");
			}
			else {
				title = Messages.getString("TITLE.VIRTUALCLASSINFO");
			}
			sShell.setText(title);

			PROPPAGE_CLASS_PAGE1Dialog part1 = new PROPPAGE_CLASS_PAGE1Dialog(
					sShell);
			PROPPAGE_CLASS_PAGE4Dialog part4 = new PROPPAGE_CLASS_PAGE4Dialog(
					sShell);

			composite = part1.SetTabPart(tabFolder);
			composite4 = part4.SetTabPart(tabFolder);

			TabItem tabItem = new TabItem(tabFolder, SWT.NONE);
			tabItem.setControl(composite);
			tabItem.setText(Messages
					.getString("TITLE.PROPPAGE_CLASS_PAGE1DIALOG"));
			tabItem = new TabItem(tabFolder, SWT.NONE);
			tabItem.setControl(composite4);
			tabItem.setText(Messages
					.getString("TITLE.PROPPAGE_CLASS_PAGE4DIALOG"));
		}

		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CLOSE"));
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						WorkView.SetView(DBSchema.ID, DBSchema.CurrentSelect,
								DBSchema.ID);
						sShell.dispose();
					}
				});
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData3.widthHint = 100;
		IDCANCEL.setLayoutData(gridData3);

		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
	}
}
