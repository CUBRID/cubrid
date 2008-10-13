package cubridmanager.cubrid.dialog;

import java.util.ArrayList;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CCombo;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.events.FocusAdapter;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.KeyEvent;
import org.eclipse.swt.events.MouseAdapter;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.TraverseEvent;
import org.eclipse.swt.events.TraverseListener;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.VerifyDigitListener;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.SchemaInfo;
import cubridmanager.cubrid.view.CubridView;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Composite;

public class EditAttributeDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="11,5"
	private Group group = null;
	private Button btnOk = null;
	private Button btnCancel = null;
	private Label lblName = null;
	private Text txtName = null;
	private Group grpDomain = null;
	private Composite cmpDomainType = null;
	private Composite cmpDomainSize = null;
	private Composite cmpDomainScale = null;
	private Label lblDomainType = null;
	private Combo cmbDomainType = null;
	private Label lblDomainSize = null;
	private Text txtDomainSize = null;
	private Label lblDomainScale = null;
	private Text txtDomainScale = null;
	private Button chkNotNull = null;
	private Button chkShared = null;
	private Button chkUnique = null;
	private Label lblDefault = null;
	private Text txtDefault = null;
	private Button chkClassAttribute = null;
	private Table tblSetTypes = null;
	private Composite cmpSetTypes = null;
	private Label lblNote = null;
	private boolean isCmbEditable = true;
	private boolean ret = false;
	private TableItem ti = null;
	private int beforeSelectionIndex = -1;
	private int currentSelectionIndex = -1;

	public EditAttributeDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public EditAttributeDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public boolean doModal() {
		return doModal(null);
	}

	public boolean doModal(TableItem item) {
		createSShell();
		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.setDefaultButton(btnOk);
		sShell.open();

		ti = item;

		if (ti != null)
			setInfo();

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 2;
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setLayout(gridLayout);
		sShell.setText(this.getText());

		createGroup();

		GridData gridData1 = new GridData(GridData.GRAB_HORIZONTAL);
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData1.widthHint = 75;
		btnOk = new Button(sShell, SWT.NONE);
		btnOk.setLayoutData(gridData1);
		btnOk.setText(Messages.getString("BUTTON.OK"));
		btnOk
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (ti == null) {
							String attname = txtName.getText().trim();
							String attdeft = txtDefault.getText();
							String retstr = CommonTool
									.ValidateCheckInIdentifier(attname);
							if (retstr.length() > 0) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.INVALIDATTRNAME"));
								return;
							}
							String atttype = makeType();
							if (atttype.length() <= 0) {
								return;
							}
							StringBuffer msg = new StringBuffer("");
							msg.append("dbname:");
							msg.append(CubridView.Current_db);
							msg.append("\n");

							msg.append("classname:");
							msg.append(PROPPAGE_CLASS_PAGE1Dialog.si.name);
							msg.append("\n");

							msg.append("attributename:");
							msg.append(attname);
							msg.append("\n");

							msg.append("type:");
							msg.append(atttype);
							msg.append("\n");

							msg.append("default:");
							msg.append(attdeft);
							msg.append("\n");

							msg.append("category:");
							if (!chkClassAttribute.getSelection()
									&& !chkShared.getSelection())
								msg.append("instance\n");
							else if (!chkClassAttribute.getSelection()
									&& chkShared.getSelection())
								msg.append("shared\n");
							else if (chkClassAttribute.getSelection()
									&& !chkShared.getSelection())
								msg.append("class\n");
							else {
								CommonTool.ErrorBox(sShell,
										"Error attribute category");
								return;
							}

							msg.append("unique:");
							msg.append(chkUnique.getSelection() ? "yes\n"
									: "no\n");
							msg.append("notnull:");
							msg.append(chkNotNull.getSelection() ? "yes\n"
									: "no\n");

							ClientSocket cs = new ClientSocket();

							if (!cs.SendBackGround(sShell, msg.toString(),
									"addattribute", Messages
											.getString("WAITING.ADDATTR"))) {
								CommonTool.ErrorBox(sShell, cs.ErrorMsg);
								return;
							}
						} else {
							String attname = txtName.getText().trim();
							String attdeft = txtDefault.getText();
							String retstr = CommonTool
									.ValidateCheckInIdentifier(attname);
							if (retstr.length() > 0) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.INVALIDATTRNAME"));
								return;
							}

							if (attdeft.length() <= 0
									&& ti.getText(6).length() > 0) {
								CommonTool.ErrorBox(sShell, Messages
										.getString("ERROR.CANNOTDELDEFAULT"));
								return;
							}

							StringBuffer msg = new StringBuffer("");
							msg.append("dbname:");
							msg.append(CubridView.Current_db);
							msg.append("\n");

							msg.append("classname:");
							msg.append(PROPPAGE_CLASS_PAGE1Dialog.si.name);
							msg.append("\n");

							msg.append("oldattributename:");
							msg.append(ti.getText(0));
							msg.append("\n");

							msg.append("newattributename:");
							msg.append(attname);
							msg.append("\n");

							if (!chkClassAttribute.getSelection())
								msg
										.append(chkClassAttribute
												.getSelection() ? "category:class\n"
												: "category:instance\n");
							msg.append("index:x\n"); 
							msg.append(chkNotNull.getSelection() ? "notnull:y\n"
											: "notnull:n\n");
							msg.append(chkUnique.getSelection() ? "unique:y\n"
									: "unique:n\n");

							msg.append("default:");
							msg.append(attdeft);
							msg.append("\n");

							ClientSocket cs = new ClientSocket();

							if (!cs.SendBackGround(sShell, msg.toString(),
									"updateattribute", Messages
											.getString("WAITING.EDITATTR"))) {
								CommonTool.ErrorBox(sShell, cs.ErrorMsg);
								return;
							}
						}

						ret = true;
						sShell.dispose();
					}
				});

		GridData gridData2 = new GridData(GridData.GRAB_HORIZONTAL);
		gridData2.widthHint = 75;
		btnCancel = new Button(sShell, SWT.NONE);
		btnCancel.setLayoutData(gridData2);
		btnCancel.setText(Messages.getString("BUTTON.CANCEL"));
		btnCancel
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						sShell.dispose();
					}
				});

		hideDomainSize();
		hideSetTypes();
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		GridData gridData32 = new GridData();
		gridData32.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData32.widthHint = 209;
		GridData gridData22 = new GridData();
		gridData22.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData22.widthHint = 209;
		GridData gridData15 = new GridData();
		gridData15.grabExcessHorizontalSpace = true;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		GridData gridData = new GridData(GridData.FILL_BOTH);
		gridData.horizontalSpan = 2;
		group = new Group(sShell, SWT.NONE);
		group.setLayoutData(gridData);
		group.setLayout(gridLayout1);
		lblName = new Label(group, SWT.NONE);
		lblName.setText(Messages.getString("LABEL.NAME"));
		lblName.setLayoutData(gridData15);
		txtName = new Text(group, SWT.BORDER);
		txtName.setLayoutData(gridData22);
		createGrpDomain();

		GridData gridData4 = new GridData();
		gridData4.horizontalSpan = 2;
		chkClassAttribute = new Button(group, SWT.CHECK);
		chkClassAttribute.setLayoutData(gridData4);
		chkClassAttribute.setText(Messages.getString("TABLE.CLASSATTRIBUTE"));
		chkClassAttribute
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (chkClassAttribute.getSelection()) {
							chkShared.setEnabled(false);
							chkNotNull.setEnabled(false);
							chkUnique.setEnabled(false);

							chkShared.setSelection(false);
							chkNotNull.setSelection(false);
							chkUnique.setSelection(false);
						} else {
							chkShared.setEnabled(true);
							chkNotNull.setEnabled(true);
							chkUnique.setEnabled(true);
						}
					}
				});

		GridData gridData21 = new GridData();
		gridData21.horizontalSpan = 2;
		chkShared = new Button(group, SWT.CHECK);
		chkShared.setLayoutData(gridData21);
		chkShared.setText("SHARED");
		chkShared
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (chkShared.getSelection()) {
							chkClassAttribute.setEnabled(false);
							chkUnique.setEnabled(false);

							chkClassAttribute.setSelection(false);
							chkUnique.setSelection(false);
						} else {
							chkClassAttribute.setEnabled(true);
							chkUnique.setEnabled(true);
						}
					}
				});

		GridData gridData11 = new GridData();
		gridData11.horizontalSpan = 2;
		chkNotNull = new Button(group, SWT.CHECK);
		chkNotNull.setLayoutData(gridData11);
		chkNotNull.setText("NOT NULL");
		chkNotNull
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (chkNotNull.getSelection()) {
							chkClassAttribute.setEnabled(false);

							chkClassAttribute.setSelection(false);
						} else {
							if (!chkShared.getSelection()
									&& !chkUnique.getSelection() && ti == null)
								chkClassAttribute.setEnabled(true);
						}
					}
				});

		GridData gridData31 = new GridData();
		gridData31.horizontalSpan = 2;
		chkUnique = new Button(group, SWT.CHECK);
		chkUnique.setLayoutData(gridData31);
		chkUnique.setText("UNIQUE");
		chkUnique
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (chkUnique.getSelection()) {
							chkClassAttribute.setEnabled(false);
							chkShared.setEnabled(false);

							chkClassAttribute.setSelection(false);
							chkShared.setSelection(false);
						} else {
							if (!chkNotNull.getSelection() && ti == null)
								chkClassAttribute.setEnabled(true);
							if (ti == null)
								chkShared.setEnabled(true);
						}
					}
				});

		lblDefault = new Label(group, SWT.NONE);
		lblDefault.setText("DEFAULT: ");
		txtDefault = new Text(group, SWT.BORDER);
		txtDefault.setLayoutData(gridData32);

		new Label(group, SWT.NONE);

		GridData gridLblNote = new GridData(GridData.FILL_VERTICAL);
		gridLblNote.widthHint = 209;
		gridLblNote.verticalSpan = 2;
		gridLblNote.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;

		lblNote = new Label(group, SWT.WRAP);
		lblNote.setText(Messages.getString("LABEL.NOTEALLTYPES"));
		lblNote.setLayoutData(gridLblNote);

		new Label(group, SWT.NONE);

	}

	/**
	 * This method initializes grpDomain
	 * 
	 */
	private void createGrpDomain() {
		GridLayout gridLayout5 = new GridLayout();
		gridLayout5.verticalSpacing = 0;
		GridData gridData3 = new GridData();
		gridData3.horizontalSpan = 2;
		gridData3.grabExcessHorizontalSpace = true;
		gridData3.widthHint = 300;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		grpDomain = new Group(group, SWT.NONE);
		grpDomain.setLayoutData(gridData3);
		grpDomain.setText(Messages.getString("LABEL.DOMAIN2"));
		createCmpDomainType();
		createCmpDomainSize();
		createCmpDomainScale();
		grpDomain.setLayout(gridLayout5);
		createCmpSetTypes();
	}

	private void createCmpDomainType() {
		GridLayout gridLayout4 = new GridLayout();
		gridLayout4.numColumns = 2;
		gridLayout4.horizontalSpacing = 0;
		gridLayout4.marginHeight = 2;
		gridLayout4.marginWidth = 0;
		gridLayout4.verticalSpacing = 0;
		GridData gridData8 = new GridData(GridData.FILL_HORIZONTAL);
		gridData8.horizontalSpan = 2;
		cmpDomainType = new Composite(grpDomain, SWT.NONE);
		cmpDomainType.setLayout(gridLayout4);
		cmpDomainType.setLayoutData(gridData8);
		lblDomainType = new Label(cmpDomainType, SWT.NONE);
		lblDomainType.setText(Messages.getString("LABEL.TYPE"));
		createCmbDomainType();
	}

	/**
	 * This method initializes cmbDomainType
	 * 
	 */
	private void createCmbDomainType() {
		GridData gridData9 = new GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData9.widthHint = 184;
		gridData9.grabExcessHorizontalSpace = true;
		cmbDomainType = new Combo(cmpDomainType, SWT.NONE);
		cmbDomainType.setVisibleItemCount(20);
		cmbDomainType.setLayoutData(gridData9);

		if (ti != null)
			return;

		int i = 0;
		// 0 : direct input, 1 ~ 6 : need size, 7 : need precision and scale, 17 ~ 19 : set type
		cmbDomainType.add(Messages.getString("COMBO.INPUT"), i++);
		cmbDomainType.add("CHAR", i++); // 1
		cmbDomainType.add("VARCHAR", i++);
		cmbDomainType.add("NCHAR", i++);
		cmbDomainType.add("NCHAR VARYING", i++);
		cmbDomainType.add("BIT", i++);
		cmbDomainType.add("BIT VARYING", i++);
		cmbDomainType.add("NUMERIC", i++); // 7
		cmbDomainType.add("INTEGER", i++);
		cmbDomainType.add("SMALLINT", i++);
		cmbDomainType.add("MONETARY", i++); // 10
		cmbDomainType.add("FLOAT", i++);
		cmbDomainType.add("DOUBLE", i++);
		cmbDomainType.add("DATE", i++);
		cmbDomainType.add("TIME", i++);
		cmbDomainType.add("TIMESTAMP", i++);
		cmbDomainType.add("STRING", i++);
		cmbDomainType.add("SET", i++); // 17
		cmbDomainType.add("MULTISET", i++);
		cmbDomainType.add("SEQUENCE", i++);

		cmbDomainType.select(0);

		ArrayList sinfo = null;
		AuthItem authrec = null;

		for (int j = 0, n = MainRegistry.Authinfo.size(); j < n; j++) {
			authrec = (AuthItem) MainRegistry.Authinfo.get(j);
			if (authrec.dbname.equals(CubridView.Current_db)) {
				sinfo = authrec.Schema;
				for (int ai = 0, an = sinfo.size(); ai < an; ai++) {
					if (((SchemaInfo) sinfo.get(ai)).virtual.equals("normal"))
						cmbDomainType.add(((SchemaInfo) sinfo.get(ai)).name,
								i++);
				}
			}
		}

		cmbDomainType
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						isCmbEditable = false;
						hideDomainSize();
						hideSetTypes();

						switch (cmbDomainType.getSelectionIndex()) {
						case 0:
							isCmbEditable = true;
							break;
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
							showDomainSize();
							break;
						case 7:
							showDomainScale();
							break;
						case 17:
						case 18:
						case 19:
							showSetTypes();
							break;
						default:
						}
						sShell.pack();
					}
				});
		cmbDomainType.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyPressed(org.eclipse.swt.events.KeyEvent e) {
				if (!isCmbEditable
						&& !(e.keyCode == SWT.ARROW_UP || e.keyCode == SWT.ARROW_DOWN))
					e.doit = false;
			}
		});
	}

	private void createCmpDomainSize() {
		GridLayout gridLayout3 = new GridLayout();
		gridLayout3.numColumns = 2;
		gridLayout3.verticalSpacing = 0;
		gridLayout3.marginHeight = 2;
		gridLayout3.marginWidth = 0;
		gridLayout3.horizontalSpacing = 0;
		GridData gridData6 = new GridData(GridData.FILL_HORIZONTAL);
		gridData6.horizontalSpan = 2;
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		cmpDomainSize = new Composite(grpDomain, SWT.NONE);
		cmpDomainSize.setLayoutData(gridData6);
		cmpDomainSize.setLayout(gridLayout3);

		GridData gridData12 = new GridData();
		gridData12.grabExcessHorizontalSpace = true;
		lblDomainSize = new Label(cmpDomainSize, SWT.NONE);
		lblDomainSize.setText(Messages.getString("LABEL.SIZE"));
		lblDomainSize.setLayoutData(gridData12);
		GridData gridData7 = new GridData();
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData7.widthHint = 200;
		txtDomainSize = new Text(cmpDomainSize, SWT.BORDER);
		txtDomainSize.setLayoutData(gridData7);
		txtDomainSize.addListener(SWT.Verify, new VerifyDigitListener());
	}

	private void createCmpDomainScale() {
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;
		gridLayout2.horizontalSpacing = 0;
		gridLayout2.marginHeight = 2;
		gridLayout2.marginWidth = 0;
		gridLayout2.verticalSpacing = 0;
		cmpDomainScale = new Composite(grpDomain, SWT.NONE);
		cmpDomainScale.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		cmpDomainScale.setLayout(gridLayout2);

		GridData gridData14 = new GridData();
		gridData14.grabExcessHorizontalSpace = true;
		GridData gridData13 = new GridData();
		gridData13.widthHint = 200;
		gridData13.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		lblDomainScale = new Label(cmpDomainScale, SWT.NONE);
		lblDomainScale.setText(Messages.getString("LABEL.SCALE"));
		lblDomainScale.setLayoutData(gridData14);
		txtDomainScale = new Text(cmpDomainScale, SWT.BORDER);
		txtDomainScale.setLayoutData(gridData13);
		txtDomainScale.addListener(SWT.Verify, new VerifyDigitListener());
	}

	/**
	 * This method initializes cmpSetTypes
	 * 
	 */
	private void createCmpSetTypes() {
		GridLayout gridLayout6 = new GridLayout();
		gridLayout6.horizontalSpacing = 0;
		gridLayout6.marginWidth = 0;
		gridLayout6.verticalSpacing = 0;
		gridLayout6.marginHeight = 0;
		GridData gridData5 = new org.eclipse.swt.layout.GridData(
				GridData.FILL_HORIZONTAL);
		gridData5.horizontalSpan = 2;
		gridData5.heightHint = 50;
		cmpSetTypes = new Composite(grpDomain, SWT.NONE);
		cmpSetTypes.setLayoutData(gridData5);
		cmpSetTypes.setLayout(gridLayout6);
		createTblSetTypes();
	}

	/**
	 * This method initializes tblSetTypes
	 * 
	 */
	private void createTblSetTypes() {
		GridData gridData16 = new GridData(GridData.FILL_BOTH);
		tblSetTypes = new Table(cmpSetTypes, SWT.BORDER | SWT.FULL_SELECTION);
		tblSetTypes.setHeaderVisible(true);
		tblSetTypes.setLayoutData(gridData16);
		tblSetTypes.setLinesVisible(true);

		TableColumn col;
		col = new TableColumn(tblSetTypes, SWT.NONE);
		col.setText(Messages.getString("LABEL.DOMAIN2"));
		col = new TableColumn(tblSetTypes, SWT.NONE);
		col.setText(Messages.getString("TABLE.SIZE"));
		col = new TableColumn(tblSetTypes, SWT.NONE);
		col.setText(Messages.getString("TABLE.SCALE"));

		final TableEditor editor = new TableEditor(tblSetTypes);
		editor.horizontalAlignment = SWT.RIGHT;
		editor.grabHorizontal = true;

		tblSetTypes.addMouseListener(new MouseAdapter() {
			public void mouseDown(MouseEvent event) {
				Control old = editor.getEditor();
				if (old != null)
					old.dispose();

				Point pt = new Point(event.x, event.y);

				final TableItem item = tblSetTypes.getItem(pt);

				beforeSelectionIndex = currentSelectionIndex;
				currentSelectionIndex = tblSetTypes.getSelectionIndex();
				if (beforeSelectionIndex < 0
						|| beforeSelectionIndex != currentSelectionIndex)
					return;

				if (item != null) {
					int column = -1;
					for (int i = 0, n = tblSetTypes.getColumnCount(); i < n; i++) {
						Rectangle rect = item.getBounds(i);
						if (rect.contains(pt)) {
							column = i;
							break;
						}
					}

					// db create auth field, cas auth field
					if (column == 0) {
						final CCombo combo = new CCombo(tblSetTypes,
								SWT.READ_ONLY);
						combo.setVisibleItemCount(15);
						combo.add(Messages.getString("COMBOITEM.SELECTION"));
						for (int i = 0; i < cmbDomainType.getItemCount(); i++) {
							String domain = cmbDomainType.getItem(i);
							if (i == 0 || (i > 16 && i < 20))
								continue;
							combo.add(domain);
						}

						combo.select(combo.indexOf(item.getText(column)));

						editor.minimumWidth = combo.computeSize(SWT.DEFAULT,
								SWT.DEFAULT).x;
						tblSetTypes.getColumn(column).setWidth(
								editor.minimumWidth);

						combo.setFocus();
						editor.setEditor(combo, item, column);

						combo.addSelectionListener(new SelectionAdapter() {
							public void widgetSelected(SelectionEvent event) {
								if (combo.getSelectionIndex() != 0) {
									if (item
											.getText(0)
											.equals(
													Messages
															.getString("COMBOITEM.SELECTION"))) {
										TableItem newItem = new TableItem(
												tblSetTypes, SWT.NONE);
										newItem
												.setText(
														0,
														Messages
																.getString("COMBOITEM.SELECTION"));
									}
									item.setText(0, combo.getText());
								} else {
									if (tblSetTypes.getSelectionIndex() < (tblSetTypes
											.getItemCount() - 1))
										tblSetTypes.remove(tblSetTypes
												.getSelectionIndex());
									else {
										item.setText(1, "");
										item.setText(2, "");
									}
								}
								combo.dispose();
							}
						});
					} else {
						String domain = item.getText(0);
						if (column == 1) {
							if (!(domain.equals("CHAR")
									|| domain.equals("VARCHAR")
									|| domain.equals("NCHAR")
									|| domain.equals("NCHAR VARYING")
									|| domain.equals("BIT")
									|| domain.equals("BIT VARYING") || domain
									.equals("NUMERIC")))
								return;
						} else {
							if (!domain.equals("NUMERIC"))
								return;
						}

						final Text text = new Text(tblSetTypes, SWT.NONE);
						text.setForeground(item.getForeground());

						if (column == 1 && !domain.equals("NUMERIC"))
							text.setTextLimit(10);
						else
							text.setTextLimit(2);

						text.setText((String) item.getText(column));
						text.setForeground(item.getForeground());
						text.selectAll();
						text.setFocus();

						editor.minimumWidth = text.getBounds().width;

						editor.setEditor(text, item, column);

						final int col = column;
						text.addTraverseListener(new TraverseListener() {
							public void keyTraversed(TraverseEvent e) {
								switch (e.detail) {
								case SWT.TRAVERSE_ESCAPE:
									e.doit = false;
									text.dispose();
									break;
								case SWT.TRAVERSE_ARROW_NEXT:
								case SWT.TRAVERSE_ARROW_PREVIOUS:
								case SWT.TRAVERSE_PAGE_NEXT:
								case SWT.TRAVERSE_PAGE_PREVIOUS:
								case SWT.TRAVERSE_TAB_NEXT:
								case SWT.TRAVERSE_TAB_PREVIOUS:
								case SWT.TRAVERSE_RETURN:
								default:
									item.setText(col, text.getText());
									text.dispose();
									break;
								}
							}
						});

						text.addFocusListener(new FocusAdapter() {
							public void focusLost(FocusEvent e) {
								item.setText(col, text.getText());
								text.dispose();
							}
						});

						text.addListener(SWT.Verify, new VerifyDigitListener());
					}
				}
			}
		});

		Menu menu = new Menu(sShell, SWT.POP_UP);
		final MenuItem itemDelete = new MenuItem(menu, SWT.PUSH);
		itemDelete.setText(Messages.getString("QEDIT.DELETE"));
		tblSetTypes.setMenu(menu);
		itemDelete.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent event) {
				deleteRecord(tblSetTypes.getSelectionIndex());
			}
		});

		tblSetTypes.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyReleased(KeyEvent e) {
				if (e.keyCode == SWT.DEL) {
					deleteRecord(tblSetTypes.getSelectionIndex());
				}
			}
		});

		TableItem emptyItem = new TableItem(tblSetTypes, SWT.NONE);
		emptyItem.setText(Messages.getString("COMBOITEM.SELECTION")); 

	}

	private void hideDomainSize() {
		GridData gridHide = new GridData();
		gridHide.heightHint = 0;
		gridHide.horizontalSpan = 2;
		cmpDomainSize.setLayoutData(gridHide);
		hideDomainScale();
	}

	private void showDomainSize() {
		GridData gridShow = new GridData(GridData.HORIZONTAL_ALIGN_FILL);
		gridShow.heightHint = -1;
		gridShow.horizontalSpan = 2;
		cmpDomainSize.setLayoutData(gridShow);
	}

	private void hideDomainScale() {
		GridData gridHide = new GridData();
		gridHide.heightHint = 0;
		gridHide.horizontalSpan = 2;
		cmpDomainScale.setLayoutData(gridHide);
	}

	private void showDomainScale() {
		showDomainSize();
		GridData gridShow = new GridData(GridData.HORIZONTAL_ALIGN_FILL);
		gridShow.heightHint = -1;
		gridShow.horizontalSpan = 2;
		cmpDomainScale.setLayoutData(gridShow);
	}

	private void hideSetTypes() {
		GridData gridHide = new GridData(GridData.FILL_BOTH);
		gridHide.heightHint = 0;
		gridHide.horizontalSpan = 2;
		cmpSetTypes.setLayoutData(gridHide);
	}

	private void showSetTypes() {
		GridData gridShow = new GridData(GridData.FILL_BOTH);
		gridShow.heightHint = 80;
		gridShow.horizontalSpan = 2;
		cmpSetTypes.setLayoutData(gridShow);

		TableLayout tLayout = new TableLayout();
		tLayout.addColumnData(new ColumnWeightData(60));
		tLayout.addColumnData(new ColumnWeightData(20));
		tLayout.addColumnData(new ColumnWeightData(20));
		tblSetTypes.setLayout(tLayout);
	}

	private void deleteRecord(int idx) {
		tblSetTypes.remove(idx);
		if (tblSetTypes.getItemCount() < 1
				|| !tblSetTypes.getItem(tblSetTypes.getItemCount() - 1)
						.getText(0).equals(
								Messages.getString("COMBOITEM.SELECTION"))) {
			TableItem emptyNew = new TableItem(tblSetTypes, SWT.NONE);
			emptyNew.setText(Messages.getString("COMBOITEM.SELECTION")); 
		}
	}

	private void setInfo() {
		if (ti == null)
			return;

		txtName.setText(ti.getText(0));
		cmbDomainType.setText(ti.getText(1));
		cmbDomainType.setEnabled(false);

		chkNotNull.setSelection(ti.getText(3).length() > 0);
		chkNotNull.setEnabled(!(PROPPAGE_CLASS_PAGE1Dialog.si.virtual
				.equals("proxy") || PROPPAGE_CLASS_PAGE1Dialog.si.virtual
				.equals("view")));

		chkShared.setSelection(ti.getText(4).length() > 0);
		chkShared.setEnabled(false);

		chkUnique.setSelection(ti.getText(5).length() > 0);
		chkUnique
				.setEnabled(!(ti.getText(8).length() > 0
						|| PROPPAGE_CLASS_PAGE1Dialog.si.virtual
								.equals("proxy") || PROPPAGE_CLASS_PAGE1Dialog.si.virtual
						.equals("view")));

		txtDefault.setText(ti.getText(6));

		chkClassAttribute.setSelection(ti.getText(8).length() > 0);
		chkClassAttribute.setEnabled(false);

		if (chkClassAttribute.getSelection()) {
			chkUnique.setEnabled(false);
			chkNotNull.setEnabled(false);
		}

		if (chkShared.getSelection()) {
			chkUnique.setEnabled(false);
		}
	}

	private String makeType() {
		String domain = cmbDomainType.getText();
		String errmsg = "";
		switch (cmbDomainType.getSelectionIndex()) {
		case 0:
			if (domain.length() < 1) {
				errmsg = Messages.getString("ERROR.INVALIDATTRTYPE");
				cmbDomainType.setFocus();
			}
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			if (txtDomainSize.getText().length() > 0) {
				int size = stringToUnsignedInteger(txtDomainSize.getText());
				if (size < 0) {
					errmsg = Messages.getString("ERROR.INVALIDSIZE");
					txtDomainSize.setFocus();
				} else if (size > 1073741823) {
					errmsg = Messages.getString("ERROR.MAXSIZEOVER");
					txtDomainSize.setFocus();
				} else
					domain += "(" + txtDomainSize.getText() + ")";
			}
			break;
		case 7:
			if (txtDomainSize.getText().length() > 0
					|| txtDomainScale.getText().length() > 0) {
				int precision = stringToUnsignedInteger(txtDomainSize.getText());
				int scale = stringToUnsignedInteger(txtDomainScale.getText());
				if (precision < 0) {
					errmsg = Messages.getString("ERROR.INVALIDPRECISION");
					txtDomainSize.setFocus();
				} else if (precision > 38) {
					errmsg = Messages.getString("ERROR.MAXPRECISIONOVER");
					txtDomainSize.setFocus();
				} else if (scale < 0) {
					if (txtDomainScale.getText().length() != 0) {
						errmsg = Messages.getString("ERROR.INVALIDSCALE");
						txtDomainScale.setFocus();
					} else
						domain += "(" + txtDomainSize.getText() + ")";
				} else if (scale > 38) {
					errmsg = Messages.getString("ERROR.MAXSCALEOVER");
					txtDomainScale.setFocus();
				} else if (precision < scale) {
					errmsg = Messages.getString("ERROR.INVALIDCOMBINATION");
					txtDomainSize.setFocus();
				} else
					domain += "(" + txtDomainSize.getText() + ","
							+ txtDomainScale.getText() + ")";
			}
			break;
		case 17:
		case 18:
		case 19:
			domain += "(";
			for (int i = 0; i < tblSetTypes.getItemCount() - 1; i++) {
				TableItem item = tblSetTypes.getItem(i);
				if (i > 0)
					domain += ",";

				domain += item.getText(0);

				if (item.getText(0).equals("CHAR")
						|| item.getText(0).equals("VARCHAR")
						|| item.getText(0).equals("NCHAR")
						|| item.getText(0).equals("NCHAR VARYING")
						|| item.getText(0).equals("BIT")
						|| item.getText(0).equals("BIT VARYING")) {
					if (item.getText(1).length() > 0) {
						int size = stringToUnsignedInteger(item.getText(1));
						if (size < 0) {
							errmsg = Messages.getString("ERROR.INVALIDSIZE");
							break;
						} else if (size > 1073741823) {
							errmsg = Messages.getString("ERROR.MAXSIZEOVER");
							break;
						} else
							domain += "(" + item.getText(1) + ")";
					}
				} else if (item.getText(0).equals("NUMERIC")) {
					if (item.getText(1).length() > 0
							|| item.getText(2).length() > 0) {
						int precision = stringToUnsignedInteger(item.getText(1));
						int scale = stringToUnsignedInteger(item.getText(2));
						if (precision < 0) {
							errmsg = Messages
									.getString("ERROR.INVALIDPRECISION");
							break;
						} else if (precision > 38) {
							errmsg = Messages
									.getString("ERROR.MAXPRECISIONOVER");
							break;
						} else if (scale < 0) {
							if (item.getText(2).length() != 0) {
								errmsg = Messages
										.getString("ERROR.INVALIDSCALE");
								break;
							} else
								domain += "(" + item.getText(1) + ")";
						} else if (scale > 38) {
							errmsg = Messages.getString("ERROR.MAXSCALEOVER");
							break;
						} else if (precision < scale) {
							errmsg = Messages
									.getString("ERROR.INVALIDCOMBINATION");
							break;
						} else
							domain += "(" + item.getText(1) + ","
									+ item.getText(2) + ")";
					}
				}
			}
			domain += ")";
			break;
		}

		if (errmsg.length() > 0) {
			CommonTool.ErrorBox(sShell, errmsg);
			domain = "";
		}

		return domain;
	}

	private int stringToUnsignedInteger(String strVal) {
		int retVal;
		try {
			retVal = Integer.parseInt(strVal);
		} catch (Exception e) {
			retVal = -1;
		}
		return retVal;
	}
}
