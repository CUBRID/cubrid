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

import java.text.NumberFormat;
import java.util.*;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Dialog;

import cubridmanager.ClientSocket;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.action.CopyAction;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.DBUserInfo;
import cubridmanager.cubrid.VolumeInfo;

import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.custom.TableEditor;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.FillLayout;

public class COPYDBDialog extends Dialog {
	private Shell dlgShell = null;
	private Composite sShell = null;
	private Group group1 = null;
	private Label label1 = null;
	private Text EDIT_COPYDB_SOURCEDB = null;
	private Label label2 = null;
	private Text EDIT_COPYDB_SRCDATADIR = null;
	private Label label3 = null;
	private Text EDIT_COPYDB_SRCLOGDIR = null;
	private Group group2 = null;
	private Label label4 = null;
	private Text EDIT_COPYDB_DESTDB = null;
	private Label label5 = null;
	private Text EDIT_COPYDB_DESTDATADIR = null;
	private Label label6 = null;
	private Text EDIT_COPYDB_EXVOLDIR = null;
	private Label label7 = null;
	private Text EDIT_COPYDB_DESTLOGDIR = null;
	private Label label8 = null;
	private Label label9 = null;
	private CLabel clabel1 = null;
	private Button CHECK_COPYDB_ADVANCED = null;
	private Table LIST_COPYDB_VOLLIST = null;
	private Button CHECK_COPYDB_OVERWRITE = null;
	private Button CHECK_COPYDB_MOVE = null;
	private Button IDOK = null;
	private Button IDCANCEL = null;
	private boolean ret = false;
	public static boolean isChanged = false;
	public static String prop_path = Messages.getString("TABLE.NEWDIRECTORYPATH");
	public static final String[] PROPS = { "a", "b", prop_path };
	public static TableItem Current_item = null;
	private CLabel cLabel = null;
	private CLabel cLabel1 = null;
	EditVolumeList listener = null;

	public COPYDBDialog(Shell parent) {
		super(parent);
	}

	public COPYDBDialog(Shell parent, int style) {
		super(parent, style);
	}

	public boolean doModal() {
		createSShell();
		CommonTool.centerShell(dlgShell);
		dlgShell.setDefaultButton(IDOK);
		dlgShell.open();

		Display display = dlgShell.getDisplay();
		while (!dlgShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
		return ret;
	}

	private void createSShell() {
		// dlgShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		dlgShell = new Shell(getParent(), SWT.APPLICATION_MODAL
				| SWT.DIALOG_TRIM);
		dlgShell.setText(Messages.getString("TITLE.COPYDBDIALOG"));
		dlgShell.setLayout(new FillLayout());
		createComposite();
	}

