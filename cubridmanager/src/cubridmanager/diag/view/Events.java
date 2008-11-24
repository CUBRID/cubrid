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
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.List;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.layout.RowData;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.graphics.Font;

import cubridmanager.Messages;

public class Events extends ViewPart {

	public static final String ID = "workview.Events"; 
	private Composite top = null;
	private Composite composite = null;
	private Composite composite1 = null;
	private Label label = null;
	private List list = null;
	private Canvas canvas = null;
	private List list1 = null;
	private Label label1 = null;
	private Table table = null;
	private Composite composite2 = null;
	private Label label2 = null;
	private Group group = null;

	public Events() {
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
		createComposite2();
		createComposite();
		top.setLayout(rowLayout);
		createComposite1();
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
		rowData.height = 250;
		rowData.width = 550;
		composite = new Composite(top, SWT.NONE);
		composite.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite.setLayoutData(rowData);
		label = new Label(composite, SWT.NONE);
		label
				.setBounds(new org.eclipse.swt.graphics.Rectangle(14, 12, 124,
						20));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setText("logging status monitor");
		list = new List(composite, SWT.BORDER);
		list.setBounds(new org.eclipse.swt.graphics.Rectangle(17, 36, 90, 137));
		createCanvas();
	}

	/**
	 * This method initializes composite1
	 * 
	 */
	private void createComposite1() {
		RowData rowData1 = new org.eclipse.swt.layout.RowData();
		rowData1.height = 250;
		rowData1.width = 550;
		composite1 = new Composite(top, SWT.NONE);
		composite1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite1.setLayoutData(rowData1);
		list1 = new List(composite1, SWT.BORDER);
		list1
				.setBounds(new org.eclipse.swt.graphics.Rectangle(16, 39, 89,
						156));
		label1 = new Label(composite1, SWT.NONE);
		label1
				.setBounds(new org.eclipse.swt.graphics.Rectangle(17, 9, 214,
						24));
		label1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label1.setText("logging activity tracking");
		createTable();
	}

	/**
	 * This method initializes canvas
	 * 
	 */
	private void createCanvas() {
		canvas = new Canvas(composite, SWT.BORDER);
		canvas.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		canvas.setBounds(new org.eclipse.swt.graphics.Rectangle(150, 34, 358,
				180));
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		table = new Table(composite1, SWT.BORDER);
		table.setHeaderVisible(true);
		table.setLinesVisible(true);
		table.setBounds(new org.eclipse.swt.graphics.Rectangle(149, 39, 354,
				195));
	}

	/**
	 * This method initializes composite2
	 * 
	 */
	private void createComposite2() {
		RowData rowData2 = new org.eclipse.swt.layout.RowData();
		rowData2.height = 50;
		rowData2.width = 550;
		composite2 = new Composite(top, SWT.NONE);
		composite2.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite2.setLayoutData(rowData2);
		label2 = new Label(composite2, SWT.NONE);
		label2
				.setBounds(new org.eclipse.swt.graphics.Rectangle(10, 7, 414,
						35));
		label2.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 18,
				SWT.NORMAL));
		label2.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label2.setText(Messages.getString("TREE.OBJECT_STATUS_ACTIVITY"));
		createGroup();
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(composite2, SWT.NONE);
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(3, 49, 548, 2));
	}

}
