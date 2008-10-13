package cubridmanager.cas.dialog;

import java.io.BufferedReader;
import java.io.StringReader;
import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.cubrid.ParameterItem;
import cubridmanager.cubrid.Parameters;
import cubridmanager.cubrid.SqlxInitParameters;
import cubridmanager.ClientSocket;
import cubridmanager.ColumnSelectionSortListener;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Label;

public class BROKER_EDITORDialog extends Dialog {
	private Shell dlgShell = null; // @jve:decl-index=0:visual-constraint="10,51"
	private Composite sShell = null;
	private Composite cmpBtnArea = null;
	private Button IDCANCEL = null;
	public static String Current_select = new String(""); 
	public static TableItem Current_row = null;
	private Button btnEdit = null;
	private Text textArea = null;
	public BROKER_EDITORDialog(Shell parent) {
		super(parent);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(btnEdit);
		dlgShell.open();
		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return 0;
	}

	private void createSShell() {
		//dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages
				.getString("TITLE.PROPPAGE1_BROKER_OFFSETDIALOG"));
		dlgShell.setLayout(new FillLayout());
		dlgShell.setSize(new org.eclipse.swt.graphics.Point(480,512));
		createComposite();
	}

	private void createComposite() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(new GridLayout());
		textArea = new Text(sShell, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL | SWT.BORDER);
		textArea.setLayoutData(gridData3);
		createCmpBtnArea();
	}

	private void setinfo() {
		textArea.setText(MainRegistry.BrokerConf);
	}

	/**
	 * This method initializes cmpBtnArea
	 * 
	 */
	private void createCmpBtnArea() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		GridData gridData2 = new GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayoutData(gridData2);
		cmpBtnArea.setLayout(gridLayout);

		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData.widthHint = 75;
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		btnEdit = new Button(cmpBtnArea, SWT.NONE);
		btnEdit.setText(Messages.getString("BUTTON.EDIT"));
		btnEdit.setLayoutData(gridData);
		btnEdit
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String strbuf = null;
						String msg = new String("");
						try {
							BufferedReader br = new BufferedReader(
									new StringReader(textArea.getText()));
							while ((strbuf = br.readLine()) != null) {
								msg += "confdata:" + strbuf + "\n";
							}
						} catch (Exception ee) {

						}
						
						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(
									dlgShell,
									msg,
									"broker_setparam",
									Messages.getString("WAITING.SETBROKERCONF"))) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
						}
						CommonTool.MsgBox(dlgShell, Messages.getString("MSG.SUCCESS"), Messages.getString("MSG.AFTERRESTARTCAS"));
						dlgShell.dispose();
					}						
				});
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.widthHint = 75;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		IDCANCEL = new Button(cmpBtnArea, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CLOSE"));
		IDCANCEL.setLayoutData(gridData1);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						dlgShell.dispose();
					}
				});
	}
}

