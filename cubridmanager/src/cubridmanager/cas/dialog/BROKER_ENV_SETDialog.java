package cubridmanager.cas.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cas.view.CASView;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.GroupMarker;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.viewers.CellEditor;
import org.eclipse.jface.viewers.ICellModifier;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.jface.viewers.TextCellEditor;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class BROKER_ENV_SETDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	public static Table LIST_BROKER_ENV_SET = null;
	private Group group2 = null;
	private Label label1 = null;
	private Text EDIT_BROKER_ENVSET_PARAM = null;
	private Label label2 = null;
	private Text EDIT_BROKER_ENVSET_PARAMVAL = null;
	private Button BUTTON_BROKER_ENV_SET = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	public static final String[] PROPS = new String[2];

	public static DelParaAction delaction = null;

	public BROKER_ENV_SETDialog(Shell parent) {
		super(parent);
		delaction = new DelParaAction(Messages.getString("POPUP.REMOVEPARA"));
	}

	public BROKER_ENV_SETDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(BUTTON_BROKER_ENV_SET);
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
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TOOL.SETSOURCEENVACTION"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData35 = new org.eclipse.swt.layout.GridData();
		gridData35.verticalSpan = 2;
		gridData35.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData35.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData35.widthHint = 90;
		GridData gridData34 = new org.eclipse.swt.layout.GridData();
		gridData34.widthHint = 140;
		GridData gridData33 = new org.eclipse.swt.layout.GridData();
		gridData33.widthHint = 140;
		GridLayout gridLayout32 = new GridLayout();
		gridLayout32.numColumns = 3;
		GridLayout gridLayout33 = new GridLayout();
		gridLayout33.numColumns = 1;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 90;
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData2.widthHint = 90;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.BROKERENVIRONMENT"));
		group1.setLayout(gridLayout33);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.ADDINGPARAMETER"));
		group2.setLayout(gridLayout32);
		group2.setLayoutData(gridData1);
		createTable1();
		label1 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.PARAMETER"));
		EDIT_BROKER_ENVSET_PARAM = new Text(group2, SWT.BORDER);
		EDIT_BROKER_ENVSET_PARAM.setLayoutData(gridData33);
		BUTTON_BROKER_ENV_SET = new Button(group2, SWT.NONE);
		BUTTON_BROKER_ENV_SET.setText(Messages.getString("BUTTON.ADD"));
		BUTTON_BROKER_ENV_SET.setLayoutData(gridData35);
		BUTTON_BROKER_ENV_SET
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String name = EDIT_BROKER_ENVSET_PARAM.getText().trim();
						String val = EDIT_BROKER_ENVSET_PARAMVAL.getText()
								.trim();
						if (name.length() <= 0)
							return;
						TableItem item = new TableItem(LIST_BROKER_ENV_SET,
								SWT.NONE);
						item.setText(0, name);
						item.setText(1, val);
						EDIT_BROKER_ENVSET_PARAM.setText("");
						EDIT_BROKER_ENVSET_PARAMVAL.setText("");
					}
				});
		label2 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.VALUE"));
		EDIT_BROKER_ENVSET_PARAMVAL = new Text(group2, SWT.BORDER);
		EDIT_BROKER_ENVSET_PARAMVAL.setLayoutData(gridData34);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData2);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg;
						msg = "bname:" + CASView.Current_broker + "\n";
						msg += "open:param\n";

						for (int i = 0, n = LIST_BROKER_ENV_SET.getItemCount(); i < n; i++) {
							String name = LIST_BROKER_ENV_SET.getItem(i)
									.getText(0).trim();
							String val = LIST_BROKER_ENV_SET.getItem(i)
									.getText(1).trim();
							if (name.length() <= 0)
								continue;
							msg += name += ":" + val + "\n";
						}
						msg += "close:param\n";
						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(dlgShell, msg,
								"setbrokerenvinfo", Messages
										.getString("WAITING.SETBROKERENV"))) {
							CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
							return;
						}
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
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	private void createTable1() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.widthHint = 330;
		gridData1.heightHint = 245;
		final TableViewer tv = new TableViewer(group1, SWT.FULL_SELECTION
				| SWT.BORDER);
		LIST_BROKER_ENV_SET = tv.getTable();
		LIST_BROKER_ENV_SET.setLinesVisible(true);
		LIST_BROKER_ENV_SET.setHeaderVisible(true);
		LIST_BROKER_ENV_SET.setLayoutData(gridData1);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_BROKER_ENV_SET.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_BROKER_ENV_SET, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.PARAMETER"));
		tblcol = new TableColumn(LIST_BROKER_ENV_SET, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.VALUE"));

		CellEditor[] editors = new CellEditor[2];
		editors[0] = new TextCellEditor(LIST_BROKER_ENV_SET);
		editors[1] = new TextCellEditor(LIST_BROKER_ENV_SET);
		for (int i = 0; i < 2; i++) {
			PROPS[i] = LIST_BROKER_ENV_SET.getColumn(i).getText();
		}
		tv.setColumnProperties(PROPS);
		tv.setCellEditors(editors);
		tv.setCellModifier(new ArgCellModifier(tv));
		hookContextMenu(tv.getControl());
	}

	public static void hookContextMenu(Control popctrl) {
		MenuManager menuMgr = new MenuManager("proxyMenu", "proxycontextMenu");
		menuMgr.setRemoveAllWhenShown(true);
		menuMgr.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				if (LIST_BROKER_ENV_SET.getSelectionIndex() < 0)
					return;
				manager.add(new Separator());
				GroupMarker marker = new GroupMarker(
						IWorkbenchActionConstants.MB_ADDITIONS);
				manager.add(marker);
				manager.add(delaction);
			}
		});
		Menu menu = menuMgr.createContextMenu(popctrl);
		MenuItem newContextMenuItem = new MenuItem(menu, SWT.NONE);
		newContextMenuItem.setText("proxycontext.item");
		popctrl.setMenu(menu);
	}

	private void setinfo() {
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i += 2) {
			TableItem item = new TableItem(LIST_BROKER_ENV_SET, SWT.NONE);
			item.setText(0, (String) MainRegistry.Tmpchkrst.get(i));
			item.setText(1, (String) MainRegistry.Tmpchkrst.get(i + 1));
		}
		for (int i = 0, n = LIST_BROKER_ENV_SET.getColumnCount(); i < n; i++) {
			LIST_BROKER_ENV_SET.getColumn(i).pack();
		}
	}
}

