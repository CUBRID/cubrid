package cubridmanager.cubrid.dialog;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.cubrid.Jobs;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.JobAutomation;
import java.util.ArrayList;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Spinner;

public class JOB_BACKUPDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Label label1 = null;
	private Text EDIT_BACKUP_ID = null;
	private Label label2 = null;
	private Combo COMBO_BACKUP_LEVEL = null;
	private Label label3 = null;
	private Text EDIT_BACKUP_PATH = null;
	private Group group1 = null;
	private Label label4 = null;
	private Combo COMBO_BACKUP_TYPE = null;
	private Label label5 = null;
	private Combo COMBO_BACKUP_DETAIL = null;
	private CLabel clabel1 = null;
	private Label label6 = null;
	private Text EDIT_BACKUP_TIME = null;
	private Label label7 = null;
	private Button JOB_BACKUP_CHK_STORE = null;
	private Button JOB_BACKUP_CHK1 = null;
	private Button JOB_BACKUP_CHK2 = null;
	private Button RADIO_JOB_BACKUP_ONLINE = null;
	private Button RADIO_JOB_BACKUP_OFFLINE = null;
	private Group group2 = null;
	private Button BUTTON_BACKUP_UPDATE = null;
	private Button BUTTON_JOB_BACKUP_HELP = null;
	private Button IDOK = null;
	private Group group3 = null;
	private Group group4 = null;
	private Label label8 = null;
	private Label label9 = null;
	private Label label10 = null;
	public String gubun = null;
	public String backuppath = null;
	public String dbname = null;
	private boolean ret = false;
	private Jobs jobs = null;
	private CLabel cLabel = null;
	GridData gridDatahelp = null;
	private Button JOB_BACKUP_CHK_C = null;
	private Button JOB_BACKUP_CHK_ZIP = null;
	private Spinner JOB_BACKUP_SPINNER_MT = null;
	private Label label_mt = null;

	public JOB_BACKUPDialog(Shell parent) {
		super(parent);
	}

	public JOB_BACKUPDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();

		dlgShell.setDefaultButton(IDOK);
		setinfo();
		dlgShell.pack();
		CommonTool.centerShell(dlgShell);
		dlgShell.open();
		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.JOB_BACKUPDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData51 = new org.eclipse.swt.layout.GridData();
		gridData51.horizontalSpan = 1;
		gridData51.widthHint = 50;
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.horizontalSpan = 2;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.horizontalSpan = 2;
		GridData gridData25 = new org.eclipse.swt.layout.GridData();
		gridData25.horizontalSpan = 2;
		GridData gridData24 = new org.eclipse.swt.layout.GridData();
		gridData24.widthHint = 94;
		gridData24.grabExcessHorizontalSpace = true;
		GridData gridData23 = new org.eclipse.swt.layout.GridData();
		gridData23.widthHint = 3;
		gridData23.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData23.grabExcessVerticalSpace = false;
		gridData23.verticalSpan = 2;
		gridData23.horizontalAlignment = org.eclipse.swt.layout.GridData.BEGINNING;
		GridLayout gridLayout22 = new GridLayout();
		gridLayout22.numColumns = 5;
		GridData gridData20 = new org.eclipse.swt.layout.GridData();
		gridData20.horizontalSpan = 2;
		gridData20.grabExcessVerticalSpace = true;
		GridData gridData19 = new org.eclipse.swt.layout.GridData();
		gridData19.horizontalSpan = 2;
		gridData19.grabExcessVerticalSpace = true;
		GridData gridData18 = new org.eclipse.swt.layout.GridData();
		gridData18.horizontalSpan = 2;
		gridData18.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData18.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData18.grabExcessHorizontalSpace = true;
		gridData18.verticalSpan = 6;
		GridData gridData17 = new org.eclipse.swt.layout.GridData();
		gridData17.horizontalSpan = 2;
		gridData17.grabExcessVerticalSpace = true;
		gridData17.grabExcessHorizontalSpace = true;
		GridData gridData16 = new org.eclipse.swt.layout.GridData();
		gridData16.horizontalSpan = 4;
		gridData16.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData16.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData15 = new org.eclipse.swt.layout.GridData();
		gridData15.horizontalSpan = 3;
		gridData15.widthHint = 340;
		GridData gridData14 = new org.eclipse.swt.layout.GridData();
		gridData14.widthHint = 126;
		GridLayout gridLayout13 = new GridLayout();
		gridLayout13.numColumns = 4;
		GridData gridData12 = new org.eclipse.swt.layout.GridData();
		gridData12.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData12.grabExcessHorizontalSpace = true;
		gridData12.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.grabExcessHorizontalSpace = true;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData10.grabExcessHorizontalSpace = true;
		gridData10.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 5;
		gridData6.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData6.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridDatahelp = gridData6;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.widthHint = 80;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.widthHint = 80;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.widthHint = 80;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.widthHint = 80;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.grabExcessHorizontalSpace = true;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 5;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 5;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group3 = new Group(sShell, SWT.NONE);
		group3.setLayoutData(gridData);
		group3.setLayout(gridLayout13);
		label1 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.BACKUPID"));
		EDIT_BACKUP_ID = new Text(group3, SWT.BORDER);
		EDIT_BACKUP_ID.setLayoutData(gridData14);
		label2 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.BACKUPLEVEL1"));
		createCombo1();
		label3 = new Label(group3, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.BACKUPPATH"));
		EDIT_BACKUP_PATH = new Text(group3, SWT.BORDER);
		EDIT_BACKUP_PATH.setLayoutData(gridData15);
		group1 = new Group(group3, SWT.NONE);
		group1.setText(Messages.getString("GROUP.BACKUPPERIOD"));
		group1.setLayout(gridLayout22);
		group1.setLayoutData(gridData16);
		label4 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.PERIODTYPE"));
		createCombo2();
		clabel1 = new CLabel(group1, SWT.SHADOW_IN);
		clabel1.setLayoutData(gridData23);
		label6 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.BACKUPTIME"));
		EDIT_BACKUP_TIME = new Text(group1, SWT.BORDER);
		EDIT_BACKUP_TIME.setLayoutData(gridData24);
		label5 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.PERIODDETAIL"));
		createCombo3();
		label7 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.EX0200AM2"));
		label7.setLayoutData(gridData25);
		JOB_BACKUP_CHK_STORE = new Button(group3, SWT.CHECK);
		JOB_BACKUP_CHK_STORE
				.setText(Messages.getString("CHECK.STOREOLDBACKUP"));
		JOB_BACKUP_CHK_STORE.setLayoutData(gridData17);
		group2 = new Group(group3, SWT.NONE);
		group2.setText(Messages.getString("GROUP.ONLINEOFFLINE"));
		group2.setLayout(new GridLayout());
		group2.setLayoutData(gridData18);
		JOB_BACKUP_CHK1 = new Button(group3, SWT.CHECK);
		JOB_BACKUP_CHK1.setText(Messages.getString("CHECK.DELETEARCHIVE"));
		JOB_BACKUP_CHK1.setLayoutData(gridData19);
		JOB_BACKUP_CHK2 = new Button(group3, SWT.CHECK);
		JOB_BACKUP_CHK2.setText(Messages.getString("CHECK.UPDATESTATISTICS1"));
		JOB_BACKUP_CHK2.setLayoutData(gridData20);
		JOB_BACKUP_CHK_C = new Button(group3, SWT.CHECK);
		JOB_BACKUP_CHK_C
				.setText(Messages.getString("CHECK.CHCKINGCONSISTENCY"));
		JOB_BACKUP_CHK_C.setLayoutData(gridData31);
		JOB_BACKUP_CHK_ZIP = new Button(group3, SWT.CHECK);
		JOB_BACKUP_CHK_ZIP.setText(Messages.getString("CHECK.USECOMPRESS"));
		JOB_BACKUP_CHK_ZIP.setLayoutData(gridData41);
		JOB_BACKUP_CHK_ZIP.setSelection(true);
		label_mt = new Label(group3, SWT.NONE);
		label_mt.setText(Messages.getString("LABEL.READTHREADALLOWED"));
		JOB_BACKUP_SPINNER_MT = new Spinner(group3, SWT.BORDER);
		JOB_BACKUP_SPINNER_MT.setLayoutData(gridData51);
		JOB_BACKUP_SPINNER_MT.setMinimum(0);
		RADIO_JOB_BACKUP_ONLINE = new Button(group2, SWT.RADIO);
		RADIO_JOB_BACKUP_ONLINE.setText(Messages
				.getString("RADIO.ONLINEBACKUP"));
		RADIO_JOB_BACKUP_OFFLINE = new Button(group2, SWT.RADIO);
		RADIO_JOB_BACKUP_OFFLINE.setText(Messages
				.getString("RADIO.OFFLINEBACKUP"));
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData1);
		BUTTON_BACKUP_UPDATE = new Button(sShell, SWT.NONE);
		BUTTON_BACKUP_UPDATE.setText(Messages.getString("BUTTON.OK"));
		BUTTON_BACKUP_UPDATE.setLayoutData(gridData3);
		BUTTON_BACKUP_UPDATE
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (sendapply()) {
							ret = true;
							dlgShell.dispose();
						}
					}
				});
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.CANCEL"));
		IDOK.setLayoutData(gridData5);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		BUTTON_JOB_BACKUP_HELP = new Button(sShell, SWT.NONE);
		BUTTON_JOB_BACKUP_HELP.setText(Messages.getString("BUTTON.HELP"));
		BUTTON_JOB_BACKUP_HELP.setLayoutData(gridData4);
		BUTTON_JOB_BACKUP_HELP
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (BUTTON_JOB_BACKUP_HELP.getText().equals(
								Messages.getString("BUTTON.HIDEHELP"))) {
							gridDatahelp.heightHint = 0;
							group4.setVisible(false);
							BUTTON_JOB_BACKUP_HELP.setText(Messages
									.getString("BUTTON.HELP"));
							dlgShell.pack();
						} else {
							gridDatahelp.heightHint = -1;
							BUTTON_JOB_BACKUP_HELP.setText(Messages
									.getString("BUTTON.HIDEHELP"));
							group4.setVisible(true);
							dlgShell.pack();
						}
					}
				});

		group4 = new Group(sShell, SWT.NONE);
		group4.setLayout(new GridLayout());
		group4.setLayoutData(gridData6);
		label8 = new Label(group4, SWT.LEFT | SWT.WRAP);
		label8.setText(Messages.getString("LABEL.THISISADIALOG"));
		label8.setLayoutData(gridData10);
		label9 = new Label(group4, SWT.LEFT | SWT.WRAP);
		label9.setText(Messages.getString("LABEL.PERIODTYPECHOICE"));
		label9.setLayoutData(gridData11);
		label10 = new Label(group4, SWT.LEFT | SWT.WRAP);
		label10.setText(Messages.getString("LABEL.TT0FULLBACKUP"));
		label10.setLayoutData(gridData12);
		dlgShell.pack();
	}

	private void createCombo1() {
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.widthHint = 82;
		COMBO_BACKUP_LEVEL = new Combo(group3, SWT.DROP_DOWN | SWT.READ_ONLY);
		COMBO_BACKUP_LEVEL.setLayoutData(gridData7);
	}

	private void createCombo2() {
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.widthHint = 114;
		gridData8.grabExcessHorizontalSpace = true;
		COMBO_BACKUP_TYPE = new Combo(group1, SWT.DROP_DOWN | SWT.READ_ONLY);
		COMBO_BACKUP_TYPE.setLayoutData(gridData8);
		COMBO_BACKUP_TYPE
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						updatetype();
						// jobs.perioddetail="";
					}
				});
	}

	private void createCombo3() {
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.widthHint = 114;
		COMBO_BACKUP_DETAIL = new Combo(group1, SWT.DROP_DOWN);
		COMBO_BACKUP_DETAIL.setLayoutData(gridData9);
	}

	private void setinfo() {
		int idx;

		if (gubun.equals("add")) { // ADD
			dlgShell.setText(Messages.getString("TITLE.ADDBACKUPPLAN"));
			jobs = new Jobs("", backuppath, "Monthly", "1", "", "0", "OFF",
					"OFF", "OFF", "ON", "n", "n", "0");
		} else { // UPDATE
			dlgShell.setText(Messages.getString("TITLE.UPDATEBACKUPPLAN"));
			jobs = JobAutomation.objrec;
			EDIT_BACKUP_ID.setEnabled(false);
		}
		EDIT_BACKUP_ID.setText(jobs.backupid);
		EDIT_BACKUP_PATH.setText(jobs.path);
		EDIT_BACKUP_TIME.setText(jobs.time);
		JOB_BACKUP_CHK_STORE.setSelection((jobs.storeold.equals("ON")) ? true
				: false);
		JOB_BACKUP_CHK1.setSelection((jobs.archivedel.equals("ON")) ? true
				: false);
		JOB_BACKUP_CHK2.setSelection((jobs.updatestatus.equals("ON")) ? true
				: false);
		if (jobs.onoff.equals("ON"))
			RADIO_JOB_BACKUP_ONLINE.setSelection(true);
		else
			RADIO_JOB_BACKUP_OFFLINE.setSelection(true);

		COMBO_BACKUP_LEVEL.add("0", 0);
		COMBO_BACKUP_LEVEL.add("1", 1);
		COMBO_BACKUP_LEVEL.add("2", 2);
		idx = COMBO_BACKUP_LEVEL.indexOf(jobs.level);
		if (idx < 0)
			idx = 0;
		COMBO_BACKUP_LEVEL.select(idx);

		COMBO_BACKUP_TYPE.add(Messages.getString("COMBOITEM.MONTHLY"), 0);
		COMBO_BACKUP_TYPE.add(Messages.getString("COMBOITEM.WEEKLY"), 1);
		COMBO_BACKUP_TYPE.add(Messages.getString("COMBOITEM.DAILY"), 2);
		COMBO_BACKUP_TYPE.add(Messages.getString("COMBOITEM.SPECIAL"), 3);

		if (jobs.periodtype.equals("Monthly"))
			idx = 0;
		else if (jobs.periodtype.equals("Weekly"))
			idx = 1;
		else if (jobs.periodtype.equals("Daily"))
			idx = 2;
		else if (jobs.periodtype.equals("Special"))
			idx = 3;
		else
			idx = 0;

		COMBO_BACKUP_TYPE.select(idx);
		updatetype();
		gridDatahelp.heightHint = 0;
		group4.setVisible(false);
		JOB_BACKUP_CHK_ZIP.setSelection(jobs.zip.equals("y") ? true : false);
		JOB_BACKUP_CHK_C.setSelection(jobs.check.equals("y") ? true : false);

		int i_mt;
		try {
			i_mt = Integer.parseInt(jobs.mt);
		} catch (Exception ee) {
			i_mt = 0;
		}
		JOB_BACKUP_SPINNER_MT.setSelection(i_mt);
	}

	private void updatetype() {
		int type = COMBO_BACKUP_TYPE.getSelectionIndex();
		int index = 0;
		COMBO_BACKUP_DETAIL.removeAll();
		COMBO_BACKUP_DETAIL.setEnabled(true);
		switch (type) {
		// Monthly
		case 0:
			for (int i = 0; i < 31; i++)
				COMBO_BACKUP_DETAIL.add(Integer.toString(i + 1), i);
			if (jobs.periodtype.equals("Monthly"))
				index = Integer.parseInt(jobs.perioddetail) - 1;
			break;
		// Weekly
		case 1:
			for (int i = 0; i < 7; i++)
				COMBO_BACKUP_DETAIL.add(
						Messages.getString("COMBOITEM.DAY" + i), i);

			if (jobs.perioddetail.equals("Sunday"))
				index = 0;
			else if (jobs.perioddetail.equals("Monday"))
				index = 1;
			else if (jobs.perioddetail.equals("Tuesday"))
				index = 2;
			else if (jobs.perioddetail.equals("Wednesday"))
				index = 3;
			else if (jobs.perioddetail.equals("Thursday"))
				index = 4;
			else if (jobs.perioddetail.equals("Friday"))
				index = 5;
			else if (jobs.perioddetail.equals("Saturday"))
				index = 6;
			else
				index = 0;
			break;
		// Daily
		case 2:
			COMBO_BACKUP_DETAIL.setEnabled(false);
			break;
		// Special Day
		case 3:
			COMBO_BACKUP_DETAIL.add("yyyy-mm-dd", 0);
			if (jobs.periodtype.equals("Special")) {
				COMBO_BACKUP_DETAIL.add(jobs.perioddetail, 1);
				index = COMBO_BACKUP_DETAIL.indexOf(jobs.perioddetail);
			}
			break;
		default:
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDPERIODTYPE"));
			COMBO_BACKUP_TYPE.setFocus();
			break;
		}
		if (index < 0)
			index = 0;
		COMBO_BACKUP_DETAIL.select(index);
	}

	private boolean sendapply() {
		jobs.backupid = EDIT_BACKUP_ID.getText();
		jobs.path = EDIT_BACKUP_PATH.getText();
		if (jobs.backupid == null || jobs.backupid.indexOf(" ") >= 0
				|| jobs.backupid.length() < 1) {
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDBACKUPID"));
			EDIT_BACKUP_ID.setFocus();
			return false;
		}
		if (jobs.path == null || jobs.path.indexOf(" ") >= 0
				|| jobs.backupid.length() < 1) {
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDBACKUPPATH"));
			EDIT_BACKUP_PATH.setFocus();
			return false;
		}
		String msg = "dbname:" + dbname + "\n";
		msg += "backupid:" + jobs.backupid + "\n";
		msg += "path:" + jobs.path + "\n";

		switch (COMBO_BACKUP_TYPE.getSelectionIndex()) {
		case 0:
			jobs.periodtype = "Monthly";
			jobs.perioddetail = COMBO_BACKUP_DETAIL.getText();
			break;
		case 1:
			jobs.periodtype = "Weekly";
			switch (COMBO_BACKUP_DETAIL.getSelectionIndex()) {
			case 0:
				jobs.perioddetail = "Sunday";
				break;
			case 1:
				jobs.perioddetail = "Monday";
				break;
			case 2:
				jobs.perioddetail = "Tuesday";
				break;
			case 3:
				jobs.perioddetail = "Wednesday";
				break;
			case 4:
				jobs.perioddetail = "Thursday";
				break;
			case 5:
				jobs.perioddetail = "Friday";
				break;
			case 6:
				jobs.perioddetail = "Saturday";
				break;
			default:
				CommonTool.ErrorBox(dlgShell, Messages
						.getString("ERROR.INVALIDPERIODDETAIL"));
				COMBO_BACKUP_DETAIL.setFocus();
				return false;
			}
			break;
		case 2:
			jobs.periodtype = "Daily";
			jobs.perioddetail = "nothing";
			break;
		case 3:
			jobs.periodtype = "Special";
			jobs.perioddetail = COMBO_BACKUP_DETAIL.getText();
			if (jobs.perioddetail == null
					|| !CommonTool.checkDate(jobs.perioddetail)) {
				CommonTool.ErrorBox(dlgShell, Messages
						.getString("ERROR.INVALIDSPECIALDAY"));
				COMBO_BACKUP_DETAIL.setFocus();
				return false;
			}
			break;
		default:
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDPERIODTYPE"));
			COMBO_BACKUP_TYPE.setFocus();
			return false;
		}

		msg += "period_type:" + jobs.periodtype + "\n";
		msg += "period_date:" + jobs.perioddetail + "\n";
		jobs.level = COMBO_BACKUP_LEVEL.getText();
		jobs.time = EDIT_BACKUP_TIME.getText();
		if (!CommonTool.checkTime(jobs.time)) {
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDBACKUPTIME"));
			EDIT_BACKUP_TIME.setFocus();
			return false;
		}

		msg += "time:" + jobs.time + "\n";
		msg += "level:" + jobs.level + "\n";
		jobs.onoff = (RADIO_JOB_BACKUP_ONLINE.getSelection()) ? "ON" : "OFF";
		jobs.archivedel = (JOB_BACKUP_CHK1.getSelection()) ? "ON" : "OFF";
		jobs.storeold = (JOB_BACKUP_CHK_STORE.getSelection()) ? "ON" : "OFF";
		jobs.updatestatus = (JOB_BACKUP_CHK2.getSelection()) ? "ON" : "OFF";
		msg += "archivedel:" + jobs.archivedel + "\n";
		msg += "updatestatus:" + jobs.updatestatus + "\n";
		msg += "storeold:" + jobs.storeold + "\n";
		msg += "onoff:" + jobs.onoff + "\n";
		msg += "zip:" + ((JOB_BACKUP_CHK_ZIP.getSelection()) ? "y" : "n")
				+ "\n";
		msg += "check:" + ((JOB_BACKUP_CHK_C.getSelection()) ? "y" : "n")
				+ "\n";
		msg += "mt:" + String.valueOf(JOB_BACKUP_SPINNER_MT.getSelection())
				+ "\n";

		boolean chkid = false;
		ArrayList jobinfo = Jobs.JobsInfo_get(CubridView.Current_db);
		for (int i = 0, n = jobinfo.size(); i < n; i++) {
			if (((Jobs) jobinfo.get(i)).backupid.equals(jobs.backupid)) {
				chkid = true;
				break;
			}
		}

		String cmds = null;
		if (gubun.equals("add")) {
			if (chkid) {
				CommonTool.ErrorBox(dlgShell, Messages
						.getString("ERROR.BACKUPIDALREADYEXIST"));
				EDIT_BACKUP_ID.setFocus();
				return false;
			}
			cmds = "addbackupinfo";
		} else if (gubun.equals("update")) {
			if (!chkid) {
				CommonTool.ErrorBox(dlgShell, Messages
						.getString("ERROR.BACKUPIDDOESNOTEXIST"));
				EDIT_BACKUP_ID.setFocus();
				return false;
			}
			cmds = "setbackupinfo";
		}
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(dlgShell, msg, cmds)) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return false;
		}

		cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, "dbname:" + CubridView.Current_db,
				"getbackupinfo", Messages
						.getString("WAITING.GETTINGBACKUPINFO"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
		}
		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();
		return true;
	}
}
