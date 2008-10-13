package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ComboBoxCellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Label;
import org.eclipse.jface.wizard.WizardPage;

import cubridmanager.cas.CASItem;
import cubridmanager.cas.action.SetParameterAction;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE2_BROKER_OFFSETDialog extends WizardPage {
	public static final String PAGE_NAME = "PROPPAGE2_BROKER_OFFSETDialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	public static Table LIST_BROKER_OFFSET_LIST1 = null;
	private Group group2 = null;
	private Label label1 = null;
	private static int oldidx = -1;
	public static final String[] PROPS = new String[2];
	public static final String[] onoffstr = { "ON", "OFF" };
	CellEditor[] editors = new CellEditor[3];
	public static boolean[] comboflag = null;
	TableViewer tv = null;
	MultiEditCellModifier cellact = null;

	public PROPPAGE2_BROKER_OFFSETDialog() {
		super(PAGE_NAME, Messages
				.getString("TITLE.PROPPAGE2_BROKER_OFFSETDIALOG"), null);
	}

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(true);
		setinfo();
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
		dlgShell.setText(Messages
				.getString("TITLE.PROPPAGE2_BROKER_OFFSETDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData34 = new org.eclipse.swt.layout.GridData();
		gridData34.heightHint = 100;
		gridData34.grabExcessVerticalSpace = true;
		gridData34.grabExcessHorizontalSpace = true;
		gridData34.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData34.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData34.widthHint = 400;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessVerticalSpace = true;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(new GridLayout());
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.ADVANCEDOPTION"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.DESCRIPTION"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData1);
		createTable1();
		label1 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label1.setLayoutData(gridData34);
		sShell.pack();
	}

	private void createTable1() {
		tv = new TableViewer(group1, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.V_SCROLL);
		LIST_BROKER_OFFSET_LIST1 = tv.getTable();
		LIST_BROKER_OFFSET_LIST1.setLinesVisible(true);
		LIST_BROKER_OFFSET_LIST1.setHeaderVisible(true);
		GridData gridData34 = new org.eclipse.swt.layout.GridData();
		gridData34.heightHint = 200;
		gridData34.grabExcessVerticalSpace = true;
		gridData34.grabExcessHorizontalSpace = true;
		gridData34.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData34.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData34.widthHint = 400;
		LIST_BROKER_OFFSET_LIST1.setLayoutData(gridData34);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_BROKER_OFFSET_LIST1.setLayout(tlayout);

		LIST_BROKER_OFFSET_LIST1
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int curidx = LIST_BROKER_OFFSET_LIST1
								.getSelectionIndex();
						if (curidx >= 0 && curidx != oldidx) {
							oldidx = curidx;
							TableItem item = (TableItem) e.item;
							if (comboflag[curidx]) {
								editors[1] = editors[2];
								if (item.getText(0).equals("KEEP_CONNECTION"))
									((ComboBoxCellEditor) editors[1])
											.setItems(new String[] { "AUTO",
													"ON", "OFF" });
								else
									((ComboBoxCellEditor) editors[1])
											.setItems(onoffstr);
								tv.setCellEditors(editors);
								tv.setCellModifier(cellact);
							} else {
								editors[1] = editors[0];
								tv.setCellEditors(editors);
								tv.setCellModifier(cellact);
							}
							String lbl = LIST_BROKER_OFFSET_LIST1.getItem(
									curidx).getText(0);
							String tiptxt = "TOOLTIP.PARA_" + lbl;
							label1.setText(Messages.getString(tiptxt));
						}
					}
				});
		TableColumn tblcol = new TableColumn(LIST_BROKER_OFFSET_LIST1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PARAMETER"));
		tblcol = new TableColumn(LIST_BROKER_OFFSET_LIST1, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VALUE"));

		editors[0] = new TextCellEditor(LIST_BROKER_OFFSET_LIST1);
		editors[1] = new ComboBoxCellEditor(LIST_BROKER_OFFSET_LIST1, onoffstr,
				SWT.READ_ONLY);
		editors[2] = editors[1];
		for (int i = 0; i < 2; i++) {
			PROPS[i] = LIST_BROKER_OFFSET_LIST1.getColumn(i).getText();
		}
		tv.setColumnProperties(PROPS);
		tv.setCellEditors(editors);
		cellact = new MultiEditCellModifier(tv);
	}

	private void setinfo() {
		comboflag = new boolean[SetParameterAction.bpi.paraname.size()];
		for (int j = 0, jn = SetParameterAction.bpi.paraname.size(); j < jn; j++) {
			TableItem item = new TableItem(LIST_BROKER_OFFSET_LIST1, SWT.NONE);
			String lbl = (String) SetParameterAction.bpi.paraname.get(j);
			item.setText(0, lbl);
			item.setText(1, (String) SetParameterAction.bpi.paraval.get(j));
			if (lbl.equals("ACCESS_LOG") || lbl.equals("AUTO_ADD_APPL_SERVER")
					|| lbl.equals("LOG_BACKUP") || lbl.equals("SQL_LOG")
					|| lbl.equals("SESSION") || lbl.equals("SERVICE")
					|| lbl.equals("ENC_APPL") || lbl.equals("OID_CHECK")
					|| lbl.equals("ENTRY_VALUE_TRIM")
					|| lbl.equals("STRIPPED_COLUMN_NAME")
					|| lbl.equals("KEEP_CONNECTION")) {
				comboflag[j] = true;
			} else
				comboflag[j] = false;
		}
		for (int i = 0, n = LIST_BROKER_OFFSET_LIST1.getColumnCount(); i < n; i++) {
			LIST_BROKER_OFFSET_LIST1.getColumn(i).pack();
		}
	}
}

