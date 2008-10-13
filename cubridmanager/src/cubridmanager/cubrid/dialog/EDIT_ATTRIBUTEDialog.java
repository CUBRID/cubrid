package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE1Dialog;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.RowLayout;

public class EDIT_ATTRIBUTEDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Group group1 = null;
	private Label label2 = null;
	private Button RADIO_ATTRIBUTE_EDIT_CATEGORY_CLASS = null;
	private Button RADIO_ATTRIBUTE_EDIT_CATEGORY_INSTANCE = null;
	private Label label3 = null;
	private Text EDIT_ATTRIBUTE_EDIT_NAME = null;
	private Label label4 = null;
	private Text EDIT_ATTRIBUTE_EDIT_TYPE = null;
	private Label label5 = null;
	private Button RADIO_ATTRIBUTE_EDIT_SHARED_YES = null;
	private Button RADIO_ATTRIBUTE_EDIT_SHARED_NO = null;
	private Label label6 = null;
	private Button RADIO_ATTRIBUTE_EDIT_INDEXED_YES = null;
	private Button RADIO_ATTRIBUTE_EDIT_INDEXED_NO = null;
	private Label label7 = null;
	private Button RADIO_ATTRIBUTE_EDIT_NOTNULL_YES = null;
	private Button RADIO_ATTRIBUTE_EDIT_NOTNULL_NO = null;
	private Label label8 = null;
	private Button RADIO_ATTRIBUTE_EDIT_UNIQUE_YES = null;
	private Button RADIO_ATTRIBUTE_EDIT_UNIQUE_NO = null;
	private Label label9 = null;
	private Text EDIT_ATTRIBUTE_EDIT_DEFAULT = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Label label10 = null;
	private Label label11 = null;
	private Label label12 = null;
	private Label label13 = null;
	private boolean ret = false;
	private TableItem ti = null;

	public EDIT_ATTRIBUTEDialog(Shell parent) {
		super(parent);
	}

	public EDIT_ATTRIBUTEDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal(TableItem item) {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		ti = item;

		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.EDIT_ATTRIBUTEDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.horizontalSpan = 2;
		gridData31.horizontalIndent = 25;
		GridData gridData30 = new org.eclipse.swt.layout.GridData();
		gridData30.horizontalSpan = 2;
		gridData30.horizontalIndent = 30;
		GridData gridData29 = new org.eclipse.swt.layout.GridData();
		gridData29.horizontalSpan = 2;
		gridData29.horizontalIndent = 30;
		GridData gridData28 = new org.eclipse.swt.layout.GridData();
		gridData28.horizontalSpan = 2;
		GridData gridData27 = new org.eclipse.swt.layout.GridData();
		gridData27.widthHint = 166;
		GridData gridData26 = new org.eclipse.swt.layout.GridData();
		gridData26.widthHint = 166;
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.grabExcessHorizontalSpace = false;
		gridData25.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData25.widthHint = 166;
		gridData25.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridLayout gridLayout24 = new GridLayout();
		gridLayout24.numColumns = 2;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.widthHint = 100;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.widthHint = 100;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.EDITATTRIBUTE"));
		label1.setLayoutData(gridData);
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout24);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.TYPE"));
		Composite comp1 = new Composite(group1, SWT.NULL);
		comp1.setLayout(new RowLayout());
		RADIO_ATTRIBUTE_EDIT_CATEGORY_CLASS = new Button(comp1, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_CATEGORY_CLASS.setText(Messages
				.getString("RADIO.CLASS"));
		RADIO_ATTRIBUTE_EDIT_CATEGORY_INSTANCE = new Button(comp1, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_CATEGORY_INSTANCE.setText(Messages
				.getString("RADIO.INSTANCE"));
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.NAME"));
		EDIT_ATTRIBUTE_EDIT_NAME = new Text(group1, SWT.BORDER);
		EDIT_ATTRIBUTE_EDIT_NAME.setLayoutData(gridData25);
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.DOMAIN"));
		EDIT_ATTRIBUTE_EDIT_TYPE = new Text(group1, SWT.BORDER);
		EDIT_ATTRIBUTE_EDIT_TYPE.setLayoutData(gridData26);
		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.SHAREDATTRIBUTE"));
		Composite comp2 = new Composite(group1, SWT.NULL);
		comp2.setLayout(new RowLayout());
		RADIO_ATTRIBUTE_EDIT_SHARED_YES = new Button(comp2, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_SHARED_YES
				.setText(Messages.getString("RADIO.YES"));
		RADIO_ATTRIBUTE_EDIT_SHARED_NO = new Button(comp2, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_SHARED_NO.setText(Messages.getString("RADIO.NO"));
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.INDEXED"));
		Composite comp3 = new Composite(group1, SWT.NULL);
		comp3.setLayout(new RowLayout());
		RADIO_ATTRIBUTE_EDIT_INDEXED_YES = new Button(comp3, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_INDEXED_YES.setText(Messages
				.getString("RADIO.YES"));
		RADIO_ATTRIBUTE_EDIT_INDEXED_NO = new Button(comp3, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_INDEXED_NO.setText(Messages.getString("RADIO.NO"));
		label8 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label8.setText(Messages.getString("LABEL.UNIQUE"));
		Composite comp5 = new Composite(group1, SWT.NULL);
		comp5.setLayout(new RowLayout());
		RADIO_ATTRIBUTE_EDIT_UNIQUE_YES = new Button(comp5, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_UNIQUE_YES
				.setText(Messages.getString("RADIO.YES"));
		RADIO_ATTRIBUTE_EDIT_UNIQUE_NO = new Button(comp5, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_UNIQUE_NO.setText(Messages.getString("RADIO.NO"));
		label7 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.NOTNULLCONSTRAINT"));
		Composite comp4 = new Composite(group1, SWT.NULL);
		comp4.setLayout(new RowLayout());
		RADIO_ATTRIBUTE_EDIT_NOTNULL_YES = new Button(comp4, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_NOTNULL_YES.setText(Messages
				.getString("RADIO.YES"));
		RADIO_ATTRIBUTE_EDIT_NOTNULL_NO = new Button(comp4, SWT.RADIO);
		RADIO_ATTRIBUTE_EDIT_NOTNULL_NO.setText(Messages.getString("RADIO.NO"));
		label9 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label9.setText(Messages.getString("LABEL.DEFAULTVALUE"));
		EDIT_ATTRIBUTE_EDIT_DEFAULT = new Text(group1, SWT.BORDER);
		EDIT_ATTRIBUTE_EDIT_DEFAULT.setLayoutData(gridData27);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String attname = EDIT_ATTRIBUTE_EDIT_NAME.getText()
								.trim();
						String attdeft = EDIT_ATTRIBUTE_EDIT_DEFAULT.getText();
						String retstr = CommonTool
								.ValidateCheckInIdentifier(attname);
						if (retstr.length() > 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDATTRNAME"));
							return;
						}

						if (attdeft.length() <= 0 && ti.getText(6).length() > 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.CANNOTDELDEFAULT"));
							return;
						}
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:"
								+ PROPPAGE_CLASS_PAGE1Dialog.si.name + "\n";
						msg += "oldattributename:" + ti.getText(0) + "\n";
						msg += "newattributename:" + attname + "\n";
						msg += "category:"
								+ ((RADIO_ATTRIBUTE_EDIT_CATEGORY_INSTANCE
										.getSelection()) ? "instance" : "class")
								+ "\n";
						msg += "index:"
								+ (RADIO_ATTRIBUTE_EDIT_INDEXED_YES
										.getSelection() ? "y" : "n") + "\n";
						msg += "notnull:"
								+ (RADIO_ATTRIBUTE_EDIT_NOTNULL_YES
										.getSelection() ? "y" : "n") + "\n";
						msg += "unique:"
								+ (RADIO_ATTRIBUTE_EDIT_UNIQUE_YES
										.getSelection() ? "y" : "n") + "\n";
						msg += "default:" + attdeft + "\n";

						ClientSocket cs = new ClientSocket();

						if (!cs.SendBackGround(dlgShell, msg,
								"updateattribute", Messages
										.getString("WAITING.EDITATTR"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						ret = true;
						dlgShell.dispose();
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData3);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		label10 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label10.setText(Messages.getString("LABEL.NOTEALLTYPES"));
		label10.setLayoutData(gridData28);
		label11 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label11.setText(Messages.getString("LABEL.INDEFAULTVALUE"));
		label11.setLayoutData(gridData29);
		label13 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label13.setText(Messages.getString("LABEL.INSINGLEQUOTES"));
		label13.setLayoutData(gridData30);
		label12 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label12.setText(Messages.getString("LABEL.EXABCDEFABC"));
		label12.setLayoutData(gridData31);
		dlgShell.pack();
	}

	private void setinfo() {
		if (ti.getText(8).length() > 0)
			RADIO_ATTRIBUTE_EDIT_CATEGORY_CLASS.setSelection(true);
		else
			RADIO_ATTRIBUTE_EDIT_CATEGORY_INSTANCE.setSelection(true);
		RADIO_ATTRIBUTE_EDIT_CATEGORY_CLASS.setEnabled(false);
		RADIO_ATTRIBUTE_EDIT_CATEGORY_INSTANCE.setEnabled(false);
		EDIT_ATTRIBUTE_EDIT_NAME.setText(ti.getText(0));
		EDIT_ATTRIBUTE_EDIT_TYPE.setText(ti.getText(1));
		EDIT_ATTRIBUTE_EDIT_TYPE.setEnabled(false);
		// shared
		if (ti.getText(4).length() > 0)
			RADIO_ATTRIBUTE_EDIT_SHARED_YES.setSelection(true);
		else
			RADIO_ATTRIBUTE_EDIT_SHARED_NO.setSelection(true);
		RADIO_ATTRIBUTE_EDIT_SHARED_YES.setEnabled(false);
		RADIO_ATTRIBUTE_EDIT_SHARED_NO.setEnabled(false);

		if (ti.getText(2).length() > 0)
			RADIO_ATTRIBUTE_EDIT_INDEXED_YES.setSelection(true);
		else
			RADIO_ATTRIBUTE_EDIT_INDEXED_NO.setSelection(true);
		// unique
		if (ti.getText(5).length() > 0)
			RADIO_ATTRIBUTE_EDIT_UNIQUE_YES.setSelection(true);
		else
			RADIO_ATTRIBUTE_EDIT_UNIQUE_NO.setSelection(true);
		if (ti.getText(8).length() > 0
				|| PROPPAGE_CLASS_PAGE1Dialog.si.virtual.equals("view")) {
			RADIO_ATTRIBUTE_EDIT_UNIQUE_YES.setEnabled(false);
			RADIO_ATTRIBUTE_EDIT_UNIQUE_NO.setEnabled(false);
		}
		if (ti.getText(8).length() > 0) {
			RADIO_ATTRIBUTE_EDIT_INDEXED_YES.setEnabled(false);
			RADIO_ATTRIBUTE_EDIT_INDEXED_NO.setEnabled(false);
		}

		// not null
		if (ti.getText(3).length() > 0)
			RADIO_ATTRIBUTE_EDIT_NOTNULL_YES.setSelection(true);
		else
			RADIO_ATTRIBUTE_EDIT_NOTNULL_NO.setSelection(true);
		if (PROPPAGE_CLASS_PAGE1Dialog.si.virtual.equals("view")) {
			RADIO_ATTRIBUTE_EDIT_NOTNULL_YES.setEnabled(false);
			RADIO_ATTRIBUTE_EDIT_NOTNULL_NO.setEnabled(false);
		}
		EDIT_ATTRIBUTE_EDIT_DEFAULT.setText(ti.getText(6));
	}
}
