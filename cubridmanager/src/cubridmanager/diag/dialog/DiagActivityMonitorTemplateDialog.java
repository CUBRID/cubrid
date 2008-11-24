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

import org.eclipse.jface.viewers.ColumnWeightData;
import org.eclipse.jface.viewers.TableLayout;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TreeItem;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.diag.DiagActivityMonitorTemplate;
import cubridmanager.diag.DiagMonitorConfig;
import cubridmanager.diag.view.DiagView;
import org.eclipse.swt.widgets.Combo;

public class DiagActivityMonitorTemplateDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="6,12"
	private Composite composite = null;
	private Group group1 = null;
	private Tree treeActivityMonitorSource = null;
	private Button button3 = null;
	private Button button4 = null;
	private Button button5 = null;
	private Group group2 = null;
	private Label label = null;
	private Label label1 = null;
	private Text textDesc = null;
	private Button button8 = null;
	private Table tableSelectedItem = null;
	private Text textTemplateName = null;
	private Button button6 = null;
	private boolean isUpdate = false;
	private Label label2 = null;
	private Combo comboTargetDatabase = null;

	public DiagActivityMonitorTemplateDialog(Shell parent) {
		super(parent);
	}

	public DiagActivityMonitorTemplateDialog(Shell parent, int style) {
		super(parent, style);
	}

	public int doModal(String templateName) {
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
			String currentSiteName = MainRegistry.GetCurrentSiteName();
			DiagActivityMonitorTemplate diagActivityMonitorTemplate = MainRegistry
					.getActivityTemplateByName(currentSiteName, templateName);

			if (diagActivityMonitorTemplate == null) {
				CommonTool.ErrorBox(Messages
						.getString("ERROR.TEMPLATENOTEXIST"));
				sShell.dispose();
			} else {
				if (!diagActivityMonitorTemplate.targetdb.trim().equals("")) {
					comboTargetDatabase.setEnabled(true);
					comboTargetDatabase
							.setText(diagActivityMonitorTemplate.targetdb);
				}

				textTemplateName
						.setText(diagActivityMonitorTemplate.templateName);
				textDesc.setText(diagActivityMonitorTemplate.desc);
				setCurrentMonitorConfig(diagActivityMonitorTemplate.activity_config);
			}

			isUpdate = true;
			textTemplateName.setEditable(false);
		}

		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		CommonTool.centerShell(sShell);

		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(SWT.DIALOG_TRIM | SWT.APPLICATION_MODAL);
		sShell.setText(Messages.getString("TITLE.ACTIVITYTEMPLATECONFIG"));
		createComposite();
		sShell.setSize(new org.eclipse.swt.graphics.Point(437, 450));
		button8 = new Button(sShell, SWT.NONE);
		button8.setBounds(new org.eclipse.swt.graphics.Rectangle(343, 391, 74,
				21));
		button8.setText(Messages.getString("BUTTON.CANCEL"));
		button6 = new Button(sShell, SWT.NONE);
		button6.setBounds(new org.eclipse.swt.graphics.Rectangle(247, 391, 74,
				21));
		button6.setText(Messages.getString("BUTTON.DIAGSAVE"));
		button6.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
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
									"updateactivitytemplate")) {
								CommonTool.ErrorBox(psh, cs.ErrorMsg);
								return;
							}
						} else {
							if (!cs.SendClientMessage(psh, msg,
									"addactivitytemplate")) {
								CommonTool.ErrorBox(psh, cs.ErrorMsg);
								return;
							}
						}

						// Add new information when message send is success.
						DiagView.myNavi.saveExpandedState();
						DiagView.refresh();
						sShell.dispose();
					}
				});
		button8.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		composite = new Composite(sShell, SWT.NONE);
		createGroup1();
		createGroup2();
		composite.setBounds(new org.eclipse.swt.graphics.Rectangle(7, 9, 418, 372));
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		group1 = new Group(composite, SWT.NONE);
		group1.setText(Messages.getString("LABEL.SETEVENT"));
		createTree();
		group1.setBounds(new org.eclipse.swt.graphics.Rectangle(9, 6, 397,
						269));
		button3 = new Button(group1, SWT.NONE);
		button3.setBounds(new Rectangle(151, 76, 57, 18));
		button3.setText(Messages.getString("BUTTON.DIAGADD"));
		button3.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						String msg = new String();
						for (int i = 0; i < treeActivityMonitorSource
								.getSelectionCount(); i++) {
							msg = "";
							TreeItem item = (treeActivityMonitorSource
									.getSelection())[i];
							msg = insertItemToTargetList(item);
							if (msg != "") {
								CommonTool.ErrorBox(msg);
							}
						}
					}
				});
		button4 = new Button(group1, SWT.NONE);
		button4.setBounds(new Rectangle(152, 108, 56, 18));
		button4.setText(Messages.getString("BUTTON.DIAGREMOVE"));
		button4
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						int i;
						TableItem selectedItem = null;

						for (i = 0; i < tableSelectedItem.getSelectionCount(); i++) {
							selectedItem = tableSelectedItem.getSelection()[i];
							selectedItem.dispose();
						}

						for (i = 0; i < tableSelectedItem.getItemCount(); i++) {
							if (tableSelectedItem.getItem(i).getText(0).equals(
									"db"))
								break;
						}

						if (i == tableSelectedItem.getItemCount())
							comboTargetDatabase.setEnabled(false);
					}
				});
		button5 = new Button(group1, SWT.NONE);
		button5.setBounds(new org.eclipse.swt.graphics.Rectangle(271, 226, 65,
				20));
		button5.setEnabled(false);
		button5.setText(Messages.getString("LABEL.SETFILTER"));
		createTableSelected();
	}

	/**
	 * This method initializes tree
	 * 
	 */
	private void createTree() {
		treeActivityMonitorSource = new Tree(group1, SWT.BORDER);
		treeActivityMonitorSource.setBounds(new Rectangle(10, 21, 136, 225));

		TreeItem root = new TreeItem(treeActivityMonitorSource, SWT.NONE);
		TreeItem db = new TreeItem(root, SWT.NONE);
		TreeItem cas = new TreeItem(root, SWT.NONE);
		TreeItem resource = new TreeItem(root, SWT.NONE);
		TreeItem driver = new TreeItem(root, SWT.NONE);
		root.setExpanded(true);

		root.setText("Status Monitor List");
		db.setText("db");
		cas.setText("cas");
		resource.setText("resource");
		driver.setText("driver");

		insertTreeObject(db);
		insertTreeObject(cas);
	}

	/**
	 * This method initializes group2
	 * 
	 */
	private void createGroup2() {
		group2 = new Group(composite, SWT.NONE);
		group2.setText(Messages.getString("LABEL.TEMPLATE"));
		group2.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 288, 394,
				75));
		label = new Label(group2, SWT.NONE);
		label.setBounds(new Rectangle(15, 19, 52, 13));
		label.setText(Messages.getString("LABEL.DIAGTEMPLATENAME"));
		label1 = new Label(group2, SWT.NONE);
		label1.setBounds(new Rectangle(16, 42, 55, 15));
		label1.setText(Messages.getString("LABEL.DIAGTEMPLATEDESC"));
		textDesc = new Text(group2, SWT.BORDER);
		textDesc.setBounds(new org.eclipse.swt.graphics.Rectangle(74, 41, 178,
				18));
		textTemplateName = new Text(group2, SWT.BORDER);
		textTemplateName.setBounds(new org.eclipse.swt.graphics.Rectangle(75,
				17, 178, 18));
		label2 = new Label(group2, SWT.NONE);
		label2.setBounds(new org.eclipse.swt.graphics.Rectangle(277, 18, 108,
				20));
		label2.setText(Messages.getString("LABEL.TARGETDATABASE"));
		createComboTargetDatabase();
	}

	/**
	 * This method initializes tableSelected
	 * 
	 */
	private void createTableSelected() {
		tableSelectedItem = new Table(group1, SWT.BORDER | SWT.FULL_SELECTION);
		tableSelectedItem.setHeaderVisible(true);
		tableSelectedItem.setLinesVisible(true);
		tableSelectedItem.setBounds(new org.eclipse.swt.graphics.Rectangle(216,
				23, 166, 189));

		TableLayout tlayout = new TableLayout();
		tlayout.addColumnData(new ColumnWeightData(30, 40, true));
		tlayout.addColumnData(new ColumnWeightData(60, 80, true));
		tlayout.addColumnData(new ColumnWeightData(30, 40, true));
		tlayout.addColumnData(new ColumnWeightData(10, 20, true));
		tableSelectedItem.setLayout(tlayout);

		TableColumn categoryColumn = new TableColumn(tableSelectedItem,
				SWT.LEFT);
		TableColumn nameColumn = new TableColumn(tableSelectedItem, SWT.LEFT);
		categoryColumn.setText(Messages.getString("TABLE.DIAGCATEGORY"));
		nameColumn.setText(Messages.getString("TABLE.DIAGTARGET"));

		categoryColumn.setWidth(40);
		nameColumn.setWidth(130);
	}

	public void insertTreeObject(TreeItem parent) {
		String parent_text = parent.getText();
		if (parent_text == "db") {
			TreeItem query_fullscan = new TreeItem(parent, SWT.NONE);
			TreeItem lock_deadlock = new TreeItem(parent, SWT.NONE);
			TreeItem buffer_page_read = new TreeItem(parent, SWT.NONE);
			TreeItem buffer_page_write = new TreeItem(parent, SWT.NONE);

			query_fullscan.setText("query_fullscan");
			lock_deadlock.setText("deadlock");
			buffer_page_read.setText("buffer_page_read");
			buffer_page_write.setText("buffer_page_write");

			parent.setExpanded(true);
		} else if (parent_text == "cas") {
			TreeItem request_sec = new TreeItem(parent, SWT.NONE);
			TreeItem transaction_sec = new TreeItem(parent, SWT.NONE);
			request_sec.setText("request");
			transaction_sec.setText("transaction");

			parent.setExpanded(true);
		} else if (parent_text == "resource") {

		} else if (parent_text == "driver") {

		}
	}

	public String makeMsg(String name, String desc) {
		String msg = new String();
		String targetdb = comboTargetDatabase.getText();

		msg = "name:";
		msg += name;
		msg += "\n";
		msg += "desc:" + desc + "\n";
		if (targetdb.trim().length() != 0)
			msg += "db_name:" + targetdb + "\n";
		msg += "open:target_config\n";
		for (int i = 0; i < tableSelectedItem.getItemCount(); i++) {
			TableItem currentItem = tableSelectedItem.getItem(i);
			if (currentItem.getText(0).equals("cas")) {
				if (currentItem.getText(1).equals("request")) {
					msg += "cas_act_request:yes\n";
				} else if (currentItem.getText(1).equals("transaction")) {
					msg += "cas_act_transaction:yes\n";
				}
			} else if (currentItem.getText(0).equals("db")) {
				if (currentItem.getText(1).equals("query_fullscan")) {
					msg += "act_cub_query_fullscan:yes\n";
				} else if (currentItem.getText(1).equals("deadlock")) {
					msg += "act_cub_lock_deadlock:yes\n";
				} else if (currentItem.getText(1).equals("buffer_page_read")) {
					msg += "act_cub_buffer_page_read:yes\n";
				} else if (currentItem.getText(1).equals("buffer_page_write")) {
					msg += "act_cub_buffer_page_write:yes\n";
				}
			} else if (currentItem.getText(0).equals("driver")) {
			} else if (currentItem.getText(0).equals("resource")) {
			}
		}

		msg += "close:target_config\n";
		return msg;
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
		} else {// if
				// (parent.getParentItem().getText().equals(source_root.getText()))
				// {
			// leaf node
			int targetCount = tableSelectedItem.getItemCount();
			int i;
			for (i = 0; i < targetCount; i++) {
				if (parent.getText().equals(
						tableSelectedItem.getItem(i).getText(0))) {
					// category is same
					if (item.getText().equals(
							tableSelectedItem.getItem(i).getText(1))) {
						// and item name is same then that item has been existed.
						break;
					}
				}
			}

			if (i == targetCount) {
				// add item 
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, parent.getText());
				newItem.setText(1, item.getText());

				if (parent.getText().equals("db"))
					comboTargetDatabase.setEnabled(true);
			}
		}

		return "";
	}

	public String setCurrentMonitorConfig(DiagMonitorConfig config) {
		if (config.NEED_CAS_ACT_DATA_REQ()) {
			TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
			newItem.setText(0, "cas");
			newItem.setText(1, "request");
		}
		if (config.NEED_CAS_ACT_DATA_TRAN()) {
			TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
			newItem.setText(0, "cas");
			newItem.setText(1, "transaction");
		}
		if (config.dbData.needCubActivity) {
			if (config.dbData.act_query_fullscan) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "db");
				newItem.setText(1, "query_fullscan");
			}
			if (config.dbData.act_buffer_page_read) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "db");
				newItem.setText(1, "buffer_page_read");
			}
			if (config.dbData.act_buffer_page_write) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "db");
				newItem.setText(1, "buffer_page_write");
			}
			if (config.dbData.act_lock_deadlock) {
				TableItem newItem = new TableItem(tableSelectedItem, SWT.NONE);
				newItem.setText(0, "db");
				newItem.setText(1, "deadlock");
			}
		}

		return "";
	}

	/**
	 * This method initializes comboTargetDatabase
	 * 
	 */
	private void createComboTargetDatabase() {
		comboTargetDatabase = new Combo(group2, SWT.READ_ONLY);
		comboTargetDatabase.setEnabled(false);
		comboTargetDatabase.setBounds(new org.eclipse.swt.graphics.Rectangle(
				277, 41, 108, 21));
	}
}
