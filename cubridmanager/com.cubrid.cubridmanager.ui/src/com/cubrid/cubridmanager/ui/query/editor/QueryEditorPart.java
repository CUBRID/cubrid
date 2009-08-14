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
package com.cubrid.cubridmanager.ui.query.editor;

import java.io.File;
import java.io.IOException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.text.NumberFormat;
import java.util.Vector;

import org.apache.log4j.Logger;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CTabFolder;
import org.eclipse.swt.custom.CTabItem;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.TypedEvent;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.ProgressBar;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.ToolBar;
import org.eclipse.swt.widgets.ToolItem;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.help.IWorkbenchHelpSystem;

import com.cubrid.cubridmanager.core.common.StringUtil;
import com.cubrid.cubridmanager.core.common.jdbc.JDBCConnectionManager;
import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.core.common.model.ServerInfo;
import com.cubrid.cubridmanager.core.query.QueryOptions;
import com.cubrid.cubridmanager.help.CubridManagerHelpContextIDs;
import com.cubrid.cubridmanager.ui.CubridManagerUIPlugin;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.StructQueryPlan;
import com.cubrid.cubridmanager.ui.query.action.CopyAction;
import com.cubrid.cubridmanager.ui.query.action.CutAction;
import com.cubrid.cubridmanager.ui.query.action.FindReplaceAction;
import com.cubrid.cubridmanager.ui.query.action.PasteAction;
import com.cubrid.cubridmanager.ui.query.action.QueryOpenAction;
import com.cubrid.cubridmanager.ui.query.action.RedoAction;
import com.cubrid.cubridmanager.ui.query.action.UndoAction;
import com.cubrid.cubridmanager.ui.query.control.CombinedQueryComposite;
import com.cubrid.cubridmanager.ui.query.control.QueryEditorToolBar;
import com.cubrid.cubridmanager.ui.query.control.QueryExecuter;
import com.cubrid.cubridmanager.ui.query.control.QuerySourceViewer;
import com.cubrid.cubridmanager.ui.query.control.SqlParser;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridEditorPart;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.event.CubridNodeChangedEvent;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDPreparedStatement;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDStatement;
import cubrid.jdbc.jci.CUBRIDCommandType;
import cubrid.sql.CUBRIDOID;

/**
 * 
 * This query editor part is responsible to execute sql
 * 
 * @author pangqiren 2009-3-2
 */
