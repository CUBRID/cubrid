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

import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.VerifyDigitListener;
import cubridmanager.cubrid.UnloadInfo;
import cubridmanager.cubrid.action.LoadAction;

import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Combo;

public class LoadDbDialog extends Dialog {
	private Shell sShell = null;
	private Composite cmpTargetDb = null;
	private CLabel lblTargetDb = null;
	private Text txtTargetDb = null;
	private CLabel lblUserName = null;
	private Text txtUserName = null;
	private Group grpUnloadFiles = null;
	private Button chkUnloadList = null;
	private Combo cmbUnloadList = null;
	private CLabel lblShadowIn = null;
	private Composite cmpUnloadFiles = null;
	private Button chkSchema = null;
	private Text txtSchema = null;
	private Button btnSchema = null;
	private Button chkData = null;
	private Text txtData = null;
	private Button btnData = null;
	private Button chkIndex = null;
	private Text txtIndex = null;
	private Button btnIndex = null;
	private Button chkTrigger = null;
	private Text txtTrigger = null;
	private Button btnTrigger = null;
	private Group grpOptions = null;
	private Button rdoCheckAndLoad = null;
	private Button rdoLoad = null;
	private Button rdoCheck = null;
	private Button chkCommitPeriod = null;
	private Text txtCommitPeriod = null;
	private Button chkEstimatedSize = null;
	private Text txtEstimatedSize = null;
	private Button chkNoRef = null;
	private Button chkNoLog = null;
	private Composite cmpBtnArea = null;
	private Button btnOk = null;
	private Button btnCancel = null;
	private Composite cmpUnloadFiles2 = null;
	private CLabel lblSchema = null;
	private Combo cmbSchema = null;
	private CLabel lblData = null;
	private Combo cmbData = null;
	private CLabel lblIndex = null;
	private Combo cmbIndex = null;
	private CLabel lblTrigger = null;
	private Combo cmbTrigger = null;
	private FileDialog fDlg = null;

	private boolean ret = false;

	public LoadDbDialog(Shell parent) {
		super(parent);
	}

	public LoadDbDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		setinfo();

