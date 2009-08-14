/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */
package com.cubrid.cubridmanager.ui.query.control;

import java.io.File;
import java.io.IOException;

import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CTabFolder;
import org.eclipse.swt.custom.CTabItem;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.custom.ViewForm;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.ToolBar;

import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.StructQueryPlan;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;

/**
 * A composite to show the query area and result in same view.
 * 
 * @author wangsl 2009-3-11
 */
public class CombinedQueryComposite extends
		Composite {

	private Composite topComp;
	private SashForm sashTop;
	private Composite cmpHead;

	private CTabFolder tabResultFolderMiddle;
	private CTabItem tabItemLogResult = null;
	private SashForm sashBottom;
	private CTabItem tabItemResult;
	private TabFolder tabFolderMiddle = null;

	private Composite cmpMiddleResultFolder = null;
	private CombinedQueryPlanComposite cmpMiddlePlanFolder = null;

	private Table tblResult;
	private QuerySourceViewer querySourceView;
	private QueryEditorPart editor;
	private StyledText logSqlText;
	private StyledText logMessagesArea;
	private Font font;

	public CombinedQueryComposite(Composite parent, int style,
			QueryEditorPart queryEditorPart) {
		super(parent, style);
		this.editor = queryEditorPart;
		setLayout(new FillLayout());
		topComp = new Composite(this, SWT.NONE);
		topComp.setLayout(new FillLayout());
		String[] fontData = QueryOptions.getFont(editor.getSelectedServer() != null ? editor.getSelectedServer().getServerInfo()
				: null);
		font = SWTResourceManager.getFont(fontData[0],
				Integer.valueOf(fontData[1]), Integer.valueOf(fontData[2]));
		createSashTop();
	}

	public void makeEmptyResult() {
		tabFolderMiddle.setSelection(0);
		SashForm bottomSash = new SashForm(tabResultFolderMiddle, SWT.VERTICAL);
		bottomSash.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		Table tbl = new Table(bottomSash, SWT.H_SCROLL | SWT.V_SCROLL
				| SWT.FULL_SELECTION | SWT.MULTI);
		setDropTraget(tbl);
		tbl.setHeaderVisible(true);
		tbl.setLinesVisible(true);

		TableColumn column = new TableColumn(tbl, SWT.NONE);
		column.setWidth(60);

		SashForm tailSash = new SashForm(bottomSash, SWT.NONE);
		tailSash.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		StyledText sqlText = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		sqlText.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		new StyledText(tailSash, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		bottomSash.setWeights(new int[] { 70, 30 });
		CTabItem tab = new CTabItem(tabResultFolderMiddle, SWT.NONE);
		tab.setText(Messages.QEDIT_RESULT);
		tab.setControl(bottomSash);
	}

	public void makeLogResult(String sqlStr, String messageStr) {
		tabFolderMiddle.setSelection(0);
		SashForm tailSash = new SashForm(tabResultFolderMiddle, SWT.NONE);
		tailSash.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		logSqlText = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		CommonTool.registerContextMenu(logSqlText, false);
		logSqlText.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		logSqlText.setText(sqlStr);
		logSqlText.setTopIndex(logSqlText.getLineCount() - 1);
		logMessagesArea = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		CommonTool.registerContextMenu(logMessagesArea, false);
		logMessagesArea.setText(messageStr);
		logMessagesArea.setTopIndex(logMessagesArea.getLineCount() - 1);
		tabItemLogResult = new CTabItem(tabResultFolderMiddle, SWT.NONE);
		tabItemLogResult.setText(Messages.QEDIT_LOGSRESULT);
		tabItemLogResult.setControl(tailSash);
	}

	public void makeResult(QueryExecuter result) {
		sashTop.setWeights(new int[] { 3, 7 });
		if (tabResultFolderMiddle == null || tabResultFolderMiddle.isDisposed()) {
			return;
		}
		tabFolderMiddle.setSelection(0);
		ViewForm viewForm = new ViewForm(tabResultFolderMiddle, SWT.NONE);
		SashForm bottomSash = new SashForm(viewForm, SWT.VERTICAL);
		bottomSash.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		Table tbl = new Table(bottomSash, SWT.H_SCROLL | SWT.V_SCROLL
				| SWT.FULL_SELECTION | SWT.MULTI);
		setDropTraget(tbl);
		if (font != null)
			tbl.setFont(font);
		tbl.setHeaderVisible(true);
		tbl.setLinesVisible(true);

		SashForm tailSash = new SashForm(bottomSash, SWT.HORIZONTAL);
		tailSash.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		StyledText sqlText = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		CommonTool.registerContextMenu(sqlText, false);
		sqlText.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		StyledText messagesArea = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		CommonTool.registerContextMenu(messagesArea, false);

		result.makeResult(tbl, sqlText, messagesArea);
		bottomSash.setWeights(new int[] { 6, 1 });
		CTabItem tab = new CTabItem(tabResultFolderMiddle, SWT.NONE);
		tab.setText(Messages.QEDIT_RESULT + (result.idx + 1));
		ToolBar toolBar = new ToolBar(viewForm, SWT.FLAT);
		ToolBarManager toolBarManager = new ToolBarManager(toolBar);
		result.makeActions(toolBarManager);
		viewForm.setContent(bottomSash);
		viewForm.setTopRight(toolBar);
		tab.setControl(viewForm);

		// Auto set column size, maximum is 300px
		for (int i = 1; i < tbl.getColumnCount(); i++) {
			tbl.getColumns()[i].pack();
			if (tbl.getColumns()[i].getWidth() > 300)
				tbl.getColumns()[i].setWidth(300);
		}
	}

	private void createSashTop() {
		sashTop = new SashForm(topComp, SWT.VERTICAL);
		sashTop.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		sashTop.setLayout(new FillLayout());
		createCmpHead();
		createTabMiddleFolder();
		createTabMiddleResult();
		cmpMiddlePlanFolder.initialize();
		sashTop.setWeights(new int[] { 45, 55 });
	}

	/**
	 * This method initializes cmpHead
	 * 
	 */
	private void createCmpHead() {
		cmpHead = new Composite(sashTop, SWT.NONE);
		GridLayout gridLayout = new GridLayout();
		gridLayout.horizontalSpacing = 0;
		gridLayout.verticalSpacing = 0;
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;
		cmpHead.setLayout(gridLayout);
		GridData gridData2 = new GridData();
		gridData2.grabExcessHorizontalSpace = true;
		gridData2.horizontalSpan = SWT.FILL;
		cmpHead.setLayoutData(gridData2);

		querySourceView = new QuerySourceViewer(cmpHead, SWT.NONE, this);
		querySourceView.setLayoutData(new GridData(SWT.FILL, SWT.FILL, true,
				true));

	}

	private void createTabMiddleFolder() {
		tabFolderMiddle = new TabFolder(sashTop, SWT.TOP);
		// query result tab area
		cmpMiddleResultFolder = new Composite(tabFolderMiddle, SWT.NONE);
		cmpMiddleResultFolder.setLayout(new FillLayout());
		TabItem tabTopResult = new TabItem(tabFolderMiddle, SWT.NONE);
		tabTopResult.setText(Messages.QEDIT_RESULT_FOLDER);
		tabTopResult.setControl(cmpMiddleResultFolder);
		// query plan tab area
		cmpMiddlePlanFolder = new CombinedQueryPlanComposite(tabFolderMiddle,
				SWT.NONE, editor);
	}

	/**
	 * Initializing a Result Tab
	 */
	private void createTabMiddleResult() {
		tabResultFolderMiddle = new CTabFolder(cmpMiddleResultFolder,
				SWT.BOTTOM | SWT.BORDER | SWT.CLOSE);
		tabResultFolderMiddle.setSimple(false);
		tabResultFolderMiddle.setUnselectedImageVisible(true);
		tabResultFolderMiddle.setUnselectedCloseVisible(true);
		tabResultFolderMiddle.setLayout(new GridLayout(1, true));
		sashBottom = new SashForm(tabResultFolderMiddle, SWT.VERTICAL);
		sashBottom.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		createTable();
		createCmpTail();
		sashBottom.setWeights(new int[] { 75, 25 });
		tabItemResult = new CTabItem(tabResultFolderMiddle, SWT.NONE);
		tabItemResult.setText(Messages.QEDIT_RESULT);
		tabItemResult.setControl(sashBottom);
	}

	/**
	 * This method initializes table
	 * 
	 */
	private void createTable() {
		tblResult = new Table(sashBottom, SWT.NONE);
		tblResult.setHeaderVisible(true);
		tblResult.setLinesVisible(true);
	}

	/**
	 * This method initializes cmpTail
	 * 
	 */
	private void createCmpTail() {
		GridData gridData = new GridData();
		gridData.horizontalAlignment = SWT.FILL;
		gridData.verticalAlignment = SWT.FILL;
		SashForm sashTail = new SashForm(sashBottom, SWT.HORIZONTAL);
		sashTail.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		sashTail.setLayoutData(gridData);
		Text txaRunQuery = new Text(sashTail, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		txaRunQuery.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));

		new StyledText(sashTail, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
	}

	/**
	 * set drop target for table
	 * 
	 * @param table
	 */
	private void setDropTraget(Table table) {
		editor.getDragController().addTableDropTarget(table);
	}

	public QuerySourceViewer getQuerySourceView() {
		return querySourceView;
	}

	public boolean save() throws IOException {
		File file = editor.getFile();
		if (file == null || !file.exists()) {
			File f = QueryEditorPart.getSelectedFile();
			if (f == null) {
				return false;
			}
			editor.setFile(f);
			file = f;
		}
		querySourceView.document.setFileName(file.getAbsolutePath());
		querySourceView.document.save();
		return true;
	}

	public QueryEditorPart getEditor() {
		return editor;
	}

	public CTabFolder getTabResultFolderMiddle() {
		return tabResultFolderMiddle;
	}

	public TabFolder getTabFolderMiddle() {
		return tabFolderMiddle;
	}

	public CTabItem getTabItemLogResult() {
		return tabItemLogResult;
	}

	public StyledText getLogMessagesArea() {
		return logMessagesArea;
	}

	public StyledText getLogSqlText() {
		return logSqlText;
	}

	public void makePlan(StructQueryPlan sq, int tabIdx) {
		cmpMiddlePlanFolder.addPlanTab(sq, true);
		tabFolderMiddle.setSelection(1);
	}

	public void clearPlanTab() {
		cmpMiddlePlanFolder.clearPlanTabs();
	}

	public Table getResultTable() {
		return tblResult;
	}

}
