package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.SWT;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.action.LoadAction;
import cubridmanager.cubrid.UnloadInfo;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Table;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class LOADDB_PAGE2Dialog extends WizardPage {
	public static final String PAGE_NAME = "LOADDB_PAGE2Dialog";
	private Composite comparent = null;
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	public Button RADIO_LOADDB_SELECTFILEPATH = null;
	public Button RADIO_LOADDB_INSERTFILEPATH = null;
	private Label label1 = null;
	public Combo COMBO_LOADDB_SRCDB = null;
	public Button CHECK_LOADDB_LOADSCHEMA = null;
	public Table LIST_LOADDB_SCHEMA = null;
	private Label label2 = null;
	public Text EDIT_LOADDB_SCHEMA = null;
	public Button CHECK_LOADDB_LOADOBJECT = null;
	public Table LIST_LOADDB_OBJECT = null;
	private Label label3 = null;
	public Text EDIT_LOADDB_OBJECT = null;
	public Button CHECK_LOADDB_LOADINDEX = null;
	public Table LIST_LOADDB_INDEX = null;
	private Label label4 = null;
	public Text EDIT_LOADDB_INDEX = null;

	/*
	 * for the future : public Button CHECK_LOADDB_LOADTRIGGER= null; public Table
	 * LIST_LOADDB_TRIGGER= null; private Label label9 = null; public Text
	 * EDIT_LOADDB_TRIGGER = null; private CLabel label8 = null;
	 */

	private CLabel label5 = null;
	private CLabel label6 = null;
	private CLabel label7 = null;

	public LOADDB_PAGE2Dialog() {
		super(PAGE_NAME, Messages.getString("TITLE.LOADDB_PAGE2DIALOG"), null);
	}

	public int doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
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
		dlgShell.setText(Messages.getString("TITLE.LOADDB_PAGE2DIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData70 = new org.eclipse.swt.layout.GridData();
		gridData70.horizontalSpan = 2;
		gridData70.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData70.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData70.grabExcessHorizontalSpace = true;
		gridData70.heightHint = 3;
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.grabExcessHorizontalSpace = true;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.grabExcessHorizontalSpace = true;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.widthHint = 350;
		gridData12.horizontalSpan = 1;
		gridData12.grabExcessVerticalSpace = true;
		gridData12.horizontalIndent = 0;
		GridData gridData81 = new org.eclipse.swt.layout.GridData();
		gridData81.horizontalSpan = 1;
		gridData81.horizontalIndent = 20;
		gridData81.widthHint = -1;
		gridData81.grabExcessVerticalSpace = true;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalSpan = 2;
		gridData7.grabExcessVerticalSpace = true;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalIndent = 0;
		gridData6.widthHint = 350;
		gridData6.grabExcessVerticalSpace = true;
		gridData6.horizontalSpan = 1;
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		gridData51.horizontalSpan = 1;
		gridData51.horizontalIndent = 20;
		gridData51.grabExcessVerticalSpace = true;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 2;
		gridData4.grabExcessVerticalSpace = true;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalIndent = 0;
		gridData3.horizontalSpan = 1;
		gridData3.grabExcessVerticalSpace = true;
		gridData3.widthHint = 350;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.horizontalSpan = 1;
		gridData21.grabExcessVerticalSpace = true;
		gridData21.horizontalIndent = 20;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalSpan = 2;
		gridData11.grabExcessVerticalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 2;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.grabExcessHorizontalSpace = true;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;

		// sShell = new Composite(dlgShell, SWT.NONE);
		sShell = new Composite(comparent, SWT.NONE); // comment out to use VE
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.UNLOADEDFILE"));
		group1.setLayout(gridLayout2);
		group1.setLayoutData(gridData);
		label1 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.AVAILABLEUNLOADED"));
		createCombo1();
		label5 = new CLabel(sShell, SWT.SHADOW_IN);
		label5.setLayoutData(gridData70);
		CHECK_LOADDB_LOADSCHEMA = new Button(sShell, SWT.CHECK);
		CHECK_LOADDB_LOADSCHEMA.setText(Messages.getString("CHECK.LOADSCHEMA"));
		CHECK_LOADDB_LOADSCHEMA.setLayoutData(gridData11);
		CHECK_LOADDB_LOADSCHEMA
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_LOADDB_LOADSCHEMA.getSelection()) {
							if (LIST_LOADDB_SCHEMA.isVisible())
								LIST_LOADDB_SCHEMA.setEnabled(true);
							else
								EDIT_LOADDB_SCHEMA.setEnabled(true);
						} else {
							if (LIST_LOADDB_SCHEMA.isVisible())
								LIST_LOADDB_SCHEMA.setEnabled(false);
							else
								EDIT_LOADDB_SCHEMA.setEnabled(false);
						}
					}
				});
		label2 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.FILELOCATION"));
		label2.setLayoutData(gridData21);
		EDIT_LOADDB_SCHEMA = new Text(sShell, SWT.BORDER);
		EDIT_LOADDB_SCHEMA.setLayoutData(gridData3);
		createTable1();
		label6 = new CLabel(sShell, SWT.SHADOW_IN);
		label6.setLayoutData(gridData70);
		CHECK_LOADDB_LOADOBJECT = new Button(sShell, SWT.CHECK);
		CHECK_LOADDB_LOADOBJECT
				.setText(Messages.getString("CHECK.LOADOBJECTS"));
		CHECK_LOADDB_LOADOBJECT.setLayoutData(gridData4);
		CHECK_LOADDB_LOADOBJECT
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_LOADDB_LOADOBJECT.getSelection()) {
							if (LIST_LOADDB_OBJECT.isVisible())
								LIST_LOADDB_OBJECT.setEnabled(true);
							else
								EDIT_LOADDB_OBJECT.setEnabled(true);
						} else {
							if (LIST_LOADDB_OBJECT.isVisible())
								LIST_LOADDB_OBJECT.setEnabled(false);
							else
								EDIT_LOADDB_OBJECT.setEnabled(false);
						}

					}
				});
		label3 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.FILELOCATION"));
		label3.setLayoutData(gridData51);
		EDIT_LOADDB_OBJECT = new Text(sShell, SWT.BORDER);
		EDIT_LOADDB_OBJECT.setLayoutData(gridData6);
		createTable2();
		label7 = new CLabel(sShell, SWT.SHADOW_IN);
		label7.setLayoutData(gridData70);
		CHECK_LOADDB_LOADINDEX = new Button(sShell, SWT.CHECK);
		CHECK_LOADDB_LOADINDEX.setText(Messages.getString("CHECK.LOADINDEX"));
		CHECK_LOADDB_LOADINDEX.setLayoutData(gridData7);
		CHECK_LOADDB_LOADINDEX
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_LOADDB_LOADINDEX.getSelection()) {
							if (LIST_LOADDB_INDEX.isVisible())
								LIST_LOADDB_INDEX.setEnabled(true);
							else
								EDIT_LOADDB_INDEX.setEnabled(true);
						} else {
							if (LIST_LOADDB_INDEX.isVisible())
								LIST_LOADDB_INDEX.setEnabled(false);
							else
								EDIT_LOADDB_INDEX.setEnabled(false);
						}

					}
				});
		label4 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.FILELOCATION"));
		label4.setLayoutData(gridData81);
		EDIT_LOADDB_INDEX = new Text(sShell, SWT.BORDER);
		EDIT_LOADDB_INDEX.setLayoutData(gridData12);
		createTable3();

		/*
		 * for the future : label8 = new CLabel(sShell, SWT.SHADOW_IN);
		 * label8.setLayoutData(gridData70);
		 * 
		 * CHECK_LOADDB_LOADTRIGGER = new Button(sShell, SWT.CHECK);
		 * CHECK_LOADDB_LOADTRIGGER.setText(Messages.getString("CHECK.LOADTRIGGER"));
		 * CHECK_LOADDB_LOADTRIGGER.setLayoutData(gridData7);
		 * CHECK_LOADDB_LOADTRIGGER .addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { if
		 * (CHECK_LOADDB_LOADTRIGGER.getSelection()) { if
		 * (LIST_LOADDB_TRIGGER.isVisible())
		 * LIST_LOADDB_TRIGGER.setEnabled(true); else
		 * EDIT_LOADDB_TRIGGER.setEnabled(true); } else { if
		 * (LIST_LOADDB_TRIGGER.isVisible())
		 * LIST_LOADDB_TRIGGER.setEnabled(false); else
		 * EDIT_LOADDB_TRIGGER.setEnabled(false); }
		 *  } }); label9 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		 * label9.setText(Messages.getString("LABEL.FILELOCATION"));
		 * label9.setLayoutData(gridData81); EDIT_LOADDB_TRIGGER = new
		 * Text(sShell, SWT.BORDER);
		 * EDIT_LOADDB_TRIGGER.setLayoutData(gridData12); createTable4();
		 */

		RADIO_LOADDB_SELECTFILEPATH = new Button(group1, SWT.RADIO);
		RADIO_LOADDB_SELECTFILEPATH.setText(Messages
				.getString("RADIO.SELECTUNLOADED"));
		RADIO_LOADDB_SELECTFILEPATH.setLayoutData(gridData31);
		RADIO_LOADDB_SELECTFILEPATH
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						LIST_LOADDB_SCHEMA.setVisible(true);
						LIST_LOADDB_INDEX.setVisible(true);
						LIST_LOADDB_OBJECT.setVisible(true);
						EDIT_LOADDB_SCHEMA.setVisible(false);
						EDIT_LOADDB_INDEX.setVisible(false);
						EDIT_LOADDB_OBJECT.setVisible(false);
						label2.setVisible(false);
						label3.setVisible(false);
						label4.setVisible(false);
						CHECK_LOADDB_LOADSCHEMA.setEnabled(false);
						CHECK_LOADDB_LOADINDEX.setEnabled(false);
						CHECK_LOADDB_LOADOBJECT.setEnabled(false);
						COMBO_LOADDB_SRCDB.setEnabled(true);
						COMBO_LOADDB_SRCDB.select(0);
						/*
						 * for the future : LIST_LOADDB_TRIGGER.setVisible(true);
						 * EDIT_LOADDB_TRIGGER.setVisible(false);
						 * label9.setVisible(false);
						 * CHECK_LOADDB_LOADTRIGGER.setEnabled(false);
						 */
					}
				});
		RADIO_LOADDB_INSERTFILEPATH = new Button(group1, SWT.RADIO);
		RADIO_LOADDB_INSERTFILEPATH.setText(Messages
				.getString("RADIO.INSERTUNLOADED"));
		RADIO_LOADDB_INSERTFILEPATH.setLayoutData(gridData41);
		RADIO_LOADDB_INSERTFILEPATH
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						LIST_LOADDB_SCHEMA.setVisible(false);
						LIST_LOADDB_INDEX.setVisible(false);
						LIST_LOADDB_OBJECT.setVisible(false);
						EDIT_LOADDB_SCHEMA.setVisible(true);
						EDIT_LOADDB_INDEX.setVisible(true);
						EDIT_LOADDB_OBJECT.setVisible(true);
						label2.setVisible(true);
						label3.setVisible(true);
						label4.setVisible(true);
						CHECK_LOADDB_LOADSCHEMA.setEnabled(true);
						CHECK_LOADDB_LOADINDEX.setEnabled(true);
						CHECK_LOADDB_LOADOBJECT.setEnabled(true);
						if (CHECK_LOADDB_LOADSCHEMA.getSelection())
							EDIT_LOADDB_SCHEMA.setEnabled(true);
						else
							EDIT_LOADDB_SCHEMA.setEnabled(false);
						if (CHECK_LOADDB_LOADOBJECT.getSelection())
							EDIT_LOADDB_OBJECT.setEnabled(true);
						else
							EDIT_LOADDB_OBJECT.setEnabled(false);
						if (CHECK_LOADDB_LOADINDEX.getSelection())
							EDIT_LOADDB_INDEX.setEnabled(true);
						else
							EDIT_LOADDB_INDEX.setEnabled(false);
						COMBO_LOADDB_SRCDB.setEnabled(false);
						/*
						 * for the future : LIST_LOADDB_TRIGGER.setVisible(false);
						 * EDIT_LOADDB_TRIGGER.setVisible(true);
						 * label9.setVisible(true);
						 * CHECK_LOADDB_LOADTRIGGER.setEnabled(true); if
						 * (CHECK_LOADDB_LOADTRIGGER.getSelection())
						 * EDIT_LOADDB_TRIGGER.setEnabled(true); else
						 * EDIT_LOADDB_TRIGGER.setEnabled(false);
						 */
					}
				});
		setinfo();
		sShell.pack();
	}

	private void createCombo1() {
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 200;
		gridData1.grabExcessVerticalSpace = true;
		COMBO_LOADDB_SRCDB = new Combo(sShell, SWT.DROP_DOWN);
		COMBO_LOADDB_SRCDB.setLayoutData(gridData1);
		COMBO_LOADDB_SRCDB
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						if (COMBO_LOADDB_SRCDB.getSelectionIndex() > 0) {
							UnloadInfo ui = null;
							String seldb = COMBO_LOADDB_SRCDB.getText();
							TableItem item;
							for (int i = 0, n = LoadAction.unloaddb.size(); i < n; i++) {
								ui = (UnloadInfo) LoadAction.unloaddb.get(i);
								if (ui.dbname.equals(seldb))
									break;
								ui = null;
							}
							if (ui != null) {
								if (ui.schemaDate.size() > 0) {
									LIST_LOADDB_SCHEMA.setEnabled(true);
									CHECK_LOADDB_LOADSCHEMA.setEnabled(true);
									CHECK_LOADDB_LOADSCHEMA.setSelection(true);
									LIST_LOADDB_SCHEMA.removeAll();
									for (int i = 0, n = ui.schemaDate.size(); i < n; i++) {
										item = new TableItem(
												LIST_LOADDB_SCHEMA, SWT.NONE);
										item.setText(0, (String) ui.schemaDate
												.get(i));
										item.setText(1, (String) ui.schemaDir
												.get(i));
									}
									LIST_LOADDB_SCHEMA.select(0);
									for (int i = 0, n = LIST_LOADDB_SCHEMA
											.getColumnCount(); i < n; i++) {
										LIST_LOADDB_SCHEMA.getColumn(i).pack();
									}

								} else {
									LIST_LOADDB_SCHEMA.removeAll();
									LIST_LOADDB_SCHEMA.setEnabled(false);
									CHECK_LOADDB_LOADSCHEMA.setEnabled(false);
									CHECK_LOADDB_LOADSCHEMA.setSelection(false);
								}

								if (ui.objectDate.size() > 0) {
									LIST_LOADDB_OBJECT.setEnabled(true);
									CHECK_LOADDB_LOADOBJECT.setEnabled(true);
									CHECK_LOADDB_LOADOBJECT.setSelection(true);
									LIST_LOADDB_OBJECT.removeAll();
									for (int i = 0, n = ui.objectDate.size(); i < n; i++) {
										item = new TableItem(
												LIST_LOADDB_OBJECT, SWT.NONE);
										item.setText(0, (String) ui.objectDate
												.get(i));
										item.setText(1, (String) ui.objectDir
												.get(i));
									}
									LIST_LOADDB_OBJECT.select(0);
									for (int i = 0, n = LIST_LOADDB_OBJECT
											.getColumnCount(); i < n; i++) {
										LIST_LOADDB_OBJECT.getColumn(i).pack();
									}

								} else {
									LIST_LOADDB_OBJECT.removeAll();
									LIST_LOADDB_OBJECT.setEnabled(false);
									CHECK_LOADDB_LOADOBJECT.setEnabled(false);
									CHECK_LOADDB_LOADOBJECT.setSelection(false);
								}

								if (ui.indexDate.size() > 0) {
									LIST_LOADDB_INDEX.setEnabled(true);
									CHECK_LOADDB_LOADINDEX.setEnabled(true);
									CHECK_LOADDB_LOADINDEX.setSelection(true);
									LIST_LOADDB_INDEX.removeAll();
									for (int i = 0, n = ui.indexDate.size(); i < n; i++) {
										item = new TableItem(LIST_LOADDB_INDEX,
												SWT.NONE);
										item.setText(0, (String) ui.indexDate
												.get(i));
										item.setText(1, (String) ui.indexDir
												.get(i));
									}
									LIST_LOADDB_INDEX.select(0);
									for (int i = 0, n = LIST_LOADDB_INDEX
											.getColumnCount(); i < n; i++) {
										LIST_LOADDB_INDEX.getColumn(i).pack();
									}

								} else {
									LIST_LOADDB_INDEX.removeAll();
									LIST_LOADDB_INDEX.setEnabled(false);
									CHECK_LOADDB_LOADINDEX.setEnabled(false);
									CHECK_LOADDB_LOADINDEX.setSelection(false);
								}

								/*
								 * for the future : if (ui.triggerDate.size()>0) {
								 * LIST_LOADDB_TRIGGER.setEnabled(true);
								 * CHECK_LOADDB_LOADTRIGGER.setEnabled(true);
								 * CHECK_LOADDB_LOADTRIGGER.setSelection(true);
								 * LIST_LOADDB_TRIGGER.removeAll(); for (int
								 * i=0,n=ui.triggerDate.size(); i < n; i++) {
								 * item = new TableItem(LIST_LOADDB_TRIGGER,
								 * SWT.NONE); item.setText(0,
								 * (String)ui.triggerDate.get(i));
								 * item.setText(1,
								 * (String)ui.triggerDir.get(i)); }
								 * LIST_LOADDB_TRIGGER.select(0); for (int i =
								 * 0, n = LIST_LOADDB_TRIGGER.getColumnCount();
								 * i < n; i++) {
								 * LIST_LOADDB_TRIGGER.getColumn(i).pack(); }
								 *  } else { LIST_LOADDB_TRIGGER.removeAll();
								 * LIST_LOADDB_TRIGGER.setEnabled(false);
								 * CHECK_LOADDB_LOADTRIGGER.setEnabled(false);
								 * CHECK_LOADDB_LOADTRIGGER.setSelection(false); }
								 */
							}
						} else {
							LIST_LOADDB_SCHEMA.setEnabled(false);
							LIST_LOADDB_INDEX.setEnabled(false);
							LIST_LOADDB_OBJECT.setEnabled(false);
							LIST_LOADDB_SCHEMA.removeAll();
							LIST_LOADDB_INDEX.removeAll();
							LIST_LOADDB_OBJECT.removeAll();
							CHECK_LOADDB_LOADSCHEMA.setSelection(false);
							CHECK_LOADDB_LOADINDEX.setSelection(false);
							CHECK_LOADDB_LOADOBJECT.setSelection(false);
							CHECK_LOADDB_LOADSCHEMA.setEnabled(false);
							CHECK_LOADDB_LOADINDEX.setEnabled(false);
							CHECK_LOADDB_LOADOBJECT.setEnabled(false);
							/*
							 * for the future : LIST_LOADDB_TRIGGER.setEnabled(false);
							 * LIST_LOADDB_TRIGGER.removeAll();
							 * CHECK_LOADDB_LOADTRIGGER.setSelection(false);
							 * CHECK_LOADDB_LOADTRIGGER.setEnabled(false);
							 */
						}
					}
				});
	}

	private void createTable1() {
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 2;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.grabExcessVerticalSpace = true;
		gridData2.heightHint = 50;
		gridData2.widthHint = 350;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_LOADDB_SCHEMA = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_LOADDB_SCHEMA.setLinesVisible(true);
		LIST_LOADDB_SCHEMA.setLayoutData(gridData2);
		LIST_LOADDB_SCHEMA.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		LIST_LOADDB_SCHEMA.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_LOADDB_SCHEMA, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.UNLOADEDTIME"));
		tblcol = new TableColumn(LIST_LOADDB_SCHEMA, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.FILELOCATION"));
	}

	private void createTable2() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.grabExcessVerticalSpace = true;
		gridData5.horizontalSpan = 2;
		gridData5.heightHint = 50;
		gridData5.widthHint = 350;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		LIST_LOADDB_OBJECT = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_LOADDB_OBJECT.setLinesVisible(true);
		LIST_LOADDB_OBJECT.setLayoutData(gridData5);
		LIST_LOADDB_OBJECT.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		LIST_LOADDB_OBJECT.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_LOADDB_OBJECT, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.UNLOADEDTIME"));
		tblcol = new TableColumn(LIST_LOADDB_OBJECT, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.FILELOCATION"));
	}

	private void createTable3() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalSpan = 2;
		gridData8.widthHint = 350;
		gridData8.grabExcessVerticalSpace = true;
		gridData8.grabExcessHorizontalSpace = true;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.heightHint = 50;
		LIST_LOADDB_INDEX = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE
				| SWT.BORDER);
		LIST_LOADDB_INDEX.setLinesVisible(true);
		LIST_LOADDB_INDEX.setLayoutData(gridData8);
		LIST_LOADDB_INDEX.setHeaderVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		tlayout.addColumnData(new ColumnWeightData(50, 30, true));
		LIST_LOADDB_INDEX.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_LOADDB_INDEX, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.UNLOADEDTIME"));
		tblcol = new TableColumn(LIST_LOADDB_INDEX, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.FILELOCATION"));
	}

	/*
	 * for the future : private void createTable4() { GridData gridData8 = new
	 * org.eclipse.swt.layout.GridData(); gridData8.horizontalSpan = 2;
	 * gridData8.widthHint = 350; gridData8.grabExcessVerticalSpace = true;
	 * gridData8.grabExcessHorizontalSpace = true; gridData8.horizontalAlignment =
	 * org.eclipse.swt.layout.GridData.FILL; gridData8.verticalAlignment =
	 * org.eclipse.swt.layout.GridData.FILL; gridData8.heightHint = 35;
	 * LIST_LOADDB_TRIGGER = new Table(sShell, SWT.FULL_SELECTION | SWT.SINGLE |
	 * SWT.BORDER); LIST_LOADDB_TRIGGER.setLinesVisible(true);
	 * LIST_LOADDB_TRIGGER.setLayoutData(gridData8);
	 * LIST_LOADDB_TRIGGER.setHeaderVisible(true); TableLayout tlayout = new
	 * TableLayout(); tlayout.addColumnData(new ColumnWeightData(50, 30, true));
	 * tlayout.addColumnData(new ColumnWeightData(50, 30, true));
	 * LIST_LOADDB_TRIGGER.setLayout(tlayout);
	 * 
	 * TableColumn tblcol = new TableColumn(LIST_LOADDB_TRIGGER, SWT.LEFT);
	 * tblcol.setText(Messages.getString("TABLE.UNLOADEDTIME")); tblcol = new
	 * TableColumn(LIST_LOADDB_TRIGGER, SWT.LEFT);
	 * tblcol.setText(Messages.getString("TABLE.FILELOCATION")); }
	 */

	public void createControl(Composite parent) {
		comparent = parent;
		createComposite();
		sShell.setParent(parent);
		setControl(sShell);
		setPageComplete(false);
	}

	private void setinfo() {
		RADIO_LOADDB_INSERTFILEPATH.setSelection(false);
		RADIO_LOADDB_SELECTFILEPATH.setSelection(true);

		int numInfo = LoadAction.unloaddb.size();
		if (numInfo > 0) {
			COMBO_LOADDB_SRCDB.add(
					Messages.getString("COMBO.SELECTUNLOADEDDB"), 0);
			for (int i = 0; i < numInfo; i++) {
				UnloadInfo ui = (UnloadInfo) LoadAction.unloaddb.get(i);
				COMBO_LOADDB_SRCDB.add(ui.dbname, i + 1);
			}
		} else {
			COMBO_LOADDB_SRCDB
					.add(Messages.getString("COMBO.NOUNLOADFILES"), 0);
		}
		COMBO_LOADDB_SRCDB.select(0);

		CHECK_LOADDB_LOADSCHEMA.setToolTipText(Messages
				.getString("TOOLTIP.CHECKSCHEMA"));
		EDIT_LOADDB_SCHEMA.setToolTipText(Messages
				.getString("TOOLTIP.EDITSCHEMA"));
		CHECK_LOADDB_LOADOBJECT.setToolTipText(Messages
				.getString("TOOLTIP.CHECKOBJECT"));
		EDIT_LOADDB_OBJECT.setToolTipText(Messages
				.getString("TOOLTIP.EDITOBJECT"));
		CHECK_LOADDB_LOADINDEX.setToolTipText(Messages
				.getString("TOOLTIP.CHECKINDEX"));
		EDIT_LOADDB_INDEX.setToolTipText(Messages
				.getString("TOOLTIP.EDITINDEX"));
		LIST_LOADDB_SCHEMA.setToolTipText(Messages
				.getString("TOOLTIP.LISTSCHEMA"));
		LIST_LOADDB_OBJECT.setToolTipText(Messages
				.getString("TOOLTIP.LISTOBJECT"));
		LIST_LOADDB_INDEX.setToolTipText(Messages
				.getString("TOOLTIP.LISTINDEX"));

		label2.setVisible(false);
		EDIT_LOADDB_SCHEMA.setVisible(false);

		label3.setVisible(false);
		EDIT_LOADDB_OBJECT.setVisible(false);

		label4.setVisible(false);
		EDIT_LOADDB_INDEX.setVisible(false);

		/*
		 * for the future :
		 * CHECK_LOADDB_LOADTRIGGER.setToolTipText(Messages.getString("TOOLTIP.CHECKTRIGGER"));
		 * EDIT_LOADDB_TRIGGER.setToolTipText(Messages.getString("TOOLTIP.EDITTRIGGER"));
		 * LIST_LOADDB_TRIGGER.setToolTipText(Messages.getString("TOOLTIP.LISTTRIGGER"));
		 * 
		 * label9.setVisible(false); EDIT_LOADDB_TRIGGER.setVisible(false);
		 */
	}
}