		CommonTool.centerShell(sShell);
		sShell.setDefaultButton(btnOk);
		sShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.LOADDB"));
		sShell.setLayout(new GridLayout());
		createCmpTargetDb();
		createGrpUnloadFiles();
		createGrpOptions();
		createCmpBtnArea();
	}

	/**
	 * This method initializes cmpTargetDb
	 * 
	 */
	private void createCmpTargetDb() {
		cmpTargetDb = new Composite(sShell, SWT.NONE);
		cmpTargetDb.setLayout(new GridLayout(2, false));
		cmpTargetDb.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		lblTargetDb = new CLabel(cmpTargetDb, SWT.NONE);
		lblTargetDb.setText(Messages.getString("LABEL.TARGETDATABASE"));
		txtTargetDb = new Text(cmpTargetDb, SWT.BORDER);
		txtTargetDb.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		txtTargetDb.setEnabled(false);
		txtTargetDb.setToolTipText(Messages.getString("TOOLTIP.EDITDBNAME"));

		lblUserName = new CLabel(cmpTargetDb, SWT.NONE);
		lblUserName.setText(Messages.getString("LABEL.USERNAME"));
		txtUserName = new Text(cmpTargetDb, SWT.BORDER);
		txtUserName.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		txtUserName.setEnabled(false);
		txtUserName.setToolTipText(Messages.getString("TOOLTIP.EDITUSERNAME"));
	}

	/**
	 * This method initializes grpUnloadFiles
	 * 
	 */
	private void createGrpUnloadFiles() {
		GridLayout gridLayout1 = new GridLayout(2, false);
		grpUnloadFiles = new Group(sShell, SWT.NONE);
		grpUnloadFiles.setText(Messages.getString("GROUP.UNLOADEDFILE"));
		grpUnloadFiles.setLayout(gridLayout1);
		GridData gridData = new GridData();
		gridData.horizontalSpan = 2;
		chkUnloadList = new Button(grpUnloadFiles, SWT.CHECK);
		chkUnloadList.setLayoutData(gridData);
		chkUnloadList.setText(Messages.getString("RADIO.SELECTUNLOADED"));
		chkUnloadList
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});

		createCmbUnloadList();

		GridData gridShowIn = new org.eclipse.swt.layout.GridData(
				GridData.HORIZONTAL_ALIGN_FILL);
		gridShowIn.horizontalSpan = 2;
		gridShowIn.heightHint = 3;
		lblShadowIn = new CLabel(grpUnloadFiles, SWT.SHADOW_IN);
		lblShadowIn.setLayoutData(gridShowIn);
		createCmpUnloadFiles();
		createCmpUnloadFiles2();
	}

	/**
	 * This method initializes cmbUnloadList
	 * 
	 */
	private void createCmbUnloadList() {
		GridData gridData1 = new GridData();
		gridData1.widthHint = 250;
		gridData1.horizontalAlignment = GridData.BEGINNING;
		gridData1.horizontalIndent = 20;
		cmbUnloadList = new Combo(grpUnloadFiles, SWT.DROP_DOWN | SWT.READ_ONLY);
		cmbUnloadList.setLayoutData(gridData1);
		cmbUnloadList
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						cmbSchema.remove(1, cmbSchema.getItemCount() - 1);
						cmbData.remove(1, cmbData.getItemCount() - 1);
						cmbIndex.remove(1, cmbIndex.getItemCount() - 1);
						cmbTrigger.remove(1, cmbTrigger.getItemCount() - 1);
						if (cmbUnloadList.getSelectionIndex() > 0) {
							UnloadInfo ui = null;
							for (int i = 0, n = LoadAction.unloaddb.size(); i < n; i++) {
								ui = (UnloadInfo) LoadAction.unloaddb.get(i);
								if (ui.dbname.equals(cmbUnloadList.getText()))
									break;
								ui = null;
							}

							if (ui != null) {
								int i, n;
								for (i = 0, n = ui.schemaDate.size(); i < n; i++)
									cmbSchema.add("[".concat(
											ui.schemaDate.get(i).toString())
											.concat("]").concat(
													ui.schemaDir.get(i)
															.toString()));
								for (i = 0, n = ui.objectDate.size(); i < n; i++)
									cmbData.add("[".concat(
											ui.objectDate.get(i).toString())
											.concat("]").concat(
													ui.objectDir.get(i)
															.toString()));
								for (i = 0, n = ui.indexDate.size(); i < n; i++)
									cmbIndex.add("[".concat(
											ui.indexDate.get(i).toString())
											.concat("]").concat(
													ui.indexDir.get(i)
															.toString()));
								for (i = 0, n = ui.triggerDate.size(); i < n; i++)
									cmbTrigger.add("[".concat(
											ui.triggerDate.get(i).toString())
											.concat("]").concat(
													ui.triggerDir.get(i)
															.toString()));
							}
						}
						cmbSchema.select(cmbSchema.getItemCount() - 1);
						cmbData.select(cmbData.getItemCount() - 1);
						cmbIndex.select(cmbIndex.getItemCount() - 1);
						cmbTrigger.select(cmbTrigger.getItemCount() - 1);
						setWidgetEnabled();
					}
				});
	}

	/**
	 * This method initializes cmpUnloadFiles
	 * 
	 */
	private void createCmpUnloadFiles() {
		GridLayout gridLayout = new GridLayout(3, false);
		gridLayout.marginHeight = 0;
		gridLayout.marginWidth = 0;
		GridData gridCmpUnloadFiles = new GridData(GridData.FILL_HORIZONTAL);
		gridCmpUnloadFiles.horizontalSpan = 2;
		cmpUnloadFiles = new Composite(grpUnloadFiles, SWT.NONE);
		cmpUnloadFiles.setLayoutData(gridCmpUnloadFiles);
		cmpUnloadFiles.setLayout(gridLayout);

		chkSchema = new Button(cmpUnloadFiles, SWT.CHECK);
		chkSchema.setText(Messages.getString("CHECK.LOADSCHEMA"));
		chkSchema.setToolTipText(Messages.getString("TOOLTIP.CHECKSCHEMA"));
		chkSchema
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		GridData gridTxtSchema = new GridData(
				GridData.HORIZONTAL_ALIGN_BEGINNING);
		gridTxtSchema.widthHint = 200;
		txtSchema = new Text(cmpUnloadFiles, SWT.BORDER);
		txtSchema.setLayoutData(gridTxtSchema);
		txtSchema.setToolTipText(Messages.getString("TOOLTIP.EDITSCHEMA"));
		txtSchema
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						setWidgetEnabled();
					}
				});
		btnSchema = new Button(cmpUnloadFiles, SWT.NONE);
		btnSchema.setText(Messages.getString("BUTTON.OPENFILE"));
		btnSchema
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						fDlg
								.setText(Messages
										.getString("CHECK.LOADSCHEMA")
										.concat("- ")
										.concat(
												Messages
														.getString("STRING.FILESELECT")));
						String fileName = fDlg.open();
						if (fileName != null)
							txtSchema.setText(fileName);
					}
				});

		chkData = new Button(cmpUnloadFiles, SWT.CHECK);
		chkData.setText(Messages.getString("CHECK.LOADOBJECTS"));
		chkData.setToolTipText(Messages.getString("TOOLTIP.CHECKOBJECT"));
		chkData
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		txtData = new Text(cmpUnloadFiles, SWT.BORDER);
		txtData.setToolTipText(Messages.getString("TOOLTIP.EDITOBJECT"));
		txtData.setLayoutData(gridTxtSchema);
		txtData.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
			public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
				setWidgetEnabled();
			}
		});
		btnData = new Button(cmpUnloadFiles, SWT.NONE);
		btnData.setText(Messages.getString("BUTTON.OPENFILE"));
		btnData
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						fDlg
								.setText(Messages
										.getString("CHECK.LOADOBJECTS")
										.concat("- ")
										.concat(
												Messages
														.getString("STRING.FILESELECT")));
						String fileName = fDlg.open();
						if (fileName != null)
							txtData.setText(fileName);
					}
				});

		chkIndex = new Button(cmpUnloadFiles, SWT.CHECK);
		chkIndex.setText(Messages.getString("CHECK.LOADINDEX"));
		chkIndex.setToolTipText(Messages.getString("TOOLTIP.CHECKINDEX"));
		chkIndex
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		txtIndex = new Text(cmpUnloadFiles, SWT.BORDER);
		txtIndex.setToolTipText(Messages.getString("TOOLTIP.EDITINDEX"));
		txtIndex.setLayoutData(gridTxtSchema);
		txtIndex.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
			public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
				setWidgetEnabled();
			}
		});
		btnIndex = new Button(cmpUnloadFiles, SWT.NONE);
		btnIndex.setText(Messages.getString("BUTTON.OPENFILE"));
		btnIndex
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						fDlg
								.setText(Messages
										.getString("CHECK.LOADINDEX")
										.concat("- ")
										.concat(
												Messages
														.getString("STRING.FILESELECT")));
						String fileName = fDlg.open();
						if (fileName != null)
							txtIndex.setText(fileName);
					}
				});

		chkTrigger = new Button(cmpUnloadFiles, SWT.CHECK);
		chkTrigger.setText(Messages.getString("CHECK.LOADTRIGGER"));
		chkTrigger.setToolTipText(Messages.getString("TOOLTIP.CHECKTRIGGER"));
		chkTrigger
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		txtTrigger = new Text(cmpUnloadFiles, SWT.BORDER);
		txtTrigger.setToolTipText(Messages.getString("TOOLTIP.EDITTRIGGER"));
		txtTrigger.setLayoutData(gridTxtSchema);
		txtTrigger
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						setWidgetEnabled();
					}
				});
		btnTrigger = new Button(cmpUnloadFiles, SWT.NONE);
		btnTrigger.setText(Messages.getString("BUTTON.OPENFILE"));
		btnTrigger
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						fDlg
								.setText(Messages
										.getString("CHECK.LOADTRIGGER")
										.concat("- ")
										.concat(
												Messages
														.getString("STRING.FILESELECT")));
						String fileName = fDlg.open();
						if (fileName != null)
							txtTrigger.setText(fileName);
					}
				});
	}

	private void createCmpUnloadFiles2() {
		GridLayout gridLayout = new GridLayout(2, false);
		gridLayout.marginHeight = 0;
		gridLayout.marginWidth = 0;
		GridData gridCmpUnloadFiles = new GridData(GridData.FILL_HORIZONTAL);
		gridCmpUnloadFiles.horizontalSpan = 2;
		cmpUnloadFiles2 = new Composite(grpUnloadFiles, SWT.NONE);
		cmpUnloadFiles2.setLayoutData(gridCmpUnloadFiles);
		cmpUnloadFiles2.setLayout(gridLayout);

		GridData gridData4 = new GridData(GridData.HORIZONTAL_ALIGN_FILL);
		gridData4.widthHint = 270;
		lblSchema = new CLabel(cmpUnloadFiles2, SWT.NONE);
		lblSchema.setText(Messages.getString("CHECK.LOADSCHEMA"));
		lblSchema.setToolTipText(Messages.getString("TOOLTIP.LISTSCHEMA"));
		cmbSchema = new Combo(cmpUnloadFiles2, SWT.DROP_DOWN | SWT.READ_ONLY);
		cmbSchema.setLayoutData(gridData4);
		cmbSchema
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		cmbSchema.add(Messages.getString("ERROR.SELECTSCHEMAFILETOLOAD"));

		lblData = new CLabel(cmpUnloadFiles2, SWT.NONE);
		lblData.setText(Messages.getString("CHECK.LOADOBJECTS"));
		lblData.setToolTipText(Messages.getString("TOOLTIP.LISTOBJECT"));
		cmbData = new Combo(cmpUnloadFiles2, SWT.DROP_DOWN | SWT.READ_ONLY);
		cmbData.setLayoutData(gridData4);
		cmbData
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		cmbData.add(Messages.getString("ERROR.SELECTOBJECTFILETOLOAD"));

		lblIndex = new CLabel(cmpUnloadFiles2, SWT.NONE);
		lblIndex.setText(Messages.getString("CHECK.LOADINDEX"));
		lblIndex.setToolTipText(Messages.getString("TOOLTIP.LISTINDEX"));
		cmbIndex = new Combo(cmpUnloadFiles2, SWT.DROP_DOWN | SWT.READ_ONLY);
		cmbIndex.setLayoutData(gridData4);
		cmbIndex
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		cmbIndex.add(Messages.getString("ERROR.SELECTINDEXFILETOLOAD"));

		lblTrigger = new CLabel(cmpUnloadFiles2, SWT.NONE);
		lblTrigger.setText(Messages.getString("CHECK.LOADTRIGGER"));
		lblTrigger.setToolTipText(Messages.getString("TOOLTIP.LISTTRIGGER"));
		cmbTrigger = new Combo(cmpUnloadFiles2, SWT.DROP_DOWN | SWT.READ_ONLY);
		;
		cmbTrigger.setLayoutData(gridData4);
		cmbTrigger
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setWidgetEnabled();
					}
				});
		cmbTrigger.add(Messages.getString("ERROR.SELECTTRIGGERFILETOLOAD"));
	}

	/**
	 * This method initializes grpOptions
	 * 
	 */
	private void createGrpOptions() {
		grpOptions = new Group(sShell, SWT.NONE);
		grpOptions.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		grpOptions.setLayout(new GridLayout(2, false));
		grpOptions.setText(Messages.getString("GROUP.LOADOPTION"));

		GridData gridHoriSpan = new GridData(GridData.FILL_HORIZONTAL);
		gridHoriSpan.horizontalSpan = 2;
		rdoCheckAndLoad = new Button(grpOptions, SWT.RADIO);
		rdoCheckAndLoad.setText(Messages.getString("RADIO.DATAFILECHECKAND"));
		rdoCheckAndLoad.setLayoutData(gridHoriSpan);
		rdoLoad = new Button(grpOptions, SWT.RADIO);
		rdoLoad.setText(Messages.getString("RADIO.LOADONLY"));
		rdoLoad.setLayoutData(gridHoriSpan);
		rdoCheck = new Button(grpOptions, SWT.RADIO);
		rdoCheck.setText(Messages.getString("RADIO.DATAFILECHECKONLY"));
		rdoCheck.setLayoutData(gridHoriSpan);

		GridData gridShowIn = new org.eclipse.swt.layout.GridData(
				GridData.FILL_HORIZONTAL);
		gridShowIn.horizontalSpan = 2;
		gridShowIn.heightHint = 3;
		lblShadowIn = new CLabel(grpOptions, SWT.SHADOW_IN);
		lblShadowIn.setLayoutData(gridShowIn);

		chkCommitPeriod = new Button(grpOptions, SWT.CHECK);
		chkCommitPeriod.setText(Messages.getString("CHECK.COMMITPERIOD"));
		chkCommitPeriod
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (chkCommitPeriod.getSelection()) {
							txtCommitPeriod.setEnabled(true);
							txtCommitPeriod.setText("10000");
						} else {
							txtCommitPeriod.setEnabled(false);
							txtCommitPeriod.setText("");
						}
					}
				});
		txtCommitPeriod = new Text(grpOptions, SWT.BORDER);
		txtCommitPeriod.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		txtCommitPeriod
				.setToolTipText(Messages.getString("TOOLTIP.EDITPERIOD"));
		txtCommitPeriod.setEnabled(false);
		txtCommitPeriod.setTextLimit(10);
		txtCommitPeriod.addListener(SWT.Verify, new VerifyDigitListener());

		chkEstimatedSize = new Button(grpOptions, SWT.CHECK);
		chkEstimatedSize.setText(Messages.getString("CHECK.ESTIMATEDSIZE"));
		chkEstimatedSize
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (chkEstimatedSize.getSelection()) {
							txtEstimatedSize.setEnabled(true);
							txtEstimatedSize.setText("5000");
						} else {
							txtEstimatedSize.setEnabled(false);
							txtEstimatedSize.setText("");
						}
					}
				});
		txtEstimatedSize = new Text(grpOptions, SWT.BORDER);
		txtEstimatedSize.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		txtEstimatedSize.setToolTipText(Messages
				.getString("TOOLTIP.ESTIMATEDSIZE2"));
		txtEstimatedSize.setEnabled(false);
		txtEstimatedSize.setTextLimit(10);
		txtEstimatedSize.addListener(SWT.Verify, new VerifyDigitListener());

		chkNoRef = new Button(grpOptions, SWT.CHECK);
		chkNoRef.setText(Messages.getString("CHECK.OIDISNOTUSE"));
		chkNoRef.setLayoutData(gridHoriSpan);
		chkNoRef.setToolTipText(Messages.getString("TOOLTIP.OIDISNOTUSE"));
		chkNoLog = new Button(grpOptions, SWT.CHECK);
		chkNoLog.setText(Messages.getString("CHECK.NOLOG"));
		chkNoLog.setLayoutData(gridHoriSpan);
		chkNoLog.setToolTipText(Messages.getString("TOOLTIP.NOLOG"));
		chkNoLog.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (chkNoLog.getSelection())
					CommonTool.WarnBox(sShell, Messages.getString("MSG.NOLOG"));
			}
		});
	}

	/**
	 * This method initializes cmpBtnArea
	 * 
	 */
	private void createCmpBtnArea() {
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayout(new GridLayout(2, true));
		cmpBtnArea.setLayoutData(new GridData(GridData.HORIZONTAL_ALIGN_END));

		GridData gridData2 = new GridData();
		gridData2.widthHint = 75;
		btnOk = new Button(cmpBtnArea, SWT.NONE);
		btnOk.setText(Messages.getString("BUTTON.OK"));
		btnOk.setLayoutData(gridData2);

		btnOk
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (sendLoad()) {
							ret = true;
							sShell.dispose();
						}
					}
				});
		GridData gridData3 = new GridData();
		gridData3.widthHint = 75;
		btnCancel = new Button(cmpBtnArea, SWT.NONE);
		btnCancel.setText(Messages.getString("BUTTON.CANCEL"));
		btnCancel.setLayoutData(gridData3);
		btnCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						sShell.dispose();
					}
				});

	}

	private void setinfo() {
		fDlg = new FileDialog(sShell, SWT.OPEN | SWT.APPLICATION_MODAL);
		txtTargetDb.setText(LoadAction.ai.dbname);
		rdoCheckAndLoad.setSelection(true);
		txtUserName.setText("dba");

		cmbUnloadList.add(Messages.getString("COMBO.SELECTUNLOADEDDB"), 0);
		for (int i = 0, numInfo = LoadAction.unloaddb.size(); i < numInfo; i++) {
			UnloadInfo ui = (UnloadInfo) LoadAction.unloaddb.get(i);
			cmbUnloadList.add(ui.dbname, i + 1);
		}
		cmbUnloadList.select(0);
		cmbSchema.select(0);
		cmbData.select(0);
		cmbIndex.select(0);
		cmbTrigger.select(0);

		setWidgetEnabled();
	}

	private void setWidgetEnabled() {
		btnOk.setEnabled(false);
		if (!chkUnloadList.getSelection()) { // if unchecked(direct file input)
			cmbUnloadList.setEnabled(false);
			showCmpUnloadFiles2();

			txtSchema.setEnabled(chkSchema.getSelection());
			btnSchema.setEnabled(chkSchema.getSelection());
			txtData.setEnabled(chkData.getSelection());
			btnData.setEnabled(chkData.getSelection());
			txtIndex.setEnabled(chkIndex.getSelection());
			btnIndex.setEnabled(chkIndex.getSelection());
			txtTrigger.setEnabled(chkTrigger.getSelection());
			btnTrigger.setEnabled(chkTrigger.getSelection());

			if (chkSchema.getSelection()
					&& txtSchema.getText().trim().length() > 0)
				btnOk.setEnabled(true);
			else if (chkData.getSelection()
					&& txtData.getText().trim().length() > 0)
				btnOk.setEnabled(true);
			else if (chkIndex.getSelection()
					&& txtIndex.getText().trim().length() > 0)
				btnOk.setEnabled(true);
			else if (chkTrigger.getSelection()
					&& txtTrigger.getText().trim().length() > 0)
				btnOk.setEnabled(true);
		} else { // If checked (select list)
			cmbUnloadList.setEnabled(true);
			showCmpUnloadFiles();

			cmbSchema.setToolTipText(cmbSchema.getText());
			cmbData.setToolTipText(cmbData.getText());
			cmbIndex.setToolTipText(cmbIndex.getText());
			cmbTrigger.setToolTipText(cmbTrigger.getText());

			if (cmbSchema.getSelectionIndex() > 0)
				btnOk.setEnabled(true);
			else if (cmbData.getSelectionIndex() > 0)
				btnOk.setEnabled(true);
			else if (cmbIndex.getSelectionIndex() > 0)
				btnOk.setEnabled(true);
			else if (cmbTrigger.getSelectionIndex() > 0)
				btnOk.setEnabled(true);
		}
	}

	private void showCmpUnloadFiles() {
		GridData gridShow = new GridData(GridData.FILL_HORIZONTAL);
		gridShow.horizontalSpan = 2;
		gridShow.heightHint = -1;

		GridData gridHide = new GridData(GridData.FILL_HORIZONTAL);
		gridShow.horizontalSpan = 2;
		gridShow.heightHint = 0;

		cmpUnloadFiles.setLayoutData(gridShow);
		cmpUnloadFiles2.setLayoutData(gridHide);

		sShell.pack();
	}

	private void showCmpUnloadFiles2() {
		GridData gridShow = new GridData(GridData.FILL_HORIZONTAL);
		gridShow.horizontalSpan = 2;
		gridShow.heightHint = -1;

		GridData gridHide = new GridData(GridData.FILL_HORIZONTAL);
		gridShow.horizontalSpan = 2;
		gridShow.heightHint = 0;

		cmpUnloadFiles.setLayoutData(gridHide);
		cmpUnloadFiles2.setLayoutData(gridShow);

		sShell.pack();
	}

	private boolean sendLoad() {
		LoadAction.resultMsg = new StringBuffer("");
		StringBuffer msg = new StringBuffer();
		msg.append("dbname:");
		msg.append(txtTargetDb.getText());
		msg.append("\n");

		if (rdoCheckAndLoad.getSelection())
			msg.append("checkoption:both\n");
		else if (rdoLoad.getSelection())
			msg.append("checkoption:load\n");
		else if (rdoCheck.getSelection())
			msg.append("checkoption:syntax\n");

		msg.append("period:");
		msg.append(chkCommitPeriod.getSelection() ? txtCommitPeriod.getText()
				: "none");
		msg.append("\n");

		msg.append("user:");
		msg.append(txtUserName.getText());
		msg.append("\n");

		msg.append("estimated:");
		msg.append(chkEstimatedSize.getSelection() ? txtEstimatedSize.getText()
				: "none");
		msg.append("\n");

		msg.append("oiduse:");
		msg.append(chkNoRef.getSelection() ? "no\n" : "yes\n");
		msg.append("nolog:");
		msg.append(chkNoLog.getSelection() ? "yes\n" : "no\n");

		StringBuffer msgSchemaAndObject = new StringBuffer();
		if (!chkUnloadList.getSelection()) {
			if (chkSchema.getSelection()
					&& txtSchema.getText().trim().length() == 0) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("TOOLTIP.EDITSCHEMA"));
				txtSchema.setFocus();
				return false;
			}
			if (chkData.getSelection()
					&& txtData.getText().trim().length() == 0) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("TOOLTIP.EDITOBJECT"));
				txtData.setFocus();
				return false;
			}
			if (chkIndex.getSelection()
					&& txtIndex.getText().trim().length() == 0) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("OOLTIP.EDITINDEX"));
				txtIndex.setFocus();
				return false;
			}
			if (chkTrigger.getSelection()
					&& txtTrigger.getText().trim().length() == 0) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("TOOLTIP.EDITTRIGGER"));
				txtTrigger.setFocus();
				return false;
			}

			if (chkSchema.getSelection()) {
				msgSchemaAndObject.append("schema:");
				msgSchemaAndObject.append(txtSchema.getText());
				msgSchemaAndObject.append("\n");
			} else
				msgSchemaAndObject.append("schema:none\n");

			if (chkData.getSelection()) {
				msgSchemaAndObject.append("object:");
				msgSchemaAndObject.append(txtData.getText());
				msgSchemaAndObject.append("\n");
			} else
				msgSchemaAndObject.append("object:none\n");

			if (chkIndex.getSelection()) {
				msgSchemaAndObject.append("index:");
				msgSchemaAndObject.append(txtIndex.getText());
				msgSchemaAndObject.append("\n");
			} else
				msgSchemaAndObject.append("index:none\n");
		} else { // Checked (selected from list)
			if (cmbUnloadList.getSelectionIndex() < 1
					|| cmbSchema.getSelectionIndex() < 1
					&& cmbData.getSelectionIndex() < 1
					&& cmbIndex.getSelectionIndex() < 1
					&& cmbTrigger.getSelectionIndex() < 1) {
				CommonTool.ErrorBox(sShell, Messages
						.getString("ERROR.NOUNLOADINFORMATION"));
				return false;
			}

			if (cmbSchema.getSelectionIndex() > 0) {
				msgSchemaAndObject.append("schema:");
				msgSchemaAndObject.append(cmbSchema.getText().split("]")[1]);
				msgSchemaAndObject.append("\n");
			} else
				msgSchemaAndObject.append("schema:none\n");

			if (cmbData.getSelectionIndex() > 0) {
				msgSchemaAndObject.append("object:");
				msgSchemaAndObject.append(cmbData.getText().split("]")[1]);
				msgSchemaAndObject.append("\n");
			} else
				msgSchemaAndObject.append("object:none\n");

			if (cmbIndex.getSelectionIndex() > 0) {
				msgSchemaAndObject.append("index:");
				msgSchemaAndObject.append(cmbIndex.getText().split("]")[1]);
				msgSchemaAndObject.append("\n");
			} else
				msgSchemaAndObject.append("index:none\n");
		}

		ClientSocket cs;
		// schema, object, trigger load
		if (!msgSchemaAndObject.toString().equals(
				"schema:none\nobject:none\nindex:none\n")) {
			cs = new ClientSocket();
			if (!cs.SendBackGround(sShell, msg.toString().concat(
					msgSchemaAndObject.toString()), "loaddb", Messages
					.getString("WAITING.LOADDB"))) {
				CommonTool.ErrorBox(sShell, cs.ErrorMsg);
				return false;
			}

			for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
				LoadAction.resultMsg.append(MainRegistry.Tmpchkrst.get(i)
						.toString().replaceAll("\r",
								cubridmanager.MainConstants.NEW_LINE));
				LoadAction.resultMsg
						.append(cubridmanager.MainConstants.NEW_LINE);
			}
		}

		// trigger load
		StringBuffer msgTrigger = new StringBuffer();

		if (!chkUnloadList.getSelection()) {
			if (chkTrigger.getSelection()) {
				msgTrigger.append("schema:");
				msgTrigger.append(txtTrigger.getText());
				msgTrigger.append("\n");
			} else
				// trigger doesn't exist
				return true;
		} else {
			if (cmbTrigger.getSelectionIndex() > 0) {
				msgTrigger.append("schema:");
				msgTrigger.append(cmbTrigger.getText().split("]")[1]);
				msgTrigger.append("\n");
			} else
				// trigger doesn't exist
				return true;
		}

		msgTrigger.append("object:none\nindex:none\n");

		cs = new ClientSocket();
		if (!cs.SendBackGround(sShell, msg.toString().concat(
				msgTrigger.toString()), "loaddb", Messages
				.getString("WAITING.LOADDB"))) {
			CommonTool.ErrorBox(sShell, cs.ErrorMsg);
			return false;
		}

		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
			LoadAction.resultMsg.append(MainRegistry.Tmpchkrst.get(i)
					.toString().replaceAll("\r",
							cubridmanager.MainConstants.NEW_LINE));
			LoadAction.resultMsg.append(cubridmanager.MainConstants.NEW_LINE);
		}

		return true;
	}
}