	private void createComposite() {
		GridData gridData91 = new org.eclipse.swt.layout.GridData();
		gridData91.widthHint = 184;
		gridData91.grabExcessHorizontalSpace = true;
		gridData91.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData81 = new org.eclipse.swt.layout.GridData();
		gridData81.widthHint = 184;
		gridData81.grabExcessHorizontalSpace = true;
		gridData81.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData71 = new org.eclipse.swt.layout.GridData();
		gridData71.widthHint = 184;
		gridData71.grabExcessHorizontalSpace = true;
		gridData71.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData61 = new org.eclipse.swt.layout.GridData();
		gridData61.widthHint = 184;
		gridData61.grabExcessHorizontalSpace = true;
		gridData61.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout5 = new GridLayout();
		gridLayout5.numColumns = 2;
		GridData gridData41 = new org.eclipse.swt.layout.GridData();
		gridData41.widthHint = 184;
		gridData41.grabExcessHorizontalSpace = true;
		gridData41.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData31 = new org.eclipse.swt.layout.GridData();
		gridData31.widthHint = 184;
		gridData31.grabExcessHorizontalSpace = true;
		gridData31.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData21 = new org.eclipse.swt.layout.GridData();
		gridData21.widthHint = 184;
		gridData21.grabExcessHorizontalSpace = true;
		gridData21.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 2;
		GridData gridData10 = new org.eclipse.swt.layout.GridData();
		gridData10.widthHint = 80;
		GridData gridDataz9 = new org.eclipse.swt.layout.GridData();
		gridDataz9.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		GridData gridData9 = new org.eclipse.swt.layout.GridData();
		gridData9.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		gridData9.widthHint = 80;
		gridData9.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		GridData gridData8 = new org.eclipse.swt.layout.GridData();
		gridData8.grabExcessHorizontalSpace = true;
		GridData gridData7 = new org.eclipse.swt.layout.GridData();
		gridData7.grabExcessHorizontalSpace = true;
		GridData gridData6 = new org.eclipse.swt.layout.GridData();
		gridData6.horizontalSpan = 4;
		GridData gridData5 = new org.eclipse.swt.layout.GridData();
		gridData5.horizontalSpan = 4;
		GridData gridData4 = new org.eclipse.swt.layout.GridData();
		gridData4.horizontalSpan = 4;
		GridData gridData3 = new org.eclipse.swt.layout.GridData();
		gridData3.horizontalSpan = 4;
		gridData3.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData3.verticalAlignment = org.eclipse.swt.layout.GridData.CENTER;
		gridData3.grabExcessHorizontalSpace = false;
		gridData3.heightHint = 3;
		GridData gridData2 = new org.eclipse.swt.layout.GridData();
		gridData2.horizontalSpan = 3;
		gridData2.widthHint = 200;
		GridData gridData1 = new org.eclipse.swt.layout.GridData();
		gridData1.horizontalSpan = 4;
		gridData1.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridData gridData = new org.eclipse.swt.layout.GridData();
		gridData.horizontalSpan = 4;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		GridLayout gridLayout = new GridLayout();
		gridLayout.numColumns = 4;
		sShell = new Composite(dlgShell, SWT.NONE);
		sShell.setLayout(gridLayout);
		group1 = new Group(sShell, SWT.NONE);
		group1.setText(Messages.getString("GROUP.SOURCEDATABASE"));
		group1.setLayout(gridLayout1);
		group1.setLayoutData(gridData);
		group2 = new Group(sShell, SWT.NONE);
		group2.setText(Messages.getString("GROUP.DESTINATIONDATABASE"));
		group2.setLayout(gridLayout5);
		group2.setLayoutData(gridData1);
		label1 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label1.setText(Messages.getString("LABEL.DATABASENAME1"));
		EDIT_COPYDB_SOURCEDB = new Text(group1, SWT.BORDER);
		EDIT_COPYDB_SOURCEDB.setEditable(false);
		EDIT_COPYDB_SOURCEDB.setLayoutData(gridData21);
		label2 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label2.setText(Messages.getString("LABEL.DATABASEDIRECTORY"));
		EDIT_COPYDB_SRCDATADIR = new Text(group1, SWT.BORDER);
		EDIT_COPYDB_SRCDATADIR.setEditable(false);
		EDIT_COPYDB_SRCDATADIR.setLayoutData(gridData31);
		label3 = new Label(group1, SWT.LEFT | SWT.WRAP);
		label3.setText(Messages.getString("LABEL.LOGFILEDIRECTORY"));
		EDIT_COPYDB_SRCLOGDIR = new Text(group1, SWT.BORDER);
		EDIT_COPYDB_SRCLOGDIR.setEditable(false);
		EDIT_COPYDB_SRCLOGDIR.setLayoutData(gridData41);
		label4 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label4.setText(Messages.getString("LABEL.DATABASENAME1"));
		EDIT_COPYDB_DESTDB = new Text(group2, SWT.BORDER);
		EDIT_COPYDB_DESTDB.setLayoutData(gridData61);
		EDIT_COPYDB_DESTDB
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						String tmp = EDIT_COPYDB_DESTDB.getText();
						if (tmp == null || tmp.length() <= 0)
							IDOK.setEnabled(false);
						else
							IDOK.setEnabled(true);
						if (!isChanged) {
							String newpath = MainRegistry.envCUBRID_DATABASES
									+ "/" + tmp;
							EDIT_COPYDB_DESTDATADIR.setText(newpath);
							EDIT_COPYDB_EXVOLDIR.setText(newpath);
							EDIT_COPYDB_DESTLOGDIR.setText(newpath);
							LIST_COPYDB_VOLLIST.getItem(0).setText(1, tmp);
							LIST_COPYDB_VOLLIST.getItem(0).setText(2, newpath);
							NumberFormat nf = NumberFormat.getInstance();
							nf.setMinimumIntegerDigits(3);
							for (int i = 1, n = LIST_COPYDB_VOLLIST
									.getItemCount(); i < n; i++) {
								TableItem ti = LIST_COPYDB_VOLLIST.getItem(i);
								ti.setText(1, tmp + "_x" + nf.format(i));
								ti.setText(2, newpath);
							}
							isChanged = false;
						}
					}
				});
		label5 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label5.setText(Messages.getString("LABEL.DATABASEDIRECTORY"));
		EDIT_COPYDB_DESTDATADIR = new Text(group2, SWT.BORDER);
		EDIT_COPYDB_DESTDATADIR.setLayoutData(gridData71);
		EDIT_COPYDB_DESTDATADIR
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						isChanged = true;
					}
				});
		label6 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label6.setText(Messages.getString("LABEL.EXTENDEDVOLUME"));
		EDIT_COPYDB_EXVOLDIR = new Text(group2, SWT.BORDER);
		EDIT_COPYDB_EXVOLDIR.setLayoutData(gridData81);
		EDIT_COPYDB_EXVOLDIR
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						isChanged = true;
					}
				});
		label7 = new Label(group2, SWT.LEFT | SWT.WRAP);
		label7.setText(Messages.getString("LABEL.LOGFILEDIRECTORY"));
		EDIT_COPYDB_DESTLOGDIR = new Text(group2, SWT.BORDER);
		EDIT_COPYDB_DESTLOGDIR.setLayoutData(gridData91);
		EDIT_COPYDB_DESTLOGDIR
				.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
					public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
						isChanged = true;
					}
				});
		label8 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label8.setText(Messages.getString("LABEL.FREEDISKSPACE"));
		label8.setLayoutData(gridDataz9);
		label9 = new Label(sShell, SWT.LEFT | SWT.WRAP);
		label9.setLayoutData(gridData2);
		clabel1 = new CLabel(sShell, SWT.SHADOW_IN);
		clabel1.setLayoutData(gridData3);
		CHECK_COPYDB_ADVANCED = new Button(sShell, SWT.CHECK);
		CHECK_COPYDB_ADVANCED.setText(Messages
				.getString("CHECK.COPYINDIVIDUAL"));
		CHECK_COPYDB_ADVANCED.setLayoutData(gridData4);
		CHECK_COPYDB_ADVANCED
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (CHECK_COPYDB_ADVANCED.getSelection()) {
							LIST_COPYDB_VOLLIST.setEnabled(true);
							EDIT_COPYDB_DESTDATADIR.setEnabled(false);
							EDIT_COPYDB_EXVOLDIR.setEnabled(false);
						} else {
							LIST_COPYDB_VOLLIST.setEnabled(false);
							EDIT_COPYDB_DESTDATADIR.setEnabled(true);
							EDIT_COPYDB_EXVOLDIR.setEnabled(true);
						}
					}
				});
		createTable1();
		CHECK_COPYDB_OVERWRITE = new Button(sShell, SWT.CHECK);
		CHECK_COPYDB_OVERWRITE.setText(Messages.getString("CHECK.OVERWRITE"));
		CHECK_COPYDB_OVERWRITE.setLayoutData(gridData5);
		CHECK_COPYDB_MOVE = new Button(sShell, SWT.CHECK);
		CHECK_COPYDB_MOVE.setText(Messages.getString("CHECK.MOVE"));
		CHECK_COPYDB_MOVE.setLayoutData(gridData6);
		cLabel = new CLabel(sShell, SWT.NONE);
		cLabel.setLayoutData(gridData7);
		cLabel1 = new CLabel(sShell, SWT.NONE);
		cLabel1.setLayoutData(gridData8);
		IDOK = new Button(sShell, SWT.NONE);
		IDOK.setText(Messages.getString("BUTTON.OK"));
		IDOK.setLayoutData(gridData9);
		IDOK.setEnabled(false);
		IDOK
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String tmp = EDIT_COPYDB_DESTDB.getText();
						if (tmp == null || tmp.length() <= 0
								|| tmp.indexOf(" ") >= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTTARGETDB"));
							return;
						}
						tmp = EDIT_COPYDB_DESTLOGDIR.getText();
						if (tmp == null || tmp.length() <= 0
								|| tmp.indexOf(" ") >= 0) {
							CommonTool.ErrorBox(dlgShell, Messages
									.getString("ERROR.INPUTLOGDIRECTORY"));
							return;
						}
						if (CHECK_COPYDB_OVERWRITE.getSelection()) {
							if (MainRegistry.Authinfo_find(EDIT_COPYDB_DESTDB
									.getText()) == null) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.NODBTOOVERWRITE"));
								return;
							}
						} else {
							if (MainRegistry.Authinfo_find(EDIT_COPYDB_DESTDB
									.getText()) != null) {
								CommonTool
										.ErrorBox(
												dlgShell,
												Messages
														.getString("ERROR.DESITINATIONDBEXIST"));
								return;
							}
						}
						if (CommonTool.WarnYesNo(dlgShell, Messages
								.getString("WARNYESNO.COPYDB")) == SWT.YES) {
							ClientSocket cs = new ClientSocket();
							if (!CheckDirs(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							dlgShell.update();
							if (MainRegistry.Tmpchkrst.size() > 0) { // create
																		// directory
																		// confirm
								NEWDIRECTORYDialog newdlg = new NEWDIRECTORYDialog(
										dlgShell);
								if (newdlg.doModal() == 0)
									return;
							}
							if ((CommonTool.atof(MainRegistry.TmpVolsize) / (1024 * 1024)) > CopyAction.ai.freespace) {
								CommonTool.ErrorBox(dlgShell, Messages
										.getString("ERROR.NOTENOUGHSPACE"));
								return;
							}
							if (((CommonTool.atof(MainRegistry.TmpVolsize) / (1024 * 1024)) * 1.1) > CopyAction.ai.freespace) {
								if (CommonTool
										.WarnYesNo(
												dlgShell,
												Messages
														.getString("WARNYESNO.COPYDBSPACEOVER")) != SWT.YES)
									return;
							}

							cs = new ClientSocket();
							if (!SendCopy(cs)) {
								CommonTool.ErrorBox(dlgShell, cs.ErrorMsg);
								return;
							}
							ret = true;
							dlgShell.dispose();
						}
					}
				});
		IDCANCEL = new Button(sShell, SWT.NONE);
		IDCANCEL.setText(Messages.getString("BUTTON.CANCEL"));
		IDCANCEL.setLayoutData(gridData10);
		IDCANCEL
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						ret = false;
						dlgShell.dispose();
					}
				});
		dlgShell.pack();
		setinfo();
	}

	private void createTable1() {
		LIST_COPYDB_VOLLIST = new Table(sShell, SWT.FULL_SELECTION | SWT.BORDER);
		LIST_COPYDB_VOLLIST.setLinesVisible(true);
		LIST_COPYDB_VOLLIST.setHeaderVisible(true);
		GridData gridDatazz = new org.eclipse.swt.layout.GridData();
		gridDatazz.widthHint = 404;
		gridDatazz.heightHint = 168;
		gridDatazz.horizontalSpan = 4;
		LIST_COPYDB_VOLLIST.setLayoutData(gridDatazz);

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 100, true));
		tlayout.addColumnData(new ColumnWeightData(50, 200, true));
		LIST_COPYDB_VOLLIST.setLayout(tlayout);

		TableColumn tblcol = new TableColumn(LIST_COPYDB_VOLLIST, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.CURRENTVOLNAME"));
		tblcol = new TableColumn(LIST_COPYDB_VOLLIST, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NEWVOLNAME"));
		tblcol = new TableColumn(LIST_COPYDB_VOLLIST, SWT.LEFT);
		tblcol.setText(Messages.getString("TABLE.NEWDIRECTORYPATH"));

		listener = new EditVolumeList(LIST_COPYDB_VOLLIST);
		LIST_COPYDB_VOLLIST.addListener(SWT.MouseUp, listener);
	}

	class VolOrderComparator implements Comparator {
		public int compare(Object obj1, Object obj2) {
			int order1 = ((VolumeInfo) obj1).order;
			int order2 = ((VolumeInfo) obj2).order;
			return order1 < order2 ? -1 : (order1 == order2 ? 0 : 1);
		}
	}

	private void setinfo() {
		VolumeInfo vi;
		TableItem item;
		Collections.sort(CopyAction.ai.Volinfo, new VolOrderComparator());
		for (int i = 0, n = CopyAction.ai.Volinfo.size(); i < n; i++) {
			vi = (VolumeInfo) CopyAction.ai.Volinfo.get(i);
			if (!vi.type.equals("GENERIC") && !vi.type.equals("DATA")
					&& !vi.type.equals("INDEX") && !vi.type.equals("TEMP"))
				continue;
			item = new TableItem(LIST_COPYDB_VOLLIST, SWT.NONE);
			item.setText(0, vi.spacename);
			item.setText(1, vi.spacename);
			item.setText(2, MainRegistry.envCUBRID_DATABASES);
		}
		for (int i = 0, n = LIST_COPYDB_VOLLIST.getColumnCount(); i < n; i++) {
			LIST_COPYDB_VOLLIST.getColumn(i).pack();
		}
		Collections.sort(CopyAction.ai.Volinfo);
		EDIT_COPYDB_SOURCEDB.setText(CopyAction.ai.dbname);
		EDIT_COPYDB_SRCDATADIR.setText(CopyAction.ai.dbdir);
		EDIT_COPYDB_SRCLOGDIR.setText(CopyAction.ai.dbdir);
		EDIT_COPYDB_DESTDATADIR.setText(MainRegistry.envCUBRID_DATABASES);
		EDIT_COPYDB_EXVOLDIR.setText(MainRegistry.envCUBRID_DATABASES);
		EDIT_COPYDB_DESTLOGDIR.setText(MainRegistry.envCUBRID_DATABASES);
		label9.setText(CopyAction.ai.freespace + "(MB)");
		LIST_COPYDB_VOLLIST.setEnabled(false);
		CHECK_COPYDB_ADVANCED.setSelection(false);
		ClientSocket cs = new ClientSocket();
		cs.SendClientMessage(dlgShell, "dbname:" + CopyAction.ai.dbname,
				"getdbsize");
		isChanged = false;
	}

	private boolean CheckDirs(ClientSocket cs) {
		String requestMsg = "";
		String tmp = "";
		if (CHECK_COPYDB_ADVANCED.getSelection()) {
			for (int i = 0, n = LIST_COPYDB_VOLLIST.getItemCount(); i < n; i++) {
				TableItem ti = LIST_COPYDB_VOLLIST.getItem(i);
				tmp = "dir:" + ti.getText(2) + "\n";
				if (requestMsg.indexOf(tmp) < 0)
					requestMsg += tmp;
			}
		} else {
			requestMsg += "dir:" + EDIT_COPYDB_DESTDATADIR.getText() + "\n";
			if (!EDIT_COPYDB_DESTDATADIR.getText().equals(
					EDIT_COPYDB_EXVOLDIR.getText()))
				requestMsg += "dir:" + EDIT_COPYDB_EXVOLDIR.getText() + "\n";
		}
		tmp = "dir:" + EDIT_COPYDB_DESTLOGDIR.getText() + "\n";
		if (requestMsg.indexOf(tmp) < 0)
			requestMsg += tmp;

		if (cs.SendBackGround(dlgShell, requestMsg, "checkdir", Messages
				.getString("WAITING.CHECKINGDIRECTORY"))) {
			return true;
		}
		return false;
	}

	private boolean SendCopy(ClientSocket cs) {
		String requestMsg = "";
		requestMsg += "srcdbname:" + EDIT_COPYDB_SOURCEDB.getText() + "\n";
		requestMsg += "destdbname:" + EDIT_COPYDB_DESTDB.getText() + "\n";
		requestMsg += "destdbpath:" + EDIT_COPYDB_DESTDATADIR.getText() + "\n";
		requestMsg += "exvolpath:" + EDIT_COPYDB_EXVOLDIR.getText() + "\n";
		requestMsg += "logpath:" + EDIT_COPYDB_DESTLOGDIR.getText() + "\n";
		if (CHECK_COPYDB_OVERWRITE.getSelection())
			requestMsg += "overwrite:y\n";
		else
			requestMsg += "overwrite:n\n";

		if (CHECK_COPYDB_MOVE.getSelection())
			requestMsg += "move:y\n";
		else
			requestMsg += "move:n\n";

		if (CHECK_COPYDB_ADVANCED.getSelection()) {
			requestMsg += "advanced:on\n";
			requestMsg += "open:volume\n";

			String oldVolName, newVolName, oldVolDir, newVolDir;
			for (int i = 0, n = LIST_COPYDB_VOLLIST.getItemCount(); i < n; i++) {
				TableItem ti = LIST_COPYDB_VOLLIST.getItem(i);
				oldVolDir = ((VolumeInfo) CopyAction.ai.Volinfo.get(i)).location;
				oldVolName = ti.getText(0);
				newVolName = ti.getText(1);
				newVolDir = ti.getText(2);
				oldVolDir = oldVolDir.replaceAll(":", "|");
				newVolDir = newVolDir.replaceAll(":", "|");
				requestMsg += oldVolDir + "/" + oldVolName + ":" + newVolDir
						+ "/" + newVolName + "\n";
			}
			requestMsg += "close:volume\n";
		} else {
			requestMsg += "advanced:off\n";
		}

		if (cs.SendBackGround(dlgShell, requestMsg, "copydb", Messages
				.getString("WAITING.COPYDB"))) {
			DBUserInfo ui = MainRegistry.getDBUserInfo(EDIT_COPYDB_SOURCEDB
					.getText());
			DBUserInfo destui = new DBUserInfo(EDIT_COPYDB_DESTDB.getText(),
					"", "");
			destui.isDBAGroup = ui.isDBAGroup;

			MainRegistry.addDBUserInfo(destui);
			AuthItem authrec = MainRegistry.Authinfo_find(EDIT_COPYDB_SOURCEDB
					.getText());
			authrec.setinfo = false;
			authrec.status = MainConstants.STATUS_STOP;

			AuthItem destai = new AuthItem(EDIT_COPYDB_DESTDB.getText(), "",
					"", MainRegistry.Authinfo_find(ui.dbname).status, false);

			MainRegistry.Authinfo_add(destai);
			return true;
		}
		return false;
	}
}