class MultiEditCellModifier implements ICellModifier {
	Table tbl = null;

	TableViewer celltv = null;

	Shell tvshell = null;

	int idx = 0;

	public MultiEditCellModifier(Viewer viewer) {
		tbl = ((TableViewer) viewer).getTable();
		celltv = (TableViewer) viewer;
		tvshell = tbl.getShell();
	}

	public boolean canModify(Object element, String property) {
		if (property == null)
			return false;
		if (PROPPAGE2_BROKER_OFFSETDialog.PROPS[0].equals(property))
			return false;
		idx = tbl.getSelectionIndex();
		CellEditor[] ce = celltv.getCellEditors();
		if (PROPPAGE2_BROKER_OFFSETDialog.comboflag[idx]) {
			if (ce[1] instanceof TextCellEditor)
				return false;
		} else {
			if (ce[1] instanceof ComboBoxCellEditor)
				return false;
		}
		return true;
	}

	public Object getValue(Object element, String property) {
		idx = tbl.getSelectionIndex();
		TableItem[] tis = tbl.getSelection();
		if (PROPPAGE2_BROKER_OFFSETDialog.comboflag[idx]) {
			if (tis[0].getText(0).equals("KEEP_CONNECTION")) {
				if (tis[0].getText(1).equals("AUTO"))
					return new Integer(0);
				else if (tis[0].getText(1).equals("ON"))
					return new Integer(1);
				else
					return new Integer(2);
			} else
				return (tis[0].getText(1).equals("ON")) ? new Integer(0)
						: new Integer(1);
		} else {
			return tis[0].getText(1);
		}
	}