class ArgCellModifier implements ICellModifier {
	Table tbl = null;

	public ArgCellModifier(Viewer viewer) {
		tbl = ((TableViewer) viewer).getTable();
	}

	public boolean canModify(Object element, String property) {
		return true;
	}

	public Object getValue(Object element, String property) {
		TableItem[] tis = tbl.getSelection();
		if (BROKER_ENV_SETDialog.PROPS[0].equals(property))
			return tis[0].getText(0);
		else
			return tis[0].getText(1);
	}

	public void modify(Object element, String property, Object value) {
		if (BROKER_ENV_SETDialog.PROPS[0].equals(property))
			((TableItem) element).setText(0, (String) value);
		else
			((TableItem) element).setText(1, (String) value);
		((TableItem) element).getParent().redraw();
	}
}

class DelParaAction extends Action {
	public DelParaAction(String text) {
		super(text);
		setId("DelParaAction");
		setToolTipText(text);
	}

	public void run() {
		int idx = BROKER_ENV_SETDialog.LIST_BROKER_ENV_SET.getSelectionIndex();
		if (idx < 0)
			return;
		BROKER_ENV_SETDialog.LIST_BROKER_ENV_SET.remove(idx);
		BROKER_ENV_SETDialog.LIST_BROKER_ENV_SET.redraw();
	}
}