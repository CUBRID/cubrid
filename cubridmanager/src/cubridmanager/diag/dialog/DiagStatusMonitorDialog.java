/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.diag.dialog;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.util.ArrayList;
import java.util.Timer;
import java.util.TimerTask;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.GC;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.diag.DiagStatusMonitorTemplate;
import cubridmanager.diag.DiagStatusResult;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagStatusMonitorDialog extends Dialog implements ActionListener,
		Runnable {
	private int DiagEndCode = Window.CANCEL;
	// component
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="4,3"
	private Button button = null;
	private Button button1 = null;
	private Composite composite = null;
	private Canvas canvas = null;
	private Table table = null;
	private Label label = null;
	private Label label1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Text textCurrent = null;
	private Text textMax = null;
	private Text textAvr = null;
	private Text textMin = null;
	private Label label4 = null;
	private DiagStatusMonitorTemplate currentTemplate = null;
	private Timer timer = null;
	private int time_flag = 0;
	private static int time_axes_factor = 3;
	private GC gc = null;
	public ArrayList diagDataList = null;
	private Label label5 = null;
	private Label label6 = null;
	private Label label7 = null;
	private Label label8 = null;
	private Composite compositeDisplay = null;
	private Composite compositeMonitorValue = null;
	private Label label9 = null;
	private Label label10 = null;
	private Label label11 = null;
	private Label label13 = null;
	private Label label16 = null;
	private Label label19 = null;
	private Label label20 = null;
	private boolean initial_flag = true;
	private int send_count = 0;
	private boolean thread_is_ended = false;
	private boolean shell_disposed = false;
	public DiagStatusResult diagStatusResult = new DiagStatusResult();
	private DiagStatusResult diagOldStatusResult = new DiagStatusResult();

	public void actionPerformed(ActionEvent e) {
	}

	public class timerTask extends TimerTask {
		public void run() {
			time_flag = 1;
		}
	}

	public void run() {
		Display display = sShell.getDisplay();
		while (true) {
			if (shell_disposed)
				break;

			if (time_flag == 1) {
				if (sendStatusMonitorMsg() == -1) {
					DiagEndCode = SWT.CANCEL;
					thread_is_ended = true;
					break;
				}
				display.syncExec(new Runnable() {
					public void run() {
						if (!sShell.isDisposed())
							displayDiagData();
						else {
							timer.cancel();
							thread_is_ended = true;
							return;
						}

					}
				});
				time_flag = 0;
			}
			try {
				Thread.sleep(200);
			} catch (Exception e) {
			}
		}
	}

	public int sendStatusMonitorMsg() {
		/* send message to server */
		String msg = new String();
		ClientSocket cs = new ClientSocket();
		Shell psh = sShell;

		msg += "db_name:" + currentTemplate.targetdb + "\n";
		msg += "mon_db:yes\n";
		if (currentTemplate.monitor_config.dbData.need_mon_cub_query) {
			if (currentTemplate.monitor_config.dbData.query_open_page)
				msg += "mon_cub_query_open_page:yes\n";
			if (currentTemplate.monitor_config.dbData.query_opened_page)
				msg += "mon_cub_query_opened_page:yes\n";
			if (currentTemplate.monitor_config.dbData.query_slow_query)
				msg += "mon_cub_query_slow_query:yes\n";
			if (currentTemplate.monitor_config.dbData.query_full_scan)
				msg += "mon_cub_query_full_scan:yes\n";
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_conn) {
			if (currentTemplate.monitor_config.dbData.conn_cli_request)
				msg += "mon_cub_conn_cli_request:yes\n";
			if (currentTemplate.monitor_config.dbData.conn_aborted_clients)
				msg += "mon_cub_conn_aborted_clients:yes\n";
			if (currentTemplate.monitor_config.dbData.conn_conn_req)
				msg += "mon_cub_conn_conn_req:yes\n";
			if (currentTemplate.monitor_config.dbData.conn_conn_reject)
				msg += "mon_cub_conn_conn_reject:yes\n";
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_buffer) {
			if (currentTemplate.monitor_config.dbData.buffer_page_write)
				msg += "mon_cub_buffer_page_write:yes\n";
			if (currentTemplate.monitor_config.dbData.buffer_page_read)
				msg += "mon_cub_buffer_page_read:yes\n";
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_lock) {
			if (currentTemplate.monitor_config.dbData.lock_deadlock)
				msg += "mon_cub_lock_deadlock:yes\n";
			if (currentTemplate.monitor_config.dbData.lock_request)
				msg += "mon_cub_lock_request:yes\n";
		}
		if (currentTemplate.monitor_config.casData.needCasStatus) {
			msg += "mon_cas:yes\n";
			if (currentTemplate.monitor_config.casData.status_request)
				msg += "cas_mon_req:yes\n";
			if (currentTemplate.monitor_config.casData.status_transaction_sec)
				msg += "cas_mon_tran:yes\n";
			if (currentTemplate.monitor_config.casData.status_active_session)
				msg += "cas_mon_act_session:yes\n";
		}

		msg += "mon_driver:no\n";
		msg += "mon_resource:no\n";
		msg += "act_db:no\n";
		msg += "act_cas:no\n";
		msg += "act_driver:no\n";

		cs.socketOwner = this;
		cs.DiagMessageType = MainRegistry.DIAGMESSAGE_TYPE_STATUS;
		diagOldStatusResult.copy_from(diagStatusResult);

		if (!cs.SendMessageUsingSpecialDelimiter(psh, msg, "getdiagdata")) {
			CommonTool.ErrorBox(psh, cs.ErrorMsg);
			return -1;
		}

		send_count++;
		if (initial_flag && (send_count >= 2))
			initial_flag = false;

		return 0;
	}

	public DiagStatusMonitorDialog(Shell parent) {
		super(parent);
	}

	public DiagStatusMonitorDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal(String templateName) {
		diagDataList = new ArrayList();
		createSShell();
		CommonTool.centerShell(sShell);
		sShell.open();
		Display display = sShell.getDisplay();
		int time_interval;

		gc = new GC(canvas);
		gc.setForeground(display.getSystemColor(SWT.COLOR_BLUE));

		currentTemplate = MainRegistry.getStatusTemplateByName(MainRegistry
				.GetCurrentSiteName(), templateName);

		String title = new String(sShell.getText());
		sShell.setText(title + "(" + currentTemplate.templateName + ")");

		String subtitle = new String();
		subtitle = Messages.getString("LABEL.DIAGSAMPLINGTERM");
		subtitle += " : " + currentTemplate.sampling_term
				+ Messages.getString("QEDIT.SECOND");
		if (!currentTemplate.targetdb.trim().equals("")) {
			subtitle += "        " + Messages.getString("LABEL.TARGETDATABASE")
					+ " : " + currentTemplate.targetdb;
		}
		label9.setText(subtitle);

		if (currentTemplate == null) {
			/* make error */
			DiagEndCode = Window.OK;
			return DiagEndCode;
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_query) {
			if (currentTemplate.monitor_config.dbData.query_open_page) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "open_page");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.query_open_page_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.query_open_page_magnification));
			}
			if (currentTemplate.monitor_config.dbData.query_opened_page) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "opened_page");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.query_opened_page_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.query_opened_page_magnification));
			}

			if (currentTemplate.monitor_config.dbData.query_slow_query) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "slow_query");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.query_slow_query_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.query_slow_query_magnification));
			}

			if (currentTemplate.monitor_config.dbData.query_full_scan) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "full_scan");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.query_full_scan_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.query_full_scan_magnification));
			}
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_conn) {
			if (currentTemplate.monitor_config.dbData.conn_cli_request) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "client_request");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.conn_cli_request_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.conn_cli_request_magnification));
			}
			if (currentTemplate.monitor_config.dbData.conn_aborted_clients) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "aborted_clients");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.conn_aborted_clients_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.conn_aborted_clients_magnification));
			}
			if (currentTemplate.monitor_config.dbData.conn_conn_req) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "conn_request");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.conn_conn_req_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.conn_conn_req_magnification));
			}
			if (currentTemplate.monitor_config.dbData.conn_conn_reject) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "conn_rejected");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.conn_conn_reject_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.conn_conn_reject_magnification));
			}
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_buffer) {
			if (currentTemplate.monitor_config.dbData.buffer_page_write) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "buffer_page_write");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.buffer_page_write_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.buffer_page_write_magnification));
			}
			if (currentTemplate.monitor_config.dbData.buffer_page_read) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "buffer_page_read");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.buffer_page_read_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.buffer_page_read_magnification));
			}
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_lock) {
			if (currentTemplate.monitor_config.dbData.lock_deadlock) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "deadlock");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.lock_deadlock_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.lock_deadlock_magnification));
			}
			if (currentTemplate.monitor_config.dbData.lock_request) {
				TableItem item1 = new TableItem(table, SWT.NONE);
				item1.setText(0, "lock_request");
				item1.setText(1, "0");
				item1.setBackground(
								2,
								Display
										.getCurrent()
										.getSystemColor(
												currentTemplate.monitor_config.dbData.lock_request_color));
				item1.setText(
								3,
								String
										.valueOf(currentTemplate.monitor_config.dbData.lock_request_magnification));
			}
		}

		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_REQ()) {
			TableItem item1 = new TableItem(table, SWT.NONE);
			item1.setText(0, "Requests/Sec");
			item1.setText(1, "0");
			item1.setBackground(2, Display.getCurrent().getSystemColor(
					currentTemplate.monitor_config
							.GET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR()));
			item1.setText(3, String.valueOf(currentTemplate.monitor_config
					.GET_CLIENT_MONITOR_INFO_CAS_REQ_MAG()));
		}
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_ACT_SESSION()) {
			TableItem item1 = new TableItem(table, SWT.NONE);
			item1.setText(0, "Active Session");
			item1.setText(1, "0");
			item1.setBackground(
							2,
							Display
									.getCurrent()
									.getSystemColor(
											currentTemplate.monitor_config
													.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR()));
			item1.setText(3, String.valueOf(currentTemplate.monitor_config
					.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_MAG()));
		}
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_TRAN()) {
			TableItem item1 = new TableItem(table, SWT.NONE);
			item1.setText(0, "Transactions/Sec");
			item1.setText(1, "0");
			item1.setBackground(2, Display.getCurrent().getSystemColor(
					currentTemplate.monitor_config
							.GET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR()));
			item1.setText(3, String.valueOf(currentTemplate.monitor_config
					.GET_CLIENT_MONITOR_INFO_CAS_TRAN_MAG()));
		}

		if (table.getItemCount() > 0)
			table.setSelection(0);

		time_interval = (int) (Float.parseFloat(currentTemplate.sampling_term) * 1000);

		timer = new Timer(true); // run as a deamon
		timerTask task = new timerTask();
		timer.schedule(task, 1000, time_interval);
		shell_disposed = false;
		Thread thread = new Thread(this, "jobthread");
		thread.start();
		while (!sShell.isDisposed() && !thread_is_ended) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		timer.cancel();
		shell_disposed = true;
		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 90;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData2.grabExcessHorizontalSpace = false;
		gridData2.widthHint = 90;
		gridData2.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = -1;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData1.grabExcessHorizontalSpace = true;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		// sShell = new Shell(SWT.PRIMARY_MODAL | SWT.DIALOG_TRIM);
		sShell = new Shell(getParent(), SWT.PRIMARY_MODAL | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.DIAGSTATUSMONITOR"));
		sShell.setLayout(gridLayout);
		createComposite();
		label8 = new Label(sShell, SWT.NONE);
		label8.setLayoutData(gridData1);
		button1 = new Button(sShell, SWT.NONE);
		button1.setText(Messages.getString("BUTTON.DIAGHELP"));

		button1.setLayoutData(gridData3);
		button = new Button(sShell, SWT.NONE);
		button.setText(Messages.getString("BUTTON.CLOSE"));
		button.setLayoutData(gridData2);
		button.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});

		sShell.addShellListener(new org.eclipse.swt.events.ShellAdapter() {
			public void shellClosed(org.eclipse.swt.events.ShellEvent e) {
				gc.dispose();
			}
		});
		sShell.pack(true);
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 3;
		composite = new Composite(sShell, SWT.NONE);
		composite.setLayout(new GridLayout());
		composite.setLayoutData(gridData);
		createCompositeDisplay();
		createCompositeMonitorValue();
		createTable();
	}

	/**
	 * This method initializes canvas
	 * 
	 */
	private void createCanvas() {
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.horizontalSpan = 2;
		gridData15.heightHint = 200;
		gridData15.widthHint = 400;
		gridData15.verticalSpan = 7;
		canvas = new Canvas(compositeDisplay, SWT.NONE);
		canvas.setBackground(new Color(Display.getCurrent(), 100, 100, 100));
		canvas.setLayoutData(gridData15);
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.grabExcessVerticalSpace = true;
		gridData13.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData13.heightHint = 180;
		gridData13.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		table = new Table(composite, SWT.FULL_SELECTION | SWT.BORDER
				| SWT.MULTI);
		table.setHeaderVisible(true);
		table.setLayoutData(gridData13);
		table.setLinesVisible(true);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(60, 60, true));
		tlayout.addColumnData(new ColumnWeightData(60, 60, true));
		tlayout.addColumnData(new ColumnWeightData(15, 18, true));
		tlayout.addColumnData(new ColumnWeightData(60, 60, true));
		table.setLayout(tlayout);

		TableColumn NameColumn = new TableColumn(table, SWT.LEFT);
		TableColumn ValueColumn = new TableColumn(table, SWT.LEFT);
		TableColumn ColorColumn = new TableColumn(table, SWT.LEFT);
		TableColumn MagColumn = new TableColumn(table, SWT.LEFT);

		NameColumn.setText(Messages.getString("TABLE.DIAGNAME"));
		ValueColumn.setText(Messages.getString("TABLE.DIAGVALUE"));
		ColorColumn.setText(Messages.getString("TABLE.DIAGCOLOR"));
		ColorColumn.setResizable(false);
		MagColumn.setText(Messages.getString("TABLE.DIAGMAG"));

		NameColumn.setWidth(220);
		ValueColumn.setWidth(60);
		ColorColumn.setWidth(18);
		MagColumn.setWidth(50);

		table.addListener(SWT.Selection, new Listener() {
			public void handleEvent(Event event) {
				TableItem col = (TableItem) event.item;
				updateStatusValue(col.getText(0));
			}
		});

	}

	private void displayDiagData() {
		if (initial_flag)
			return;

		DiagStatusResult diagStatusResultDelta = new DiagStatusResult();
		diagStatusResultDelta.getDelta(diagStatusResult, diagOldStatusResult);

		if (currentTemplate.monitor_config.dbData.need_mon_cub_query) {
			if (currentTemplate.monitor_config.dbData.query_open_page) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("open_page")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_query_open_page);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.query_opened_page) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("opened_page")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_query_opened_page);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.query_slow_query) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("slow_query")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_query_slow_query);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.query_full_scan) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("full_scan")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_query_full_scan);
						break;
					}
				}
			}
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_conn) {
			if (currentTemplate.monitor_config.dbData.conn_cli_request) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("client_request")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_conn_cli_request);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.conn_aborted_clients) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("aborted_clients")) {
						table
								.getItem(i)
								.setText(
										1,
										diagStatusResultDelta.server_conn_aborted_clients);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.conn_conn_req) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("conn_request")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_conn_conn_req);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.conn_conn_reject) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("conn_rejected")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_conn_conn_reject);
						break;
					}
				}
			}
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_buffer) {
			if (currentTemplate.monitor_config.dbData.buffer_page_write) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("buffer_page_write")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_buffer_page_write);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.buffer_page_read) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("buffer_page_read")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_buffer_page_read);
						break;
					}
				}
			}
		}
		if (currentTemplate.monitor_config.dbData.need_mon_cub_lock) {
			if (currentTemplate.monitor_config.dbData.lock_deadlock) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("deadlock")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_lock_deadlock);
						break;
					}
				}
			}
			if (currentTemplate.monitor_config.dbData.lock_request) {
				for (int i = 0; i < table.getItemCount(); i++) {
					if (table.getItem(i).getText().equals("lock_request")) {
						table.getItem(i).setText(1,
								diagStatusResultDelta.server_lock_request);
						break;
					}
				}
			}
		}

		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_REQ()) {
			for (int i = 0; i < table.getItemCount(); i++) {
				if (table.getItem(i).getText().equals("Requests/Sec")) {
					table.getItem(i).setText(1,
							diagStatusResultDelta.GetCAS_Request_Sec());
					break;
				}
			}
		}
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_ACT_SESSION()) {
			for (int i = 0; i < table.getItemCount(); i++) {
				if (table.getItem(i).getText().equals("Active Session")) {
					table.getItem(i).setText(1,
							diagStatusResultDelta.GetCAS_Active_Session());
					break;
				}
			}
		}
		if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_TRAN()) {
			for (int i = 0; i < table.getItemCount(); i++) {
				if (table.getItem(i).getText().equals("Transactions/Sec")) {
					table.getItem(i).setText(1,
							diagStatusResultDelta.GetCAS_Transaction_Sec());
					break;
				}
			}
		}

		diagDataList.add(diagStatusResultDelta);

		updateStatusValue(table.getItem(table.getSelectionIndex()).getText());
		Display display = sShell.getDisplay();
		int canvas_right = canvas.getBounds().width;
		int canvas_bottom = canvas.getBounds().height;
		int canvas_top = 5;

		gc.fillRectangle(0, 0, canvas_right, canvas_bottom);
		canvas_right -= 1;
		canvas_bottom -= 15; // '0' position in value.

		for (int i = diagDataList.size() - 1, j = 0; i > 0; i--, j++) {
			int x_from = canvas_right - j * time_axes_factor;
			int x_to = canvas_right - (j + 1) * time_axes_factor;
			float y_factor = (canvas_bottom - canvas_top) / (float) 100;

			if (x_to < 0)
				continue;

			if (currentTemplate.monitor_config.dbData.need_mon_cub_query) {
				if (currentTemplate.monitor_config.dbData.query_open_page) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.query_open_page_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_query_open_page) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_query_open_page) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.query_open_page_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.query_opened_page) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.query_opened_page_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_query_opened_page) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_query_opened_page) * y_factor)
								+ canvas_top;
					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.query_opened_page_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.query_slow_query) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.query_slow_query_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_query_slow_query) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_query_slow_query) * y_factor)
								+ canvas_top;
					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.query_slow_query_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}

				if (currentTemplate.monitor_config.dbData.query_full_scan) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.query_full_scan_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_query_full_scan) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_query_full_scan) * y_factor)
								+ canvas_top;
					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.query_full_scan_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
			}
			if (currentTemplate.monitor_config.dbData.need_mon_cub_conn) {
				if (currentTemplate.monitor_config.dbData.conn_cli_request) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.conn_cli_request_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_conn_cli_request) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_conn_cli_request) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.conn_cli_request_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.conn_aborted_clients) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.conn_aborted_clients_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_conn_aborted_clients) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_conn_aborted_clients) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.conn_aborted_clients_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.conn_conn_req) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.conn_conn_req_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_conn_conn_req) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_conn_conn_req) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.conn_conn_req_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.conn_conn_reject) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.conn_conn_reject_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_conn_conn_reject) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_conn_conn_reject) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.conn_conn_reject_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
			}
			if (currentTemplate.monitor_config.dbData.need_mon_cub_buffer) {
				if (currentTemplate.monitor_config.dbData.buffer_page_write) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.buffer_page_write_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_buffer_page_write) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_buffer_page_write) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.buffer_page_write_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.buffer_page_read) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.buffer_page_read_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_buffer_page_read) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_buffer_page_read) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.buffer_page_read_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
			}
			if (currentTemplate.monitor_config.dbData.need_mon_cub_lock) {
				if (currentTemplate.monitor_config.dbData.lock_deadlock) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.lock_deadlock_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_lock_deadlock) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_lock_deadlock) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.lock_deadlock_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config.dbData.lock_request) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config.dbData.lock_request_magnification;

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i)).server_lock_request) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1)).server_lock_request) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config.dbData.lock_request_color));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
			}

			if (currentTemplate.monitor_config.NEED_CAS_MON_DATA()) {
				if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_REQ()) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config
							.GET_CLIENT_MONITOR_INFO_CAS_REQ_MAG();

					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i))
														.GetCAS_Request_Sec()) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1))
														.GetCAS_Request_Sec()) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
							.getSystemColor(currentTemplate.monitor_config
									.GET_CLIENT_MONITOR_INFO_CAS_REQ_COLOR()));
					gc.drawLine(x_from, y_from, x_to, y_to);

				}
				if (currentTemplate.monitor_config.NEED_CAS_MON_DATA_TRAN()) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config
							.GET_CLIENT_MONITOR_INFO_CAS_TRAN_MAG();
					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i))
														.GetCAS_Transaction_Sec()) * y_factor)
								+ canvas_top;

						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1))
														.GetCAS_Transaction_Sec()) * y_factor)
								+ canvas_top;

					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}
					gc.setForeground(display
							.getSystemColor(currentTemplate.monitor_config
									.GET_CLIENT_MONITOR_INFO_CAS_TRAN_COLOR()));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
				if (currentTemplate.monitor_config
						.NEED_CAS_MON_DATA_ACT_SESSION()) {
					int y_from, y_to;
					float y_mag = currentTemplate.monitor_config
							.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_MAG();
					try {
						y_from = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i))
														.GetCAS_Active_Session()) * y_factor)
								+ canvas_top;
						y_to = canvas_bottom
								- (int) (y_mag
										* Integer
												.parseInt(((DiagStatusResult) diagDataList
														.get(i - 1))
														.GetCAS_Active_Session()) * y_factor)
								+ canvas_top;
					} catch (Exception e) {
						y_from = canvas_bottom;
						y_to = canvas_bottom;
					}

					gc.setForeground(display
									.getSystemColor(currentTemplate.monitor_config
											.GET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION_COLOR()));
					gc.drawLine(x_from, y_from, x_to, y_to);
				}
			}
		}
	}

	public void updateStatusValue(String monitorString) {
		int size;
		int cur_val = 0;
		int min = Integer.MAX_VALUE;
		int max = Integer.MIN_VALUE;
		float avr = (float) 0.0;
		size = diagDataList.size();

		if (size < 1)
			return;

		if (monitorString.equals("open_page")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_query_open_page);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_query_open_page);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("opened_page")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_query_opened_page);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_query_opened_page);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("slow_query")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_query_slow_query);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_query_slow_query);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("full_scan")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_query_full_scan);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_query_full_scan);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("client_request")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_conn_cli_request);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_conn_cli_request);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("aborted_clients")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_conn_aborted_clients);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_conn_aborted_clients);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("conn_request")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_conn_conn_req);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_conn_conn_req);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("conn_rejected")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_conn_conn_reject);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_conn_conn_reject);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("buffer_page_write")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_buffer_page_write);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_buffer_page_write);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("buffer_page_read")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_buffer_page_read);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_buffer_page_read);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("deadlock")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_lock_deadlock);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_lock_deadlock);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("lock_request")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1))).server_lock_request);
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).server_lock_request);
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("Requests/Sec")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1)))
							.GetCAS_Request_Sec());
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).GetCAS_Request_Sec());
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("Active Session")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1)))
							.GetCAS_Active_Session());
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).GetCAS_Active_Session());
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else if (monitorString.equals("Transactions/Sec")) {
			textCurrent
					.setText(((DiagStatusResult) (diagDataList.get(size - 1)))
							.GetCAS_Transaction_Sec());
			for (int i = 0; i < size; i++) {
				cur_val = Integer.parseInt(((DiagStatusResult) (diagDataList
						.get(i))).GetCAS_Transaction_Sec());
				if (min > cur_val)
					min = cur_val;
				if (max < cur_val)
					max = cur_val;
				avr = (avr * (float) i + (float) cur_val) / (float) (i + 1);
			}
		} else
			return;

		textMin.setText(String.valueOf(min));
		textMax.setText(String.valueOf(max));
		textAvr.setText(String.valueOf(avr));
	}

	/**
	 * This method initializes compositeDisplay
	 * 
	 */
	private void createCompositeDisplay() {
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.heightHint = 0;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.heightHint = 10;
		GridData gridData22 = new org.eclipse.swt.layout.GridData();
		gridData22.heightHint = 2;
		gridData22.widthHint = 0;
		GridData gridData20 = new org.eclipse.swt.layout.GridData();
		gridData20.heightHint = 0;
		gridData20.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData20.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData20.widthHint = 0;
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.heightHint = 15;
		gridData18.grabExcessHorizontalSpace = false;
		gridData18.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData18.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData18.widthHint = 15;
		gridData18.grabExcessVerticalSpace = false;
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.heightHint = 65;
		gridData17.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData17.widthHint = 15;
		gridData17.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		gridData16.heightHint = 68;
		gridData16.horizontalIndent = 0;
		gridData16.widthHint = 18;
		gridData16.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.horizontalSpan = 2;
		gridData14.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData14.grabExcessHorizontalSpace = false;
		gridData14.grabExcessVerticalSpace = true;
		gridData14.verticalSpan = 1;
		gridData14.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 3;
		gridLayout2.verticalSpacing = 5;
		compositeDisplay = new Composite(composite, SWT.NONE);
		label4 = new Label(compositeDisplay, SWT.NONE);
		label4.setLayoutData(gridData14);
		label9 = new Label(compositeDisplay, SWT.NONE);
		label9.setLayoutData(gridData20);
		label13 = new Label(compositeDisplay, SWT.NONE);
		label13.setLayoutData(gridData22);
		createCanvas();
		label6 = new Label(compositeDisplay, SWT.RIGHT);
		label6.setText("100");
		label6.setLayoutData(gridData16);
		label16 = new Label(compositeDisplay, SWT.NONE);
		label16.setLayoutData(gridData19);
		label7 = new Label(compositeDisplay, SWT.RIGHT);
		label7.setText("50");
		label7.setLayoutData(gridData17);
		label20 = new Label(compositeDisplay, SWT.NONE);
		label5 = new Label(compositeDisplay, SWT.RIGHT);
		label5.setText("0");
		label5.setLayoutData(gridData18);
		label19 = new Label(compositeDisplay, SWT.NONE);
		label19.setLayoutData(gridData21);
		compositeDisplay.setLayout(gridLayout2);
	}

	/**
	 * This method initializes compositeMonitorValue
	 * 
	 */
	private void createCompositeMonitorValue() {
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.grabExcessHorizontalSpace = true;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.grabExcessVerticalSpace = false;
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.grabExcessHorizontalSpace = true;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.grabExcessHorizontalSpace = true;
		gridData10.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData10.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData9.grabExcessHorizontalSpace = true;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.widthHint = 40;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.widthHint = 40;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.widthHint = 40;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData5.grabExcessHorizontalSpace = false;
		gridData5.widthHint = 40;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 8;
		gridLayout1.horizontalSpacing = 5;
		gridLayout1.marginWidth = 5;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData4.grabExcessHorizontalSpace = false;
		gridData4.grabExcessVerticalSpace = true;
		gridData4.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		compositeMonitorValue = new Composite(composite, SWT.NONE);
		compositeMonitorValue.setLayoutData(gridData4);
		compositeMonitorValue.setLayout(gridLayout1);
		label = new Label(compositeMonitorValue, SWT.NONE);
		label.setText(Messages.getString("LABEL.CURRENT"));
		label.setLayoutData(gridData5);
		textCurrent = new Text(compositeMonitorValue, SWT.LEFT);
		textCurrent.setText("0.0");
		textCurrent.setLayoutData(gridData9);
		textCurrent.setEditable(false);
		label1 = new Label(compositeMonitorValue, SWT.NONE);
		label1.setText(Messages.getString("LABEL.AVR"));
		label1.setLayoutData(gridData6);
		textAvr = new Text(compositeMonitorValue, SWT.LEFT);
		textAvr.setText("0.0");
		textAvr.setLayoutData(gridData11);
		textAvr.setEditable(false);
		label2 = new Label(compositeMonitorValue, SWT.NONE);
		label2.setText(Messages.getString("LABEL.MAX"));
		label2.setLayoutData(gridData7);
		textMax = new Text(compositeMonitorValue, SWT.LEFT);
		textMax.setText("0.0");
		textMax.setLayoutData(gridData10);
		textMax.setEditable(false);
		label3 = new Label(compositeMonitorValue, SWT.NONE);
		label3.setText(Messages.getString("LABEL.MIN"));
		label3.setLayoutData(gridData8);
		textMin = new Text(compositeMonitorValue, SWT.LEFT);
		textMin.setText("0.0");
		textMin.setLayoutData(gridData12);
		textMin.setEditable(false);
	}
}