public class QueryEditorPart extends
		CubridEditorPart {

	private static final Logger logger = LogUtil.getLogger(QueryEditorPart.class);
	public static final String ID = QueryEditorPart.class.getName();
	private boolean isDirty = false;

	private QueryEditorToolBar qeToolBar;
	private CombinedQueryComposite combinedQueryComposite;
	private File file;

	// query connection
	private Connection queryConn = null;
	private CUBRIDResultSet rs = null;

	// public Vector vectorQueryPlans = new Vector();
	private Vector<QueryExecuter> curResult = new Vector<QueryExecuter>();
	public CUBRIDPreparedStatement stmt = null;
	private boolean isAutocommit;
	private ToolItem rollbackItem;
	private ToolItem commitItem;
	private boolean isActive;
	private ToolItem queryPlanItem;
	private long execEndTime = 0;
	private QueryThread queryThread;
	private ToolItem runItem;
	private boolean isOnlyQueryPlan;
	private int line;

	private ProgressBar runQueryPb;
	private Button stopButton;
	private QueryExecuter result = null;
	private ToolItem autoCommitItem;
	private DragController dragController;

	// to support the Schema Info Viewer in the Query Explain 
	private String currentSchemaName = null;

	public QueryEditorPart() {
	}

	@Override
	public void init(IEditorSite site, IEditorInput input) throws PartInitException {
		super.init(site, input);
		this.setSite(site);
		this.setInput(input);
		this.setPartName(input.getName());
		this.setTitleToolTip(input.getToolTipText());
		if (input.getImageDescriptor() != null)
			this.setTitleImage(input.getImageDescriptor().createImage());
		hookRetragetActions();
	}

	/**
	 * when dispose query editor, interrupt query thread, clear result and query
	 * plan, reset query connection
	 */
	public void dispose() {
		if (isTransaction()) {

			MessageDialog dialog = new MessageDialog(
					Display.getDefault().getActiveShell(),
					com.cubrid.cubridmanager.ui.common.Messages.titleConfirm,
					null,
					Messages.bind(
							Messages.connCloseConfirm,
							new String[] { this.getSelectedDatabase().getLabel() }),
					MessageDialog.QUESTION, new String[] { Messages.btnYes,
							Messages.btnNo }, 0) {

				@Override
				protected void buttonPressed(int buttonId) {
					switch (buttonId) {
					case 0:
						try {
							queryConn.commit();
						} catch (SQLException ex) {
							CommonTool.openErrorBox(ex.getErrorCode()
									+ CommonTool.getLineSeparator()
									+ ex.getMessage());
							logger.error(ex);
						}
						setReturnCode(0);
						close();
						break;
					case 1:
						try {
							queryConn.rollback();
						} catch (SQLException ex) {
							CommonTool.openErrorBox(ex.getErrorCode()
									+ CommonTool.getLineSeparator()
									+ ex.getMessage());
							logger.error(ex);
						}
						setReturnCode(1);
						close();
						break;
					default:
						break;
					}
				}

			};
			int returnVal = dialog.open();
			if (returnVal != 0 && returnVal != 1) {
				try {
					queryConn.rollback();
				} catch (SQLException ex) {
					CommonTool.openErrorBox(ex.getErrorCode()
							+ CommonTool.getLineSeparator() + ex.getMessage());
					logger.error(ex);
				}
			}
		}
		super.dispose();
		try {
			if (queryConn != null)
				queryConn.close();
			if (queryThread != null && !queryThread.isInterrupted()) {
				queryThread.interrupt();
				queryThread = null;
			}
			clearResult();
			clearPlan();
		} catch (Exception e) {
			logger.error(e);
		} finally {
			queryConn = null;
			combinedQueryComposite.getQuerySourceView().release();
		}
		result.dispose();
	}

	/**
	 * 
	 * set the query editor database connection
	 * 
	 * @param dababase
	 */
	public void connect(CubridDatabase database) {
		qeToolBar.setDatabase(database);
	}

	protected void hookRetragetActions() {
		IActionBars bar = this.getEditorSite().getActionBars();
		bar.setGlobalActionHandler(UndoAction.ID,
				ActionManager.getInstance().getAction(UndoAction.ID));
		bar.setGlobalActionHandler(RedoAction.ID,
				ActionManager.getInstance().getAction(RedoAction.ID));
		IAction action = ActionManager.getInstance().getAction(CutAction.ID);
		action.setEnabled(true);
		bar.setGlobalActionHandler(CutAction.ID, action);
		IAction copyAction = ActionManager.getInstance().getAction(
				CopyAction.ID);
		bar.setGlobalActionHandler(CopyAction.ID, copyAction);
		copyAction.setEnabled(true);
		IAction pasteAction = ActionManager.getInstance().getAction(
				PasteAction.ID);
		bar.setGlobalActionHandler(PasteAction.ID, pasteAction);
		pasteAction.setEnabled(true);
		bar.setGlobalActionHandler(FindReplaceAction.ID,
				ActionManager.getInstance().getAction(FindReplaceAction.ID));
		bar.setGlobalActionHandler(QueryOpenAction.ID,
				ActionManager.getInstance().getAction(QueryOpenAction.ID));
		bar.updateActionBars();
	}

	@Override
	public void createPartControl(Composite parent) {
		Composite top = new Composite(parent, SWT.NONE);
		final GridLayout gridLayout = new GridLayout();
		gridLayout.verticalSpacing = 0;
		gridLayout.marginWidth = 0;
		gridLayout.marginHeight = 0;
		gridLayout.horizontalSpacing = 0;
		top.setLayout(gridLayout);

		IWorkbenchHelpSystem whs = PlatformUI.getWorkbench().getHelpSystem();
		whs.setHelp(top, CubridManagerHelpContextIDs.queryEditor);

		final Composite composite_1 = new Composite(top, SWT.NONE);
		final GridData gd_composite_1 = new GridData(SWT.FILL, SWT.FILL, true,
				false);
		composite_1.setLayoutData(gd_composite_1);
		final GridLayout gridLayout_1 = new GridLayout();
		gridLayout_1.marginHeight = 0;
		gridLayout_1.horizontalSpacing = 0;
		gridLayout_1.marginWidth = 0;
		gridLayout_1.numColumns = 2;
		composite_1.setLayout(gridLayout_1);

		final Composite composite = new Composite(composite_1, SWT.NONE);
		final GridLayout gl = new GridLayout();
		gl.numColumns = 2;
		gl.marginHeight = 0;
		gl.horizontalSpacing = 0;
		composite.setLayout(gl);
		final GridData gd_composite = new GridData(SWT.LEFT, SWT.FILL, true,
				false);
		gd_composite.grabExcessVerticalSpace = true;
		composite.setLayoutData(gd_composite);

		createToolbar(composite);

		combinedQueryComposite = new CombinedQueryComposite(top, SWT.NONE, this);
		final GridData gd_combinedQueryComposite = new GridData(SWT.FILL,
				SWT.FILL, false, true);
		gd_combinedQueryComposite.heightHint = 209;
		gd_combinedQueryComposite.widthHint = 320;
		combinedQueryComposite.setLayoutData(gd_combinedQueryComposite);
		initConnection(qeToolBar.getSelectedDb());
		dragController = new DragController(this);
		dragController.register();
	}

	private void createToolbar(final Composite composite) {

		qeToolBar = new QueryEditorToolBar(composite, this);
		qeToolBar.addDatabaseChangedListener(new Listener() {

			public void handleEvent(Event event) {
				Object data = event.data;
				if (data != null && data instanceof CubridDatabase) {
					boolean enable = ((CubridDatabase) data) != QueryEditorToolBar.NULL_DATABASE;
					runItem.setEnabled(enable);
					queryPlanItem.setEnabled(enable);
					initConnection((CubridDatabase) data);
				}
			}

		});
		final ToolBar toolBar = qeToolBar;
		ToolItem openItem = new ToolItem(toolBar, SWT.PUSH);
		openItem.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/file_open.png"));
		openItem.setToolTipText(Messages.open);
		openItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				IActionBars bar = getEditorSite().getActionBars();
				IAction handler = bar.getGlobalActionHandler(QueryOpenAction.ID);
				handler.run();
			}
		});

		ToolItem saveItem = new ToolItem(toolBar, SWT.PUSH);
		saveItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/file_save.png"));
		saveItem.setToolTipText(Messages.save);
		saveItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				if (isDirty) {
					doSave(null);
				}
			}
		});

		ToolItem saveAsItem = new ToolItem(toolBar, SWT.PUSH);
		saveAsItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/file_saveas.png"));
		saveAsItem.setToolTipText(Messages.saveAs);
		saveAsItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				doSaveAs();
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		runItem = new ToolItem(toolBar, SWT.PUSH);
		runItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_run.png"));
		runItem.setToolTipText(Messages.run + "(F5)");
		runItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				isOnlyQueryPlan = false;
				executeRun(e);
			}

		});
		runItem.setEnabled(false);
		new ToolItem(toolBar, SWT.SEPARATOR);

		commitItem = new ToolItem(toolBar, SWT.PUSH);
		commitItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_commit.png"));
		commitItem.setDisabledImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_commit_disabled.png"));
		commitItem.setToolTipText(Messages.commit);
		commitItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				commit();
			}
		});
		rollbackItem = new ToolItem(toolBar, SWT.PUSH);
		rollbackItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_rollback.png"));
		rollbackItem.setDisabledImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_rollback_disabled.png"));
		rollbackItem.setToolTipText(Messages.rollback);
		rollbackItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				rollback();
			}
		});

		autoCommitItem = new ToolItem(toolBar, SWT.CHECK);
		autoCommitItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_auto_true.png"));
		autoCommitItem.setDisabledImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_auto_false.png"));
		autoCommitItem.setToolTipText(Messages.autoCommit);
		autoCommitItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				if (checkActive())
					return;
				setAutocommit(autoCommitItem.getSelection());
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		queryPlanItem = new ToolItem(toolBar, SWT.PUSH);
		queryPlanItem.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_execution_plan.png"));
		queryPlanItem.setToolTipText(Messages.queryPlanTip + "(F6)");
		queryPlanItem.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				isOnlyQueryPlan = true;
				executeRun(e);
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		ToolItem itemUndo = new ToolItem(toolBar, SWT.PUSH);
		itemUndo.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_undo.png"));
		itemUndo.setToolTipText(Messages.undoTip + "(Ctrl+Z)");
		itemUndo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				undo();
			}
		});

		ToolItem itemRedo = new ToolItem(toolBar, SWT.PUSH);
		itemRedo.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_redo.png"));
		itemRedo.setToolTipText(Messages.redoTip + "(Ctrl+Y)");
		itemRedo.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				redo();
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		ToolItem itemFind = new ToolItem(toolBar, SWT.PUSH);
		itemFind.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_find_replace.png"));
		itemFind.setToolTipText(Messages.findTip + "(Ctrl+F)");
		itemFind.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				find();
			}

		});

		ToolItem itemFindNext = new ToolItem(toolBar, SWT.PUSH);
		itemFindNext.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_find_next.png"));
		itemFindNext.setToolTipText(Messages.findNextTip + "(F3)");
		itemFindNext.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				findNext();
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		ToolItem itemComment = new ToolItem(toolBar, SWT.PUSH);
		itemComment.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_comment_add.png"));
		itemComment.setToolTipText(Messages.commentTip + "(Ctrl+/)");
		itemComment.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {

				combinedQueryComposite.getQuerySourceView().comment();
			}
		});

		ToolItem itemUncomment = new ToolItem(toolBar, SWT.PUSH);
		itemUncomment.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_comment_delete.png"));
		itemUncomment.setToolTipText(Messages.unCommentTip + "(Ctrl+/)");
		itemUncomment.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				combinedQueryComposite.getQuerySourceView().uncomment();
			}
		});

		new ToolItem(toolBar, SWT.SEPARATOR);

		ToolItem itemIndent = new ToolItem(toolBar, SWT.PUSH);
		itemIndent.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_indent_add.png"));
		itemIndent.setToolTipText(Messages.indentTip + "(Tab)");
		itemIndent.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				combinedQueryComposite.getQuerySourceView().indent();
			}
		});

		ToolItem itemUnindent = new ToolItem(toolBar, SWT.PUSH);
		itemUnindent.setImage(CubridManagerUIPlugin.getImage("icons/queryeditor/query_indent_delete.png"));
		itemUnindent.setToolTipText(Messages.unIndentTip + "(Shift+Tab)");
		itemUnindent.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				combinedQueryComposite.getQuerySourceView().unindent();
			}
		});

		ToolItem itemFormatterr = new ToolItem(toolBar, SWT.PUSH);
		itemFormatterr.setImage(CubridManagerUIPlugin.getImage("/icons/queryeditor/query_format.png"));
		itemFormatterr.setToolTipText(Messages.formatTip + "(Ctrl+Shift+F)");
		itemFormatterr.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
				combinedQueryComposite.getQuerySourceView().format();
			}
		});
	}

	private void setAutocommit(boolean autocommit) {
		isAutocommit = autocommit;
		autoCommitItem.setSelection(autocommit);
		setActive(false);
		try {
			if (queryConn != null) {
				queryConn.setAutoCommit(isAutocommit);
			}
		} catch (SQLException e) {
			CommonTool.openErrorBox(Messages.cantChangeStatus
					+ CommonTool.getLineSeparator() + e.getErrorCode()
					+ CommonTool.getLineSeparator() + Messages.errorHead
					+ e.getMessage());
			logger.error(e);
		}
	}

	private boolean checkActive() {
		if (!isAutocommit && isActive) {
			MessageBox mb = new MessageBox(getSite().getShell(), SWT.OK
					| SWT.ICON_WARNING);
			mb.setText(Messages.info);
			mb.setMessage(Messages.transActive);
			mb.open();
			autoCommitItem.setSelection(false);
			return true;
		}
		return false;
	}

	/**
	 * is there any transaction not committed ?
	 * 
	 * @return
	 */
	public boolean isTransaction() {
		return !isAutocommit && isActive;
	}

	/**
	 * when click 'run' button, execute the query operation
	 * 
	 * @param e
	 */
	public void executeRun(TypedEvent e) {
		if ((long) (e.time & 0xFFFFFFFFL) < execEndTime) {
			return;
		}

		long startTime, endTime, timeInterval;
		startTime = System.currentTimeMillis();
		runQuery();
		endTime = System.currentTimeMillis();
		timeInterval = endTime - startTime;

		execEndTime = (long) (e.time & 0xFFFFFFFFL) + timeInterval;

	}

	/**
	 * put query string to the query editor
	 * 
	 * @param query
	 */
	public void setQuery(String query) {
		combinedQueryComposite.getQuerySourceView().setQueries(query);
	}

	/**
	 * execute all the selected sql script on editor, if not, execute all the
	 * script on editor
	 */
	public void runQuery() {
		String queries = combinedQueryComposite.getQuerySourceView().getQueries();
		if (queries == null || queries.trim().equals("")) {
			return;
		}
		if (queryConn == null) {
			initConnection(qeToolBar.getSelectedDb());
		}
		if (isOnlyQueryPlan)
			runQueryPlanOnly(queries);
		else
			runQuery(queries);
	}

	/**
	 * execute the selected sql script
	 * 
	 * @param queries
	 */
	private void runQuery(final String queries) {
		clearResult();
		runItem.setEnabled(false);
		queryPlanItem.setEnabled(false);
		makeProgressBar();
		queryThread = new QueryThread(Messages.proRunQuery, queries, this);
		queryThread.start();
	}

	private void makeProgressBar() {
		CTabFolder tabMiddle = combinedQueryComposite.getTabResultFolderMiddle();
		if (tabMiddle == null || tabMiddle.isDisposed()) {
			return;
		}

		Composite comp = new Composite(tabMiddle, SWT.NONE);
		comp.setLayout(new GridLayout(6, true));
		runQueryPb = new ProgressBar(comp, SWT.HORIZONTAL | SWT.INDETERMINATE);
		GridData gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 5;
		runQueryPb.setLayoutData(gridData);

		stopButton = new Button(comp, SWT.PUSH);
		stopButton.setText(Messages.stopBtn);
		gridData = new GridData(GridData.FILL_HORIZONTAL);
		gridData.horizontalSpan = 1;
		stopButton.setLayoutData(gridData);

		stopButton.addSelectionListener(new SelectionAdapter() {
			public void widgetSelected(SelectionEvent e) {
				new Thread() {
					public void run() {
						if (queryThread != null) {
							if (result != null)
								result.dispose();
							try {
								if (stmt != null) {
									stmt.cancel();
								}
							} catch (SQLException e1) {
								logger.error(e1);
							}
							QueryUtil.freeQuery(stmt, rs);
							queryThread = null;
						}
					}
				}.start();
			}
		});
		CTabItem tab = new CTabItem(tabMiddle, SWT.NONE);
		tab.setText(Messages.proRunQuery);
		tab.setControl(comp);
		tabMiddle.setSelection(0);
	}

	private void clearResult() {
		CTabFolder tabMiddle = combinedQueryComposite.getTabResultFolderMiddle();
		if (tabMiddle != null && !tabMiddle.isDisposed()) {
			while (tabMiddle.getItemCount() > 0) {
				if (!tabMiddle.getItem(0).getControl().isDisposed())
					tabMiddle.getItem(0).getControl().dispose();
				tabMiddle.getItem(0).dispose();
			}
		}
		if (curResult != null)
			curResult.clear();
	}

	/**
	 * dispose plan tabs
	 */
	private void clearPlan() {
		combinedQueryComposite.clearPlanTab();
	}

	/**
	 * fetch execution plans while running SQLs
	 * 
	 * @param queries
	 */
	private void runQueryPlanOnly(String queries) {
		// clearPlan();
		runItem.setEnabled(false);
		queryPlanItem.setEnabled(false);
		Vector<String> qVector = null;
		CUBRIDStatement statement = null;

		qVector = queriesToQuery(queries);
		int i = 0;
		try {
			int len = qVector.size();
			for (i = 0; i < len; i++) {
				String sql = qVector.get(i).toString();
				statement = (CUBRIDStatement) queryConn.createStatement();
				StructQueryPlan sq = new StructQueryPlan(sql,
						statement.getQueryplan(sql));
				combinedQueryComposite.makePlan(sq, i);
				QueryUtil.freeQuery(statement);
			}
		} catch (Exception ee) {
			int errorCode = 0;
			if (ee instanceof CUBRIDException) {
				errorCode = ((CUBRIDException) ee).getErrorCode();
			} else if (ee instanceof SQLException) {
				errorCode = ((SQLException) ee).getErrorCode();
			}
			String errmsg = "";

			if (isAutocommit)
				try {
					queryConn.rollback();
				} catch (SQLException e1) {
					logger.error(e1);
				}

			QuerySourceViewer sourceViewer = combinedQueryComposite.getQuerySourceView();
			sourceViewer.txtFind((String) qVector.get(i), 0, false, false,
					true, false);
			StyledText txaEdit = sourceViewer.getText();

			int line = txaEdit.getLineAtOffset(txaEdit.getSelection().x) + 1;

			errmsg += Messages.runError + errorCode
					+ CommonTool.getLineSeparator() + line + Messages.errWhere
					+ CommonTool.getLineSeparator() + Messages.errorHead
					+ ee.getMessage();
			CTabItem tabItemLogResult = combinedQueryComposite.getTabItemLogResult();
			CTabFolder tabResultFolderMiddle = combinedQueryComposite.getTabResultFolderMiddle();
			StyledText logMessagesArea = combinedQueryComposite.getLogMessagesArea();
			StyledText logSqlText = combinedQueryComposite.getLogSqlText();
			if (tabItemLogResult != null && !tabItemLogResult.isDisposed()
					&& logMessagesArea != null && !logMessagesArea.isDisposed()
					&& logSqlText != null && !logSqlText.isDisposed()) {
				tabResultFolderMiddle.setSelection(0);
				String logMessage = logMessagesArea.getText();
				if (logMessage != null && logMessage.length() > 0) {
					logMessage += CommonTool.getLineSeparator();
				}
				logSqlText.setText(logSqlText.getText()
						+ CommonTool.getLineSeparator()
						+ qVector.get(i).toString());
				logSqlText.setTopIndex(logSqlText.getLineCount() - 1);
				logMessagesArea.setText(logMessage
						+ CommonTool.getLineSeparator() + errmsg);
				logMessagesArea.setTopIndex(logMessagesArea.getLineCount() - 1);
				TabFolder tabFolderMiddle = combinedQueryComposite.getTabFolderMiddle();
				if (tabFolderMiddle != null && !tabFolderMiddle.isDisposed()) {
					tabFolderMiddle.setSelection(0);
				}
			} else {
				while (tabResultFolderMiddle.getItemCount() > 0) {
					tabResultFolderMiddle.getItem(0).dispose();
				}
				combinedQueryComposite.makeLogResult(qVector.get(i).toString(),
						errmsg);
				for (int j = 0; j < curResult.size(); j++) {
					combinedQueryComposite.makeResult((QueryExecuter) curResult.get(j));
				}
				tabResultFolderMiddle.setSelection(0);
			}
			logger.error(ee.getMessage(), ee);
		} finally {
			QueryUtil.freeQuery(statement);
		}
		runItem.setEnabled(true);
		queryPlanItem.setEnabled(true);
		isOnlyQueryPlan = false;
	}

	private Vector<String> queriesToQuery(String queries) {
		char[] buffer = queries.toCharArray();
		boolean sglQuote = false;
		boolean dblQuote = false;
		boolean isLineComment = false;
		boolean isBlockComment = false;
		char prevChar = '\0';
		Vector<String> qVector = new Vector<String>();
		int start = 0;
		int end = 0;

		for (int i = 0; i < buffer.length; i++) {
			if (buffer[i] == '\'' && !dblQuote)
				sglQuote = !sglQuote;
			if (buffer[i] == '"' && !sglQuote)
				dblQuote = !dblQuote;

			if (!dblQuote && !sglQuote) {
				if (!isLineComment) {
					if (prevChar == '-') {
						if (buffer[i] == '-' && !isBlockComment) {
							isLineComment = true;
						}
					} else if (prevChar == '/') {
						if (buffer[i] == '/' && !isBlockComment) {
							isLineComment = true;
						}
					}
				} else if (buffer[i] == '\n') {
					isLineComment = false;
				}

				if (!isBlockComment) {
					if (prevChar == '/') {
						if (buffer[i] == '*' && !isLineComment) {
							isBlockComment = true;
						}
					}
				} else {
					if (prevChar == '*') {
						if (buffer[i] == '/') {
							isBlockComment = false;
						}
					}
				}
			}

			prevChar = buffer[i];

			if (!isLineComment && !isBlockComment && !dblQuote && !sglQuote
					&& buffer[i] == ';') {
				start = end;
				end = i + 1;
				String aQuery = queries.substring(start, end).trim();

				if (isNotEmptyQuery(aQuery))
					qVector.addElement(aQuery);
			}
		}
		if (end < queries.length() - 1) {
			String aQuery = queries.substring(end, queries.length()).trim();

			if (isNotEmptyQuery(aQuery))
				qVector.addElement(aQuery);
		}

		return qVector;
	}

	private boolean isNotEmptyQuery(String query) {
		String[] queryOneLine = query.split("\n");
		String tempQuery = "";
		boolean skipLine = false;
		boolean inComment = false;

		for (int j = 0; j < queryOneLine.length; j++) {
			queryOneLine[j] = queryOneLine[j].trim();
			int position = queryOneLine[j].length();
			if (queryOneLine[j].indexOf("--") > -1)
				position = Math.min(position, queryOneLine[j].indexOf("--"));
			if (queryOneLine[j].indexOf("/*") > -1) {
				position = Math.min(position, queryOneLine[j].indexOf("/*"));
				inComment = true;
			}
			if (queryOneLine[j].indexOf("//") > -1)
				position = Math.min(position, queryOneLine[j].indexOf("//"));
			queryOneLine[j] = queryOneLine[j].substring(0, position);
			if (queryOneLine[j].indexOf("*/") > -1) {
				queryOneLine[j] = queryOneLine[j].substring(queryOneLine[j].indexOf("*/") + 2);
				inComment = false;
				skipLine = false;
			}

			if (!skipLine) {
				tempQuery += queryOneLine[j];
			}
			if (inComment)
				skipLine = true;
		}
		if (tempQuery.length() > 0 && tempQuery.compareTo(";") != 0)
			return true;
		else
			return false;
	}

	@Override
	/**
	 * on the editor focused, show the selected server name on window title
	 */
	public void setFocus() {
		combinedQueryComposite.getQuerySourceView().getText().setFocus();
		CubridDatabase database = getSelectedDatabase();
		Shell shell = getSite().getShell();
		if (database == QueryEditorToolBar.NULL_DATABASE) {
			if (shell != null && !shell.isDisposed()) {
				shell.setText(LayoutManager.CUBRID_MANAGER_TITLE + " - "
						+ QueryEditorToolBar.NO_DATABASE_SELECTED);
			}
		} else {
			String title = LayoutManager.getTitle(database);
			if (shell != null) {
				shell.setText(LayoutManager.CUBRID_MANAGER_TITLE);
				if (title != null && title.trim().length() > 0) {
					shell.setText(LayoutManager.CUBRID_MANAGER_TITLE + " - "
							+ title);
				}
			}
		}
	}

	@Override
	/**
	 * save sql script
	 */
	public void doSave(IProgressMonitor monitor) {
		try {
			boolean success = combinedQueryComposite.save();
			if (!success) {
				monitor.setCanceled(true);
				return;
			}
			isDirty = false;
			firePropertyChange(PROP_DIRTY);
		} catch (IOException e) {
			logger.error(e.getMessage());
			CommonTool.openErrorBox(getSite().getShell(), e.getMessage());
		}
	}

	@Override
	public void doSaveAs() {
		File bak = getFile();
		try {
			File f = getSelectedFile();
			if (f != null) {
				setFile(f);
				doSave(null);
			}
		} catch (IOException e) {
			logger.error(e.getMessage());
			if (bak != null) {
				setFile(bak);
			}
			CommonTool.openErrorBox(getSite().getShell(), e.getMessage());
		}

	}

	/**
	 * get the file through file selection dialog
	 * 
	 * @return
	 * @throws IOException
	 */
	public static File getSelectedFile() throws IOException {
		FileDialog dialog = openFileSaveDialog();
		String result = dialog.open();
		if (result == null) {
			return null;
		}
		File f = new File(result);
		if (!f.exists()) {
			f.createNewFile();
		}
		return f;
	}

	/**
	 * open a dialog to set the save file
	 * 
	 * @return
	 */
	public static FileDialog openFileSaveDialog() {
		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}
		return dialog;
	}

	/**
	 * open a dialog to set the save file
	 * 
	 * @return
	 */
	public static FileDialog openFileSavePlanDialog() {
		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.xml" });
		dialog.setFilterNames(new String[] { "XML File" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}
		return dialog;
	}

	/**
	 * open a dialog to set the save file
	 * 
	 * @return
	 */
	public static FileDialog openFileOpenDialog() {
		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.OPEN | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}
		return dialog;
	}

	public static FileDialog openFileOpenPlanDialog() {
		FileDialog dialog = new FileDialog(
				PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell(),
				SWT.OPEN | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.xml" });
		dialog.setFilterNames(new String[] { "XML File" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}
		return dialog;
	}

	@Override
	/**
	 * is the query editor not save
	 */
	public boolean isDirty() {
		return isDirty;
	}

	public void setDirty(boolean dirty) {
		this.isDirty = dirty;
		firePropertyChange(IEditorPart.PROP_DIRTY);
	}

	@Override
	public boolean isSaveAsAllowed() {
		return true;
	}

	/**
	 * show script in the editor
	 * 
	 * @param script
	 */
	public void setScript(String script) {
		combinedQueryComposite.getQuerySourceView().setScript(script);
		setDirty(false);

	}

	public void setFile(File file) {
		this.file = file;
		setPartName(file.getName());
	}

	public File getFile() {
		return file;
	}

	public void find() {
		QuerySourceViewer source = combinedQueryComposite.getQuerySourceView();
		source.find();
	}

	private void findNext() {
		QuerySourceViewer source = combinedQueryComposite.getQuerySourceView();
		source.findNext();
	}

	/**
	 * do the undo operation
	 */
	public void undo() {
		QuerySourceViewer source = combinedQueryComposite.getQuerySourceView();
		source.undo();

	}

	/**
	 * do the redo operation
	 */
	public void redo() {
		QuerySourceViewer source = combinedQueryComposite.getQuerySourceView();
		source.redo();

	}

	/**
	 * initialize the query database
	 * 
	 * @param database
	 */
	public boolean initConnection(CubridDatabase database) {
		ServerInfo serverInfo = database.getServer() != null ? database.getServer().getServerInfo()
				: null;
		boolean autoCommit = QueryOptions.getAutoCommit(serverInfo);
		setAutocommit(autoCommit);
		boolean queryplan = QueryOptions.getQueryPlan(serverInfo);
		queryPlanItem.setEnabled(queryplan && queryPlanItem.getEnabled());
		if (database == QueryEditorToolBar.NULL_DATABASE) {
			resetJDBCConnection();
			return false;
		}
		try {
			queryConn = JDBCConnectionManager.getConnection(
					database.getDatabaseInfo(), isAutocommit);
		} catch (SQLException e) {
			queryConn = null;
			logger.error(e.getMessage());
			runItem.setEnabled(false);
			queryPlanItem.setEnabled(false);
			CommonTool.openErrorBox(e.getMessage());

		}
		return true;
	}

	/**
	 * reset the query connection
	 * 
	 * @return
	 */
	public boolean resetJDBCConnection() {
		if (queryConn != null && isActive) {
			MessageDialog dialog = new MessageDialog(
					getSite().getShell(),
					com.cubrid.cubridmanager.ui.common.Messages.titleConfirm,
					null,
					Messages.bind(
							Messages.connCloseConfirm,
							new String[] { this.getSelectedDatabase().getLabel() }),
					MessageDialog.QUESTION, new String[] { Messages.btnYes,
							Messages.btnNo, Messages.cancel }, 0) {

				@Override
				protected void buttonPressed(int buttonId) {
					switch (buttonId) {
					case 0:
						commit();
						setReturnCode(0);
						close();
						break;
					case 1:
						rollback();
						setReturnCode(1);
						close();
						break;
					case 2:
						setReturnCode(2);
						close();
					default:
						break;
					}
				}

			};
			int returnVal = dialog.open();
			if (returnVal == 2 || returnVal == -1) {
				return false;
			}
		}
		QueryUtil.freeQuery(queryConn);
		queryConn = null;
		return true;
	}

	/**
	 * shut down the current connection
	 */
	public void shutDownConnection() {
		if (queryConn != null) {
			try {
				queryConn.close();
			} catch (SQLException e) {
				CommonTool.openErrorBox(StringUtil.getStackTrace(e, "\n"));

			} finally {
				queryConn = null;

			}
		}
	}

	/**
	 * if the database selection successful
	 * 
	 * @return
	 */
	public boolean isConnected() {
		return qeToolBar != null && !qeToolBar.isNull();
	}

	/**
	 * get selected server
	 * 
	 * @return
	 */
	public CubridServer getSelectedServer() {
		return qeToolBar.getSelectedDb().getServer();
	}

	/**
	 * get selected database
	 * 
	 * @return
	 */
	public CubridDatabase getSelectedDatabase() {
		if (qeToolBar == null) {
			return null;
		}
		return qeToolBar.getSelectedDb();
	}

	/**
	 * get current jdbc connection
	 * 
	 * @return
	 */
	public Connection getConnection() {
		return queryConn;
	}

	/**
	 * update row data on result table
	 * 
	 * @param strOid
	 * @param column
	 * @param value
	 * @throws SQLException
	 */
	public void updateResult(String strOid, String[] column, String[] value) throws SQLException {
		if (isAutocommit) {
			queryConn.setAutoCommit(true);
		} else {
			queryConn.setAutoCommit(false);
		}
		CUBRIDOID oid = CUBRIDOID.getNewInstance((CUBRIDConnection) queryConn,
				strOid);
		for (int i = 0; value != null && i < value.length; i++) {
			if (value[i].equals(QueryOptions.STR_NULL))
				value[i] = null;
		}
		oid.setValues(column, value);
		if (isAutocommit) {
			queryConn.commit();
		}
		setActive(true);
	}

	private void setActive(boolean isActive) {
		if (!isAutocommit) {
			this.isActive = isActive;
			rollbackItem.setEnabled(isActive);
			commitItem.setEnabled(isActive);
		} else {
			this.isActive = false;
			rollbackItem.setEnabled(false);
			commitItem.setEnabled(false);
		}
	}

	/**
	 * delete data through data oid
	 * 
	 * @param strOid
	 * @throws SQLException
	 */
	public void deleteResult(String[] strOid) throws SQLException {
		int i = 0;
		for (i = 0; i < strOid.length; i++) {
			CUBRIDOID.getNewInstance((CUBRIDConnection) queryConn, strOid[i]).remove();
		}
		if (isAutocommit) {
			queryConn.commit();
		}
		setActive(true);
		CommonTool.openInformationBox(Messages.delete, Messages.bind(
				Messages.deleteOk, i));
	}

	/**
	 * a thread focus on execute query operation
	 * 
	 * @author wangsl 2009-6-5
	 */
	class QueryThread extends
			Thread {

		private String queries;
		private QueryEditorPart queryEditor;

		public QueryThread(String name, String query, QueryEditorPart part) {
			super(name);
			this.queries = query;
			this.queryEditor = part;
		}

		@Override
		public void run() {
			final Vector<String> qVector = queriesToQuery(queries);
			int i = 0;
			int cntResults = 0;
			String noSelectSql = "";
			String logs = "";
			boolean hasModifyQuery = false;
			boolean isIsolationHigher = false;
			long beginTimestamp = 0;
			double elapsedTime = 0.0;
			NumberFormat nf = NumberFormat.getInstance();
			nf.setMaximumFractionDigits(3);
			result = null;
			String multiQuerySql = null;
			try {
				if (qVector.size() > 0)
					isIsolationHigher = isIsolationHigherThanRepeatableRead(
							queryConn, isActive);
				for (i = 0; i < qVector.size(); i++) {
					String sql = qVector.get(i).toString();
					if (QueryOptions.getSearchLimit(getSelectedServer() != null ? getSelectedServer().getServerInfo()
							: null) > 0) {
						multiQuerySql = SqlParser.parse(sql);
					}
					if (multiQuerySql == null) {
						beginTimestamp = System.currentTimeMillis();
						stmt = (CUBRIDPreparedStatement) queryConn.prepareStatement(
								sql,
								ResultSet.TYPE_FORWARD_ONLY,
								QueryOptions.getOidInfo(getSelectedServer() != null ? getSelectedServer().getServerInfo()
										: null) ? ResultSet.CONCUR_UPDATABLE
										: ResultSet.CONCUR_READ_ONLY,
								ResultSet.HOLD_CURSORS_OVER_COMMIT);
					}
					long endTimestamp;
					if (multiQuerySql != null) {
						result = new QueryExecuter(queryEditor, cntResults, "",
								getSelectedDatabase());
						result.setMultiQuerySql(multiQuerySql);
						result.setQueryMsg((i + 1) + Messages.querySeq
								+ CommonTool.getLineSeparator());
						try {
							result.makeTable(1);
						} catch (SQLException ee) {
							throw ee;
						}
						curResult.addElement(result);
						cntResults++;
					} else if (stmt.hasResultSet()) {
						stmt.setQueryInfo(false);
						stmt.setOnlyQueryPlan(false);
						try {
							stmt.executeQuery();
							endTimestamp = System.currentTimeMillis();
							rs = (CUBRIDResultSet) stmt.getResultSet();
						} catch (SQLException ee) {
							throw ee;
						}
						elapsedTime = (endTimestamp - beginTimestamp) * 0.001;
						String elapsedTimeStr = nf.format(elapsedTime);
						if (elapsedTime < 0.001) {
							elapsedTimeStr = "0.000";
						}
						result = new QueryExecuter(queryEditor, cntResults,
								sql, getSelectedDatabase());
						result.makeTable(rs);
						String queryMsg = (i + 1) + Messages.querySeq + "[ "
								+ elapsedTimeStr + " " + Messages.second
								+ " , " + Messages.totalRows + " : "
								+ result.cntRecord + " ]"
								+ CommonTool.getLineSeparator();
						result.setQueryMsg(queryMsg);
						switch (stmt.getStatementType()) {
						case CUBRIDCommandType.CUBRID_STMT_EVALUATE:
						case CUBRIDCommandType.CUBRID_STMT_CALL:
							hasModifyQuery = true;
							break;
						}
						curResult.addElement(result);
						cntResults++;
					} else {
						byte execType = stmt.getStatementType();
						/*
						 * the previous version , the variable threadExecResult
						 * is class field, but why ? is it necessary?
						 * 
						 */
						int threadExecResult = 0;
						try {
							threadExecResult = stmt.executeUpdate();
							endTimestamp = System.currentTimeMillis();
						} catch (SQLException ee) {
							throw ee;
						}
						elapsedTime = (endTimestamp - beginTimestamp) * 0.001;
						logs += (i + 1) + Messages.querySeq + " ";
						int cntModify = threadExecResult;
						noSelectSql += sql + CommonTool.getLineSeparator();
						hasModifyQuery = true;
						switch (execType) {
						case CUBRIDCommandType.CUBRID_STMT_ALTER_CLASS:
						case CUBRIDCommandType.CUBRID_STMT_ALTER_SERIAL:
						case CUBRIDCommandType.CUBRID_STMT_RENAME_CLASS:
						case CUBRIDCommandType.CUBRID_STMT_RENAME_TRIGGER:
							logs += Messages.alterOk;
							break;
						case CUBRIDCommandType.CUBRID_STMT_CREATE_CLASS:
						case CUBRIDCommandType.CUBRID_STMT_CREATE_INDEX:
						case CUBRIDCommandType.CUBRID_STMT_CREATE_TRIGGER:
						case CUBRIDCommandType.CUBRID_STMT_CREATE_SERIAL:
							logs += Messages.createOk;
							break;
						case CUBRIDCommandType.CUBRID_STMT_DROP_DATABASE:
						case CUBRIDCommandType.CUBRID_STMT_DROP_CLASS:
						case CUBRIDCommandType.CUBRID_STMT_DROP_INDEX:
						case CUBRIDCommandType.CUBRID_STMT_DROP_LABEL:
						case CUBRIDCommandType.CUBRID_STMT_DROP_TRIGGER:
						case CUBRIDCommandType.CUBRID_STMT_DROP_SERIAL:
						case CUBRIDCommandType.CUBRID_STMT_REMOVE_TRIGGER:
							logs += Messages.dropOk;
							break;
						case CUBRIDCommandType.CUBRID_STMT_INSERT:
							logs += Messages.bind(Messages.insertOk, cntModify);
							break;
						case CUBRIDCommandType.CUBRID_STMT_SELECT:
							break;
						case CUBRIDCommandType.CUBRID_STMT_UPDATE:
							logs += Messages.bind(Messages.updateOk2, cntModify);
							break;
						case CUBRIDCommandType.CUBRID_STMT_DELETE:
							logs += Messages.bind(Messages.deleteOk, cntModify);
							break;
						/* others are 'Successfully execution' */
						/*
						 * Under two line works disable button when query's
						 * last command is commit/rollback
						 */
						case CUBRIDCommandType.CUBRID_STMT_COMMIT_WORK:
						case CUBRIDCommandType.CUBRID_STMT_ROLLBACK_WORK:
							hasModifyQuery = false;
						default:
							logs += Messages.queryOk;
							break;
						}
						String elapsedTimeStr = nf.format(elapsedTime);
						if (elapsedTime < 0.001) {
							elapsedTimeStr = "0.000";
						}
						logs += "[" + elapsedTimeStr + " " + Messages.second
								+ "]" + CommonTool.getLineSeparator();
					}
					QueryUtil.freeQuery(stmt, rs);
				}
				if (isAutocommit)
					queryConn.commit();
			} catch (final SQLException e) {
				try {
					if (isAutocommit)
						queryConn.rollback();
				} catch (SQLException e1) {
					logger.error(e1);
				}
				if (multiQuerySql != null && result != null) {
					noSelectSql += result.getQuerySql();
					logs += result.getQueryMsg();
				} else {
					final String errorSql = (String) qVector.get(i);
					Display.getDefault().syncExec(new Runnable() {

						public void run() {
							QuerySourceViewer sourceView = combinedQueryComposite.getQuerySourceView();
							StyledText txaEdit = sourceView.getText();
							if (txaEdit != null && !txaEdit.isDisposed()) {
								sourceView.txtFind(errorSql, 0, false, false,
										true, false);
								line = txaEdit.getLineAtOffset(txaEdit.getSelection().x) + 1;
							}
						}
					});
					noSelectSql += errorSql;
					logs += Messages.runError + e.getErrorCode()
							+ CommonTool.getLineSeparator() + line
							+ Messages.errWhere + CommonTool.getLineSeparator()
							+ Messages.errorHead + e.getMessage();
				}
			} finally {
				final String logsBak = logs;
				final String noSelectSqlBak = noSelectSql;
				final int cntResultsBak = i;
				final boolean hasModifyQueryBak = hasModifyQuery;
				final boolean isIsolationHigherBak = isIsolationHigher;
				Display.getDefault().syncExec(new Runnable() {
					public void run() {
						CTabFolder tabMiddle = combinedQueryComposite.getTabResultFolderMiddle();
						if (tabMiddle != null && !tabMiddle.isDisposed()) {
							while (tabMiddle.getItemCount() > 0) {
								if (!tabMiddle.getItem(0).getControl().isDisposed())
									tabMiddle.getItem(0).getControl().dispose();
								tabMiddle.getItem(0).dispose();
							}
							if (cntResultsBak < 1
									&& logsBak.trim().length() <= 0)
								combinedQueryComposite.makeEmptyResult();
							else {
								if (logsBak.trim().length() > 0) {
									combinedQueryComposite.makeLogResult(
											noSelectSqlBak, logsBak);
								}
								for (int j = 0; j < curResult.size(); j++) {
									combinedQueryComposite.makeResult((QueryExecuter) curResult.get(j));
								}
							}

							if (!hasModifyQueryBak && !isIsolationHigherBak) {
								try {
									if (queryConn != null
											&& !queryConn.isClosed())
										queryConn.commit();
								} catch (SQLException e) {
									logger.error(e);
								}
								setActive(false);
							} else
								setActive(true);
							runItem.setEnabled(true);
							if (QueryOptions.getQueryPlan(getSelectedServer() != null ? getSelectedServer().getServerInfo()
									: null))
								queryPlanItem.setEnabled(true);
							if (tabMiddle.getItemCount() > 0) {
								tabMiddle.setSelection(tabMiddle.getItemCount() - 1);
							}
						}
					}
				});
				QueryUtil.freeQuery(stmt, rs);
			}

		}

		private Vector<String> queriesToQuery(String queries) {
			char[] buffer = queries.toCharArray();
			boolean sglQuote = false;
			boolean dblQuote = false;
			boolean isLineComment = false;
			boolean isBlockComment = false;
			char prevChar = '\0';
			Vector<String> qVector = new Vector<String>();
			int start = 0;
			int end = 0;

			for (int i = 0; i < buffer.length; i++) {
				if (buffer[i] == '\'' && !dblQuote)
					sglQuote = !sglQuote;
				if (buffer[i] == '"' && !sglQuote)
					dblQuote = !dblQuote;

				if (!dblQuote && !sglQuote) {
					if (!isLineComment) {
						if (prevChar == '-') {
							if (buffer[i] == '-' && !isBlockComment) {
								isLineComment = true;
							}
						} else if (prevChar == '/') {
							if (buffer[i] == '/' && !isBlockComment) {
								isLineComment = true;
							}
						}
					} else if (buffer[i] == '\n') {
						isLineComment = false;
					}

					if (!isBlockComment) {
						if (prevChar == '/') {
							if (buffer[i] == '*' && !isLineComment) {
								isBlockComment = true;
							}
						}
					} else {
						if (prevChar == '*') {
							if (buffer[i] == '/') {
								isBlockComment = false;
							}
						}
					}
				}

				prevChar = buffer[i];

				if (!isLineComment && !isBlockComment && !dblQuote && !sglQuote
						&& buffer[i] == ';') {
					start = end;
					end = i + 1;
					String aQuery = queries.substring(start, end).trim();

					if (isNotEmptyQuery(aQuery))
						qVector.addElement(aQuery);
				}
			}
			if (end < queries.length() - 1) {
				String aQuery = queries.substring(end, queries.length()).trim();

				if (isNotEmptyQuery(aQuery))
					qVector.addElement(aQuery);
			}

			return qVector;
		}

		private boolean isNotEmptyQuery(String query) {
			String[] queryOneLine = query.split("\n");
			String tempQuery = "";
			boolean skipLine = false;
			boolean inComment = false;

			for (int j = 0; j < queryOneLine.length; j++) {
				queryOneLine[j] = queryOneLine[j].trim();
				int position = queryOneLine[j].length();
				if (queryOneLine[j].indexOf("--") > -1)
					position = Math.min(position, queryOneLine[j].indexOf("--"));
				if (queryOneLine[j].indexOf("/*") > -1) {
					position = Math.min(position, queryOneLine[j].indexOf("/*"));
					inComment = true;
				}
				if (queryOneLine[j].indexOf("//") > -1)
					position = Math.min(position, queryOneLine[j].indexOf("//"));
				queryOneLine[j] = queryOneLine[j].substring(0, position);
				if (queryOneLine[j].indexOf("*/") > -1) {
					queryOneLine[j] = queryOneLine[j].substring(queryOneLine[j].indexOf("*/") + 2);
					inComment = false;
					skipLine = false;
				}

				if (!skipLine) {
					tempQuery += queryOneLine[j];
				}
				if (inComment)
					skipLine = true;
			}
			if (tempQuery.length() > 0 && tempQuery.compareTo(";") != 0)
				return true;
			else
				return false;
		}

	}

	/**
	 * Decide transaction close using by connection's isolation level for Select
	 * query.
	 * 
	 * @param conn isolation level validation connection
	 * @param isActive Transaction is exist? (if transaction is exist, return
	 *        value is always true.)
	 * @return boolean
	 *         <ul>
	 *         <li>true: keep transaction </li>
	 *         <li>false: close transaction</li>
	 *         </ul>
	 */
	public boolean isIsolationHigherThanRepeatableRead(Connection conn,
			boolean isActive) {
		try {
			if (isActive)
				return true;

			switch (conn.getTransactionIsolation()) {
			case Connection.TRANSACTION_NONE:
			case Connection.TRANSACTION_READ_COMMITTED:
			case Connection.TRANSACTION_READ_UNCOMMITTED:
			case CUBRIDConnection.TRAN_REP_CLASS_COMMIT_INSTANCE:
			case CUBRIDConnection.TRAN_REP_CLASS_UNCOMMIT_INSTANCE:
				return false;
			case Connection.TRANSACTION_REPEATABLE_READ:
			case Connection.TRANSACTION_SERIALIZABLE:
				return true;
			default:
				return true;
			}
		} catch (final SQLException e) {
			Display.getDefault().syncExec(new Runnable() {
				public void run() {
					CommonTool.openErrorBox(
							QueryEditorPart.this.getEditorSite().getShell(),
							e.getErrorCode() + CommonTool.getLineSeparator()
									+ e.getMessage());
				}
			});
			logger.error(e);
			return false;
		}
	}

	/**
	 * get the sql string on editor
	 * 
	 * @return
	 */
	public String getSelectText() {
		return combinedQueryComposite.getQuerySourceView().getText().getSelectionText();
	}

	/**
	 * set sql string to editor
	 * 
	 * @param contents
	 */
	public void setSelectText(String contents) {
		StyledText text = combinedQueryComposite.getQuerySourceView().getText();
		Point range = text.getSelectionRange();
		text.replaceTextRange(range.x, range.y, contents);
		text.setSelection(range.x + contents.length());
	}

	/**
	 * delete the selected text
	 */
	public void deleteSelectedText() {
		setSelectText("");

	}

	/**
	 * get the run item on toolbar
	 * 
	 * @return
	 */
	public ToolItem getRunItem() {
		return runItem;
	}

	public ToolItem getRunPlanItem() {
		return this.queryPlanItem;
	}

	/**
	 * get query connection
	 * 
	 * @return
	 */
	public Connection getQueryConn() {
		return queryConn;
	}

	/**
	 * set query connection
	 * 
	 * @param queryConn
	 */
	public void setQueryConn(Connection queryConn) {
		this.queryConn = queryConn;
	}

	/**
	 * when query options change, refresh
	 */
	public void refreshQueryOptions() {
		ServerInfo serverInfo = getSelectedServer() != null ? getSelectedServer().getServerInfo()
				: null;
		boolean enableQueryPlan = QueryOptions.getQueryPlan(serverInfo);
		boolean autoCommitted = QueryOptions.getAutoCommit(serverInfo);
		autoCommitItem.setSelection(autoCommitted);
		queryPlanItem.setEnabled(enableQueryPlan);
	}

	/**
	 * when navigator node change ,refresh the database list on query editor
	 */
	public void nodeChanged(CubridNodeChangedEvent e) {
		ICubridNode cubridNode = e.getCubridNode();
		if (cubridNode == null) {
			return;
		}
		CubridNodeType type = cubridNode.getType();
		if (type != CubridNodeType.SERVER
				&& type != CubridNodeType.DATABASE_FOLDER
				&& type != CubridNodeType.DATABASE) {
			return;
		}
		qeToolBar.refresh();
	}

	/**
	 * format the sql script
	 */
	public void format() {
		combinedQueryComposite.getQuerySourceView().format();
	}

	/**
	 * jdbc connection commit transaction
	 */
	public void commit() {
		try {
			queryConn.commit();

		} catch (SQLException ex) {
			CommonTool.openErrorBox(ex.getErrorCode()
					+ CommonTool.getLineSeparator() + ex.getMessage());
			commitItem.setEnabled(false);
			rollbackItem.setEnabled(false);
			logger.error(ex);
		} finally {
			setActive(false);
		}
	}

	/**
	 * jdbc connection roll back transaction
	 */
	public void rollback() {
		try {
			queryConn.rollback();
		} catch (SQLException ex) {
			CommonTool.openErrorBox(ex.getErrorCode()
					+ CommonTool.getLineSeparator() + ex.getMessage());
			commitItem.setEnabled(false);
			rollbackItem.setEnabled(false);
			logger.error(ex);
		} finally {
			setActive(false);
		}
	}

	public StyledText getSqlTextEditor() {
		return combinedQueryComposite.getQuerySourceView().getText();
	}

	public Table getResultTable() {
		return combinedQueryComposite.getResultTable();
	}

	public String getCurrentSchemaName() {
		return this.currentSchemaName;
	}

	public void setCurrentSchemaName(String schemaName) {
		this.currentSchemaName = schemaName;
	}

	public DragController getDragController() {
		return dragController;
	}

}
