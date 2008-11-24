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

import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Display;

import cubridmanager.Messages;

public class CompareResult {

	private Shell shlResult = null; // @jve:decl-index=0:visual-constraint="10,10"
	private Table tblLeft = null;
	private Table tblRight = null;
	private SashForm splitResult = null;
	private Composite cmpLeft = null;
	private Composite cmpRight = null;
	private Shell shlQuery = null; // @jve:decl-index=0:visual-constraint="445,14"
	private SashForm splitQuery = null;
	private Composite cmpQueryLeft = null;
	private Composite cmpQueryRight = null;
	private Text txaLeft = null;
	private Text txaRight = null;
	private Button btnCloseQuery = null;
	private Button btnCloseResult = null;
	private Button btnShowQuery = null;
	private Combo cmbLeft = null;
	private Combo cmbRight = null;
	private Label lblLeft = null;
	private Label lblRight = null;

	public CompareResult(Table left, Table right) {
		// tblLeft = left;
		// tblRight = right;
		createshlResult();
		shlResult.open();
	}

	/**
	 * This method initializes shlResult
	 */
	private void createshlResult() {
		shlResult = new Shell();
		shlResult.setText(Messages.getString("QEDIT.COMPARERESULTS"));
		shlResult.setBounds(new org.eclipse.swt.graphics.Rectangle(22, 22, 600,
				450));
		createSplitResult();
		shlResult
				.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
					public void controlResized(
							org.eclipse.swt.events.ControlEvent e) {
						// TODO Auto-generated Event stub controlResized()
						adjustResultWindows();
					}
				});
		btnCloseResult = new Button(shlResult, SWT.NONE);
		btnCloseResult.setBounds(new org.eclipse.swt.graphics.Rectangle(341,
				294, 75, 22));
		btnCloseResult.setText(Messages.getString("QEDIT.CLOSE"));
		btnCloseResult
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// TODO Auto-generated Event stub widgetSelected()
						shlResult.close();
						shlResult = null;
					}
				});
		btnShowQuery = new Button(shlResult, SWT.NONE);
		btnShowQuery.setBounds(new org.eclipse.swt.graphics.Rectangle(258, 294,
				75, 22));
		btnShowQuery.setText(Messages.getString("QEDIT.SHOWQUERY"));
		btnShowQuery
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// TODO Auto-generated Event stub widgetSelected()
						createShlQuery();
						shlQuery.open();
					}
				});
	}

	/**
	 * This method initializes splitResult
	 * 
	 */
	private void createSplitResult() {
		splitResult = new SashForm(shlResult, SWT.NONE);
		splitResult.SASH_WIDTH = 6;
		createCmpLeft();
		createCmpRight();
		splitResult.setBounds(new org.eclipse.swt.graphics.Rectangle(1, 2, 418,
				285));
	}

	/**
	 * This method initializes cmpLeft
	 * 
	 */
	private void createCmpLeft() {
		cmpLeft = new Composite(splitResult, SWT.NONE);
		createTable();
		createCmbLeft();
		cmpLeft.addPaintListener(new org.eclipse.swt.events.PaintListener() {
			public void paintControl(org.eclipse.swt.events.PaintEvent e) {
				// TODO Auto-generated Event stub paintControl()
				adjustSplitResult();
			}
		});
	}

	/**
	 * This method initializes cmpRight
	 * 
	 */
	private void createCmpRight() {
		cmpRight = new Composite(splitResult, SWT.NONE);
		createTblRight();
		createCmbRight();
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		tblLeft = new Table(cmpLeft, SWT.NONE);
		tblLeft.setHeaderVisible(true);
		tblLeft.setLinesVisible(true);
		tblLeft.setLocation(new org.eclipse.swt.graphics.Point(0, 25));
		tblLeft.setSize(new org.eclipse.swt.graphics.Point(204, 259));
	}

	/**
	 * This method initializes tblRight1
	 * 
	 */
	private void createTblRight() {
		tblRight = new Table(cmpRight, SWT.NONE);
		tblRight.setHeaderVisible(true);
		tblRight.setLinesVisible(true);
		tblRight.setLocation(new org.eclipse.swt.graphics.Point(0, 25));
		tblRight.setSize(new org.eclipse.swt.graphics.Point(204, 260));
	}

	/**
	 * This method initializes shlQuery
	 * 
	 */
	private void createShlQuery() {
		shlQuery = new Shell(SWT.APPLICATION_MODAL | SWT.SHELL_TRIM);
		shlQuery.setText(Messages.getString("QEDIT.COMPARERESULTS") + " "
				+ Messages.getString("QEDIT.SQL"));
		shlQuery.setBounds(new org.eclipse.swt.graphics.Rectangle(66, 66, 600,
				450));
		createSplitQuery();
		shlQuery
				.addControlListener(new org.eclipse.swt.events.ControlAdapter() {
					public void controlResized(
							org.eclipse.swt.events.ControlEvent e) {
						adjustQueryWindows(); // TODO Auto-generated Event
												// stub controlResized()
					}
				});
		btnCloseQuery = new Button(shlQuery, SWT.NONE);
		btnCloseQuery.setBounds(new org.eclipse.swt.graphics.Rectangle(343,
				289, 75, 22));
		btnCloseQuery.setText(Messages.getString("QEDIT.CLOSE"));
		btnCloseQuery
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						// TODO Auto-generated Event stub widgetSelected()
						shlQuery.close();
						shlQuery = null;
					}
				});
	}

	/**
	 * This method initializes splitQuery
	 * 
	 */
	private void createSplitQuery() {
		splitQuery = new SashForm(shlQuery, SWT.NONE);
		splitQuery.SASH_WIDTH = 6;
		createCmpQueryLeft();
		createCmpQueryRight();
		splitQuery.setBounds(new org.eclipse.swt.graphics.Rectangle(1, 3, 417,
				278));
	}

	/**
	 * This method initializes cmpQueryLeft
	 * 
	 */
	private void createCmpQueryLeft() {
		cmpQueryLeft = new Composite(splitQuery, SWT.NONE);
		cmpQueryLeft
				.addPaintListener(new org.eclipse.swt.events.PaintListener() {
					public void paintControl(org.eclipse.swt.events.PaintEvent e) {
						// TODO Auto-generated Event stub paintControl()
						adjustSplitQuery();
					}
				});
		txaLeft = new Text(cmpQueryLeft, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		txaLeft.setBounds(new org.eclipse.swt.graphics.Rectangle(5, 25, 199,
				247));
		txaLeft.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		lblLeft = new Label(cmpQueryLeft, SWT.NONE);
		lblLeft
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 5, 100, 15));
		lblLeft.setText(Messages.getString("QEDIT.RESULT") + 1);
	}

	/**
	 * This method initializes cmpQueryRight
	 * 
	 */
	private void createCmpQueryRight() {
		cmpQueryRight = new Composite(splitQuery, SWT.NONE);
		txaRight = new Text(cmpQueryRight, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		txaRight.setBounds(new org.eclipse.swt.graphics.Rectangle(2, 25, 204,
				245));
		txaRight.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		lblRight = new Label(cmpQueryRight, SWT.NONE);
		lblRight
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 5, 100, 15));
		lblRight.setText(Messages.getString("QEDIT.RESULT") + 2);
	}

	/**
	 * This method initializes cmbLeft
	 * 
	 */
	private void createCmbLeft() {
		cmbLeft = new Combo(cmpLeft, SWT.NONE);
		cmbLeft.setText(Messages.getString("QEDIT.RESULT") + 1);
		cmbLeft
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 0, 100, 20));
	}

	/**
	 * This method initializes cmbRight
	 * 
	 */
	private void createCmbRight() {
		cmbRight = new Combo(cmpRight, SWT.NONE);
		cmbRight.setText(Messages.getString("QEDIT.RESULT") + 2);
		cmbRight
				.setBounds(new org.eclipse.swt.graphics.Rectangle(0, 0, 100, 20));
	}

	private void adjustResultWindows() {
		Rectangle rctResult = shlResult.getClientArea();
		splitResult.setBounds(rctResult.x, rctResult.y, rctResult.width,
				rctResult.height - 35);

		btnShowQuery.setLocation(rctResult.width - 160, rctResult.height - 30);
		btnCloseResult.setLocation(rctResult.width - 85, rctResult.height - 30);
	}

	private void adjustSplitResult() {
		Point pntCmpLeft = cmpLeft.getSize();
		Point pntCmpRight = cmpRight.getSize();

		tblLeft.setBounds(0, 25, pntCmpLeft.x, pntCmpLeft.y - 25);
		tblRight.setBounds(0, 25, pntCmpRight.x, pntCmpRight.y - 25);
	}

	private void adjustQueryWindows() {
		Rectangle rctQuery = shlQuery.getClientArea();
		splitQuery.setBounds(rctQuery.x, rctQuery.y, rctQuery.width,
				rctQuery.height - 35);

		btnCloseQuery.setLocation(rctQuery.width - 85, rctQuery.height - 30);
	}

	private void adjustSplitQuery() {
		Point pntCmpLeft = cmpQueryLeft.getSize();
		Point pntCmpRight = cmpQueryRight.getSize();

		txaLeft.setBounds(0, 25, pntCmpLeft.x, pntCmpLeft.y - 25);
		txaRight.setBounds(0, 25, pntCmpRight.x, pntCmpRight.y - 25);
	}
}
