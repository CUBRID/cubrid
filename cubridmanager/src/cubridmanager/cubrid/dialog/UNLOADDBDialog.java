package cubridmanager.cubrid.dialog;

import java.util.ArrayList;

import org.eclipse.swt.widgets.DirectoryDialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.VerifyDigitListener;
import cubridmanager.WaitingMsgBox;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.action.UnloadAction;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class UNLOADDBDialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="7,5"
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_UNLOADDB_DBNAME = null;
	private Label label2 = null;
	private Text EDIT_UNLOADDB_TARGETDIR = null;
	private Button btnSearch;
	private Group group2 = null;
	private Group grpSchema = null;
	private Button rdoSchemaAll = null;
	private Button rdoSchemaIncludeSelected = null;
	private Button rdoSchemaNotInclude = null;
	private Group grpData = null;
	private Button rdoDataIncludeSelected = null;
	private Button rdoDataNotInclude = null;
	private Table LIST_UNLOADDB_CLASSES = null;
	private Group group1 = null;
	private Button CHECK_UNLOADDB_TMPDIR = null;
	private Text EDIT_UNLOADDB_TMPDIR = null;
	private Button CHECK_OUTPUT_PREFIX = null;
	private Text EDIT_OUTPUT_PREFIX = null;
	private Button CHECK_INCLUDE_REF = null;
	private Button CHECK_DELIMITED_IDENTIFIER = null;
	private Button CHECK_ESTIMATED_SIZE = null;
	private Text EDIT_ESTIMATED_SIZE = null;
	private Button CHECK_CACHED_PAGES = null;
	private Text EDIT_CACHED_PAGES = null;
	private Button CHECK_LO_FILE = null;
	private Text EDIT_LO_FILE = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	public boolean isSchemaOnly = false;
	private boolean ret = false;
	private Composite cmpBtnArea = null;
	private Composite cmpTxtArea;
	private Composite cmpTmpDir;
	private Button btnSearchTmpDir;
	private StringBuffer checkDirStr = new StringBuffer();
	private String target = new String();
	private String ioOptStr = new String();
	private StringBuffer classList = new StringBuffer();

	public UNLOADDBDialog(Shell parent) {
		super(parent);
	}

	public UNLOADDBDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createDlgShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createDlgShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.UNLOADDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createSShell();
	}

	private void createSShell() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;

		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		createCmpTxtArea();

		createGroup1();
		createGroup2();

		createCmpBtnArea();
		setinfo();
		dlgShell.pack();
	}

	private void createCmpTxtArea() {
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		cmpTxtArea = new Composite(sShell, SWT.NONE);
		cmpTxtArea.setLayout(gridLayout);
		cmpTxtArea.setLayoutData(gridData51);

		label1 = new Label(cmpTxtArea, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.TARGETDATABASE"));
		GridData gridData = new GridData();
		gridData.widthHint = 200;
		EDIT_UNLOADDB_DBNAME = new Text(cmpTxtArea, SWT.BORDER);
		EDIT_UNLOADDB_DBNAME.setEditable(false);
		EDIT_UNLOADDB_DBNAME.setLayoutData(gridData);

		new Label(cmpTxtArea, SWT.NONE);

		label2 = new Label(cmpTxtArea, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.TARGETDIRECTORY"));
		EDIT_UNLOADDB_TARGETDIR = new Text(cmpTxtArea, SWT.BORDER);
		EDIT_UNLOADDB_TARGETDIR.setLayoutData(gridData);

		btnSearch = new Button(cmpTxtArea, SWT.NONE);
		btnSearch.setText(Messages.getString("BUTTON.OPENFILE"));
		btnSearch
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DirectoryDialog dirDlg = new DirectoryDialog(dlgShell);
						dirDlg.setMessage(Messages
								.getString("STRING.DIRSELECT"));
						String dir = dirDlg.open();
						if (dir != null)
							EDIT_UNLOADDB_TARGETDIR.setText(dir);
					}
				});
	}

	private void createGroup1() {
		// Group 1 control
		GridData gridDataGrp = new GridData();
		gridDataGrp.verticalAlignment = GridData.FILL;
		gridDataGrp.widthHint = 300;
		gridDataGrp.verticalSpan = 2;
		GridLayout gridLayoutGrp1 = new GridLayout();
		gridLayoutGrp1.numColumns = 2;
		gridLayoutGrp1.verticalSpacing = 0;
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.UNLOADOPTION"));
		group1.setLayout(gridLayoutGrp1);
		group1.setLayoutData(gridDataGrp);

		GridData gridDataGrp1 = new GridData();
		gridDataGrp1.horizontalSpan = 2;
		gridDataGrp1.horizontalAlignment = GridData.FILL;
		CHECK_UNLOADDB_TMPDIR = new Button(group1, SWT.CHECK);
		CHECK_UNLOADDB_TMPDIR.setText(Messages.getString("CHECK.USETEMPORARY"));
		CHECK_UNLOADDB_TMPDIR.setLayoutData(gridDataGrp1);
		CHECK_UNLOADDB_TMPDIR.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean enabled = CHECK_UNLOADDB_TMPDIR.getSelection();
				EDIT_UNLOADDB_TMPDIR.setEnabled(enabled);
				btnSearchTmpDir.setEnabled(enabled);
			}
		});

		createCmpTmpDir();

		GridData gridDataText = new GridData();
		gridDataText.horizontalAlignment = GridData.FILL;
		CHECK_OUTPUT_PREFIX = new Button(group1, SWT.CHECK);
		CHECK_OUTPUT_PREFIX.setText(Messages.getString("CHECK.OUTPUTPREFIX"));
		CHECK_OUTPUT_PREFIX.setLayoutData(new GridData(GridData.GRAB_VERTICAL));
		CHECK_OUTPUT_PREFIX.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean isChecked = CHECK_OUTPUT_PREFIX.getSelection();
				EDIT_OUTPUT_PREFIX.setEnabled(isChecked);
				if (!isChecked)
					EDIT_OUTPUT_PREFIX.setText("");
				else
					EDIT_OUTPUT_PREFIX.setText(UnloadAction.ai.dbname);
			}
		});
		EDIT_OUTPUT_PREFIX = new Text(group1, SWT.BORDER);
		EDIT_OUTPUT_PREFIX.setLayoutData(gridDataText);
		GridData gridHoriSpan = new GridData(GridData.GRAB_VERTICAL);
		gridHoriSpan.horizontalSpan = 2;
		CHECK_INCLUDE_REF = new Button(group1, SWT.CHECK);
		CHECK_INCLUDE_REF.setText(Messages.getString("CHECK.INCLUDEREF"));
		CHECK_INCLUDE_REF.setLayoutData(gridHoriSpan);
		CHECK_DELIMITED_IDENTIFIER = new Button(group1, SWT.CHECK);
		CHECK_DELIMITED_IDENTIFIER.setText(Messages
				.getString("CHECK.DELIMITED"));
		CHECK_DELIMITED_IDENTIFIER.setLayoutData(gridHoriSpan);
		CHECK_ESTIMATED_SIZE = new Button(group1, SWT.CHECK);
		CHECK_ESTIMATED_SIZE.setText(Messages.getString("CHECK.ESTIMATEDSIZE"));
		CHECK_ESTIMATED_SIZE
				.setLayoutData(new GridData(GridData.GRAB_VERTICAL));
		CHECK_ESTIMATED_SIZE.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean isChecked = CHECK_ESTIMATED_SIZE.getSelection();
				EDIT_ESTIMATED_SIZE.setEnabled(isChecked);
				if (!isChecked)
					EDIT_ESTIMATED_SIZE.setText("");
			}
		});
		EDIT_ESTIMATED_SIZE = new Text(group1, SWT.BORDER);
		EDIT_ESTIMATED_SIZE.setLayoutData(gridDataText);
		EDIT_ESTIMATED_SIZE.addListener(SWT.Verify, new VerifyDigitListener());
		CHECK_CACHED_PAGES = new Button(group1, SWT.CHECK);
		CHECK_CACHED_PAGES.setText(Messages.getString("CHECK.CACHEDPAGES"));
		CHECK_CACHED_PAGES.setLayoutData(new GridData(GridData.GRAB_VERTICAL));
		CHECK_CACHED_PAGES.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean isChecked = CHECK_CACHED_PAGES.getSelection();
				EDIT_CACHED_PAGES.setEnabled(isChecked);
				if (!isChecked)
					EDIT_CACHED_PAGES.setText("");
				else
					EDIT_CACHED_PAGES.setText("100");
			}
		});
		EDIT_CACHED_PAGES = new Text(group1, SWT.BORDER);
		EDIT_CACHED_PAGES.setLayoutData(gridDataText);
		EDIT_CACHED_PAGES.addListener(SWT.Verify, new VerifyDigitListener());
		CHECK_LO_FILE = new Button(group1, SWT.CHECK);
		CHECK_LO_FILE.setText(Messages.getString("CHECK.LOFILE"));
		CHECK_LO_FILE.setLayoutData(new GridData(GridData.GRAB_VERTICAL));
		CHECK_LO_FILE.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				boolean isChecked = CHECK_LO_FILE.getSelection();
				EDIT_LO_FILE.setEnabled(isChecked);
				if (!isChecked)
					EDIT_LO_FILE.setText("");
				else
					EDIT_LO_FILE.setText("0");
			}
		});
		EDIT_LO_FILE = new Text(group1, SWT.BORDER);
		EDIT_LO_FILE.setLayoutData(gridDataText);
		EDIT_LO_FILE.addListener(SWT.Verify, new VerifyDigitListener());
	}

	private void createCmpTmpDir() {
		GridData gridDataTmpDir = new GridData();
		gridDataTmpDir.grabExcessHorizontalSpace = true;
		gridDataTmpDir.horizontalSpan = 2;
		gridDataTmpDir.horizontalAlignment = GridData.FILL;
		gridDataTmpDir.verticalAlignment = GridData.CENTER;

		cmpTmpDir = new Composite(group1, SWT.NONE);
		GridLayout gridLayout2 = new GridLayout(2, false);
		gridLayout2.marginWidth = 0;
		gridLayout2.marginHeight = 0;
		cmpTmpDir.setLayoutData(gridDataTmpDir);
		cmpTmpDir.setLayout(gridLayout2);

		GridData gridTxtTmpDir = new GridData(GridData.FILL_HORIZONTAL);
		gridTxtTmpDir.horizontalIndent = 20;
		EDIT_UNLOADDB_TMPDIR = new Text(cmpTmpDir, SWT.BORDER);
		EDIT_UNLOADDB_TMPDIR.setLayoutData(gridTxtTmpDir);

		btnSearchTmpDir = new Button(cmpTmpDir, SWT.NONE);
		btnSearchTmpDir.setText(Messages.getString("BUTTON.OPENFILE"));
		btnSearchTmpDir
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DirectoryDialog dirDlg = new DirectoryDialog(dlgShell);
						dirDlg.setMessage(Messages
								.getString("STRING.DIRSELECT"));
						String dir = dirDlg.open();
						if (dir != null)
							EDIT_UNLOADDB_TMPDIR.setText(dir);
					}
				});
	}

	private void createGroup2() {
		// Group2 control
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		GridData gridDataGrp2 = new GridData(GridData.HORIZONTAL_ALIGN_FILL);
		group2 = new Group(sShell, SWT.NONE);
		group2.setLayoutData(gridDataGrp2);
		group2.setText(Messages.getString("GROUP.UNLOADTARGET"));

		createGrpSchema();
		createGrpData();
		group2.setLayout(gridLayout1);
		createTable1();

	}

	private void createTable1() {
		GridData gridData5 = new GridData(GridData.FILL_HORIZONTAL);
		gridData5.heightHint = 80;
		gridData5.horizontalSpan = 2;
		LIST_UNLOADDB_CLASSES = new Table(group2, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER | SWT.CHECK);
		LIST_UNLOADDB_CLASSES.setLinesVisible(true);
		LIST_UNLOADDB_CLASSES.setLayoutData(gridData5);
		LIST_UNLOADDB_CLASSES
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						TableItem item = (TableItem) e.item;
						if (e.detail == SWT.CHECK)
							LIST_UNLOADDB_CLASSES
									.setSelection(new TableItem[] { item });

						if (LIST_UNLOADDB_CLASSES.getSelectionIndex() == 0) {
							for (int i = 1, n = LIST_UNLOADDB_CLASSES
									.getItemCount(); i < n; i++)
								LIST_UNLOADDB_CLASSES.getItem(i).setChecked(
										item.getChecked());
						}
						setBtnEnable();
					}
				});
		new TableColumn(LIST_UNLOADDB_CLASSES, SWT.LEFT);
		// TableColumn tblcol = new TableColumn(LIST_UNLOADDB_CLASSES, SWT.LEFT
		// | SWT.CHECK);
		// tblcol.setText(Messages.getString("TABLE.CLASS"));

	}

	/**
	 * This method initializes grpSchema
	 * 
	 */
	private void createGrpSchema() {
		GridData gridData1 = new GridData(GridData.FILL_HORIZONTAL);
		grpSchema = new Group(group2, SWT.NONE);
		grpSchema.setLayout(new GridLayout());
		grpSchema.setLayoutData(gridData1);
		grpSchema.setText(Messages.getString("GROUP.SCHEMA"));

		rdoSchemaAll = new Button(grpSchema, SWT.RADIO);
		rdoSchemaAll.setText(Messages.getString("RADIO.SCHEMAALL"));
		rdoSchemaAll.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		rdoSchemaAll.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
		rdoSchemaIncludeSelected = new Button(grpSchema, SWT.RADIO);
		rdoSchemaIncludeSelected.setText(Messages
				.getString("RADIO.SCHEMASELECTED"));
		rdoSchemaIncludeSelected.setLayoutData(new GridData(
				GridData.FILL_HORIZONTAL));
		rdoSchemaIncludeSelected.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
		rdoSchemaNotInclude = new Button(grpSchema, SWT.RADIO);
		rdoSchemaNotInclude.setText(Messages.getString("RADIO.SCHEMANO"));
		rdoSchemaNotInclude
				.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		rdoSchemaNotInclude.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
	}

	/**
	 * This method initializes grpData
	 * 
	 */
	private void createGrpData() {
		GridData gridData2 = new GridData(GridData.FILL_BOTH);
		grpData = new Group(group2, SWT.NONE);
		grpData.setLayout(new GridLayout());
		grpData.setLayoutData(gridData2);
		grpData.setText(Messages.getString("GROUP.DATA"));

		rdoDataIncludeSelected = new Button(grpData, SWT.RADIO);
		rdoDataIncludeSelected
				.setText(Messages.getString("RADIO.DATASELECTED"));
		rdoDataIncludeSelected.setLayoutData(new GridData(GridData.FILL_BOTH));
		rdoDataIncludeSelected.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
		rdoDataNotInclude = new Button(grpData, SWT.RADIO);
		rdoDataNotInclude.setText(Messages.getString("RADIO.DATANO"));
		rdoDataNotInclude.setLayoutData(new GridData(GridData.FILL_BOTH));
		rdoDataNotInclude.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				setBtnEnable();
			}
		});
	}

	private void createCmpBtnArea() {
		GridData gridCmpBtnArea = new GridData(GridData.FILL_HORIZONTAL);
		gridCmpBtnArea.horizontalSpan = 2;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayout(new GridLayout(2, false));
		cmpBtnArea.setLayoutData(gridCmpBtnArea);

		GridData gridDataBtn = new GridData();
		gridDataBtn.widthHint = 75;
		gridDataBtn.horizontalAlignment = GridData.END;
		gridDataBtn.grabExcessHorizontalSpace = true;
		IDOK = new Button(cmpBtnArea, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridDataBtn);
		IDOK.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				String target = EDIT_UNLOADDB_TARGETDIR.getText();
				if (target == null || target.length() <= 0
						|| target.indexOf(" ") >= 0) {
					CommonTool.ErrorBox(dlgShell, Messages
							.getString("ERROR.INVALIDTARGETDIRECTORY"));
					return;
				}
				if (CHECK_UNLOADDB_TMPDIR.getSelection()) {
					String tmpdir = EDIT_UNLOADDB_TMPDIR.getText();
					if (tmpdir == null || tmpdir.length() <= 0
							|| tmpdir.indexOf(" ") >= 0) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.INVALIDHASHFILEDIRECTORY"));
						return;
					}
				}
				if (CHECK_ESTIMATED_SIZE.getSelection()
						&& EDIT_ESTIMATED_SIZE.getText().length() == 0) {
					CommonTool.ErrorBox(dlgShell, Messages
							.getString("ERROR.INVALIDEVALUE"));
					EDIT_ESTIMATED_SIZE.setFocus();
					return;
				}
				if (CHECK_OUTPUT_PREFIX.getSelection()) {
					String prefix = EDIT_OUTPUT_PREFIX.getText();
					if (prefix == null || prefix.length() == 0
							|| prefix.indexOf(" ") >= 0) {
						CommonTool.ErrorBox(dlgShell, Messages
								.getString("ERROR.INVALIDEVALUE"));
						EDIT_OUTPUT_PREFIX.setFocus();
						return;
					}
				}
				if (CHECK_CACHED_PAGES.getSelection()
						&& EDIT_CACHED_PAGES.getText().length() == 0) {
					CommonTool.ErrorBox(dlgShell, Messages
							.getString("ERROR.INVALIDEVALUE"));
					EDIT_CACHED_PAGES.setFocus();
					return;
				}
				if (CHECK_LO_FILE.getSelection()
						&& EDIT_LO_FILE.getText().length() == 0) {
					CommonTool.ErrorBox(dlgShell, Messages
							.getString("ERROR.INVALIDEVALUE"));
					EDIT_LO_FILE.setFocus();
					return;
				}

				setTarget();

				ClientSocket cs = new ClientSocket();
				if (CheckDir(cs)) {
					dlgShell.update();
					cs = new ClientSocket();
					if (CheckFiles(cs)) {
						dlgShell.update();
						cs = new ClientSocket();
						if (SendUnload(cs)) {
							ret = true;
							dlgShell.dispose();
						} else
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
					} else
						CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
				} else
					CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			}
		});
		gridDataBtn = new GridData();
		gridDataBtn.widthHint = 75;
		gridDataBtn.horizontalAlignment = GridData.END;
		IDCANCEL = new Button(cmpBtnArea, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridDataBtn);
		IDCANCEL.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				ret = false;
				dlgShell.dispose();
			}
		});
	}

	private void setinfo() {
		EDIT_UNLOADDB_DBNAME.setText(UnloadAction.ai.dbname);
		EDIT_UNLOADDB_TARGETDIR.setText(UnloadAction.ai.dbdir);
		CHECK_UNLOADDB_TMPDIR.setSelection(false);
		EDIT_UNLOADDB_TMPDIR.setEnabled(false);
		EDIT_UNLOADDB_TMPDIR.setText(UnloadAction.ai.dbdir + "/hashfile");
		btnSearchTmpDir.setEnabled(false);

		EDIT_OUTPUT_PREFIX.setEnabled(false);
		EDIT_ESTIMATED_SIZE.setEnabled(false);
		EDIT_CACHED_PAGES.setEnabled(false);
		EDIT_LO_FILE.setEnabled(false);

		ArrayList sinfo = SchemaInfo.SchemaInfo_get(UnloadAction.ai.dbname);
		SchemaInfo si;
		int usercnt = 0;
		for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
			si = (SchemaInfo) sinfo.get(ai);
			if (si.type.equals("system"))
				continue;
			TableItem item = new TableItem(LIST_UNLOADDB_CLASSES, SWT.NONE);
			item.setText(0, si.name);
			usercnt++;
		}
		rdoSchemaAll.setSelection(true);
		rdoDataIncludeSelected.setSelection(true);
		if (usercnt > 0) {
			TableItem selectAll = new TableItem(LIST_UNLOADDB_CLASSES,
					SWT.NONE, 0);
			selectAll.setText(Messages.getString("BUTTON.SELECTALL"));
		}

		EDIT_UNLOADDB_TMPDIR.setToolTipText(Messages
				.getString("TOOLTIP.EDITTMPDIR"));
		CHECK_UNLOADDB_TMPDIR.setToolTipText(Messages
				.getString("TOOLTIP.CHECKUSETMP"));
		EDIT_UNLOADDB_TARGETDIR.setToolTipText(Messages
				.getString("TOOLTIP.EDITTARGETDIR"));
		EDIT_UNLOADDB_DBNAME.setToolTipText(Messages
				.getString("TOOLTIP.EDITDBNAME"));
		EDIT_OUTPUT_PREFIX.setToolTipText(Messages
				.getString("TOOLTIP.OUTPUTPREFIX"));
		CHECK_INCLUDE_REF.setToolTipText(Messages
				.getString("TOOLTIP.INCLUDEREF"));
		CHECK_DELIMITED_IDENTIFIER.setToolTipText(Messages
				.getString("TOOLTIP.DELIMITED"));
		CHECK_ESTIMATED_SIZE.setToolTipText(Messages
				.getString("TOOLTIP.ESTIMATEDSIZE2"));
		EDIT_ESTIMATED_SIZE.setToolTipText(Messages
				.getString("TOOLTIP.ESTIMATEDSIZE2"));
		CHECK_CACHED_PAGES.setToolTipText(Messages
				.getString("TOOLTIP.CACHEDPAGES"));
		EDIT_CACHED_PAGES.setToolTipText(Messages
				.getString("TOOLTIP.CACHEDPAGES"));
		CHECK_LO_FILE.setToolTipText(Messages.getString("TOOLTIP.LOFILE"));
		EDIT_LO_FILE.setToolTipText(Messages.getString("TOOLTIP.LOFILE"));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 30, true));
		LIST_UNLOADDB_CLASSES.setLayout(tlayout);

		setBtnEnable();
	}

	private boolean CheckDir(ClientSocket cs) {
		if (cs.Connect()) {
			if (cs.Send(dlgShell, "dir:" + EDIT_UNLOADDB_TARGETDIR.getText(),
					"checkdir")) {
				WaitingMsgBox wdlg = new WaitingMsgBox(dlgShell);
				wdlg.run(Messages.getString("WAITING.CHECKINGDIRECTORY"));
				if (cs.ErrorMsg != null)
					return false;
			} else
				return false;
		} else
			return false;

		return true;
	}

	private boolean CheckFiles(ClientSocket cs) {
		if (CHECK_UNLOADDB_TMPDIR.getSelection()) {
			String tmp = EDIT_UNLOADDB_TMPDIR.getText().concat("/");
			tmp = tmp.replaceAll("//", "/");

			checkDirStr.append("file:");
			checkDirStr.append(tmp);
			checkDirStr.append("\n");
		}

		if (cs.Connect()) {
			if (cs.Send(dlgShell, checkDirStr.toString(), "checkfile")) {
				WaitingMsgBox wdlg = new WaitingMsgBox(dlgShell);
				wdlg.run(Messages.getString("WAITING.CHECKINGFILES"));
				if (cs.ErrorMsg != null)
					return false;
			} else
				return false;
		} else
			return false;

		return true;
	}

	private boolean SendUnload(ClientSocket cs) {
		String requestMsg = "";
		requestMsg += "dbname:" + EDIT_UNLOADDB_DBNAME.getText() + "\n";
		requestMsg += "targetdir:" + EDIT_UNLOADDB_TARGETDIR.getText() + "\n";

		if (CHECK_UNLOADDB_TMPDIR.getSelection()) {
			requestMsg += "usehash:yes\n";
			requestMsg += "hashdir:" + EDIT_UNLOADDB_TMPDIR.getText() + "\n";
		} else {
			requestMsg += "usehash:no\n";
			requestMsg += "hashdir:none\n";
		}

		requestMsg += target;
		if (classList.length() > 0)
			requestMsg += classList.toString();

		if (CHECK_INCLUDE_REF.getSelection())
			requestMsg += "ref:yes\n";
		else
			requestMsg += "ref:no\n";

		requestMsg += ioOptStr;

		if (CHECK_DELIMITED_IDENTIFIER.getSelection())
			requestMsg += "delimit:yes\n";
		else
			requestMsg += "delimit:no\n";

		if (CHECK_ESTIMATED_SIZE.getSelection())
			requestMsg += "estimate:" + EDIT_ESTIMATED_SIZE.getText() + "\n";
		else
			requestMsg += "estimate:none\n";

		if (CHECK_OUTPUT_PREFIX.getSelection())
			requestMsg += "prefix:" + EDIT_OUTPUT_PREFIX.getText() + "\n";
		else
			requestMsg += "prefix:none\n";

		if (CHECK_CACHED_PAGES.getSelection())
			requestMsg += "cach:" + EDIT_CACHED_PAGES.getText() + "\n";
		else
			requestMsg += "cach:none\n";

		if (CHECK_LO_FILE.getSelection())
			requestMsg += "lofile:" + EDIT_LO_FILE.getText() + "\n";
		else
			requestMsg += "lofile:none\n";

		if (cs.Connect()) {
			if (cs.Send(dlgShell, requestMsg, "unloaddb")) {
				WaitingMsgBox wdlg = new WaitingMsgBox(dlgShell);
				wdlg.run(Messages.getString("WAITING.UNLOAD"));
				if (cs.ErrorMsg != null)
					return false;
			} else
				return false;
		} else
			return false;

		return true;
	}

	private void setTarget() {
		String prefix = EDIT_OUTPUT_PREFIX.getText().equals("") ? EDIT_UNLOADDB_DBNAME
				.getText()
				: EDIT_OUTPUT_PREFIX.getText();

		String targetdir = EDIT_UNLOADDB_TARGETDIR.getText() + "/";
		targetdir = targetdir.replaceAll("//", "/");

		target = "";
		ioOptStr = "";
		classList = new StringBuffer();
		isSchemaOnly = false;

		if (rdoSchemaAll.getSelection()) {
			checkDirStr.append("file:");
			checkDirStr.append(targetdir);
			checkDirStr.append(prefix);
			checkDirStr.append("_schema\n");

			checkDirStr.append("file:");
			checkDirStr.append(targetdir);
			checkDirStr.append(prefix);
			checkDirStr.append("_indexes\n");

			checkDirStr.append("file:");
			checkDirStr.append(targetdir);
			checkDirStr.append(prefix);
			checkDirStr.append("_trigger\n");

			ioOptStr = "classonly:no\n";
			if (rdoDataIncludeSelected.getSelection()) {
				if (isSelectedAllClasses() == 1) {
					checkDirStr.append("file:");
					checkDirStr.append(targetdir);
					checkDirStr.append(prefix);
					checkDirStr.append("_objects\n");

					target = "target:both\n";
				} else if (isSelectedAllClasses() == 0) {
					isSchemaOnly = true;
					target = "target:schema\n";
				} else if (isSelectedAllClasses() == -1) {
					checkDirStr.append("file:");
					checkDirStr.append(targetdir);
					checkDirStr.append(prefix);
					checkDirStr.append("_objects\n");

					target = "target:both\n";
					makeClassList();
				}
			} else {
				isSchemaOnly = true;
				target = "target:schema\n";
			}
		} else if (rdoSchemaIncludeSelected.getSelection()) {
			checkDirStr.append("file:");
			checkDirStr.append(targetdir);
			checkDirStr.append(prefix);
			checkDirStr.append("_schema\n");

			checkDirStr.append("file:");
			checkDirStr.append(targetdir);
			checkDirStr.append(prefix);
			checkDirStr.append("_indexes\n");

			checkDirStr.append("file:");
			checkDirStr.append(targetdir);
			checkDirStr.append(prefix);
			checkDirStr.append("_trigger\n");

			ioOptStr = "classonly:yes\n";
			makeClassList();
			if (rdoDataIncludeSelected.getSelection()) {
				checkDirStr.append("file:");
				checkDirStr.append(targetdir);
				checkDirStr.append(prefix);
				checkDirStr.append("_objects\n");

				target = "target:both\n";
			} else {
				isSchemaOnly = true;
				target = "target:schema\n";
			}
		} else if (rdoSchemaNotInclude.getSelection()) {
			ioOptStr = "classonly:yes\n";
			if (rdoDataIncludeSelected.getSelection()) {
				checkDirStr.append("file:");
				checkDirStr.append(targetdir);
				checkDirStr.append(prefix);
				checkDirStr.append("_objects\n");

				target = "target:object\n";
				makeClassList();
			}
		}
	}

	private void setBtnEnable() {
		if (LIST_UNLOADDB_CLASSES.getItemCount() < 2) {
			rdoSchemaAll.setSelection(true);
			rdoSchemaIncludeSelected.setEnabled(false);
			rdoSchemaNotInclude.setEnabled(false);
			rdoDataIncludeSelected.setSelection(true);
			rdoDataIncludeSelected.setEnabled(true);
			rdoDataNotInclude.setEnabled(false);
			LIST_UNLOADDB_CLASSES.setEnabled(false);
			IDOK.setEnabled(false);
			return;
		}

		if (rdoSchemaAll.getSelection()) {
			if (rdoDataIncludeSelected.getSelection()) {
				LIST_UNLOADDB_CLASSES.setEnabled(true);
				IDOK.setEnabled(true);
			} else {
				LIST_UNLOADDB_CLASSES.setEnabled(false);
				IDOK.setEnabled(true);
			}
		} else if (rdoSchemaIncludeSelected.getSelection()) {
			if (rdoDataIncludeSelected.getSelection()) {
				LIST_UNLOADDB_CLASSES.setEnabled(true);
				if (isSelectedAllClasses() == 0)
					IDOK.setEnabled(false);
				else
					IDOK.setEnabled(true);
			} else {
				LIST_UNLOADDB_CLASSES.setEnabled(true);
				if (isSelectedAllClasses() == 0)
					IDOK.setEnabled(false);
				else
					IDOK.setEnabled(true);
			}
		} else if (rdoSchemaNotInclude.getSelection()) {
			if (rdoDataIncludeSelected.getSelection()) {
				LIST_UNLOADDB_CLASSES.setEnabled(true);
				if (isSelectedAllClasses() == 0)
					IDOK.setEnabled(false);
				else
					IDOK.setEnabled(true);
			} else {
				LIST_UNLOADDB_CLASSES.setEnabled(false);
				IDOK.setEnabled(false);
			}
		}
	}

	/**
	 * Class is selected?
	 * 
	 * @return -1: something selected, 0: nothing selected, 1: all selected
	 */
	private byte isSelectedAllClasses() {
		if (LIST_UNLOADDB_CLASSES.getItemCount() < 2)
			return 0;
		int checked = 0, n = LIST_UNLOADDB_CLASSES.getItemCount(), i;
		for (i = 1; i < n; i++) {
			if (LIST_UNLOADDB_CLASSES.getItem(i).getChecked())
				checked++;
			if (checked > 0 && !LIST_UNLOADDB_CLASSES.getItem(i).getChecked())
				return -1;
		}

		if (checked == 0)
			return 0;
		else if (checked == (i - 1))
			return 1;
		else
			return -1;
	}

	private void makeClassList() {
		classList.append("open:class\n");
		for (int i = 1, n = LIST_UNLOADDB_CLASSES.getItemCount(); i < n; i++) {
			TableItem ti = LIST_UNLOADDB_CLASSES.getItem(i);
			if (ti.getChecked()) {
				classList.append("classname:");
				classList.append(ti.getText(0));
				classList.append("\n");
			}
		}
		classList.append("close:class\n");
	}
}
