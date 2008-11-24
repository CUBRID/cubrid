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

package cubridmanager.cubrid.dialog;

import java.util.ArrayList;
import java.util.StringTokenizer;

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.SWT;

import cubridmanager.ClientSocket;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.cubrid.AutoQuery;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.JobAutomation;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Combo;

public class ADD_QUERYPLANDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	public String gubun = null;
	public String dbname = null;
	private boolean ret = false;
	private AutoQuery aq = null;
	private CLabel cLabel1 = null;
	private Group grpQueryAutojob = null;
	private CLabel lblQueryAutoID = null;
	private Text txtQueryAutoID = null;
	private Group grpQueryAutoPeriod = null;
	private CLabel lblPeriodType = null;
	private Combo cmbPeriodType = null;
	private CLabel lblVerticalDivide = null;
	private CLabel lblPeriodTime = null;
	private Text txtPeriodTime = null;
	private CLabel lblPeriodDetail = null;
	private Combo cmbPeriodDetail = null;
	private CLabel lblPeriodTimeEx = null;
	private Group grpQuery = null;
	private Text txtQuery = null;
	private String periodDetail = "";
	private String periodTime = "";

	public ADD_QUERYPLANDialog(Shell parent) {
		super(parent);
	}

	public ADD_QUERYPLANDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createDlgShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();
		setinfo();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createDlgShell() {
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.ADD_QUERYPLANDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createSShell();
	}

	private void createSShell() {
		GridData gridData11 = new org.eclipse.swt.layout.GridData();
		gridData11.widthHint = 80;
		gridData11.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData11.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData10.widthHint = 80;
		gridData10.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 3;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		createGrpQueryAutojob();

		cLabel1 = new CLabel(sShell, SWT.NONE);
		cLabel1.setLayoutData(new org.eclipse.swt.layout.GridData(
				GridData.FILL_HORIZONTAL));
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData10);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (sendApply()) {
							ret = true;
							dlgShell.dispose();
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData11);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
	}

	/**
	 * This method initializes grpQueryAutojob
	 * 
	 */
	private void createGrpQueryAutojob() {
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		GridData gridGrpQueryAutojob = new GridData(GridData.FILL_BOTH);
		gridGrpQueryAutojob.horizontalSpan = 3;
		grpQueryAutojob = new Group(sShell, SWT.NONE);
		grpQueryAutojob.setLayoutData(gridGrpQueryAutojob);
		grpQueryAutojob.setLayout(gridLayout1);

		lblQueryAutoID = new CLabel(grpQueryAutojob, SWT.NONE);
		lblQueryAutoID.setText(Messages.getString("TABLE.AUTOQUERYID"));

		GridData gridData = new GridData();
		gridData.widthHint = 126;
		txtQueryAutoID = new Text(grpQueryAutojob, SWT.BORDER);
		txtQueryAutoID.setLayoutData(gridData);
		createGrpQueryAutoPeriod();
		createGrpQuery();
	}

	/**
	 * This method initializes grpQueryAutoPeriod
	 * 
	 */
	private void createGrpQueryAutoPeriod() {
		GridLayout gridLayout2 = new GridLayout();
		gridLayout2.numColumns = 5;
		GridData gridData1 = new GridData(GridData.FILL_BOTH);
		gridData1.horizontalSpan = 2;
		grpQueryAutoPeriod = new Group(grpQueryAutojob, SWT.NONE);
		grpQueryAutoPeriod.setLayoutData(gridData1);
		grpQueryAutoPeriod.setLayout(gridLayout2);
		grpQueryAutoPeriod.setText(Messages.getString("GROUP.PERIOD"));
		lblPeriodType = new CLabel(grpQueryAutoPeriod, SWT.NONE);
		lblPeriodType.setText(Messages.getString("LABEL.PERIODTYPE"));
		createCmbPeriodType();
		GridData gridData2 = new GridData(GridData.FILL_VERTICAL);
		gridData2.verticalSpan = 2;
		gridData2.widthHint = 3;
		lblVerticalDivide = new CLabel(grpQueryAutoPeriod, SWT.SHADOW_IN);
		lblVerticalDivide.setLayoutData(gridData2);
		lblPeriodTime = new CLabel(grpQueryAutoPeriod, SWT.NONE);
		lblPeriodTime.setText(Messages.getString("LABEL.PERIODTIME"));
		txtPeriodTime = new Text(grpQueryAutoPeriod, SWT.BORDER);
		txtPeriodTime.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
		lblPeriodDetail = new CLabel(grpQueryAutoPeriod, SWT.NONE);
		lblPeriodDetail.setText(Messages.getString("LABEL.PERIODDETAIL"));
		createCmbPeriodDetail();
		GridData gridData3 = new GridData();
		gridData3.horizontalSpan = 2;
		lblPeriodTimeEx = new CLabel(grpQueryAutoPeriod, SWT.NONE);
		lblPeriodTimeEx.setText(Messages.getString("LABEL.EX0200AM2"));
		lblPeriodTimeEx.setLayoutData(gridData3);
	}

	/**
	 * This method initializes cmbPeriodType
	 * 
	 */
	private void createCmbPeriodType() {
		cmbPeriodType = new Combo(grpQueryAutoPeriod, SWT.DROP_DOWN
				| SWT.READ_ONLY);
		cmbPeriodType.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));

		cmbPeriodType.add(Messages.getString("COMBOITEM.MONTHLY"), 0);
		cmbPeriodType.add(Messages.getString("COMBOITEM.WEEKLY"), 1);
		cmbPeriodType.add(Messages.getString("COMBOITEM.DAILY"), 2);
		cmbPeriodType.add(Messages.getString("COMBOITEM.SPECIAL"), 3);
		cmbPeriodType
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						updatedate();
					}
				});
	}

	/**
	 * This method initializes cmdPeriodDetail
	 * 
	 */
	private void createCmbPeriodDetail() {
		cmbPeriodDetail = new Combo(grpQueryAutoPeriod, SWT.DROP_DOWN);
		cmbPeriodDetail.setLayoutData(new GridData(GridData.FILL_HORIZONTAL));
	}

	/**
	 * This method initializes grpQuery
	 * 
	 */
	private void createGrpQuery() {
		GridData gridData4 = new GridData(GridData.FILL_BOTH);
		gridData4.horizontalSpan = 2;
		gridData4.widthHint = 400;
		gridData4.heightHint = 60;
		grpQuery = new Group(grpQueryAutojob, SWT.NONE);
		grpQuery.setLayout(new FillLayout());
		grpQuery.setLayoutData(gridData4);
		grpQuery.setText(Messages.getString("GROUP.QUERYSTATEMENT"));
		txtQuery = new Text(grpQuery, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.BORDER);
	}

	private void setinfo() {
		if (gubun.equals("add")) { // ADD
			dlgShell.setText(Messages.getString("TITLE.ADDQUERYPLAN"));

			periodDetail = "1";
			periodTime = "";
			aq = new AutoQuery("", "MONTH", periodDetail + " " + periodTime, "");
		} else {
			aq = JobAutomation.objaq;
			dlgShell.setText(Messages.getString("TITLE.EDITQUERYPLAN"));
			txtQueryAutoID.setText(aq.QueryID);
			txtQueryAutoID.setEnabled(false);
			StringTokenizer st = new StringTokenizer(aq.TimeDetail, " ");
			if (st.countTokens() == 2) {
				periodDetail = st.nextToken();
				periodTime = st.nextToken();
			} else if (st.countTokens() < 2) {
				periodDetail = st.nextToken();
				periodTime = "";
			} else {
				periodDetail = "";
				periodTime = "";
			}
		}

		periodDetail = periodDetail.replaceAll("/", "-");
		periodTime = periodTime.replaceAll(":", "");

		if (aq.Period.equals("MONTH")) {
			cmbPeriodType.select(0);
		} else if (aq.Period.equals("WEEK")) {
			cmbPeriodType.select(1);
		} else if (aq.Period.equals("DAY")) {
			cmbPeriodType.select(2);
		} else if (aq.Period.equals("ONE")) {
			cmbPeriodType.select(3);
		}

		updatedate();
		txtQuery.setText(aq.QueryString);
		txtPeriodTime.setText(periodTime);
	}

	private void updatedate() {
		int type = cmbPeriodType.getSelectionIndex();
		int index = 0;
		cmbPeriodDetail.removeAll();
		cmbPeriodDetail.setEnabled(true);
		switch (type) {
		case 0:
			for (int i = 0; i < 31; i++)
				cmbPeriodDetail.add(Integer.toString(i + 1), i);
			if (aq.Period.equals("MONTH"))
				index = Integer.parseInt(periodDetail) - 1;
			break;
		case 1:
			for (int i = 0; i < 7; i++)
				cmbPeriodDetail.add(Messages.getString("COMBOITEM.DAY" + i), i);

			if (periodDetail.equals("SUN"))
				index = 0;
			else if (periodDetail.equals("MON"))
				index = 1;
			else if (periodDetail.equals("TUE"))
				index = 2;
			else if (periodDetail.equals("WED"))
				index = 3;
			else if (periodDetail.equals("THU"))
				index = 4;
			else if (periodDetail.equals("FRI"))
				index = 5;
			else if (periodDetail.equals("SAT"))
				index = 6;
			else
				index = 0;
			break;
		case 2:
			cmbPeriodDetail.setEnabled(false);
			break;
		case 3:
			cmbPeriodDetail.add("yyyy-mm-dd", 0);
			if (aq.Period.equals("ONE")) {
				cmbPeriodDetail.add(periodDetail, 1);
				index = 1;
			}
			break;
		default:
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDQUERYPERIODTYPE"));
			cmbPeriodDetail.setFocus();
			break;
		}
		if (index < 0)
			index = 0;
		cmbPeriodDetail.select(index);
	}

	private boolean sendApply() {
		StringBuffer msg = new StringBuffer("");

		if (txtQueryAutoID.getText().length() < 1
				|| !CommonTool.isValidDBName(txtQueryAutoID.getText())) {
			CommonTool
					.ErrorBox(dlgShell, Messages.getString("ERROR.INVALIDID"));
			txtQueryAutoID.setFocus();
			return false;
		}

		aq.QueryString = txtQuery.getText().trim();
		if (aq.QueryString.length() <= 0) {
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.ENTERQUERYSTRING"));
			return false;
		}
		aq.QueryString = aq.QueryString.replaceAll("\n", " ");
		aq.QueryString = aq.QueryString.replaceAll("\r", " ");

		switch (cmbPeriodType.getSelectionIndex()) {
		case 0:
			aq.Period = "MONTH";
			periodDetail = cmbPeriodDetail.getText();
			break;
		case 1:
			aq.Period = "WEEK";
			switch (cmbPeriodDetail.getSelectionIndex()) {
			case 0:
				periodDetail = "SUN";
				break;
			case 1:
				periodDetail = "MON";
				break;
			case 2:
				periodDetail = "TUE";
				break;
			case 3:
				periodDetail = "WED";
				break;
			case 4:
				periodDetail = "THU";
				break;
			case 5:
				periodDetail = "FRI";
				break;
			case 6:
				periodDetail = "SAT";
				break;
			default:
				CommonTool.ErrorBox(dlgShell, Messages
						.getString("ERROR.INVALIDQUERYPERIODDETAIL"));
				cmbPeriodDetail.setFocus();
				return false;
			}
			break;
		case 2:
			aq.Period = "DAY";
			periodDetail = "EVERYDAY";
			break;
		case 3:
			aq.Period = "ONE";
			periodDetail = cmbPeriodDetail.getText();
			if (periodDetail == null || !CommonTool.checkDate(periodDetail)) {
				CommonTool.ErrorBox(dlgShell, Messages
						.getString("ERROR.INVALIDSPECIALDAY"));
				cmbPeriodDetail.setFocus();
				return false;
			}
			periodDetail = periodDetail.replaceAll("-", "/");
			break;
		default:
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDQUERYPERIODTYPE"));
			cmbPeriodType.setFocus();
			return false;
		}

		periodTime = txtPeriodTime.getText();
		if (!CommonTool.checkTime(periodTime)) {
			CommonTool.ErrorBox(dlgShell, Messages
					.getString("ERROR.INVALIDQUERYTIME"));
			txtPeriodTime.setFocus();
			return false;
		}
		periodTime = periodTime.substring(0, 2) + ":" + periodTime.substring(2);

		aq.TimeDetail = periodDetail + " " + periodTime;

		ArrayList jobinfo = AutoQuery.AutoQueryInfo_get(CubridView.Current_db);
		if (gubun.equals("add")) {
			aq.QueryID = txtQueryAutoID.getText();
			for (int i = 0, n = jobinfo.size(); i < n; i++) {
				AutoQuery tmp = (AutoQuery) jobinfo.get(i);
				if (tmp.QueryID.equals(aq.QueryID)) {
					CommonTool.ErrorBox(dlgShell, Messages
							.getString("ERROR.ALREADYEXISTID"));
					txtQueryAutoID.setFocus();
					return false;
				}
			}
			jobinfo.add(aq);
		}

		msg.append("dbname:");
		msg.append(dbname);
		msg.append("\n");
		msg.append("open:planlist\n");

		for (int i = 0, n = jobinfo.size(); i < n; i++) {
			AutoQuery tmp = (AutoQuery) jobinfo.get(i);
			if (!gubun.equals("add") && tmp.QueryID.equals(aq.QueryID)) {
				tmp = aq;
			}
			msg.append("open:queryplan\n");

			msg.append("query_id:");
			msg.append(tmp.QueryID);
			msg.append("\n");
			msg.append("period:");
			msg.append(tmp.Period);
			msg.append("\n");
			msg.append("detail:");
			msg.append(tmp.TimeDetail);
			msg.append("\n");
			msg.append("query_string:");
			msg.append(tmp.QueryString);
			msg.append("\n");
			msg.append("close:queryplan\n");
		}
		msg.append("close:planlist\n");

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(dlgShell, msg.toString(), "setautoexecquery",
				Messages.getString("WAITING.UPDATEQUERYPLAN"))) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
			return false;
		}
		cs = new ClientSocket();
		if (!cs.SendClientMessage(dlgShell, "dbname:" + CubridView.Current_db,
				"getautoexecquery")) {
			CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
		}
		CubridView.myNavi.createModel();
		CubridView.viewer.refresh();

		return true;
	}
}