	public void modify(Object element, String property, Object value) {
		if (value == null)
			return;
		String lbl = ((TableItem) element).getText(0);
		if (PROPPAGE2_BROKER_OFFSETDialog.comboflag[idx]) {
			if (lbl.equals("KEEP_CONNECTION")) {
				int iKeepConnectionValue = ((Integer) value).intValue();
				if (iKeepConnectionValue == 0)
					((TableItem) element).setText(1, "AUTO");
				else if (iKeepConnectionValue == 1)
					((TableItem) element).setText(1, "ON");
				else
					((TableItem) element).setText(1, "OFF");
			} else {
				String yn = (((Integer) value).intValue() == 0) ? "ON" : "OFF";
				((TableItem) element).setText(1, yn);
			}
		} else {
			// check logic
			String chkval = ((String) value).trim();
			if (chkval.length() <= 0 || chkval.equals("Not Specified")) {
				if (lbl.equals("ACCESS_LIST") || lbl.equals("SOURCE_ENV")) {
					((TableItem) element).setText(1, "Not Specified");
				}
				// others skip new value
				return;
			}
			if (lbl.equals("ACCESS_LIST") || lbl.equals("SOURCE_ENV")) {
				for (int i = 0; i < chkval.length(); i++) {
					if (!Character.isLetterOrDigit(chkval.charAt(i))) {
						if (chkval.charAt(i) == '.' || chkval.charAt(i) == '_')
							continue;
						CommonTool.ErrorBox(tvshell, Messages
								.getString("ERROR.INVALIDFILENAME")
								+ " " + lbl);
						((TableItem) element).setText(1, "Not Specified");
						return;
					}
				}
			}
			if (CheckValid(lbl, chkval))
				((TableItem) element).setText(1, (String) value);
		}
		((TableItem) element).getParent().redraw();
	}

	boolean CheckValid(String name, String value) {
		String err = null;
		if (name.equals("BROKER_PORT")) {
			if (IsInteger(value)) {
				int newport = CommonTool.atoi(value);
				CASItem casrec;
				for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
					casrec = (CASItem) MainRegistry.CASinfo.get(i);
					if (casrec.broker_port == newport) {
						err = Messages.getString("ERROR.BROKERPORTEXIST");
						break;
					}
				}
				if (err != null)
					return true;
			} else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("MIN_NUM_APPL_SERVER")
				|| name.equals("MAX_NUM_APPL_SERVER")
				|| name.equals("APPL_SERVER_SHM_ID")
				|| name.equals("APPL_SERVER_MAX_SIZE")
				|| name.equals("COMPRESS_SIZE") || name.equals("TIME_TO_KILL")
				|| name.equals("PRIORITY_GAP")) {
			if (IsInteger(value))
				return true;
			else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("SQL_LOG_MAX_SIZE")) {
			int size = CommonTool.atoi(value);
			if (size > 0 && size < 2000001)
				return true;
			else
				err = Messages.getString("ERROR.INVALIDSQLMAXSIZE");
		} else if (name.equals("SESSION_TIMEOUT")
				|| name.equals("SQL_LOG_TIME")
				|| name.equals("MAX_STRING_LENGTH")) {
			if (value.equals("-1"))
				return true;
			if (IsInteger(value))
				return true;
			else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("SOURCE_ENV")) {
			if (!value.endsWith(".env"))
				err = Messages.getString("ERROR.WRONGENVFILENAME");
			else
				return true;
		} else if (name.equals("JOB_QUEUE_SIZE")) {
			if (IsInteger(value)) {
				int var = CommonTool.atoi(value);
				if (var > 127)
					err = Messages.getString("ERROR.JOBQUEUEMAXOVER");
				else
					return true;
			} else
				err = Messages.getString("ERROR.ISNOTINTEGER");
		} else if (name.equals("FILE_UPLOAD_DELIMITER")) {
			if (IsInteger(value))
				err = Messages.getString("ERROR.ISNOTSTRING");
			else
				return true;
		} else if (name.equals("LOG_DIR") || name.equals("APPL_ROOT")
				|| name.equals("FILE_UPLOAD_TEMP_DIR")
				|| name.equals("ACCESS_LIST"))
			return true;

		CommonTool.ErrorBox(tvshell, err);
		return false;
	}

	boolean IsInteger(String str) {
		for (int i = 0; i < str.length(); i++) {
			if (!Character.isDigit(str.charAt(i)))
				return false;
		}
		return true;
	}

}
