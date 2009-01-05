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

package cubridmanager.query.dialog;

import java.util.Hashtable;
import java.util.StringTokenizer;
import java.util.Vector;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MenuItem;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.TreeItem;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import cubridmanager.query.StructQueryPlan;
import cubridmanager.query.view.QueryEditor;

public class QueryPlan extends Dialog {
	private QueryEditor qe;
	private QueryPlan qp;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="10,43"
	private SashForm sashForm = null;
	private Composite cmpBtnArea = null;
	private Text txtQuery = null;
	private Tree treeObject = null;
	private Button btnPrev = null;
	private Button btnNext = null;
	private Button btnClose = null;
	private int size = 0;
	private int currIndex = -1;
	private Vector queryPlans = new Vector();
	private Hashtable ht_node_term = new Hashtable();
	private Hashtable ht_detail_view = new Hashtable();
	public boolean isQueryPlanViewerDlgOpen = false;

	public QueryPlan(QueryEditor qe, Shell parent) {
		super(parent, SWT.DIALOG_TRIM | SWT.MODELESS);
		this.qe = qe;
		qp = this;

	}

	public void open(Vector queryPlans) {
		createSShell();

		this.queryPlans = (Vector) queryPlans.clone();
		size = queryPlans.size();

		// Display query plan at last executed .
		fillTextArea();
		setButtonEnable();

		sShell.pack();
		CommonTool.centerShell(sShell);
		sShell.open();

		Display display = getParent().getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch()) {
				display.sleep();
			}
		}
	}

	/**
	 * This method initializes sShell
	 */
	private void createSShell() {
		// sShell = new Shell(getParent(), getStyle());
		sShell = new Shell(getParent(), SWT.SHELL_TRIM);
		createSashForm();
		createCmpBtnArea();
		sShell.setSize(new org.eclipse.swt.graphics.Point(400, 300));
		GridLayout gridLayout = new GridLayout();
		sShell.setLayout(gridLayout);

		sShell.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				qe.isQueryPlanDlgOpen = false;
			}
		});
	}

	/**
	 * This method initializes sashForm
	 * 
	 */
	private void createSashForm() {

		GridData gridData = new GridData();
		gridData.horizontalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.verticalAlignment = org.eclipse.swt.layout.GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.heightHint = 650;
		gridData.widthHint = 600;
		sashForm = new SashForm(sShell, SWT.NONE);
		sashForm.setOrientation(org.eclipse.swt.SWT.VERTICAL);
		sashForm.setLayoutData(gridData);
		txtQuery = new Text(sashForm, SWT.BORDER | SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL);
		txtQuery.setEditable(false);
		txtQuery.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		treeObject = new Tree(sashForm, SWT.BORDER | SWT.MULTI | SWT.WRAP);
		treeObject.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		treeObject.setLayoutData(gridData);

		treeObject.addMouseListener(new org.eclipse.swt.events.MouseAdapter() {
			public void mouseUp(org.eclipse.swt.events.MouseEvent e) {

				if (e.button > 1) {
					// TODO : Change direct process to menu.

					TreeItem item = treeObject.getSelection()[0];

					Menu menu = new Menu(sShell, SWT.POP_UP);
					final MenuItem m_open = new MenuItem(menu, SWT.PUSH);
					m_open.setText(Messages.getString("QUERYPLAN.ALLEXPANDED"));

					final MenuItem m_close = new MenuItem(menu, SWT.PUSH);
					m_close.setText(Messages
							.getString("QUERYPLAN.ALLUNEXPANDED"));
					if (item.getItemCount() == 0) {
						m_open.setEnabled(false);
						m_close.setEnabled(false);
					} else if (item.getExpanded()) {
						m_open.setEnabled(false);
						m_close.setEnabled(true);
					} else {
						m_open.setEnabled(true);
						m_close.setEnabled(false);
					}

					MenuItem m_view = new MenuItem(menu, SWT.PUSH);
					m_view.setText(Messages.getString("QUERYPLAN.DETAILVIEW"));
					m_view.addSelectionListener(new SelectionAdapter() {
						public void widgetSelected(SelectionEvent event) {
							MouseClick();
							/*
							 * if (!isQueryPlanViewerDlgOpen) { MouseClick(); }
							 */
						}
					});
					m_open.addSelectionListener(new SelectionAdapter() {
						public void widgetSelected(SelectionEvent event) {
							TreeItem item = treeObject.getSelection()[0];
							Expanded(item, true);
						}
					});
					m_close.addSelectionListener(new SelectionAdapter() {
						public void widgetSelected(SelectionEvent event) {
							TreeItem item = treeObject.getSelection()[0];
							Expanded(item, false);
						}
					});

					menu.setVisible(true);

					// MouseClick();
				}

			}
		});

		sashForm.setWeights(new int[] { 30, 70 });
	}

	/**
	 * This method initializes cmpBtnArea
	 * 
	 */
	private void Expanded(TreeItem tree, boolean yn) {
		if (tree.getItemCount() > 0) {
			TreeItem subitem[] = tree.getItems();
			for (int i = 0; i < tree.getItemCount(); i++)
				if (subitem[i].getItemCount() > 0)
					Expanded(subitem[i], yn);
		}
		tree.setExpanded(yn);
	}

	private void MouseClick() {
		int querytype = 0;

		QueryPlanViewer dlg = new QueryPlanViewer(qp, sShell);
		TreeItem item = treeObject.getSelection()[0];

		if (item.getText() == null)
			return;
		String str = item.getText();
		int len = str.length();

		if (str.equals("Query plan:"))
			querytype = 2;
		else if (str.equals("Query stmt:"))
			querytype = 1;
		else if (str.substring(len - 1, len).equals(":"))
			querytype = 0;
		else
			querytype = findQueryPlan(item);

		if (querytype == 1 && !str.equals("Query stmt:"))
			str = String_Cut(str, 0);

		if (item.getItemCount() > 0)
			str = str + GetChildData(item, 1, querytype);

		// if ( querytype == 2 )
		str = Plan_Found(str, item);

		// CommonTool.MsgBox(sShell, "tt", "sdfd : " +
		// node_term.get("term[1]"));
		isQueryPlanViewerDlgOpen = true;
		dlg.open(str);
	}

	// "node" and "term" find&parsing
	private String Plan_Found(String str, TreeItem item) {
		TreeItem parent_tree = null;
		String r_str = "";
		int idx = 0;
		int depth = -1;

		idx = treeObject.indexOf(item);
		if (idx < 0) {
			while (true) {
				item = item.getParentItem();
				if (item == null)
					break;
				parent_tree = item;
				depth++;
			}
			idx = treeObject.indexOf(parent_tree);
		}

		StringTokenizer st = new StringTokenizer(str, "\n");
		r_str = Plan_Found_Detail(st, "", idx, depth, 0, 0, false);

		return r_str;
	}

	private String Plan_Found_Detail(StringTokenizer st, String token, int idx,
			int depth, int space, int f_space, boolean found) {
		String r_str = "";
		String line = "";

		while (token.length() > 0 || st.hasMoreTokens()) {

			if (token.length() == 0)
				line = st.nextToken();
			else
				line = token;

			int sp = 0;
			while (line.length() > sp && line.substring(sp, sp + 1).equals(" ")) //space check
			{
				sp++;
			}
			String blank = line.substring(0, sp);
			if (found) {
				if (token.length() == 0 && f_space >= sp) {
					found = false;
					r_str = r_str + line + "\n";
				}
				token = "";
				continue;
			}

			if (!found
					&& ht_detail_view.get(idx + " " + depth + " "
							+ line.substring(sp, line.length())) != null) {
				// If token is not exist and key value is over 1 line, add "line" at head
				if (token.length() == 0)
					r_str = r_str
							+ getDetailData(idx, depth, ""
									+ ht_detail_view
											.get(idx
													+ " "
													+ depth
													+ " "
													+ line.substring(sp, line
															.length())), blank,
									line);
				else
					r_str = r_str
							+ getDetailData(idx, depth, ""
									+ ht_detail_view
											.get(idx
													+ " "
													+ depth
													+ " "
													+ line.substring(sp, line
															.length())), blank,
									"");
				found = true;
				f_space = sp;
			} else if (token.length() == 0)
				r_str = r_str + line + "\n";

			token = "";

			if (space > sp) // return prev step
			{
				r_str = r_str + line + "\n";
				break;
			} else if (space < sp) // child tree
			{
				r_str = r_str
						+ Plan_Found_Detail(st, line, idx, depth + 1, sp,
								f_space, found);
			}
		}
		return r_str;
	}

	private String getDetailData(int idx, int depth, String data, String blank,
			String line) {
		String r_str = "";
		int i = 0;

		StringTokenizer st = new StringTokenizer(data, "\n");

		blank = blank + "     ";
		depth++;

		while (st.hasMoreTokens()) {
			String str = st.nextToken();
			i++;

			r_str = r_str + blank + str + "\n";
			if (ht_detail_view.get(idx + " " + depth + " " + str) != null
					&& !ht_detail_view.get(idx + " " + depth + " " + str)
							.equals(str))
				r_str = r_str
						+ getDetailData(idx, depth, ""
								+ ht_detail_view.get(idx + " " + depth + " "
										+ str), blank, "");
		}

		if (i > 1 && line.length() > 0
				&& !(line.length() > 4 && line.substring(0, 5).equals("index")))
			r_str = line + "\n" + r_str;

		return r_str;
	}

	private String class_modify(String str, String word) {
		return str.substring(0, str.indexOf(" "))
				+ word.substring(word.indexOf(" "), word.lastIndexOf("("));
	}

	private String node_modify(String str, String word) {
		String node = "";
		String card = "";
		String page = "";
		Pattern pattern = Pattern.compile(".*:\\s+(.+)\\((\\d+)/(\\d+)\\).*");
		Matcher m = pattern.matcher(word);
		if (m.matches() && m.groupCount() == 3) {
			node = m.group(1);
			card = m.group(2);
			page = m.group(3);
		}
		return str.substring(0, str.indexOf(" ")) + node + " (card=" + card
				+ ",page=" + page + ")";
	}

	private String term_modify(String word) {
		String r_str = "";
		String split[] = new String[2];
		String split2[] = new String[21];
		int k = 0;

		int j = word.indexOf("(sel ");

		split[0] = word.substring(0, j);
		split[1] = word.substring(j, word.length());
		// CommonTool.MsgBox(sShell,"test","(sel = " + j);

		StringTokenizer st = new StringTokenizer(split[1], "() ");
		j = 0;
		while (st.hasMoreTokens()) {
			split2[j++] = st.nextToken();
		}

		for (int i = 0; i < j; i++) {
			if (split2[i].equals("not-join")) {
				k = i;
				break;
			}
		}

		if (k > 0) {
			r_str = split[0] + "(sel=" + split2[1] + ", " + split2[k - 2] + " "
					+ split2[k - 1];
			for (int i = k + 2; i < j - 1; i++) {
				if (split2[i].substring(split2[i].length() - 3,
						split2[i].length()).equals("ble"))
					r_str = r_str + ", " + split2[i];
			}
			r_str = r_str + ")";
		} else {
			if (split2[6].equals("loc"))
				r_str = split[0] + "(sel=" + split2[1] + ", " + split2[2] + " "
						+ split2[3] + ", " + split2[4] + ")";
			else
				r_str = split[0] + "(sel=" + split2[1] + ", " + split2[2] + " "
						+ split2[3] + ", " + split2[4] + ", " + split2[6] + ")";
		}

		return r_str;
	}

	private String cost_modify(String word) {
		String r_str = "";
		String split[] = new String[11];
		int i = 0;

		for (i = 5; i < word.length(); i++) {
			String temp = word.substring(i, i + 1);
			if (temp.equals("(") || temp.equals(")") || temp.equals("/"))
				temp = " ";
			r_str = r_str + temp;
		}

		StringTokenizer st = new StringTokenizer(r_str, " ");
		i = 0;
		while (st.hasMoreTokens()) {
			split[i++] = st.nextToken();
		}

		i = Integer.parseInt(split[1]) + Integer.parseInt(split[5]);
		r_str = "cost : " + i + "=[fixed=" + split[1] + "(cpu=" + split[2]
				+ ", disk=" + split[3] + "), var=" + split[5] + "(cpu="
				+ split[6] + ", disk=" + split[7] + ")]   card=" + split[9];

		return r_str;
	}

	// check selected tree whether Query plan or plan's child.
	private int findQueryPlan(TreeItem tree) {
		int rb = 0;
		TreeItem ptree = tree.getParentItem();

		String str = ptree.getText();
		int len = str.length();

		if (str.equals("Query plan:"))
			rb = 2;
		else if (str.equals("Query stmt:"))
			rb = 1;
		else if (str.substring(len - 1, len).equals(":"))
			rb = 0;
		else
			rb = findQueryPlan(ptree);

		return rb;
	}

	private String GetChildData(TreeItem tree, int blank, int querytype) {
		String str = "";
		String space = "";
		String temp = "";
		int i = 0;

		TreeItem subitem[] = tree.getItems();
		if (tree.getText().equals("Query stmt:"))
			querytype = 1;
		for (i = 0; i < blank; i++)
			space = space + "     ";
		for (i = 0; i < tree.getItemCount(); i++) {
			temp = subitem[i].getText();
			if (querytype == 1)
				temp = String_Cut(temp, blank);
			str = str + "\n" + space + temp;
			if (subitem[i].getItemCount() > 0)
				str = str + GetChildData(subitem[i], blank + 1, querytype);
		}
		return str;
	}

	private String String_Cut(String str, int blank) {
//		 Auto line feed on the basis of space
		int i = 0;
		int start = 0;
		int end = start + 100;
		String temp = "";
		String space = "";

		int len = str.length();

		if (len < end) {
			temp = str;
			return temp;
		}

		for (i = 0; i < blank; i++)
			space = space + "      ";

		while (end < len) {
			int count = 0; // for count number
			for (i = start; i < end; i++) {
				String temp2 = str.substring(i, i + 1);
				if (temp2.equals("'"))
					count++;
			}
			if ((count % 2) != 0) // if Quotation doesn't maked pair, find next and divide.
			{
				for (i = end; i < len; i++)
					if (str.substring(i, i + 1).equals("'"))
						break;
				for (; i < len; i++)
					if (str.substring(i, i + 1).equals(" "))
						break;

			} else {
				for (i = end - 1; i > start; i--) {
					if (str.substring(i, i + 1).equals(" "))
						break;
				}
			}

			if (start == 0)
				temp = temp + str.substring(start, i);
			else
				temp = temp + "\n" + space + str.substring(start, i);

			start = i + 1;
			end = start + 100;

			if (start > len)
				break;
			if (end > len) {
				end = len;
				temp = temp + "\n" + space + str.substring(start, end);
			}
		}

		return temp;
	}

	private void createCmpBtnArea() {
		GridLayout gridLayout1 = new GridLayout();
		gridLayout1.numColumns = 3;
		GridData gridData1 = new GridData();
		gridData1.horizontalAlignment = org.eclipse.swt.layout.GridData.END;
		cmpBtnArea = new Composite(sShell, SWT.NONE);
		cmpBtnArea.setLayout(gridLayout1);
		cmpBtnArea.setLayoutData(gridData1);

		GridData gridData2 = new GridData();
		gridData2.widthHint = 75;
		btnPrev = new Button(cmpBtnArea, SWT.NONE);
		btnPrev.setLayoutData(gridData2);
		btnPrev.setText(Messages.getString("BUTTON.PREV"));
		btnPrev
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (currIndex <= 0)
							return;
						if (currIndex > 0)
							currIndex--;
						setButtonEnable();
						fillTextArea(currIndex);
					}
				});

		GridData gridData3 = new GridData();
		gridData3.widthHint = 75;
		btnNext = new Button(cmpBtnArea, SWT.NONE);
		btnNext.setLayoutData(gridData3);
		btnNext.setText(Messages.getString("QEDIT.NEXT"));
		btnNext
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (currIndex >= size - 1)
							return;
						if (currIndex < size - 1)
							currIndex++;
						setButtonEnable();
						fillTextArea(currIndex);
					}
				});

		GridData gridData4 = new GridData();
		gridData4.widthHint = 75;
		btnClose = new Button(cmpBtnArea, SWT.NONE);
		btnClose.setLayoutData(gridData4);
		btnClose.setText(Messages.getString("BUTTON.CLOSE"));
		btnClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	private void fillTextArea() {
		currIndex = size - 1;
		fillTextArea(currIndex);
	}

	/*
	 * space : Space
	 * next : true - open 1step only  
	 * plan : true - Query plan's node 
	 * hidden : true - Hidden
	 * step : Query Plan List order 
	 * depth : Query Plan's depth.
	 */
	private String fillTreeItem(StringTokenizer st, TreeItem root,
			TreeItem tree, String token, int space, boolean next, boolean plan,
			boolean hidden, int step, String p_str, int depth) {
		String str = null;
		String r_str = "";
		String g_str = "";
		int sp = 0; // Space count.

		while (st.hasMoreTokens() || token.length() > 0) {
			if (r_str.length() > 0)
				str = r_str;
			else if (token.length() == 0)
				str = st.nextToken();
			else
				str = token;

			token = "";
			r_str = "";

			int len = str.length();

			if (str.equals("Join graph segments (f indicates final):")) {
				TreeItem item = new TreeItem(treeObject, SWT.NONE);
				item.setText("Query Plan List :");
				fillTreeItem(st, item, null, "", 0, false, false, true,
						step + 1, "", 0);
				item.setExpanded(true);
			} else if (str.substring(len - 1, len).equals(":")) {
				if (str.equals("Query plan:")) {
					TreeItem item = new TreeItem(root, SWT.NONE);
					item.setText(str);
					fillTreeItem(st, root, item, "", 0, true, true, false,
							step, "", 0);
					item.setExpanded(true);
				} else if (str.equals("Query stmt:")) {
					TreeItem item = new TreeItem(root, SWT.NONE);
					item.setText(str);
					fillTreeItem(st, root, item, "", 0, false, false, false,
							step, "", 0);
				} else
					fillTreeItem(st, root, null, "", 0, false, false, true,
							step, "", 0);
			} else {
				if (hidden)
					continue;

				if (plan) {
					g_str = ChangeNodeTerm(step, str);
				}
				if (str.length() >= 7) {
					if (str.substring(0, 5).equals("class")) {
						str = ChangeNode(step, str);
					} else if (str.substring(0, 5).equals("index")) {
						str = ChangeIndex(str);
					}
				}

				if (plan && p_str.length() > 0) {
					String key = step + " " + depth + " " + p_str;
					if (ht_detail_view.get(key) == null) {
						ht_detail_view.put(key, g_str);
					} else {
						ht_detail_view.put(key, ht_detail_view.get(key) + "\n"
								+ g_str);
					}
				}

				TreeItem subitem = null;
				if (str.length() < 5) {
					subitem = new TreeItem(tree, SWT.NONE);
					subitem.setText(str);
				} else if (!str.substring(0, 5).equals("filtr")
						&& !str.substring(0, 5).equals("sargs")
						&& !str.substring(0, 5).equals("order")
						&& !str.substring(0, 4).equals("edge")) {
					subitem = new TreeItem(tree, SWT.NONE);
					subitem.setText(str);
				}

				if (st.hasMoreTokens()) {
					token = st.nextToken();

					sp = 0;
					while (token.substring(sp, sp + 1).equals(" ")) // space check
					{
						sp++;
					}

					if (sp > 0)
						token = token.substring(sp, token.length());

					if (space > sp) // return prev step.
					{
						if (plan) {
							String key = step + " " + (depth + 1) + " " + str;
							ht_detail_view.put(key, g_str);
						}
						break;
					} else if (space < sp) // bonding child tree
					{
						String key = step + " " + (depth + 1) + " " + str;
						while (ht_detail_view.get(key) != null) // avoid another depth's same name
						{
							str = str + " ";
							key = key + " ";
							subitem.setText(str);
							String key2 = step + " " + depth + " " + p_str;
							ht_detail_view.put(key2, ht_detail_view.get(key2)
									+ " ");
						}

						r_str = fillTreeItem(st, root, subitem, token, sp,
								false, plan, hidden, step, str, depth + 1);
					} else if (plan) {
						String key = step + " " + (depth + 1) + " " + str;
						ht_detail_view.put(key, g_str);
					}
				}
				if (next)
					subitem.setExpanded(true);
			}
		}
		return token;
	}

	private void MakeHashData(String plan) {
		int step = -1;

		StringTokenizer st = new StringTokenizer(plan, "\n");
		while (st.hasMoreTokens()) {
			String str = st.nextToken();

			if (str.equals("Join graph segments (f indicates final):"))
				step++;

			if (str.length() > 3
					&& (str.substring(0, 4).equals("node") || str.substring(0,
							4).equals("term"))) {
				int sp = str.indexOf(":");
				ht_node_term.put(step + str.substring(0, sp), str);
			}
		}
	}

	private String ChangeIndex(String line) {
		String r_str = "";
		StringTokenizer st2 = new StringTokenizer(line, " ");
		while (st2.hasMoreTokens()) {
			String word = st2.nextToken();
			if (word.substring(0, 4).equals("term"))
				break;
			r_str = r_str + word + " ";
		}
		return r_str;
	}

	private String ChangeNode(int idx, String line) {
		String r_str = "";

		StringTokenizer st2 = new StringTokenizer(line, " ");
		while (st2.hasMoreTokens()) {
			String word = st2.nextToken();
			int len = word.length();
			if (len >= 7) // Dont need check if len is less then 7.
			{
				if (word.substring(0, 4).equals("node")) {
					r_str = class_modify(r_str, ht_node_term.get(idx + word)
							.toString());
					continue;
				}
			}
			r_str = r_str + word + " ";
		}
		return r_str;
	}

	private String ChangeNodeTerm(int idx, String line) {
		String r_str = "";
		int i = 0;
		boolean this_change = false;

		if (line.substring(0, 4).equals("cost")) {
			r_str = r_str + cost_modify(line);
		} else {
			StringTokenizer st2 = new StringTokenizer(line, " ");
			while (st2.hasMoreTokens()) {
				String word = st2.nextToken();
				int len = word.length();
				if (len >= 7) // Dont need check if len is less then 7.
				{
					if (word.substring(0, 4).equals("node")) {
						r_str = node_modify(r_str, ht_node_term.get(idx + word)
								.toString());
						this_change = true;
						continue;
					} else if (word.substring(0, 4).equals("term")) {
						String temp = ht_node_term.get(idx + word).toString();
						word = term_modify(temp.substring(word.length() + 2,
								temp.length()));
						if (st2.hasMoreTokens())
							word = word + "\n         ";
						this_change = true;
					} else if (word.substring(0, 5).equals("(term")) {
						String temp = ht_node_term.get(
								idx + word.substring(1, word.length() - 1))
								.toString();
						word = "("
								+ term_modify(temp.substring(word.length() + 2,
										temp.length())) + ")";
						if (st2.hasMoreTokens())
							word = word + "\n         ";
						this_change = true;
					}

				}
				if (i == 0) {
					r_str = r_str + word;
					i++;
				} else
					r_str = r_str + " " + word;
			}
			if (!this_change)
				return line;
		}
		return r_str;
	}

	private void fillTextArea(int index) {
		if (size > 0) {
			sShell.setText(Messages.getString("TITLE.QUERYPLAN1") + "("
					+ (index + 1) + Messages.getString("TITLE.QUERYPLAN2")
					+ ")");
			StructQueryPlan qp = (StructQueryPlan) queryPlans.get(index);

			qp.plan = qp.plan.replaceAll("\r\n", "\n");
			qp.query = qp.query.replaceAll("\r\n", "\n");
			
			// txtQuery.setText(qp.plan);
			txtQuery.setText(qp.query);
			treeObject.removeAll();
			ht_detail_view.clear();
			ht_node_term.clear();
			MakeHashData(qp.plan);
			StringTokenizer st = new StringTokenizer(qp.plan, "\n");
			if (st.hasMoreTokens()) {
				String str = st.nextToken();
				fillTreeItem(st, null, null, str, 0, false, false, false, -1,
						"", 0);
			}
		} else {
			sShell.setText(Messages.getString("TITLE.QUERYPLAN1"));
			txtQuery.setText(Messages.getString("QUERYPLAN.NOINFORMATION"));

			TreeItem item = new TreeItem(treeObject, SWT.NONE);
			item.setText(Messages.getString("QUERYPLAN.CHECKQUERYPLANENABLE"));
		}
	}

	private void setButtonEnable() {
		if (currIndex <= 0)
			btnPrev.setEnabled(false);
		else
			btnPrev.setEnabled(true);

		if (currIndex >= size - 1)
			btnNext.setEnabled(false);
		else
			btnNext.setEnabled(true);
	}
} // @jve:decl-index=0:visual-constraint="12,9"
