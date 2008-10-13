package cubridmanager.diag.dialog;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Table;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagCASLogTopConfigDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="29,9"
	private Group groupTargetFile = null;
	private Button buttonOK = null;
	private Button buttonCancel = null;
	private Button checkTransactionBasedLogTop = null;
	private Table tableTargetFile = null;
	public ArrayList selectedStringList = null;
	public ArrayList targetStringList = new ArrayList();
	public boolean option_t = false;

	public DiagCASLogTopConfigDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagCASLogTopConfigDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 60;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 60;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 130;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 3;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessHorizontalSpace = false;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		createGroupTargetFile();
		sShell.setLayout(gridLayout);
		checkTransactionBasedLogTop = new Button(sShell, SWT.CHECK);
		checkTransactionBasedLogTop.setText(Messages
				.getString("CHECK.ANALIZEOPTION_T"));
		checkTransactionBasedLogTop.setLayoutData(gridData1);
		Label label = new Label(sShell, SWT.NONE);
		label.setLayoutData(gridData2);
		buttonOK = new Button(sShell, SWT.NONE);
		buttonOK.setText(Messages.getString("BUTTON.OK"));
		buttonOK.setLayoutData(gridData4);
		buttonOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selectioncount = tableTargetFile
								.getSelectionCount();
						if (selectioncount == 0) {
							if (targetStringList.size() == 1)
								tableTargetFile.setSelection(0);
							else {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.SELECTTARGETFILE"));
								return;
							}
						}

						DiagEndCode = Window.OK;
						selectedStringList = new ArrayList();
						for (int j = 0; j < selectioncount; j++) {
							String filepath = tableTargetFile.getSelection()[j]
									.getText(0);
							selectedStringList.add(filepath);
						}
						option_t = checkTransactionBasedLogTop.getSelection();
						sShell.dispose();
					}
				});
		buttonCancel = new Button(sShell, SWT.NONE);
		buttonCancel.setText(Messages.getString("BUTTON.CANCEL"));
		buttonCancel.setLayoutData(gridData5);
		buttonCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.CANCEL;
						sShell.dispose();
					}
				});
		sShell.pack();
	}

	/**
	 * This method initializes groupTargetFile
	 * 
	 */
	private void createGroupTargetFile() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		groupTargetFile = new Group(sShell, SWT.NONE);
		groupTargetFile.setText(Messages.getString("LABEL.TARGETFILELIST"));
		groupTargetFile.setLayout(new GridLayout());
		groupTargetFile.setLayoutData(gridData);
		createTableTargetFile();
	}

	/**
	 * This method initializes tableTargetFile
	 * 
	 */
	private void createTableTargetFile() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.heightHint = 220;
		gridData3.widthHint = 240;
		tableTargetFile = new Table(groupTargetFile, SWT.MULTI
				| SWT.FULL_SELECTION);
		tableTargetFile.setHeaderVisible(true);
		tableTargetFile.setLayoutData(gridData3);
		tableTargetFile.setLinesVisible(true);
		// tableTargetFile.setBounds(new
		// org.eclipse.swt.graphics.Rectangle(8,18,318,196));
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(100, 100, true));
		TableColumn qindex = new TableColumn(tableTargetFile, SWT.LEFT);
		qindex.setText(Messages.getString("LABEL.LOGFILE"));
		qindex.setWidth(600);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();
		Display display = sShell.getDisplay();

		sShell.setText(Messages.getString("TITLE.SELECTTARGETFILE"));
		tableTargetFile.removeAll();
		for (int i = 0, n = targetStringList.size(); i < n; i++) {
			TableItem item = new TableItem(tableTargetFile, SWT.NONE);
			item.setText(0, (String) targetStringList.get(i));
		}

		if (targetStringList.size() == 1) {
			tableTargetFile.setSelection(0);
		}
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return DiagEndCode;
	}

}
