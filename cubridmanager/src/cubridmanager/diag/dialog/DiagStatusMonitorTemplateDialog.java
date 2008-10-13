package cubridmanager.diag.dialog;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Spinner;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.TreeItem;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.diag.DiagMonitorConfig;
import cubridmanager.diag.DiagStatusMonitorTemplate;
import cubridmanager.diag.view.DiagView;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagStatusMonitorTemplateDialog extends Dialog {
	private String tmpMag = null;
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="29,11"
	private Composite composite = null;
	private Group group = null;
	private Button buttonAdd = null;
	private Button buttonRemove = null;
	private Tree treeStatusMonitorSource = null;
	private Label label = null;
	private Label labeldummy = null;
	private Label label1 = null;
	private Group group1 = null;
	private Label label5 = null;
	private Label label6 = null;
	private Text textDesc = null;
	private Button button4 = null;
	private Button button = null;
	private Text textTemplateName = null;
	private Label label4 = null;
	private Spinner spinner = null;
	private Table tableSelectedItem = null;
	private Combo comboColor = null;
	private Text textMag = null;
	private Label label2 = null;
	private Label label3 = null;
	private boolean isUpdate = false;
	private Composite compositeTargetObject = null;
	private Label label8 = null;
	private Label label7 = null;
	private Composite compositeTreeList = null;
	private Label label15 = null;
	private Label label14 = null;
	private Combo comboTargetDatabase = null;

	public DiagStatusMonitorTemplateDialog(Shell parent) {
		super(parent);
	}

	public DiagStatusMonitorTemplateDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal(String templateName) {
		MainRegistry.SetCurrentSiteName(MainRegistry.HostDesc);
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();
		Display display = sShell.getDisplay();

		/* set combo(targetdbname) */
		for (int i = 0; i < MainRegistry.Authinfo.size(); i++) {
			AuthItem item = (AuthItem) MainRegistry.Authinfo.get(i);
			comboTargetDatabase.add(item.dbname);
		}

		if (templateName != null) {
			// update template
			sShell.setText(Messages.getString("TOOL.DIAG_UPDATETEMPLATE"));
			String currentSiteName = MainRegistry.GetCurrentSiteName();
			DiagStatusMonitorTemplate diagStatusMonitorTemplate = MainRegistry
					.getStatusTemplateByName(currentSiteName, templateName);

			if (diagStatusMonitorTemplate == null) {
				CommonTool.ErrorBox(Messages
						.getString("ERROR.TEMPLATENOTEXIST"));
				sShell.dispose();
			} else {
				if (!diagStatusMonitorTemplate.targetdb.trim().equals("")) {
					comboTargetDatabase.setEnabled(true);
					comboTargetDatabase
							.setText(diagStatusMonitorTemplate.targetdb);
				}

				textTemplateName
						.setText(diagStatusMonitorTemplate.templateName);
				textDesc.setText(diagStatusMonitorTemplate.desc);
				int sTerm;
				try {
					sTerm = Integer
							.parseInt(diagStatusMonitorTemplate.sampling_term);
				} catch (Exception ee) {
					sTerm = 1;
				}
				spinner.setSelection(sTerm);
				setCurrentMonitorConfig(diagStatusMonitorTemplate.monitor_config);
			}

			isUpdate = true;
			textTemplateName.setEditable(false);
		} else {
			sShell.setText(Messages.getString("TOOL.DIAGSTATUS_NEWTEMPLATE"));
		}

		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.grabExcessHorizontalSpace = true;
		gridData13.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData13.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.grabExcessHorizontalSpace = true;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.widthHint = 150;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		// sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell = new Shell(getParent(), SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		// sShell.setText(Messages.getString("TITLE.STATUSTEMPLATECONFIG"));
		// sShell.setSize(new org.eclipse.swt.graphics.Point(605,534));
		sShell.setLayout(gridLayout1);
		createComposite();
		label15 = new Label(sShell, SWT.NONE);
		label15.setLayoutData(gridData10);
		button = new Button(sShell, SWT.NONE);
		button.setText(Messages.getString("BUTTON.OK"));
		button.setLayoutData(gridData13);
		button
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = null;
						String name = textTemplateName.getText().trim();
						String desc = textDesc.getText().trim();

						if (name.length() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.EMPTYTEMPLATENAME"));
							return;
						}

						if (tableSelectedItem.getItemCount() == 0) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.EMPTYDIAGTARGETLIST"));
							return;
						}

						if (desc.length() == 0)
							desc = " ";
						if ((comboTargetDatabase.getEnabled() == true)
								&& (comboTargetDatabase.getText().trim()
										.equals(""))) {
							CommonTool.ErrorBox(sShell, Messages
									.getString("ERROR.SELECTDATABASENAME"));
							return;
						}

						msg = new String(makeMsg(name, desc));
						ClientSocket cs = new ClientSocket();
						Shell psh = sShell;

						if (isUpdate == true) {
							if (!cs.SendClientMessage(psh, msg,
									"updatestatustemplate")) {
								CommonTool.ErrorBox(psh, cs.ErrorMsg);
								return;
							}
						} else {
							if (!cs.SendClientMessage(psh, msg,
									"addstatustemplate")) {
								CommonTool.ErrorBox(psh, cs.ErrorMsg);
								return;
							}
						}

						DiagView.refresh();
						sShell.dispose();
					}
				});
		button4 = new Button(sShell, SWT.NONE);
		button4.setText(Messages.getString("BUTTON.CANCEL"));
		button4.setLayoutData(gridData12);
		button4.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
		sShell.pack(true);
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalSpan = 4;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		composite = new Composite(sShell, SWT.NONE);
		composite.setLayout(new GridLayout());
		createGroup();
		composite.setLayoutData(gridData9);
		createGroup1();
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.grabExcessHorizontalSpace = true;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group = new Group(composite, SWT.NONE);
		group.setText(Messages.getString("LABEL.TARGETOBJECT"));
		group.setLayout(new GridLayout());
		createCompositeTreeList();
		group.setLayoutData(gridData7);
		createCompositeTargetObject();
	}

	/**
	 * This method initializes tree
	 * 
	 */
	private void createTree() {
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.verticalSpan = 3;
		gridData22.heightHint = 250;
		gridData22.widthHint = 180;
		treeStatusMonitorSource = new Tree(compositeTreeList, SWT.BORDER);
		treeStatusMonitorSource.setLayoutData(gridData22);
		insertTreeObject();
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 130;
		GridData gridData20 = new org.eclipse.swt.layout.GridData();
		gridData20.widthHint = 10;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.widthHint = 180;
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.widthHint = 80;
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.widthHint = 80;
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.widthHint = 100;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.widthHint = 180;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.widthHint = 10;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 5;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.grabExcessHorizontalSpace = true;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group1 = new Group(composite, SWT.NONE);
		group1.setText(Messages.getString("LABEL.TEMPLATE"));
		group1.setLayout(gridLayout2);
		group1.setLayoutData(gridData8);
		label5 = new Label(group1, SWT.NONE);
		label5.setText(Messages.getString("LABEL.DIAGTEMPLATENAME"));
		label5.setLayoutData(gridData17);
		textTemplateName = new Text(group1, SWT.BORDER);
		textTemplateName.setLayoutData(gridData19);
		Label dummy = new Label(group1, SWT.NONE);
		dummy.setLayoutData(gridData14);
		label4 = new Label(group1, SWT.NONE);
		label4.setText(Messages.getString("LABEL.DIAGSAMPLINGTERM_SEC"));
		label4.setLayoutData(gridData21);
		spinner = new Spinner(group1, SWT.NONE | SWT.BORDER);
		spinner.setSelection(1);
		spinner.setLayoutData(gridData16);
		label6 = new Label(group1, SWT.NONE);
		label6.setText(Messages.getString("LABEL.DIAGTEMPLATEDESC"));
		label6.setLayoutData(gridData18);
		textDesc = new Text(group1, SWT.BORDER);
		textDesc.setLayoutData(gridData15);
		Label labelDummy2 = new Label(group1, SWT.NONE);
		labelDummy2.setLayoutData(gridData20);
		label14 = new Label(group1, SWT.NONE);
		label14.setText(Messages.getString("LABEL.TARGETDATABASE"));
		label14.setLayoutData(gridData21);
		createCombo();
	}

	public void insertTreeObject() {
		TreeItem root = new TreeItem(treeStatusMonitorSource, SWT.NONE);
		TreeItem db = new TreeItem(root, SWT.NONE);
		TreeItem cas = new TreeItem(root, SWT.NONE);
		root.setExpanded(true);

		root.setText(Messages.getString("LABEL.STATUSMONITORLIST"));
		db.setText(Messages.getString("LABEL.STATUSMONITORLIST_DB"));
		cas.setText(Messages.getString("LABEL.STATUSMONITORLIST_BROKER"));

		// TreeItem query_open_page = new TreeItem(parent, SWT.NONE);
		TreeItem query_opened_page = new TreeItem(db, SWT.NONE);
		TreeItem query_slow_query = new TreeItem(db, SWT.NONE);
		TreeItem query_full_scan = new TreeItem(db, SWT.NONE);
		TreeItem conn_cli_request = new TreeItem(db, SWT.NONE);
		TreeItem conn_aborted_clients = new TreeItem(db, SWT.NONE);
		TreeItem conn_conn_req = new TreeItem(db, SWT.NONE);
		TreeItem conn_conn_reject = new TreeItem(db, SWT.NONE);
		TreeItem buffer_page_write = new TreeItem(db, SWT.NONE);
		TreeItem buffer_page_read = new TreeItem(db, SWT.NONE);
		TreeItem lock_deadlock = new TreeItem(db, SWT.NONE);
		TreeItem lock_request = new TreeItem(db, SWT.NONE);

		// query_open_page.setText("open_page");
		query_opened_page.setText("opened_page");
		query_slow_query.setText("slow_query");
		query_full_scan.setText("full_scan");
		conn_cli_request.setText("client_request");
		conn_aborted_clients.setText("aborted_clients");
		conn_conn_req.setText("conn_request");
		conn_conn_reject.setText("conn_rejected");
		buffer_page_write.setText("buffer_page_write");
		buffer_page_read.setText("buffer_page_read");
		lock_deadlock.setText("deadlock");
		lock_request.setText("lock_request");

		db.setExpanded(true);

		TreeItem request_sec = new TreeItem(cas, SWT.NONE);
		TreeItem transaction_sec = new TreeItem(cas, SWT.NONE);
		TreeItem active_session = new TreeItem(cas, SWT.NONE);
		request_sec.setText("request_sec");
		active_session.setText("active_session");
		transaction_sec.setText("transaction_sec");

		cas.setExpanded(true);
	}

	public String makeMsg(String name, String desc) {
		String msg = new String();
		String sampling_term = String.valueOf(spinner.getSelection());
		String targetdb = comboTargetDatabase.getText();
		float mag;
		int color;

		msg = "name:";
		msg += name;
		msg += "\n";
		msg += "desc:" + desc + "\n";
		if (targetdb.trim().length() != 0)
			msg += "db_name:" + targetdb + "\n";
		msg += "sampling_term:" + sampling_term + "\n";
		msg += "open:target_config\n";
		for (int i = 0; i < tableSelectedItem.getItemCount(); i++) {
			TableItem currentItem = tableSelectedItem.getItem(i);
			color = DiagStatusMonitorTemplate
					.getCurrentColorConstant(currentItem.getText(2));
			try {
				mag = Float.parseFloat(currentItem.getText(3));
			} catch (Exception ee) {
				mag = (float) 1.0;
			}

			if (currentItem.getText(0).equals("broker")) {
				if (currentItem.getText(1).equals("request_sec")) {
					msg += "cas_st_request:";
				} else if (currentItem.getText(1).equals("active_session")) {
					msg += "cas_st_active_session:";
				} else if (currentItem.getText(1).equals("transaction_sec")) {
					msg += "cas_st_transaction:";
				}
			} else if (currentItem.getText(0).equals("server_query")) {
				if (currentItem.getText(1).equals("open_page")) {
					msg += "server_query_open_page:";
				} else if (currentItem.getText(1).equals("opened_page")) {
					msg += "server_query_opened_page:";
				} else if (currentItem.getText(1).equals("slow_query")) {
					msg += "server_query_slow_query:";
				} else if (currentItem.getText(1).equals("full_scan")) {
					msg += "server_query_full_scan:";
				}
			} else if (currentItem.getText(0).equals("server_connection")) {
				if (currentItem.getText(1).equals("client_request")) {
					msg += "server_conn_cli_request:";
				} else if (currentItem.getText(1).equals("aborted_clients")) {
					msg += "server_conn_aborted_clients:";
				} else if (currentItem.getText(1).equals("conn_request")) {
					msg += "server_conn_conn_req:";
				} else if (currentItem.getText(1).equals("conn_rejected")) {
					msg += "server_conn_conn_reject:";
				}
			} else if (currentItem.getText(0).equals("server_buffer")) {
				if (currentItem.getText(1).equals("buffer_page_write")) {
					msg += "server_buffer_page_write:";
				} else if (currentItem.getText(1).equals("buffer_page_read")) {
					msg += "server_buffer_page_read:";
				}
			} else if (currentItem.getText(0).equals("server_lock")) {
				if (currentItem.getText(1).equals("deadlock")) {
					msg += "server_lock_deadlock:";
				} else if (currentItem.getText(1).equals("lock_request")) {
					msg += "server_lock_request:";
				}
			} else if (currentItem.getText(0).equals("driver")) {
			} else if (currentItem.getText(0).equals("resource")) {
			} else
				continue;

			msg += color;
			msg += " " + mag;
			msg += "\n";
		}

		msg += "close:target_config\n";
		return msg;
	}

	public int ColorWhitetoBlack(int color) {
		if (color == SWT.COLOR_WHITE)
			return SWT.COLOR_BLACK;

		return color;
	}

	public String setCurrentMonitorConfig(DiagMonitorConfig config) {
		if (config.dbData.need_mon_cub_query) {
			if (config.dbData.query_full_scan) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_query");
				newItem.setText(1, "full_scan");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.query_full_scan_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.query_full_scan_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.query_full_scan_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.query_open_page) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_query");
				newItem.setText(1, "open_page");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.query_open_page_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.query_open_page_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.query_open_page_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.query_opened_page) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_query");
				newItem.setText(1, "opened_page");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.query_opened_page_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.query_opened_page_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.query_opened_page_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.query_slow_query) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_query");
				newItem.setText(1, "slow_query");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.query_slow_query_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.query_slow_query_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.query_slow_query_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
		}
		if (config.dbData.need_mon_cub_buffer) {
			if (config.dbData.buffer_page_read) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_buffer");
				newItem.setText(1, "buffer_page_read");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.buffer_page_read_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.buffer_page_read_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.buffer_page_read_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.buffer_page_write) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_buffer");
				newItem.setText(1, "buffer_page_write");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.buffer_page_write_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.buffer_page_write_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.buffer_page_write_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
		}
		if (config.dbData.need_mon_cub_conn) {
			if (config.dbData.conn_aborted_clients) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_connection");
				newItem.setText(1, "aborted_clients");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.conn_aborted_clients_color)));
				newItem.setText(
								2,
								DiagStatusMonitorTemplate
										.getColorString(config.dbData.conn_aborted_clients_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.conn_aborted_clients_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.conn_cli_request) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_connection");
				newItem.setText(1, "client_request");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.conn_cli_request_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.conn_cli_request_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.conn_cli_request_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.conn_conn_reject) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_connection");
				newItem.setText(1, "conn_rejected");
				newItem.setForeground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												ColorWhitetoBlack(config.dbData.conn_conn_reject_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.conn_conn_reject_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.conn_conn_reject_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.conn_conn_req) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_connection");
				newItem.setText(1, "conn_request");
				newItem.setForeground(2, Display.getCurrent().getSystemColor(
						ColorWhitetoBlack(config.dbData.conn_conn_req_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.conn_conn_req_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.conn_conn_req_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
		}
		if (config.dbData.need_mon_cub_lock) {
			if (config.dbData.lock_deadlock) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_lock");
				newItem.setText(1, "deadlock");
				newItem.setForeground(2, Display.getCurrent().getSystemColor(
						ColorWhitetoBlack(config.dbData.lock_deadlock_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.lock_deadlock_color));
				try {
					newItem.setText(
									3,
									String
											.valueOf(config.dbData.lock_deadlock_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
			if (config.dbData.lock_request) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "server_lock");
				newItem.setText(1, "lock_request");
				newItem.setForeground(2, Display.getCurrent().getSystemColor(
						ColorWhitetoBlack(config.dbData.lock_request_color)));
				newItem.setText(2, DiagStatusMonitorTemplate
						.getColorString(config.dbData.lock_request_color));
				try {
					newItem.setText(3, String
							.valueOf(config.dbData.lock_request_magnification));
				} catch (Exception ee) {
					newItem.setText(3, "1.0");
				}
			}
		}
		if (config.NEED_CAS_MON_DATA_REQ()) {
			TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
			newItem.setText(0, "broker");
			newItem.setText(1, "request_sec");
			newItem.setForeground(2, Display.getCurrent().getSystemColor(
					ColorWhitetoBlack(config
							.GET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR())));
			newItem.setText(2, DiagStatusMonitorTemplate.getColorString(config
					.GET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR()));
			try {
				newItem.setText(3, String.valueOf(config
						.GET_CLIENT_MONITOR_INFO_CAS_REQ_MAG()));
			} catch (Exception ee) {
				newItem.setText(3, "1.0");
			}
		}
		if (config.NEED_CAS_MON_DATA_TRAN()) {
			TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
			newItem.setText(0, "broker");
			newItem.setText(1, "transaction_sec");
			newItem.setForeground(2, Display.getCurrent().getSystemColor(
					ColorWhitetoBlack(config
							.GET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR())));
			newItem.setText(2, DiagStatusMonitorTemplate.getColorString(config
					.GET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR()));
			try {
				newItem.setText(3, String.valueOf(config
						.GET_CLIENT_MONITOR_INFO_CAS_TRAN_MAG()));
			} catch (Exception ee) {
				newItem.setText(3, "1.0");
			}
		}
		if (config.NEED_CAS_MON_DATA_ACT_SESSION()) {
			TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
			newItem.setText(0, "broker");
			newItem.setText(1, "active_session");
			newItem.setForeground(
							2,
							Display
									.getCurrent()
									.getSystemColor(
											ColorWhitetoBlack(config
													.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR())));
			newItem.setText(2, DiagStatusMonitorTemplate.getColorString(config
					.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR()));
			try {
				newItem.setText(3, String.valueOf(config
						.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_MAG()));
			} catch (Exception ee) {
				newItem.setText(3, "1.0");
			}
		}

		return "";
	}

	/**
	 * This method initializes tableSelectedItem
	 * 
	 */
	private void createTableSelectedItem() {
		GridData gridData23 = new org.eclipse.swt.layout.GridData();
		gridData23.verticalSpan = 3;
		gridData23.heightHint = 250;
		gridData23.widthHint = 250;
		tableSelectedItem = new Table(compositeTreeList, SWT.BORDER
				| SWT.FULL_SELECTION);
		tableSelectedItem.setHeaderVisible(true);
		tableSelectedItem.setLayoutData(gridData23);
		tableSelectedItem.setLinesVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(30, 40, true));
		tlayout.addColumnData(new ColumnWeightData(60, 80, true));
		tlayout.addColumnData(new ColumnWeightData(30, 40, true));
		tlayout.addColumnData(new ColumnWeightData(10, 20, true));
		tableSelectedItem.setLayout(tlayout);

		tableSelectedItem
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						comboColor.setText(tableSelectedItem.getSelection()[0]
								.getText(2));
						textMag.setText(tableSelectedItem.getSelection()[0]
								.getText(3));
					}
				});
		TableColumn categoryColumn = new TableColumn(tableSelectedItem,
				SWT.LEFT);
		TableColumn nameColumn = new TableColumn(tableSelectedItem, SWT.LEFT);
		TableColumn colorColumn = new TableColumn(tableSelectedItem, SWT.LEFT);
		TableColumn magColumn = new TableColumn(tableSelectedItem, SWT.LEFT);

		categoryColumn.setText(Messages.getString("TABLE.DIAGCATEGORY"));
		nameColumn.setText(Messages.getString("TABLE.DIAGNAME"));
		colorColumn.setText(Messages.getString("TABLE.DIAGCOLOR"));
		magColumn.setText(Messages.getString("TABLE.DIAGMAG"));

		categoryColumn.setWidth(40);
		nameColumn.setWidth(130);
		colorColumn.setWidth(70);
		magColumn.setWidth(40);
	}

	public String insertItemToTargetList(TreeItem item) {
		TreeItem parent = item.getParentItem();

		if (parent == null)
			return ""; // root node clicked

		TreeItem pparent = parent.getParentItem();

		if (pparent == null) {
			// depth 1 - db, cas, driver, resource ...
			TreeItem currentItem = null;

			for (int i = 0; i < item.getItemCount(); i++) {
				currentItem = item.getItems()[i];
				insertItemToTargetList(currentItem);
			}
		} else {
			// if
			// (parent.getParentItem().getText().equals(source_root.getText()))
			// {
			// leaf node
			int targetCount = tableSelectedItem.getItemCount();
			int i;
			for (i = 0; i < targetCount; i++) {
				if ((parent.getText().equals("db") && (tableSelectedItem
						.getItem(i).getText().indexOf("server") == 0))
						|| parent.getText().equals(
								tableSelectedItem.getItem(i).getText(0))) {
					// category is same
					if (item.getText().equals(
							tableSelectedItem.getItem(i).getText(1))) {
						// and item name is same too, then that is already existed.
						break;
					}
				}
			}

			if (i == targetCount) {
				// add item
				String colorString, magString;
				colorString = comboColor.getText();
				magString = textMag.getText();
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				if (parent.getText().equals("db")) {
					comboTargetDatabase.setEnabled(true);

					String current = item.getText();
					if (current.equals("open_page")
							|| current.equals("opened_page")
							|| current.equals("slow_query")
							|| current.equals("full_scan")) {
						newItem.setText(0, "server_query");
					} else if (current.equals("client_request")
							|| current.equals("aborted_clients")
							|| current.equals("conn_request")
							|| current.equals("conn_rejected")) {
						newItem.setText(0, "server_connection");
					} else if (current.equals("buffer_page_write")
							|| current.equals("buffer_page_read")) {
						newItem.setText(0, "server_buffer");
					} else if (current.equals("deadlock")
							|| current.equals("lock_request")) {
						newItem.setText(0, "server_lock");
					}
				} else {
					newItem.setText(0, parent.getText());
				}

				int colorIndex = comboColor.getSelectionIndex();

				newItem.setText(1, item.getText());
				if (colorString.equals("WHITE"))
					newItem.setForeground(2, Display.getCurrent()
							.getSystemColor(SWT.COLOR_BLACK));
				else
					newItem.setForeground(2, Display.getCurrent()
							.getSystemColor(colorIndex + 1));

				newItem.setText(2, colorString);
				newItem.setText(3, magString);

				/* combo color change */
				colorIndex = (colorIndex + 1) % comboColor.getItemCount();
				comboColor.select(colorIndex);
			}
		}

		return "";
	}

	/**
	 * This method initializes comboColor
	 * 
	 */
	private void createComboColor() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData5.widthHint = 65;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		comboColor = new Combo(compositeTargetObject, SWT.READ_ONLY);
		comboColor.setEnabled(true);
		comboColor.setLayoutData(gridData5);
		comboColor.addSelectionListener(new org.eclipse.swt.events.SelectionListener() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int selectedCount = tableSelectedItem
								.getSelectionCount();
						if (selectedCount > 0) {
							for (int i = 0; i < selectedCount; i++) {
								int colorIndex = comboColor.getSelectionIndex();
								String colorString = comboColor.getText();

								if (colorString.equals("WHITE"))
									tableSelectedItem.getSelection()[i]
											.setForeground(2, Display
													.getCurrent()
													.getSystemColor(
															SWT.COLOR_BLACK));
								else
									tableSelectedItem.getSelection()[i]
											.setForeground(2, Display
													.getCurrent()
													.getSystemColor(
															colorIndex + 1));

								tableSelectedItem.getSelection()[i].setText(2,
										comboColor.getText());
							}
						}
					}

					public void widgetDefaultSelected(
							org.eclipse.swt.events.SelectionEvent e) {
					}
				});

		for (int i = 1; i < DiagStatusMonitorTemplate.color_size; i++) {
			comboColor.add(DiagStatusMonitorTemplate.getColorString(i));
		}
		comboColor.select(0);
	}

	/**
	 * This method initializes compositeTargetObject
	 * 
	 */
	private void createCompositeTargetObject() {
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		gridData51.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData51.grabExcessHorizontalSpace = true;
		gridData51.grabExcessVerticalSpace = true;
		gridData51.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.grabExcessHorizontalSpace = false;
		gridData4.widthHint = 50;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 0;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 80;
		gridData21.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.widthHint = 30;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 25;
		gridData2.grabExcessHorizontalSpace = false;
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData2.horizontalSpan = 4;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 50;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = false;
		gridData.widthHint = 200;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 6;
		compositeTargetObject = new Composite(group, SWT.NONE);
		compositeTargetObject.setLayout(gridLayout);
		compositeTargetObject.setLayoutData(gridData51);
		label7 = new Label(compositeTargetObject, SWT.NONE);
		label7.setLayoutData(gridData3);
		labeldummy = new Label(compositeTargetObject, SWT.NONE);
		labeldummy.setLayoutData(gridData1);
		label2 = new Label(compositeTargetObject, SWT.RIGHT);
		label2.setText(Messages.getString("TABLE.DIAGCOLOR"));
		label2.setLayoutData(gridData11);
		createComboColor();
		label3 = new Label(compositeTargetObject, SWT.RIGHT);
		label3.setText(Messages.getString("TABLE.DIAGMAG"));
		label3.setLayoutData(gridData21);
		textMag = new Text(compositeTargetObject, SWT.BORDER);
		textMag.setText("0.1");
		textMag.setLayoutData(gridData4);
		label = new Label(compositeTargetObject, SWT.CENTER);
		label.setText(Messages.getString("LABEL.OBJECTSOURCE"));
		label.setLayoutData(gridData);
		textMag.addVerifyListener(new org.eclipse.swt.events.VerifyListener() {
			public void verifyText(org.eclipse.swt.events.VerifyEvent e) {
				tmpMag = textMag.getText();
			}
		});
		textMag.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
			public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
				String newMag = textMag.getText();
				try {
					Float.parseFloat(newMag);
				} catch (Exception ee) {
					CommonTool.ErrorBox(sShell, ee.getMessage());
					textMag.setText(tmpMag);
					return;
				}

				int selectedCount = tableSelectedItem.getSelectionCount();
				if (selectedCount > 0) {
					for (int i = 0; i < selectedCount; i++) {
						tableSelectedItem.getSelection()[i].setText(3, textMag
								.getText());
					}
				}
			}
		});
		label8 = new Label(compositeTargetObject, SWT.NONE);
		label1 = new Label(compositeTargetObject, SWT.CENTER);
		label1.setText(Messages.getString("LABEL.OBJECTTARGET"));
		label1.setLayoutData(gridData2);
		compositeTargetObject.pack(true);
	}

	/**
	 * This method initializes compositeTreeList
	 * 
	 */
	private void createCompositeTreeList() {
		GridData gridData26 = new org.eclipse.swt.layout.GridData();
		gridData26.heightHint = 120;
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.widthHint = 65;
		gridData25.verticalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData25.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData24 = new org.eclipse.swt.layout.GridData();
		gridData24.widthHint = 65;
		gridData24.verticalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData24.horizontalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout3 = new GridLayout();
		gridLayout3.numColumns = 3;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		compositeTreeList = new Composite(group, SWT.NONE);
		createTree();
		compositeTreeList.setLayout(gridLayout3);
		compositeTreeList.setLayoutData(gridData6);
		Label labeldummy = new Label(compositeTreeList, SWT.NONE);
		labeldummy.setLayoutData(gridData26);
		createTableSelectedItem();
		buttonAdd = new Button(compositeTreeList, SWT.NONE);
		buttonAdd.setText(Messages.getString("BUTTON.DIAGADD"));
		buttonAdd.setLayoutData(gridData24);
		buttonAdd.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = new String();
						for (int i = 0; i < treeStatusMonitorSource
								.getSelectionCount(); i++) {
							msg = "";
							TreeItem item = (treeStatusMonitorSource
									.getSelection())[i];
							msg = insertItemToTargetList(item);
							if (msg != "") {
								CommonTool.ErrorBox(msg);
							}
						}
					}
				});
		buttonRemove = new Button(compositeTreeList, SWT.NONE);
		buttonRemove.setText(Messages.getString("BUTTON.DIAGREMOVE"));
		buttonRemove.setLayoutData(gridData25);
		buttonRemove.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int i;
						TableItem selectedItem = null;

						for (i = 0; i < tableSelectedItem.getSelectionCount(); i++) {
							selectedItem = tableSelectedItem.getSelection()[i];
							selectedItem.dispose();
						}

						for (i = 0; i < tableSelectedItem.getItemCount(); i++) {
							if (tableSelectedItem.getItem(i).getText(0)
									.indexOf("server") == 0) {
								break;
							}
						}
						if (i == tableSelectedItem.getItemCount())
							comboTargetDatabase.setEnabled(false);
					}
				});
	}

	/**
	 * This method initializes combo
	 * 
	 */
	private void createCombo() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.widthHint = 95;
		comboTargetDatabase = new Combo(group1, SWT.READ_ONLY);
		comboTargetDatabase.setEnabled(false);
		comboTargetDatabase.setLayoutData(gridData);
	}
}