class EditVolumeList implements Listener {

	private Table table = null;

	private int curIndex = -1;

	private int newIndex = -1;

	private boolean hasChange = false;

	public EditVolumeList(Table volList) {
		table = volList;
	}

	public boolean getChanged() {
		return hasChange;
	}

	public void handleEvent(Event event) {
		Rectangle clientArea = table.getClientArea();
		Point pt = new Point(event.x, event.y);
		int index = table.getTopIndex();
		final TableEditor editor = new TableEditor(table);
		editor.horizontalAlignment = SWT.LEFT;
		editor.grabHorizontal = true;

		curIndex = newIndex;
		newIndex = table.getSelectionIndex();
		if (curIndex < 0 || curIndex != newIndex)
			return;
		while (index < table.getItemCount()) {
			boolean visible = false;
			final TableItem item = table.getItem(index);
			for (int i = 1; i < table.getColumnCount(); i++) {
				if (index == 0 && i == 1)
					continue;
				Rectangle rect = item.getBounds(i);
				if (rect.contains(pt)) {
					final int column = i;
					final Text text = new Text(table, SWT.MULTI);
					text.setEditable(true);
					Listener textListener = new Listener() {
						public void handleEvent(final Event e) {
							switch (e.type) {
							case SWT.FocusOut:
								if (!text.getText()
										.equals(item.getText(column))) {
									item.setText(column, text.getText());
									hasChange = true;
									COPYDBDialog.isChanged = true;
									RENAMEDBDialog.isChanged = true;
								}
								text.dispose();
								break;
							case SWT.Traverse:
								switch (e.detail) {
								case SWT.TRAVERSE_RETURN:
									if (!text.getText().equals(
											item.getText(column))) {
										item.setText(column, text.getText());
										hasChange = true;
										COPYDBDialog.isChanged = true;
										RENAMEDBDialog.isChanged = true;
									}
								case SWT.TRAVERSE_ESCAPE:
									text.dispose();
									e.doit = false;
								}
								break;
							}
						}
					};
					text.addListener(SWT.FocusOut, textListener);
					text.addListener(SWT.Traverse, textListener);
					editor.setEditor(text, item, i);
					text.setText(item.getText(i));
					text.selectAll();
					text.setFocus();
					return;
				}
				if (!visible && rect.intersects(clientArea)) {
					visible = true;
				}
			}
			if (!visible)
				return;
			index++;
		}
	}
}
