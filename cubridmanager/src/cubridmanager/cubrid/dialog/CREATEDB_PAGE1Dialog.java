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

package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;
import cubridmanager.*;

import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class CREATEDB_PAGE1Dialog extends WizardPage {
	public static final String PAGE_NAME = "CREATEDB_PAGE1Dialog";
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,61"
	private Composite sShell = null;
	private Composite comparent = null;
	public Text EDIT_CREATEDB_NAME = null;
	public Text EDIT_CREATEDB_NUMPAGE = null;
	public Combo COMBO_CREATEDB_PAGESIZE = null;
	public Text EDIT_CREATEDB_GENERICVOL = null;
	public Text EDIT_CREATEDB_LOGSIZE = null;
	public Text EDIT_CREATEDB_LOGVOL = null;
	public boolean is_ready = false;
	private Label label1 = null;
	private Group group1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Group group2 = null;
	private Label label4 = null;
	private Label label5 = null;
	private Group group3 = null;
	private Label label6 = null;
	public boolean isChangedGenericVol = false;
	public boolean isChangedLogVol = false;

	public CREATEDB_PAGE1Dialog() {
		super(PAGE_NAME, Messages.getString("PAGE.CREATEDBPAGE1"), null);
	}

	public int doModal() {
		createSShell();
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.CREATEDB_PAGE1DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		sShell = new Composite(comparent, SWT.NONE);
		GridData gridData60 = new org.eclipse.swt.layout.GridData();
		gridData60.grabExcessHorizontalSpace = true;
		GridData gridData59 = new org.eclipse.swt.layout.GridData();
		gridData59.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData59.grabExcessHorizontalSpace = true;
		gridData59.grabExcessVerticalSpace = true;
		gridData59.widthHint = 200;
		GridData gridData58 = new org.eclipse.swt.layout.GridData();
		gridData58.widthHint = 140;
		gridData58.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData58.grabExcessVerticalSpace = true;
		GridData gridData57 = new org.eclipse.swt.layout.GridData();
		GridLayout gridLayout56 = new GridLayout();
		gridLayout56.numColumns = 2;
		GridData gridData55 = new org.eclipse.swt.layout.GridData();
		gridData55.widthHint = 200;
		gridData55.grabExcessVerticalSpace = true;
		GridData gridData54 = new org.eclipse.swt.layout.GridData();
		gridData54.grabExcessHorizontalSpace = true;
		GridData gridData53 = new org.eclipse.swt.layout.GridData();
		gridData53.widthHint = 110;
		gridData53.grabExcessVerticalSpace = true;
		gridData53.grabExcessHorizontalSpace = true;
		GridLayout gridLayout52 = new GridLayout();
		gridLayout52.numColumns = 2;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.grabExcessVerticalSpace = true;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.widthHint = 200;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		// sShell = new Composite(dlgShell, SWT.NONE); //comment out for VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.DATABASENAME"));
		group1.setLayoutData(gridData2);
		group1.setLayout(gridLayout);
		group3 = new Group(sShell, SWT.NONE);
		group3.setText(Messages.getString("GROUP.GENERICVOLUME"));
		group3.setLayout(gridLayout52);
		group3.setLayoutData(gridData3);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.LOGVOLUMEINFORMATION"));
		group2.setLayout(gridLayout56);
		group2.setLayoutData(gridData4);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.DATABASENAME"));
		label1.setLayoutData(gridData);
		EDIT_CREATEDB_NAME = new Text(group1, SWT.BORDER);
		EDIT_CREATEDB_NAME.setLayoutData(gridData1);
		EDIT_CREATEDB_NAME
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						if (!isChangedGenericVol) {
							EDIT_CREATEDB_GENERICVOL
									.setText(MainRegistry.envCUBRID_DATABASES
											+ CommonTool.getPathSeperator(MainRegistry.envCUBRID_DATABASES)
											+ EDIT_CREATEDB_NAME.getText());
							isChangedGenericVol = false;
						}
						if (!(isChangedLogVol || isChangedGenericVol)) {
							EDIT_CREATEDB_LOGVOL
									.setText(MainRegistry.envCUBRID_DATABASES
											+ CommonTool.getPathSeperator(MainRegistry.envCUBRID_DATABASES)
											+ EDIT_CREATEDB_NAME.getText());
							isChangedLogVol = false;
						}
					}
				});
		label2 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.NUMBEROFPAGES"));
		label2.setLayoutData(gridData54);
		EDIT_CREATEDB_NUMPAGE = new Text(group3, SWT.BORDER);
		EDIT_CREATEDB_NUMPAGE.setLayoutData(gridData53);
		label3 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.PAGESIZEBYTES"));
		createCombo1();
		label4 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.GENERICVOLUME"));
		EDIT_CREATEDB_GENERICVOL = new Text(group3, SWT.BORDER);
		EDIT_CREATEDB_GENERICVOL.setLayoutData(gridData55);
		EDIT_CREATEDB_GENERICVOL
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						isChangedGenericVol = true;
						if (!isChangedLogVol) {
							EDIT_CREATEDB_LOGVOL
									.setText(EDIT_CREATEDB_GENERICVOL.getText());
							isChangedLogVol = false;
						}

					}
				});
		label5 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.LOGFILESIZENUMBER"));
		label5.setLayoutData(gridData57);
		EDIT_CREATEDB_LOGSIZE = new Text(group2, SWT.BORDER);
		EDIT_CREATEDB_LOGSIZE.setLayoutData(gridData58);
		label6 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.LOGVOLUMEPATH"));
		label6.setLayoutData(gridData60);
		EDIT_CREATEDB_LOGVOL = new Text(group2, SWT.BORDER);
		EDIT_CREATEDB_LOGVOL.setLayoutData(gridData59);
		EDIT_CREATEDB_LOGVOL
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						isChangedLogVol = true;
					}
				});
		setinfo();
		sShell.pack();
	}

	private void createCombo1() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 110;
		gridData5.grabExcessVerticalSpace = true;
		COMBO_CREATEDB_PAGESIZE = new Combo(group3, SWT.DROP_DOWN
				| SWT.READ_ONLY);
		COMBO_CREATEDB_PAGESIZE.setLayoutData(gridData5);
	}

	private void setinfo() {
		COMBO_CREATEDB_PAGESIZE.add("1024", 0);
		COMBO_CREATEDB_PAGESIZE.add("2048", 1);
		COMBO_CREATEDB_PAGESIZE.add("4096", 2);
		COMBO_CREATEDB_PAGESIZE.add("8192", 3);
		COMBO_CREATEDB_PAGESIZE.add("16384", 4);
		int page_size_index = 0, syspara = CommonTool
				.atoi(MainRegistry.DBPARA_PAGESIZE);
		if (syspara < 2048)
			page_size_index = 0;
		else if (syspara < 4096)
			page_size_index = 1;
		else if (syspara < 8192)
			page_size_index = 2;
		else if (syspara < 16384)
			page_size_index = 3;
		else if (syspara >= 16384)
			page_size_index = 4;
		else
			page_size_index = 2; // Input default value because it is invalid value
		COMBO_CREATEDB_PAGESIZE.select(page_size_index);

		EDIT_CREATEDB_NUMPAGE.setText(MainRegistry.DBPARA_GENERICNUM);
		EDIT_CREATEDB_GENERICVOL.setText(MainRegistry.envCUBRID_DATABASES);
		EDIT_CREATEDB_LOGSIZE.setText(MainRegistry.DBPARA_LOGNUM);
		EDIT_CREATEDB_LOGVOL.setText(MainRegistry.envCUBRID_DATABASES);

		EDIT_CREATEDB_NAME.setToolTipText(Messages
				.getString("TOOLTIP.NEWDBNAME"));
		EDIT_CREATEDB_NUMPAGE.setToolTipText(Messages
				.getString("TOOLTIP.EDIT_CREATEDB_NUMPAGE"));
		EDIT_CREATEDB_GENERICVOL.setToolTipText(Messages
				.getString("TOOLTIP.EDITGENERICVOL"));
		EDIT_CREATEDB_LOGSIZE.setToolTipText(Messages
				.getString("TOOLTIP.EDIT_CREATEDB_LOGSIZE"));
		EDIT_CREATEDB_LOGVOL.setToolTipText(Messages
				.getString("TOOLTIP.EDITLOGVOL"));
		COMBO_CREATEDB_PAGESIZE.setToolTipText(Messages
				.getString("TOOLTIP.COMBOPAGESIZE"));
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setPageComplete(true);
		setControl(sShell);
	}
}
