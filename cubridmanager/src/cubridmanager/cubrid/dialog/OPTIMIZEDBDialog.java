package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.action.OptimizeAction;
import cubridmanager.cubrid.SchemaInfo;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class OPTIMIZEDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	// private Label label2 = null;
	private Group group2 = null;
	private Label label3 = null;
	private Text EDIT_OPTIMIZEDB_DBNAME = null;
	private Label label4 = null;
	private Combo COMBO_OPTIMIZEDB_CLASSNAME = null;
	private Text EDIT_OPTIMIZEDB_RESULT = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	private CLabel cLabel = null;

	public OPTIMIZEDBDialog(Shell parent) {
		super(parent);
	}

	public OPTIMIZEDBDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		dlgShell = new Shell(super.getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.OPTIMIZEDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData34 = new org.eclipse.swt.layout.GridData();
		gridData34.grabExcessHorizontalSpace = true;
		GridLayout gridLayout32 = new GridLayout();
		gridLayout32.numColumns = 2;
		// GridData gridData30 = new org.eclipse.swt.layout.GridData();
		// gridData30.widthHint = 360;
		GridData gridData29 = new org.eclipse.swt.layout.GridData();
		gridData29.widthHint = 360;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 80;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 80;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setLayoutData(gridData1);
		group2.setLayout(gridLayout32);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.THEQUERYOPTIMIZER"));
		label1.setLayoutData(gridData29);
		// label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		// label2.setText(Messages.getString("LABEL.WHENACLASSHAS"));
		// label2.setLayoutData(gridData30);
		label3 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.DATABASENAME1"));
		label3.setLayoutData(gridData34);
		GridData gridData33 = new org.eclipse.swt.layout.GridData();
		gridData33.grabExcessHorizontalSpace = true;
		gridData33.horizontalAlignment = GridData.FILL;
		EDIT_OPTIMIZEDB_DBNAME = new Text(group2, SWT.BORDER);
		EDIT_OPTIMIZEDB_DBNAME.setEditable(false);
		EDIT_OPTIMIZEDB_DBNAME.setLayoutData(gridData33);
		label4 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.CLASSNAME"));
		createCombo1();
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 3;
		gridData2.widthHint = 350;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.heightHint = 70;
		EDIT_OPTIMIZEDB_RESULT = new Text(sShell, SWT.BORDER | SWT.MULTI
				| SWT.WRAP);
		EDIT_OPTIMIZEDB_RESULT.setEditable(false);
		EDIT_OPTIMIZEDB_RESULT.setLayoutData(gridData2);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData3);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData4);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ClientSocket cs = new ClientSocket();
						String classname = (COMBO_OPTIMIZEDB_CLASSNAME
								.getSelectionIndex() <= 0) ? ""
								: COMBO_OPTIMIZEDB_CLASSNAME.getText();
						if (cs.SendBackGround(dlgShell, "dbname:"
								+ OptimizeAction.ai.dbname + "\nclassname:"
								+ classname, "optimizedb", Messages
								.getString("WAITING.OPTIMIZEDB"))) {
							EDIT_OPTIMIZEDB_RESULT.append(Messages
									.getString("MSG.OPTIMIZESUCCESS"));
							EDIT_OPTIMIZEDB_RESULT
									.append(COMBO_OPTIMIZEDB_CLASSNAME
											.getText());
							EDIT_OPTIMIZEDB_RESULT.append("\n");
							ret = true;
						} else {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							EDIT_OPTIMIZEDB_RESULT.append(Messages
									.getString("MSG.OPTIMIZEFAIL"));
							EDIT_OPTIMIZEDB_RESULT
									.append(COMBO_OPTIMIZEDB_CLASSNAME
											.getText());
							EDIT_OPTIMIZEDB_RESULT.append(" - ");
							EDIT_OPTIMIZEDB_RESULT.append(cs.ErrorMsg);
							EDIT_OPTIMIZEDB_RESULT.append("\n");
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CLOSE"));
		IDCANCEL.setLayoutData(gridData5);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
		setinfo();
		dlgShell.pack();
	}

	private void createCombo1() {
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.horizontalAlignment = GridData.FILL;
		COMBO_OPTIMIZEDB_CLASSNAME = new Combo(group2, SWT.DROP_DOWN);
		COMBO_OPTIMIZEDB_CLASSNAME.setLayoutData(gridData6);
	}

	private void setinfo() {
		EDIT_OPTIMIZEDB_DBNAME.setText(OptimizeAction.ai.dbname);
		COMBO_OPTIMIZEDB_CLASSNAME.add(Messages.getString("COMBO.ALLCLASSES"),
				0);
		SchemaInfo si;
		for (int i = 0, j = 1, n = OptimizeAction.ai.Schema.size(); i < n; i++) {
			si = (SchemaInfo) OptimizeAction.ai.Schema.get(i);
			if (si.type.equals("system"))
				continue;
			COMBO_OPTIMIZEDB_CLASSNAME.add(si.name, j++);
		}
		COMBO_OPTIMIZEDB_CLASSNAME.select(0);
	}
}
