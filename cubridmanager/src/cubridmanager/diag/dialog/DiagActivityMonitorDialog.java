package cubridmanager.diag.dialog;

import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.util.ArrayList;
import java.util.Date;

import javax.swing.Timer;

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Table;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.diag.DiagActivityMonitorTemplate;
import cubridmanager.diag.DiagActivityResult;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;

public class DiagActivityMonitorDialog extends Dialog implements
		ActionListener, Runnable {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="36,8"
	private Button buttonCloseDialog = null;
	private Composite composite = null;
	private Table tableActivityMonitor = null;
	private Group group = null;
	private Text textLogFileName = null;
	private Button buttonCheckLogStartTime = null;
	private Button buttonCheckLogEndTime = null;
	private Text textLogStartTime = null;
	private Text textLogEndTime = null;
	private DiagActivityMonitorTemplate currentTemplate = null;
	private Timer timer = null;
	private int time_flag = 0;
	public ArrayList diagDataList = null;
	private Button buttonLogStart = null;
	private Button buttonLogEnd = null;
	private Label labelLogName = null;
	private Text textLogName = null;
	private Label label = null;
	private Text textLogDesc = null;
	private Label label1 = null;
	private boolean thread_is_ended = false;
	private boolean shell_disposed = false;
	public String monitor_start_time_sec = new String("0");
	public String monitor_start_time_usec = new String("0");

	public void actionPerformed(ActionEvent e) {
		time_flag = 1;
	}

	public void run() {
		Display display = sShell.getDisplay();
		while (true) {
			if (shell_disposed)
				break;

			if (time_flag == 1) {
				if (sendActivityMonitorMsg() == -1) {
					DiagEndCode = SWT.CANCEL;
					thread_is_ended = true;
					break;
				}
				display.syncExec(new Runnable() {
					public void run() {
						if (!sShell.isDisposed())
							displayDiagData();
						else {
							timer.stop();
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

	public int sendActivityMonitorMsg() {
		/* send message to server */
		String msg = new String();
		ClientSocket cs = new ClientSocket();
		Shell psh = sShell;

		if (currentTemplate != null) {
			msg += "start_time_sec:" + monitor_start_time_sec + "\n";
			msg += "start_time_usec:" + monitor_start_time_usec + "\n";
			if (currentTemplate.targetdb.trim().length() != 0)
				msg += "db_name:" + currentTemplate.targetdb + "\n";
			msg += "mon_db:no\n";
			msg += "mon_cas:no\n";
			msg += "mon_driver:no\n";
			msg += "mon_resource:no\n";
			if (currentTemplate.activity_config.dbData.needCubActivity) {
				msg += "act_db:yes\n";
				if (currentTemplate.activity_config.dbData.act_query_fullscan)
					msg += "act_cub_query_fullscan:yes\n";
				else
					msg += "act_cub_query_fullscan:no\n";

				if (currentTemplate.activity_config.dbData.act_lock_deadlock)
					msg += "act_cub_lock_deadlock:yes\n";
				else
					msg += "act_cub_lock_deadlock:no\n";

				if (currentTemplate.activity_config.dbData.act_buffer_page_read)
					msg += "act_cub_buffer_page_read:yes\n";
				else
					msg += "act_cub_buffer_page_read:no\n";

				if (currentTemplate.activity_config.dbData.act_buffer_page_write)
					msg += "act_cub_buffer_page_write:yes\n";
				else
					msg += "act_cub_buffer_page_write:no\n";
			} else
				msg += "act_db:no\n";

			if (currentTemplate.activity_config.casData.needCasActivity) {
				msg += "act_cas:yes\n";
				if (currentTemplate.activity_config.NEED_CAS_ACT_DATA_REQ())
					msg += "cas_act_req:yes\n";
				else
					msg += "cas_act_req:no\n";
				if (currentTemplate.activity_config.NEED_CAS_ACT_DATA_TRAN())
					msg += "cas_act_tran:yes\n";
				else
					msg += "cas_act_tran:no\n";
			} else
				msg += "act_cas:no\n";

			msg += "act_driver:no\n";
			cs.socketOwner = this;
			cs.DiagMessageType = MainRegistry.DIAGMESSAGE_TYPE_ACTIVITY;
			if (!cs.SendMessageUsingSpecialDelimiter(psh, msg, "getdiagdata")) {
				return -1;
			}
		}
		return 0;
	}

	public DiagActivityMonitorDialog(Shell parent) {
		super(parent);
	}

	public DiagActivityMonitorDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal(String templateName, String logName) {
		int time_interval = 500;
		if (templateName == "") {
			// log display.
		} else {
			currentTemplate = MainRegistry.getActivityTemplateByName(
					MainRegistry.GetCurrentSiteName(), templateName);
			if (currentTemplate == null) {
				/* make error */
				DiagEndCode = Window.OK;
				return DiagEndCode;
			}

			diagDataList = new ArrayList();
			createSShell();
			CommonTool.centerShell(sShell);

			String msg = new String();
			if (currentTemplate.targetdb.trim().length() != 0)
				msg += "db_name:" + currentTemplate.targetdb + "\n";
			msg += "mon_db:no\n";
			msg += "mon_cas:no\n";
			msg += "mon_driver:no\n";
			msg += "mon_resource:no\n";
			if (currentTemplate.activity_config.dbData.needCubActivity) {
				msg += "act_db:yes\n";
				if (currentTemplate.activity_config.dbData.act_query_fullscan)
					msg += "act_cub_query_fullscan:yes\n";
				else
					msg += "act_cub_query_fullscan:no\n";

				if (currentTemplate.activity_config.dbData.act_lock_deadlock)
					msg += "act_cub_lock_deadlock:yes\n";
				else
					msg += "act_cub_lock_deadlock:no\n";

				if (currentTemplate.activity_config.dbData.act_buffer_page_read)
					msg += "act_cub_buffer_page_read:yes\n";
				else
					msg += "act_cub_buffer_page_read:no\n";

				if (currentTemplate.activity_config.dbData.act_buffer_page_write)
					msg += "act_cub_buffer_page_write:yes\n";
				else
					msg += "act_cub_buffer_page_write:no\n";
			} else
				msg += "act_db:no\n";

			if (currentTemplate.activity_config.casData.needCasActivity) {
				msg += "act_cas:yes\n";
				if (currentTemplate.activity_config.NEED_CAS_ACT_DATA_REQ())
					msg += "cas_act_req:yes\n";
				else
					msg += "cas_act_req:no\n";
				if (currentTemplate.activity_config.NEED_CAS_ACT_DATA_TRAN())
					msg += "cas_act_tran:yes\n";
				else
					msg += "cas_act_tran:no\n";
			} else
				msg += "act_cas:no\n";

			msg += "act_driver:no\n";

			MainRegistry.soc.sender = this;
			if (MainRegistry.soc.SendMessage(sShell, msg, "setclientdiaginfo")) {
				if (MainRegistry.diagErrorString.length() > 0) {
					CommonTool.ErrorBox(sShell, MainRegistry.diagErrorString);
					return Window.CANCEL;
				}
			}

			timer = new Timer(time_interval, this);
			timer.setInitialDelay(time_interval);
			timer.start();
		}

		sShell.open();
		Display display = sShell.getDisplay();

		shell_disposed = false;
		Thread thread = new Thread(this, "jobthread");
		thread.start();
		while (!sShell.isDisposed() && !thread_is_ended) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		timer.stop();
		thread.destroy();
		shell_disposed = true;
		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 150;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 70;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 70;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.widthHint = 70;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Shell(SWT.MODELESS | SWT.DIALOG_TRIM);
		sShell.setText(Messages.getString("TITLE.DIAGACTIVITYMONITOR"));
		sShell.setLayout(gridLayout);
		createComposite();
		Label label1 = new Label(sShell, SWT.NONE);
		label1.setLayoutData(gridData4);
		buttonLogStart = new Button(sShell, SWT.NONE);
		buttonLogStart.setText(Messages.getString("BUTTON.LOGGINGSTART"));
		buttonLogStart.setLayoutData(gridData3);
		buttonLogStart
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// Read log information and send message.
						long startTime, endTime;

						if (buttonCheckLogStartTime.getSelection()) {
							String stringStartTime = textLogStartTime.getText()
									.trim();
							// yyyy/mm/dd hh:mm:ss
							try {
								int year = Integer.parseInt(stringStartTime
										.substring(0, 4));
								int month = Integer.parseInt(stringStartTime
										.substring(5, 7));
								int day = Integer.parseInt(stringStartTime
										.substring(8, 10));
								int hour = Integer.parseInt(stringStartTime
										.substring(11, 13));
								int min = Integer.parseInt(stringStartTime
										.substring(14, 16));
								int sec = Integer.parseInt(stringStartTime
										.substring(17, 19));

								Date start = new Date();
								start.setYear(year - 1900);
								start.setMonth(month - 1);
								start.setDate(day);
								start.setHours(hour);
								start.setMinutes(min);
								start.setSeconds(sec);

								startTime = start.getTime();
								startTime /= 1000;
							} catch (Exception ex) {
								CommonTool.ErrorBox(Messages
										.getString("ERROR.INVALIDSTARTTIME"));
								return;
							}
						} else {
							// logging start 
							startTime = (new Date()).getTime();
							startTime /= 1000; // milisec -> sec
						}

						if (buttonCheckLogEndTime.getSelection()) {
							String stringEndTime = textLogEndTime.getText();
							// yyyy/mm/dd hh:mm:ss
							try {
								int year = Integer.parseInt(stringEndTime
										.substring(0, 4));
								int month = Integer.parseInt(stringEndTime
										.substring(5, 7));
								int day = Integer.parseInt(stringEndTime
										.substring(8, 10));
								int hour = Integer.parseInt(stringEndTime
										.substring(11, 13));
								int min = Integer.parseInt(stringEndTime
										.substring(14, 16));
								int sec = Integer.parseInt(stringEndTime
										.substring(17, 19));

								Date End = new Date();
								End.setYear(year - 1900);
								End.setMonth(month - 1);
								End.setDate(day);
								End.setHours(hour);
								End.setMinutes(min);
								End.setSeconds(sec);

								endTime = End.getTime();
								endTime /= 1000; // milisec -> sec
							} catch (Exception ex) {
								CommonTool.ErrorBox(Messages
										.getString("ERROR.INVALIDENDTIME"));
								return;
							}
						} else {
							endTime = 0; // Doesn't set end time.
						}

						String fileName = textLogFileName.getText().trim();
						if (fileName.length() == 0) {
							CommonTool.ErrorBox(Messages
									.getString("ERROR.EMPTYLOGFILE"));
							return;
						}
						String logName = textLogName.getText().trim();
						if (logName.length() == 0) {
							CommonTool.ErrorBox(Messages
									.getString("ERROR.EMPTYLOGNAME"));
							return;
						}

						String logDesc = textLogDesc.getText().trim();
						String msg = new String();
						msg = "name:" + logName + "\n";
						msg += "desc:" + logDesc + "\n";
						msg += "templatename:" + currentTemplate.templateName
								+ "\n";
						msg += "filename:" + fileName + "\n";
						msg += "logstart:" + String.valueOf(startTime) + "\n";
						msg += "logend:" + String.valueOf(endTime) + "\n";
						msg += "state:ready" + "\n";

						ClientSocket cs = new ClientSocket();
						Shell psh = sShell;

						if (!cs.SendClientMessage(psh, msg, "addactivitylog")) {
							CommonTool.ErrorBox(psh, cs.ErrorMsg);
							return;
						}
					}
				});
		buttonLogEnd = new Button(sShell, SWT.NONE);
		buttonLogEnd.setText(Messages.getString("BUTTON.LOGGINGSTOP"));
		buttonLogEnd.setLayoutData(gridData2);
		buttonLogEnd
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// System.out.println("widgetSelected()"); // TODO
						// Auto-generated Event stub widgetSelected()
					}
				});
		buttonCloseDialog = new Button(sShell, SWT.NONE);
		buttonCloseDialog.setText(Messages.getString("BUTTON.CLOSE"));
		buttonCloseDialog.setLayoutData(gridData1);
		buttonCloseDialog
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						MainRegistry.soc.SendMessage(sShell, "",
								"removeclientdiaginfo");

						DiagEndCode = Window.OK;
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
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 1;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		composite = new Composite(sShell, SWT.NONE);
		createGroup();
		createTable();
		composite.setLayout(gridLayout1);
		composite.setLayoutData(gridData);
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData5.heightHint = 230;
		gridData5.widthHint = -1;
		gridData5.grabExcessHorizontalSpace = true;
		gridData5.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		tableActivityMonitor = new Table(composite, SWT.FULL_SELECTION
				| SWT.BORDER | SWT.MULTI);
		tableActivityMonitor.setHeaderVisible(true);
		tableActivityMonitor.setLayoutData(gridData5);
		tableActivityMonitor.setLinesVisible(true);
		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(10, 30, true));
		tlayout.addColumnData(new ColumnWeightData(30, 90, true));
		tlayout.addColumnData(new ColumnWeightData(30, 90, true));
		tlayout.addColumnData(new ColumnWeightData(10, 30, true));
		tlayout.addColumnData(new ColumnWeightData(20, 60, true));
		tableActivityMonitor.setLayout(tlayout);

		TableColumn eventColumn = new TableColumn(tableActivityMonitor,
				SWT.LEFT);
		TableColumn textColumn = new TableColumn(tableActivityMonitor, SWT.LEFT);
		TableColumn binColumn = new TableColumn(tableActivityMonitor, SWT.LEFT);
		TableColumn intColumn = new TableColumn(tableActivityMonitor, SWT.LEFT);
		TableColumn timeColumn = new TableColumn(tableActivityMonitor, SWT.LEFT);

		eventColumn.setText(Messages.getString("TABLE.EVENTCLASS"));
		textColumn.setText(Messages.getString("TABLE.TEXTDATA"));
		binColumn.setText(Messages.getString("TABLE.BINDATA"));
		intColumn.setText(Messages.getString("TABLE.INTDATA"));
		timeColumn.setText(Messages.getString("TABLE.TIMEDATA"));

		eventColumn.setWidth(30);
		textColumn.setWidth(90);
		binColumn.setWidth(90);
		intColumn.setWidth(30);
		timeColumn.setWidth(60);
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.grabExcessHorizontalSpace = true;
		gridData16.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData16.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.grabExcessHorizontalSpace = true;
		gridData15.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData15.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.grabExcessHorizontalSpace = true;
		gridData14.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData14.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData13 = new org.eclipse.swt.layout.GridData();
		gridData13.grabExcessHorizontalSpace = true;
		gridData13.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData13.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.grabExcessHorizontalSpace = true;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData10.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData9.widthHint = -1;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData8.widthHint = -1;
		gridData8.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData7.grabExcessHorizontalSpace = false;
		gridData7.widthHint = -1;
		gridData7.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 2;
		gridLayout2.verticalSpacing = 11;
		gridLayout2.marginHeight = 5;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.grabExcessHorizontalSpace = true;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		group = new Group(composite, SWT.NONE);
		group.setText(Messages.getString("LABEL.LOGGING"));
		group.setLayout(gridLayout2);
		group.setLayoutData(gridData6);
		labelLogName = new Label(group, SWT.CENTER);
		textLogName = new Text(group, SWT.BORDER);
		textLogName.setLayoutData(gridData12);
		label1 = new Label(group, SWT.CENTER);
		textLogFileName = new Text(group, SWT.BORDER);
		textLogFileName.setLayoutData(gridData13);
		label = new Label(group, SWT.CENTER);
		textLogDesc = new Text(group, SWT.BORDER);
		textLogDesc.setLayoutData(gridData14);
		buttonCheckLogStartTime = new Button(group, SWT.CHECK);
		buttonCheckLogStartTime.setText(Messages
				.getString("LABEL.SETLOGSTARTTIME"));
		buttonCheckLogStartTime.setLayoutData(gridData10);
		textLogStartTime = new Text(group, SWT.BORDER);
		textLogStartTime.setLayoutData(gridData15);
		buttonCheckLogEndTime = new Button(group, SWT.CHECK);
		buttonCheckLogEndTime
				.setText(Messages.getString("LABEL.SETLOGENDTIME"));
		buttonCheckLogEndTime.setLayoutData(gridData11);
		textLogEndTime = new Text(group, SWT.BORDER);
		textLogEndTime.setLayoutData(gridData16);
		labelLogName.setText(Messages.getString("LABEL.LOGNAME"));
		labelLogName.setLayoutData(gridData7);
		label.setText(Messages.getString("LABEL.LOGDESC"));
		label.setLayoutData(gridData9);
		label1.setText(Messages.getString("LABEL.LOGFILE"));
		label1.setLayoutData(gridData8);
	}

	private void displayDiagData() {
		DiagActivityResult temp = null;

		for (int i = 0; i < diagDataList.size(); i++) {
			temp = (DiagActivityResult) diagDataList.get(i);
			String text = temp.GetTextData().replace('\r', ' ');

			TableItem item = new TableItem(tableActivityMonitor, SWT.NONE);
			item.setText(0, temp.GetEventClass());
			item.setText(1, text.replace('\n', ' '));
			item.setText(2, temp.GetBinData());
			item.setText(3, temp.GetIntegerData());
			item.setText(4, temp.GetTimeData());
		}
	}
}
