package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.SuperClass;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.dialog.PROPPAGE_CLASS_PAGE2Dialog;

import java.util.ArrayList;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class ADD_RESOLUTIONDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label2 = null;
	private Button RADIO_ADD_RESOLUTION_CATEGORY_CLASS = null;
	private Button RADIO_ADD_RESOLUTION_CATEGORY_INSTANCE = null;
	private Label label3 = null;
	private Combo COMBO_ADD_RESOLUTION_SUPER = null;
	private Label label4 = null;
	private Combo COMBO_ADD_RESOLUTION_NAME = null;
	private Label label5 = null;
	private Text EDIT_ADD_RESOLUTION_ALIAS = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private Label label6 = null;
	private boolean ret = false;

	public ADD_RESOLUTIONDialog(Shell parent) {
		super(parent);
	}

	public ADD_RESOLUTIONDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

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
		dlgShell.setText(Messages.getString("TITLE.ADD_RESOLUTIONDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData33 = new org.eclipse.swt.layout.GridData();
		gridData33.horizontalSpan = 2;
		gridData33.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData33.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData33.widthHint = 184;
		GridData gridData32 = new org.eclipse.swt.layout.GridData();
		gridData32.horizontalSpan = 3;
		GridData gridData28 = new org.eclipse.swt.layout.GridData();
		gridData28.grabExcessHorizontalSpace = true;

		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);

		GridLayout gridLayout31 = new GridLayout();
		gridLayout31.numColumns = 3;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group1 = new Group(sShell, SWT.NONE);
		group1.setLayoutData(gridData1);
		group1.setLayout(gridLayout31);

		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.ISCLASSMEMBER"));
		RADIO_ADD_RESOLUTION_CATEGORY_CLASS = new Button(group1, SWT.RADIO);
		RADIO_ADD_RESOLUTION_CATEGORY_CLASS.setText(Messages
				.getString("RADIO.CLASS"));
		RADIO_ADD_RESOLUTION_CATEGORY_CLASS
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setattr();
					}
				});
		RADIO_ADD_RESOLUTION_CATEGORY_INSTANCE = new Button(group1, SWT.RADIO);
		RADIO_ADD_RESOLUTION_CATEGORY_INSTANCE.setText(Messages
				.getString("RADIO.INSTANCE"));
		RADIO_ADD_RESOLUTION_CATEGORY_INSTANCE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setattr();
					}
				});
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.SUPERCLASS"));
		createCombo1();
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.FROMWHICHTHE"));
		label6.setLayoutData(gridData32);
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.ATTRIBUTEORMETHOD"));
		createCombo2();
		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.ALIASNAMEOPTIONAL"));
		EDIT_ADD_RESOLUTION_ALIAS = new Text(group1, SWT.BORDER);
		EDIT_ADD_RESOLUTION_ALIAS.setLayoutData(gridData33);

		GridData gridData29 = new org.eclipse.swt.layout.GridData(
				GridData.GRAB_HORIZONTAL);
		gridData29.widthHint = 75;
		gridData29.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData29);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String classname = COMBO_ADD_RESOLUTION_SUPER.getText();
						String attrmeth = COMBO_ADD_RESOLUTION_NAME.getText();
						if (classname.length() <= 0 || attrmeth.length() <= 0) {
							CommonTool
									.ErrorBox(
											dlgShell,
											Messages
													.getString("ERROR.SELECTSUPERCLASSANDATTR"));
							return;
						}
						String alias = EDIT_ADD_RESOLUTION_ALIAS.getText()
								.trim();
						if (alias.indexOf(" ") >= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INVALIDALIASNAME"));
							return;
						}

						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "classname:"
								+ PROPPAGE_CLASS_PAGE2Dialog.si.name + "\n";
						msg += "super:" + classname + "\n";
						msg += "name:" + attrmeth + "\n";
						msg += "alias:" + alias + "\n";
						msg += "category:"
								+ ((RADIO_ADD_RESOLUTION_CATEGORY_CLASS
										.getSelection()) ? "class" : "instance");

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(dlgShell, msg, "addresolution",
								Messages.getString("WAITING.ADDRESOLUTION"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
						ret = true;
						dlgShell.dispose();
					}
				});

		GridData gridData30 = new org.eclipse.swt.layout.GridData(
				GridData.GRAB_HORIZONTAL);
		gridData30.widthHint = 75;
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData30);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createCombo1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.widthHint = 165;
		COMBO_ADD_RESOLUTION_SUPER = new Combo(group1, SWT.DROP_DOWN
				| SWT.READ_ONLY);
		COMBO_ADD_RESOLUTION_SUPER.setLayoutData(gridData2);
		COMBO_ADD_RESOLUTION_SUPER
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						setattr();
					}
				});
	}

	private void createCombo2() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 2;
		gridData3.widthHint = 165;
		COMBO_ADD_RESOLUTION_NAME = new Combo(group1, SWT.DROP_DOWN
				| SWT.READ_ONLY);
		COMBO_ADD_RESOLUTION_NAME.setLayoutData(gridData3);
	}

	private void setinfo() {
		RADIO_ADD_RESOLUTION_CATEGORY_CLASS.setSelection(false);
		RADIO_ADD_RESOLUTION_CATEGORY_INSTANCE.setSelection(true);
		RADIO_ADD_RESOLUTION_CATEGORY_INSTANCE.setFocus();
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
			COMBO_ADD_RESOLUTION_SUPER.add(((SuperClass) MainRegistry.Tmpchkrst
					.get(i)).name);
		}
	}

	private void setattr() {
		boolean isclass = RADIO_ADD_RESOLUTION_CATEGORY_CLASS.getSelection();
		String classname = COMBO_ADD_RESOLUTION_SUPER.getText();
		if (classname.length() <= 0)
			return;
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i++) {
			SuperClass sc = (SuperClass) MainRegistry.Tmpchkrst.get(i);
			if (classname.equals(sc.name)) {
				ArrayList attr = null;
				ArrayList meth = null;
				if (isclass) {
					attr = sc.classAttributes;
					meth = sc.classMethods;
				} else {
					attr = sc.attributes;
					meth = sc.methods;
				}
				COMBO_ADD_RESOLUTION_NAME.removeAll();
				for (int ai = 0, an = attr.size(); ai < an; ai++) {
					COMBO_ADD_RESOLUTION_NAME.add((String) attr.get(ai));
				}
				for (int ai = 0, an = meth.size(); ai < an; ai++) {
					COMBO_ADD_RESOLUTION_NAME.add((String) meth.get(ai));
				}
				break;
			}
		}
	}
}
