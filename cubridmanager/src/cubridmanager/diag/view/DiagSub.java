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

package cubridmanager.diag.view;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.layout.RowData;
import org.eclipse.swt.layout.RowLayout;

public class DiagSub extends ViewPart {

	public static final String ID = "workview.DiagSub";
	private Composite composite = null;
	private Label label = null;
	private Label label1 = null;
	private Label label3 = null;
	private Label label2 = null;
	private Tree tree = null;
	private Text text = null;
	private Text text1 = null;
	private Table table = null;

	public DiagSub() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		RowLayout rowLayout = new RowLayout();
		rowLayout.type = org.eclipse.swt.SWT.VERTICAL;
		rowLayout.wrap = false;
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		createComposite1();
		createComposite();
		top.setLayout(rowLayout);
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		RowData rowData = new org.eclipse.swt.layout.RowData();
		rowData.height = 300;
		rowData.width = 550;
		composite = new Composite(top, SWT.NONE);
		composite.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite.setLayoutData(rowData);
		label = new Label(composite, SWT.NONE);
		label
				.setBounds(new org.eclipse.swt.graphics.Rectangle(22, 32, 147,
						12));
		label.setText("Progessing diag's list");
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label1 = new Label(composite, SWT.NONE);
		label1.setBounds(new org.eclipse.swt.graphics.Rectangle(267, 205, 80,
				12));
		label1.setText("\uc9c4\ub2e8 \uc2dc\uc791 \uc2dc\uac04");
		label1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label3 = new Label(composite, SWT.NONE);
		label3.setBounds(new org.eclipse.swt.graphics.Rectangle(267, 239, 80,
				12));
		label3.setText("\uc885\ub8cc \uc608\uc815 \uc2dc\uac04");
		label3.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label2 = new Label(composite, SWT.NONE);
		label2
				.setBounds(new org.eclipse.swt.graphics.Rectangle(272, 51, 52,
						12));
		label2.setText("\uc9c4\ub2e8 \ud56d\ubaa9");
		label2.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		createTree();
		text = new Text(composite, SWT.BORDER);
		text
				.setBounds(new org.eclipse.swt.graphics.Rectangle(370, 204,
						149, 18));
		text1 = new Text(composite, SWT.BORDER);
		text1.setBounds(new org.eclipse.swt.graphics.Rectangle(370, 238, 149,
				18));
		createTable();
	}

	/**
	 * This method initializes tree
	 * 
	 */
	private void createTree() {
		tree = new Tree(composite, SWT.BORDER);
		tree
				.setBounds(new org.eclipse.swt.graphics.Rectangle(369, 47, 149,
						147));
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(composite, SWT.NONE);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table
				.setBounds(new org.eclipse.swt.graphics.Rectangle(22, 55, 158,
						200));
	}

	private Composite top = null;

	private Composite composite1 = null;

	private Label label4 = null;

	private Group group = null;

	/**
	 * This method initializes composite1
	 * 
	 */
	private void createComposite1() {
		RowData rowData1 = new org.eclipse.swt.layout.RowData();
		rowData1.height = 50;
		rowData1.width = 550;
		composite1 = new Composite(top, SWT.NONE);
		composite1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite1.setLayoutData(rowData1);
		label4 = new Label(composite1, SWT.NONE);
		label4
				.setBounds(new org.eclipse.swt.graphics.Rectangle(16, 9, 173,
						32));
		label4.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label4.setText("\uc9c4\ub2e8");
		label4.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		createGroup();
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(composite1, SWT.NONE);
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 44, 533, 4));
	}
}
