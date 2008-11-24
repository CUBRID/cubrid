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

import java.util.ArrayList;
import java.util.Collections;

import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.Messages;
import cubridmanager.query.StructQueryExample;

import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.custom.CTabItem;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.custom.CTabFolder;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Table;

public class QuerySampleDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="45,6"
	private Group group = null;
	private Button btnClose = null;
	private SashForm sashForm = null;
	private CTabFolder tabQueryAndFunction = null;
	private Table tblQuery = null;
	private Table tblFunction = null;
	private CTabFolder tabExample = null;
	private Text txtExample = null;
	private static boolean isAlreadyOpened = false;
	private static ArrayList query = new ArrayList();
	private static ArrayList function = new ArrayList();

	public QuerySampleDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public QuerySampleDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public void doModal() {
		try {
			if (isAlreadyOpened)
				return;
			else
				isAlreadyOpened = true;

			makeQuery();
			makeFunction();

			createSShell();

			sShell.pack();
			CommonTool.centerShell(sShell);
			sShell.open();
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		// sShell = new Shell(getParent());
		sShell = new Shell();
		sShell.setLayout(new GridLayout());
		sShell.setText(Messages.getString("TITLE.QUERYSAMPLE"));
		sShell.setImage(new Image(null, cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/sample.png").getImageData()));
		sShell.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				isAlreadyOpened = false;
			}
		});
		createGroup();
		GridData gridBtn = new GridData(GridData.HORIZONTAL_ALIGN_END);
		gridBtn.widthHint = 75;
		btnClose = new Button(sShell, SWT.NONE);
		btnClose.setText(Messages.getString("BUTTON.CLOSE"));
		btnClose.setLayoutData(gridBtn);
		btnClose
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		GridData gridGroup = new GridData(GridData.FILL_BOTH);
		gridGroup.widthHint = 780;
		gridGroup.heightHint = 550;
		group = new Group(sShell, SWT.NONE);
		group.setLayout(new FillLayout());
		createSashForm();
		group.setLayoutData(gridGroup);
	}

	/**
	 * This method initializes sashForm
	 * 
	 */
	private void createSashForm() {
		sashForm = new SashForm(group, SWT.NONE);
		createTabQueryAndFunction();
		createTabExample();
		sashForm.setWeights(new int[] { 30, 70 });
	}

	/**
	 * This method initializes tabExample
	 * 
	 */
	private void createTabQueryAndFunction() {
		tabQueryAndFunction = new CTabFolder(sashForm, SWT.NONE);
		createTblExample();
		CTabItem tabItemExample = new CTabItem(tabQueryAndFunction, SWT.NONE);
		tabItemExample.setControl(tblQuery);
		tabItemExample.setText(Messages.getString("TAB.QUERY"));

		createTblFunction();
		CTabItem tabItemFunction = new CTabItem(tabQueryAndFunction, SWT.NONE);
		tabItemFunction.setControl(tblFunction);
		tabItemFunction.setText(Messages.getString("TAB.FUNCTION"));

		tabQueryAndFunction.setSelection(0);
		if (tabQueryAndFunction.getSelectionIndex() == 0)
			tblQuery.getColumn(0).pack();
		else
			tblFunction.getColumn(0).pack();

		tabQueryAndFunction
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (tabQueryAndFunction.getSelectionIndex() == 0)
							tblQuery.getColumn(0).pack();
						else
							tblFunction.getColumn(0).pack();
					}
				});
	}

	/**
	 * This method initializes tblExample
	 * 
	 */
	private void createTblExample() {
		tblQuery = new Table(tabQueryAndFunction, SWT.FULL_SELECTION);

		TableColumn col = new TableColumn(tblQuery, SWT.NONE);
		col.setAlignment(SWT.LEFT);
		tblQuery
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setTxtExample(((TableItem) e.item).getData());
					}
				});

		TableItem item;
		StructQueryExample example;
		for (int i = 0, n = query.size(); i < n; i++) {
			example = (StructQueryExample) query.get(i);
			item = new TableItem(tblQuery, SWT.NONE);
			item.setText(0, example.id);
			item.setData(example.refNum);
		}
	}

	/**
	 * This method initializes tblFunction
	 * 
	 */
	private void createTblFunction() {
		tblFunction = new Table(tabQueryAndFunction, SWT.FULL_SELECTION);

		TableColumn col = new TableColumn(tblFunction, SWT.NONE);
		col.setAlignment(SWT.LEFT);
		tblFunction
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						setTxtExample(((TableItem) e.item).getData());
					}
				});

		TableItem item;
		StructQueryExample example;
		for (int i = 0, n = function.size(); i < n; i++) {
			example = (StructQueryExample) function.get(i);
			item = new TableItem(tblFunction, SWT.NONE);
			item.setText(0, example.id);
			item.setData(example.refNum);
		}
	}

	/**
	 * This method initializes tabexample
	 * 
	 */
	private void createTabExample() {
		tabExample = new CTabFolder(sashForm, SWT.NONE);
		txtExample = new Text(tabExample, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.BORDER);
		txtExample.setEditable(false);
		txtExample.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		CTabItem cTabItem = new CTabItem(tabExample, SWT.NONE);
		cTabItem.setControl(txtExample);
		cTabItem.setText(Messages.getString("TAB.EXAMPLE"));
		tabExample.setSelection(0);
	}

	private void makeQuery() {
		if (query.size() > 0)
			return;

		query.add(new StructQueryExample("+ , || , * , -", new int[] { 39, 300 }));
		query.add(new StructQueryExample("all", null));
		query.add(new StructQueryExample("distinct / unique", new int[] { 87, 129, 302 }));
		query.add(new StructQueryExample("alter", new int[] { 201, 7, 10, 11, 12, }));
		query.add(new StructQueryExample("alter class(=table) - add", new int[] { 7}));
		query.add(new StructQueryExample("alter class(=table) - change", new int[] { 11, 70}));
		query.add(new StructQueryExample("alter class(=table) - drop", new int[] { 10}));
		query.add(new StructQueryExample("alter class(=table) - rename", new int[] { 12 }));
		query.add(new StructQueryExample("asc", new int[] { 43, 303 }));
		query.add(new StructQueryExample("between - and", new int[] { 89, 90, 304 }));
		query.add(new StructQueryExample("call", new int[] { 202, 60 }));
		query.add(new StructQueryExample("case", new int[] { 135, 136 }));
		query.add(new StructQueryExample("class attribute", null));
		query.add(new StructQueryExample("create", new int[] { 203 }));
		query.add(new StructQueryExample("create class(=table)", new int[] { 203, 0, 2, 51, 50 }));
		query.add(new StructQueryExample("cycle", new int[] { 20 }));
		query.add(new StructQueryExample("db_user", new int[] { 59, 60, 61, 62, 63, 64 }));
		query.add(new StructQueryExample("default", new int[] { 2 }));
		query.add(new StructQueryExample("delete from", new int[] { 204, 35, 37 }));
		query.add(new StructQueryExample("derived tables", null));
		query.add(new StructQueryExample("desc", new int[] { 88, 303 }));
		query.add(new StructQueryExample("drop", new int[] { 205, 6}));
		query.add(new StructQueryExample("except", null));
		query.add(new StructQueryExample("exists", new int[] {307 }));
		query.add(new StructQueryExample("file", null));
		query.add(new StructQueryExample("grant", new int[] { 206, 54, 55, 56 }));
		query.add(new StructQueryExample("group by", new int[] { 79, 80, 86, 142, 308 }));
		query.add(new StructQueryExample("having", new int[] { 86, 142, 309 }));
		query.add(new StructQueryExample("in", new int[] { 37, 92, 93, 95, 310 }));
		query.add(new StructQueryExample("index", new int[] { 207, 16, 17 }));
		query.add(new StructQueryExample("inherit", null));
		query.add(new StructQueryExample("insert into", new int[] { 208, 23, 24, 27 }));
		query.add(new StructQueryExample("into", new int[] { 81, 311 }));
		query.add(new StructQueryExample("like", new int[] { 44, 312 }));
		query.add(new StructQueryExample("maxvalue", new int[] { 20 }));
		query.add(new StructQueryExample("method", null));
		query.add(new StructQueryExample("next_value(=nextval)", new int[] { 41, 42, 313 }));
		query.add(new StructQueryExample("not", new int[] { 90, 304 }));
		query.add(new StructQueryExample("not null", new int[] { 95, 314 }));
		query.add(new StructQueryExample("on", new int[] { 54, 55, 56, 57 }));
		query.add(new StructQueryExample("only", null));
		query.add(new StructQueryExample("order by", new int[] { 43, 80, 87, 88, 141, 165, 166, 302 }));
		query.add(new StructQueryExample("outer join", new int[] { 159, 160, 315 }));
		query.add(new StructQueryExample("path expression", null));
		query.add(new StructQueryExample("rename", new int[] { 210, 15, 77 }));
		query.add(new StructQueryExample("revoke", new int[] { 211, 57, 58 }));
		query.add(new StructQueryExample("select", new int[] { 212, 42, 43, 44, 45, 81, 83}));
		query.add(new StructQueryExample("sequence", new int[] { 4 }));
		query.add(new StructQueryExample("serial", new int[] { 20, 21, 22, 41, 42 }));
		query.add(new StructQueryExample("set", new int[] { 4, 39}));
		query.add(new StructQueryExample("some / any", new int[] { 96, 97, 318 }));
		query.add(new StructQueryExample("start with", new int[] { 20 }));
		query.add(new StructQueryExample("to", new int[] { 54, 55, 56 }));
		query.add(new StructQueryExample("trigger", new int[] { 213, 144, 145, 146, 147, 149}));
		query.add(new StructQueryExample("trigger - deffered", new int[] { 157, 158 }));
		query.add(new StructQueryExample("trigger - rename, alter, drop", new int[] { 153, 154, 155, 156 }));
		query.add(new StructQueryExample("trigger - set trigger", new int[] { 151, 152 }));
		query.add(new StructQueryExample("under", null));
		query.add(new StructQueryExample("union / intersection / difference", new int[] { 83, 319 }));
		query.add(new StructQueryExample("unique", new int[] { 18, 51 }));
		query.add(new StructQueryExample("update", new int[] { 214, 28, 39, 40 }));
		query.add(new StructQueryExample("update class", null));
		query.add(new StructQueryExample("using index", new int[] { 45, 46, 47, 48 }));
		query.add(new StructQueryExample("values", new int[] { 23, 24, 27 }));
		query.add(new StructQueryExample("view(=virtual class)", new int[] { 209}));
		Collections.sort(query);
	}

	private void makeFunction() {
		if (function.size() > 0)
			return;

		function.add(new StructQueryExample("ADD_MEMBER", new int[] { 64, 65 }));
		function.add(new StructQueryExample("ADD_MONTHS", new int[] { 122, 400 }));
		function.add(new StructQueryExample("ADD_USER", new int[] { 60, 63, 64 }));
		function.add(new StructQueryExample("AVG", new int[] { 79, 142, 308, 309 }));
		function.add(new StructQueryExample("BIT_LENGTH", new int[] { 105, 401 }));
		function.add(new StructQueryExample("CAST", new int[] { 98, 402 }));
		function.add(new StructQueryExample("CHAR_LENGTH", new int[] { 43, 109, 401 }));
		function.add(new StructQueryExample("CHR", new int[] { 162, 403 }));
		function.add(new StructQueryExample("COALESCE", new int[] { 163, 404 }));
		function.add(new StructQueryExample("COUNT", new int[] { 129, 309 }));
		function.add(new StructQueryExample("DECODE", new int[] { 164, 405 }));
		function.add(new StructQueryExample("DROP_MEMBER", new int[] { 66 }));
		function.add(new StructQueryExample("DROP_USER", new int[] { 61 }));
		function.add(new StructQueryExample("EXTRACT", new int[] { 104, 406 }));
		function.add(new StructQueryExample("FIND_USER", new int[] { 62, 67 }));
		function.add(new StructQueryExample("GROUPBY_NUM", new int[] { 142, 143, 407 }));
		function.add(new StructQueryExample("INST_NUM", new int[] { 140, 143, 408 }));
		function.add(new StructQueryExample("LAST_DAY", new int[] { 123, 409 }));
		function.add(new StructQueryExample("LENGTH", new int[] { 165, 401 }));
		function.add(new StructQueryExample("LENGTHB", new int[] { 166, 401 }));
		function.add(new StructQueryExample("LOGIN", new int[] { 59 }));
		function.add(new StructQueryExample("LOWER", new int[] { 110, 410 }));
		function.add(new StructQueryExample("LPAD", new int[] { 115, 116, 411 }));
		function.add(new StructQueryExample("LTRIM", new int[] { 113, 412 }));
		function.add(new StructQueryExample("MAX", new int[] { 130, 309 }));
		function.add(new StructQueryExample("MIN", new int[] { 131, 309 }));
		function.add(new StructQueryExample("MOD", new int[] { 128, 139, 413 }));
		function.add(new StructQueryExample("MONTHS_BETWEEN", new int[] { 124, 406 }));
		function.add(new StructQueryExample("NULLIF", new int[] { 167, 404 }));
		function.add(new StructQueryExample("NVL", new int[] { 168, 404 }));
		function.add(new StructQueryExample("NVL2", new int[] { 169, 404 }));
		function.add(new StructQueryExample("OCTET_LENGTH", new int[] { 106, 401 }));
		function.add(new StructQueryExample("ORDERBY_NUM", new int[] { 141, 414 }));
		function.add(new StructQueryExample("POSITION", new int[] { 107, 108, 415 }));
		function.add(new StructQueryExample("POWER", new int[] { 174, 413 }));
		function.add(new StructQueryExample("RAND", new int[] { 170, 413 }));
		function.add(new StructQueryExample("REPLACE", new int[] { 119, 415 }));
		function.add(new StructQueryExample("ROWNUM", new int[] { 137, 138, 139, 416 }));
		function.add(new StructQueryExample("RPAD", new int[] { 117, 118, 411 }));
		function.add(new StructQueryExample("RTRIM", new int[] { 114, 412 }));
		function.add(new StructQueryExample("SET_PASSWORD", new int[] { 67 }));
		function.add(new StructQueryExample("STDDEV", new int[] { 134, 309 }));
		function.add(new StructQueryExample("SUBSTR", new int[] { 171, 419 }));
		function.add(new StructQueryExample("SUBSTRB", new int[] { 172, 419 }));
		function.add(new StructQueryExample("SUBSTRING", new int[] { 108, 419 }));
		function.add(new StructQueryExample("SUM", new int[] { 132, 309 }));
		function.add(new StructQueryExample("SYS_DATE(=SYSDATE)", new int[] { 125, 417 }));
		function.add(new StructQueryExample("SYS_TIME(=SYSTIME)", new int[] { 126, 417 }));
		function.add(new StructQueryExample("SYS_TIMESTAMP(=SYSTIMESTAMP)", new int[] { 127, 417 }));
		function.add(new StructQueryExample("TO_CHAR", new int[] { 99, 402 }));
		function.add(new StructQueryExample("TO_DATE", new int[] { 100, 402 }));
		function.add(new StructQueryExample("TO_NUMBER", new int[] { 103, 402 }));
		function.add(new StructQueryExample("TO_TIME", new int[] { 101, 402 }));
		function.add(new StructQueryExample("TO_TIMESTAMP", new int[] { 102, 402 }));
		function.add(new StructQueryExample("TRANSLATE", new int[] { 120, 415 }));
		function.add(new StructQueryExample("TRIM", new int[] { 111, 112, 412 }));
		function.add(new StructQueryExample("TRUNC", new int[] { 173, 418 }));
		function.add(new StructQueryExample("UPPER", new int[] { 121, 410 }));
		function.add(new StructQueryExample("VARIANCE", new int[] { 133, 309 }));
		function.add(new StructQueryExample("ABS", new int[] { 418 }));
		function.add(new StructQueryExample("FLOOR", new int[] { 418 }));
		function.add(new StructQueryExample("ROUND", new int[] { 418 }));
		Collections.sort(function);
	}

	private void setTxtExample(Object ref) {
		txtExample.setText("");
		if (!(ref instanceof int[]))
			return;

		int[] refNum = (int[]) ref;
		StringBuffer example = new StringBuffer();
		for (int i = 0, n = refNum.length; i < n; i++) {
			example.append(Messages.getString("EXAMPLE." + refNum[i]));
			example.append(MainConstants.NEW_LINE);
			example.append(MainConstants.NEW_LINE);
		}
		txtExample.append(example.toString().replaceAll("\n",
				MainConstants.NEW_LINE));
		txtExample.setSelection(0);
	}
}
