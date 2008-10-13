package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;

import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.TabFolder;
import cubridmanager.cubrid.action.TablePropertyAction;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.SchemaInfo;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class PROPPAGE_CLASS_PAGE4Dialog extends Dialog {
	private Shell dlgShell = null;
	private Composite comparent = null;
	private Composite sShell = null;
	private Table LIST_CLASS_QUERYSPECS = null;
	private Text EDIT_CLASS_SELECTEDQUERYSPECS = null;
	private Button BUTTON_CLASS_QUERY_ADD = null;
	private Button BUTTON_CLASS_QUERY_DELETE = null;
	private Button BUTTON_CLASS_QUERY_EDIT = null;
	private Button BUTTON_CLASS_QUERY_VALIDATE = null;
	private Group group1 = null;
	private Button BUTTON_CLASS_VCLASS_VALIDATE = null;
	private Label label1 = null;
	private Label label2 = null;
	public static TableItem CurrentLIST_CLASS_QUERYSPECS = null;
	public static int CurrentLIST_CLASS_QUERYSPECSidx = 0;
	public static boolean isadd = true;
	public static SchemaInfo si = TablePropertyAction.si;

	public PROPPAGE_CLASS_PAGE4Dialog(Shell parent) {
		super(parent);
	}

	public PROPPAGE_CLASS_PAGE4Dialog(Shell parent, int style) {
		super(parent, style);
	}

	public Composite SetTabPart(TabFolder parent) {
		comparent = parent;
		createComposite();
		setinfo();
		sShell.setParent(parent);
		return sShell;
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
		dlgShell
				.setText(Messages.getString("TITLE.PROPPAGE_CLASS_PAGE4DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData60 = new org.eclipse.swt.layout.GridData();
		gridData60.widthHint = 500;
		gridData60.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData60.grabExcessHorizontalSpace = true;
		gridData60.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalSpan = 4;
		gridData8.grabExcessVerticalSpace = true;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData8.grabExcessHorizontalSpace = true;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.widthHint = 90;
		gridData7.grabExcessHorizontalSpace = true;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.widthHint = 90;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 90;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.grabExcessHorizontalSpace = true;
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData4.widthHint = 90;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 2;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.heightHint = 260;
		gridData3.widthHint = 270;
		gridData3.grabExcessHorizontalSpace = true;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 2;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(gridLayout);
		label2 = new Label(sShell, SWT.CENTER | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.QUERYSPECIFICATION"));
		label2.setLayoutData(gridData);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.SELECTEDQUERY"));
		label1.setLayoutData(gridData1);
		createTable1();
		EDIT_CLASS_SELECTEDQUERYSPECS = new Text(sShell, SWT.BORDER | SWT.MULTI
				| SWT.WRAP);
		EDIT_CLASS_SELECTEDQUERYSPECS.setEditable(false);
		EDIT_CLASS_SELECTEDQUERYSPECS.setLayoutData(gridData3);
		BUTTON_CLASS_QUERY_ADD = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_QUERY_ADD.setText(Messages.getString("BUTTON.ADD1"));
		BUTTON_CLASS_QUERY_ADD.setLayoutData(gridData4);
		BUTTON_CLASS_QUERY_ADD
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						isadd = true;
						ADD_QUERYDialog dlg = new ADD_QUERYDialog(sShell
								.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_QUERY_DELETE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_QUERY_DELETE.setText(Messages.getString("BUTTON.DELETE"));
		BUTTON_CLASS_QUERY_DELETE.setLayoutData(gridData5);
		BUTTON_CLASS_QUERY_DELETE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						updateCurrentSeletedItem();
						if (CurrentLIST_CLASS_QUERYSPECS == null)
							return;
						if (CommonTool.WarnYesNo(sShell.getShell(), Messages
								.getString("WARNYESNO.DELETE")) != SWT.YES)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "vclassname:" + si.name + "\n";
						msg += "querynumber:"
								+ (CurrentLIST_CLASS_QUERYSPECSidx + 1) + "\n";

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell.getShell(), msg,
								"dropqueryspec", Messages
										.getString("WAITING.DROPQUERYSPEC"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						setinfo();
					}
				});
		BUTTON_CLASS_QUERY_EDIT = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_QUERY_EDIT.setText(Messages.getString("BUTTON.EDIT"));
		BUTTON_CLASS_QUERY_EDIT.setLayoutData(gridData6);
		BUTTON_CLASS_QUERY_EDIT
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						updateCurrentSeletedItem();
						if (CurrentLIST_CLASS_QUERYSPECS == null)
							return;
						isadd = false;
						ADD_QUERYDialog dlg = new ADD_QUERYDialog(sShell
								.getShell());
						if (dlg.doModal()) {
							setinfo();
						}
					}
				});
		BUTTON_CLASS_QUERY_VALIDATE = new Button(sShell, SWT.NONE);
		BUTTON_CLASS_QUERY_VALIDATE.setText(Messages
				.getString("BUTTON.VALIDATE"));
		BUTTON_CLASS_QUERY_VALIDATE.setLayoutData(gridData7);
		BUTTON_CLASS_QUERY_VALIDATE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						updateCurrentSeletedItem();
						if (CurrentLIST_CLASS_QUERYSPECS == null)
							return;
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "vclassname:" + si.name + "\n";
						msg += "queryspec:"
								+ CurrentLIST_CLASS_QUERYSPECS.getText(0);

						ClientSocket cs = new ClientSocket();
						if (!cs
								.SendBackGround(
										sShell.getShell(),
										msg,
										"validatequeryspec",
										Messages
												.getString("WAITING.VALIDATEQUERYSPEC"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						CommonTool.MsgBox(sShell.getShell(), Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.VALIDATESUCCESS"));
					}
				});
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.VALIDATION"));
		group1.setLayout(new GridLayout());
		group1.setLayoutData(gridData8);
		BUTTON_CLASS_VCLASS_VALIDATE = new Button(group1, SWT.NONE);
		BUTTON_CLASS_VCLASS_VALIDATE.setText(Messages
				.getString("BUTTON.CHECKIFTHECURRENT"));
		BUTTON_CLASS_VCLASS_VALIDATE.setLayoutData(gridData60);
		BUTTON_CLASS_VCLASS_VALIDATE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = "dbname:" + CubridView.Current_db + "\n";
						msg += "vclassname:" + si.name;

						ClientSocket cs = new ClientSocket();
						if (!cs.SendBackGround(sShell.getShell(), msg,
								"validatevclass", Messages
										.getString("WAITING.VALIDATEVCLASS"))) {
							CommonTool.ErrorBox(sShell.getShell(), cs.ErrorMsg);
							return;
						}
						CommonTool.MsgBox(sShell.getShell(), Messages
								.getString("MSG.SUCCESS"), Messages
								.getString("MSG.VALIDATESUCCESS"));
					}
				});
		sShell.pack();
	}

	private void createTable1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.heightHint = 260;
		gridData2.widthHint = 270;
		gridData2.grabExcessHorizontalSpace = true;
		LIST_CLASS_QUERYSPECS = new Table(sShell, SWT.FULL_SELECTION
				| SWT.SINGLE | SWT.BORDER);
		LIST_CLASS_QUERYSPECS.setLinesVisible(true);
		LIST_CLASS_QUERYSPECS.setLayoutData(gridData2);
		LIST_CLASS_QUERYSPECS.setHeaderVisible(false);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_CLASS_QUERYSPECS.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_CLASS_QUERYSPECS, SWT.LEFT);
		tblcol.setText("col1");
		/*
		 * LIST_CLASS_QUERYSPECS.addListener(SWT.MouseDown, new Listener() {
		 * public void handleEvent(Event event) { Point pt = new Point(event.x,
		 * event.y); CurrentLIST_CLASS_QUERYSPECS =
		 * LIST_CLASS_QUERYSPECS.getItem(pt); if
		 * (CurrentLIST_CLASS_QUERYSPECS!=null) {
		 * EDIT_CLASS_SELECTEDQUERYSPECS.setText(CurrentLIST_CLASS_QUERYSPECS.getText(0));
		 * CurrentLIST_CLASS_QUERYSPECSidx=LIST_CLASS_QUERYSPECS.getSelectionIndex(); } }
		 * });
		 */
	}

	private void setinfo() {
		si = TablePropertyAction.si;
		LIST_CLASS_QUERYSPECS.setToolTipText(Messages
				.getString("TOOLTIP.CLASSLISTQUERYSPECS"));
		if (si.type.equals("system") || si.virtual.equals("normal")) {
			BUTTON_CLASS_QUERY_ADD.setEnabled(false);
			BUTTON_CLASS_QUERY_DELETE.setEnabled(false);
			BUTTON_CLASS_QUERY_EDIT.setEnabled(false);
			BUTTON_CLASS_QUERY_VALIDATE.setEnabled(false);
			BUTTON_CLASS_VCLASS_VALIDATE.setEnabled(false);
		}
		if (si.virtual.equals("normal")) {
			LIST_CLASS_QUERYSPECS.setEnabled(false);
		}
		LIST_CLASS_QUERYSPECS.removeAll();
		for (int i = 0, n = si.querySpecs.size(); i < n; i++) {
			String rec = (String) si.querySpecs.get(i);
			TableItem item = new TableItem(LIST_CLASS_QUERYSPECS, SWT.NONE);
			item.setText(0, rec);
		}

		for (int i = 0, n = LIST_CLASS_QUERYSPECS.getColumnCount(); i < n; i++) {
			LIST_CLASS_QUERYSPECS.getColumn(i).pack();
		}
		if (si.querySpecs.size() > 0) {
			LIST_CLASS_QUERYSPECS.select(0);
			EDIT_CLASS_SELECTEDQUERYSPECS
					.setText((String) si.querySpecs.get(0));
		}
	}

	private void updateCurrentSeletedItem() {
		if (LIST_CLASS_QUERYSPECS.getSelectionCount() == 0) {
			CurrentLIST_CLASS_QUERYSPECS = null;
			return;
		}

		CurrentLIST_CLASS_QUERYSPECS = LIST_CLASS_QUERYSPECS.getSelection()[0];
		CurrentLIST_CLASS_QUERYSPECSidx = LIST_CLASS_QUERYSPECS
				.getSelectionIndex();
	}

}
