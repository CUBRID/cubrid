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

package cubridmanager.query.view;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.text.NumberFormat;
import java.util.Vector;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.ToolBarManager;
import org.eclipse.jface.text.BadLocationException;
import org.eclipse.jface.text.DefaultUndoManager;
import org.eclipse.jface.text.FindReplaceDocumentAdapter;
import org.eclipse.jface.text.IDocument;
import org.eclipse.jface.text.IDocumentPartitioner;
import org.eclipse.jface.text.IRegion;
import org.eclipse.jface.text.IUndoManager;
import org.eclipse.jface.text.contentassist.ContentAssistant;
import org.eclipse.jface.text.rules.DefaultPartitioner;
import org.eclipse.jface.text.source.CompositeRuler;
import org.eclipse.jface.text.source.LineNumberRulerColumn;
import org.eclipse.jface.text.source.SourceViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.SashForm;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.custom.VerifyKeyListener;
import org.eclipse.swt.custom.ViewForm;
import org.eclipse.swt.dnd.DND;
import org.eclipse.swt.dnd.DropTarget;
import org.eclipse.swt.dnd.DropTargetAdapter;
import org.eclipse.swt.dnd.DropTargetEvent;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.events.FocusListener;
import org.eclipse.swt.events.VerifyEvent;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.FontData;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.MessageBox;
import org.eclipse.swt.widgets.TabFolder;
import org.eclipse.swt.widgets.TabItem;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.ToolBar;
import org.eclipse.swt.widgets.ToolItem;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.part.ViewPart;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionKey;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDKeyTable;
import cubrid.jdbc.driver.CUBRIDPreparedStatement;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.driver.CUBRIDStatement;
import cubrid.jdbc.jci.CUBRIDCommandType;
import cubrid.sql.CUBRIDOID;
import cubridmanager.Application;
import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.CommonTool;
import cubridmanager.CubridmanagerPlugin;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.query.ColorManager;
import cubridmanager.query.CubridKeyWordContentAssistProcessor;
import cubridmanager.query.PersistentDocument;
import cubridmanager.query.QueryCodeScanner;
import cubridmanager.query.QueryEditorSourceViewerConfiguration;
import cubridmanager.query.QueryPartitionScanner;
import cubridmanager.query.QuerySyntax;
import cubridmanager.query.QueryUtil;
import cubridmanager.query.StructQueryPlan;
import cubridmanager.query.WordTracker;
import cubridmanager.query.dialog.QueryFindDialog;
import cubridmanager.query.dialog.QueryPlan;

public class QueryEditor extends ViewPart {
	public static final String STR_NULL = "NULL";
	public static final String ID = "workview.QueryEditor";
	// TODO Needs to bewhatever is mentioned in plugin.xml
	public final int recordLimit = MainRegistry.queryEditorOption.recordlimit;
	private String EditorSequence = null;
	private String EditorCommand = null;
	private Composite top = null;
	private SashForm sashTop = null;
	private SashForm sashBottom = null;
	private Composite cmpHead = null;
	private TabFolder tabMiddle = null;
	private ToolBar toolBar1 = null;
	private ToolBar toolBar2 = null;
	private static QueryEditor APP;
	private QueryEditor qe;
	private StyledText txaEdit = null;
	private ToolItem itemCommit;
	private ToolItem itemRollback;
	private ToolItem itemRun;
	private CUBRIDPreparedStatement stmt = null;
	private CUBRIDResultSet rs = null;
	private boolean hasChanged = false;
	private boolean isClosing = false;
	private long endTimestamp = 0;
	private long execEndTime = 0;
	public Font font = null;
	public int fontColorRed = 0;
	public int fontColorGreen = 0;
	public int fontColorBlue = 0;
	public ToolItem itemQueryPlan = null;
	public SQLException execException = null;
	private Vector curResult = new Vector();
	private TabItem tabResult = null;
	private Table tblResult = null;
	private TabItem logTabResult = null;
	private File file = null;
	private String fileName = null;
	private Connection conn;
	private boolean isAutocommit = MainRegistry.queryEditorOption.autocommit;
	// private ToolItem itemPaste = null;
	// private ToolItem itemCut = null;
	private String curFindStr = null;
	public boolean isCurWrap = true;
	public boolean isCurCaseSensitive = false;
	public boolean isCurUp = false;
	private boolean isWholeWord;
	private int threadExecResult;
	private SourceViewer textViewer;
	private ColorManager colorManager;
	private QueryCodeScanner codeScanner;
	private Canvas textCanvas;
	private PersistentDocument document;
	private QueryPartitionScanner scanner;
	private IUndoManager undoManager;
	private FindReplaceDocumentAdapter frda;
	private boolean isActive = false;
	private boolean OnlyQueryPlan = false;
	private boolean RunQueryAction = false;
	public static final String QUERY_PARTITIONING = "query_partitioning";
	private CUBRIDConnectionKey conKey = null;
	public boolean isQueryPlanDlgOpen = false;
	private Vector vectorQueryPlans = new Vector();
	private boolean useCompletions = true;
	private Job runJob = null;
	private Text logSqlText = null;
	private StyledText logMessagesArea = null;
	private int line = 0;
	private Color lineNumColor = null;

	public QueryEditor() throws SQLException, ClassNotFoundException {
		super();

		if (!MainRegistry.queryEditorOption.fontString.equals("")) {
			FontData fontData = new FontData(
					MainRegistry.queryEditorOption.fontString);
			if (fontData != null) {
				font = new Font(Application.mainwindow.getShell().getDisplay(),
						fontData);
				fontColorRed = MainRegistry.queryEditorOption.fontColorRed;
				fontColorGreen = MainRegistry.queryEditorOption.fontColorGreen;
				fontColorBlue = MainRegistry.queryEditorOption.fontColorBlue;
			}
		}

		APP = this;
		qe = this;
		setUpDocument();

		String conStr = WorkView.connector.getConnectionStr();
		if (WorkView.EditorSequence != null)
			EditorSequence = new String(WorkView.EditorSequence);
		if (WorkView.EditorCommand != null)
			EditorCommand = new String(WorkView.EditorCommand);

		setConnection(conStr);
	}

	/**
	 * Sets up the document
	 */
	protected void setUpDocument() {
		colorManager = new ColorManager();
		codeScanner = new QueryCodeScanner();
		undoManager = new DefaultUndoManager(50);

		// Create the document
		document = new PersistentDocument();

		// Create the partition scanner
		scanner = new QueryPartitionScanner();
		frda = new FindReplaceDocumentAdapter(document);

		// Create the partitioner
		IDocumentPartitioner partitioner = new DefaultPartitioner(scanner,
				QueryPartitionScanner.TYPES);

		// Connect the partitioner and document
		document.setDocumentPartitioner(QUERY_PARTITIONING, partitioner);
		partitioner.connect(document);
	}

	public void doSave(IProgressMonitor monitor) {
		// TODO Auto-generated method stub

	}

	public void doSaveAs() {
		// TODO Auto-generated method stub

	}

	public void init(IEditorSite site, IEditorInput input)
			throws PartInitException {
		// TODO Auto-generated method stub
	}

	public boolean isDirty() {
		// TODO Auto-generated method stub
		return true;
	}

	public boolean isSaveAsAllowed() {
		// TODO Auto-generated method stub
		return false;
	}

	public void createPartControl(Composite parent) {
		top = new Composite(parent, SWT.NONE);
		top.setLayout(new FillLayout());
		createSashTop();
		this.setTitleToolTip(this.getTitle().concat(" - ").concat(
				WorkView.connector.getDBName()).concat("(").concat(
				WorkView.connector.getUserName()).concat(")"));
		this.setPartName(this.getTitle() + " - "
				+ WorkView.connector.getDBName());

		parent.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				if (conn != null) {
					if (!isAutocommit && isActive) {
						MessageBox mb = new MessageBox(Application.mainwindow
								.getShell(), SWT.ICON_QUESTION | SWT.YES
								| SWT.NO);
						mb.setText(Messages.getString("QEDIT.QUESTIONCOMMIT"));
						mb.setMessage(Messages
								.getString("QEDIT.COMMITBEFORECLOSE"));
						int state = mb.open();
						try {
							if (state == SWT.YES)
								conn.commit();
							else
								conn.rollback();
						} catch (Exception ee) {
						}
					}
				}
			}
		});

		if (EditorCommand != null) {
			String[] editorCommand = EditorCommand.split(":");
			try {
				if (editorCommand[1].equals("SELECTALL")) {
					setPartName(Messages.getString("TOOL.TABLESELECTALLACTION")
							+ " " + editorCommand[0]);
					conn
							.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
					((CUBRIDConnection) conn).setLockTimeout(1);
					String sql = "select * from \"" + editorCommand[0] + "\";";
					txaEdit.setText(sql);
					runQuery(sql);
				} else if (editorCommand[1].equals("SCRIPTRUN")) {
					setPartName(Messages.getString("TITLE.SCRIPTRUN"));
					conn
							.setTransactionIsolation(Connection.TRANSACTION_READ_UNCOMMITTED);
					((CUBRIDConnection) conn).setLockTimeout(1);
				}
			} catch (SQLException e1) {
				CommonTool.debugPrint(e1);
			}
		}

		// TODO : File save routine when closing windows.
		/*
		 * parent.addDisposeListener(new DisposeListener() { public void
		 * widgetDisposed(DisposeEvent e) { if (!confirmExit()) { isClosing =
		 * false; return; } if (isClosing) qe.dispose(); } });
		 */
	}

	public void dispose() {
		super.dispose();
		try {
			if (runJob != null) {
				runJob.cancel();
				runJob = null;
			}
			if (conn != null)
				conn.close();
			clearResult();
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		} finally {
			conn = null;
			if (font != null)
				font.dispose();
			if (document != null) {
				document.clear();
				document = null;
			}
			if (lineNumColor != null) {
				lineNumColor.dispose();
			}
		}
	}

	public void setFocus() {
		txaEdit.setFocus();
		ApplicationActionBarAdvisor.startServerActionOnToolbar
				.setEnabled(false);
		ApplicationActionBarAdvisor.stopServerActionOnToolbar.setEnabled(false);
	}

	/**
	 * This method initializes sashTop
	 * 
	 */
	private void createSashTop() {
		sashTop = new SashForm(top, SWT.VERTICAL);
		createCmpHead();
		createTabMiddle();
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
		gridLayout.numColumns = 2;
		cmpHead.setLayout(gridLayout);
		createToolBar1();
		createToolBar2();

		CompositeRuler ruler = new CompositeRuler();
		LineNumberRulerColumn lineCol = new LineNumberRulerColumn();
		if (lineNumColor == null) {
			lineNumColor = new Color(Display.getCurrent(), 236, 233, 216);
		}
		lineCol.setBackground(lineNumColor);
		ruler.addDecorator(0, lineCol);

		textViewer = new SourceViewer(cmpHead, ruler, SWT.V_SCROLL
				| SWT.H_SCROLL);
		textViewer.configure(new QueryEditorSourceViewerConfiguration());
		textViewer.setDocument(document);

		undoManager.connect(textViewer);

		final ContentAssistant assistant = new ContentAssistant();
		final WordTracker tracker = new WordTracker(QuerySyntax.KEYWORDS);
		assistant.setContentAssistProcessor(
				new CubridKeyWordContentAssistProcessor(tracker),
				IDocument.DEFAULT_CONTENT_TYPE);
		assistant.install(textViewer);

		textCanvas = (Canvas) textViewer.getControl();
		GridData gridData = new GridData();
		gridData.horizontalAlignment = SWT.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.verticalAlignment = SWT.FILL;
		gridData.grabExcessVerticalSpace = true;
		gridData.horizontalSpan = 2;
		textCanvas.setLayoutData(gridData);

		txaEdit = (StyledText) textViewer.getTextWidget();
		txaEdit.setIndent(6);
		if (font != null)
			txaEdit.setFont(font);
		
		MenuManager menuManager = new MenuManager();
		menuManager.setRemoveAllWhenShown(true);
		menuManager.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				if (ApplicationActionBarAdvisor.copyClipboardAction != null) {
					ApplicationActionBarAdvisor.copyClipboardAction
							.setEnabled(true);
					manager
							.add(ApplicationActionBarAdvisor.copyClipboardAction);
				}
				if (ApplicationActionBarAdvisor.cutAction != null) {
					ApplicationActionBarAdvisor.cutAction.setEnabled(true);
					manager.add(ApplicationActionBarAdvisor.cutAction);
				}
				if (ApplicationActionBarAdvisor.pasteAction != null) {
					ApplicationActionBarAdvisor.pasteAction.setEnabled(true);
					manager.add(ApplicationActionBarAdvisor.pasteAction);
				}
			}
		});
		Menu contextMenu = menuManager.createContextMenu(txaEdit);
		txaEdit.setMenu(contextMenu);
		
		txaEdit.addModifyListener(new org.eclipse.swt.events.ModifyListener() {
			public void modifyText(org.eclipse.swt.events.ModifyEvent e) {
				if (!hasChanged) {
					hasChanged = true;
					setPartName(qe.getTitle() + " *");
				}
			}
		});
		txaEdit.addVerifyKeyListener(new VerifyKeyListener() {
			public void verifyKey(VerifyEvent e) {
				if ((e.stateMask & SWT.SHIFT) != 0) {
					if (e.keyCode == SWT.TAB)
						e.doit = false;
				} else if (e.keyCode == SWT.TAB) {
					e.doit = false;
				} else {
					e.doit = true;
				}
			}
		});
		txaEdit.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyPressed(org.eclipse.swt.events.KeyEvent e) {

				boolean Run_Query = false;

				if (e.keyCode == SWT.F5) // Run
				{
					RunAction(e);
					Run_Query = true;
				} else if (e.keyCode == SWT.F3) {
					if (txaEdit.getSelectionCount() > 0)
						curFindStr = txaEdit.getSelectionText();
					if (curFindStr == null)
						return;
					if ((e.stateMask & SWT.SHIFT) != 0) { // SHIFT F3 event
						if (!txtFind(curFindStr, -1, isCurWrap, !isCurUp,
								isCurCaseSensitive, isWholeWord))
							CommonTool.InformationBox(Messages
									.getString("TOOLTIP.QEDIT.FIND"), Messages
									.getString("QEDIT.NOTFOUND"));
					} else { // F3 event
						if (!txtFind(curFindStr, -1, isCurWrap, isCurUp,
								isCurCaseSensitive, isWholeWord))
							CommonTool.InformationBox(Messages
									.getString("TOOLTIP.QEDIT.FIND"), Messages
									.getString("QEDIT.NOTFOUND"));
					}
				} else if ((e.stateMask & SWT.CTRL) != 0) {
					if (e.keyCode == '/')
						inputComment(false);
					else if (e.keyCode == 'z') {
						e.doit = false;
						undo();
					} else if (e.keyCode == 'y')
						redo();
					else if (e.keyCode == 'f')
						find();
					else if (e.keyCode == 'h')
						replace();
					else if (e.keyCode == 'e' || e.keyCode == SWT.CR) {
						RunAction(e);
						Run_Query = true;
					} else if (e.keyCode == 'l') {
						if (!isQueryPlanDlgOpen) {
							if (!RunQueryAction) {
								OnlyQueryPlan = true;
								RunAction(e);
							}
							isQueryPlanDlgOpen = true;
							QueryPlan dlg = new QueryPlan(qe,
									Application.mainwindow.getShell());
							dlg.open(vectorQueryPlans);
						}
						Run_Query = true;
					}
				} else if ((e.stateMask & SWT.ALT) != 0) {
					if (e.keyCode == SWT.BS) {
						// CommonTool.MsgBox(Application.mainwindow.getShell(),"alt","456");
						// RollBackAction();
					}
				} else if ((e.stateMask & SWT.SHIFT) != 0) {
					if (e.keyCode == SWT.TAB)
						unindent();
				} else if (e.keyCode == SWT.TAB) {
					indent();
				}

				if ((e.character >= 'A' && e.character <= 'Z')
						|| (e.character >= 'a' && e.character <= 'z')) {
					if (useCompletions)
						assistant.showPossibleCompletions();
					useCompletions = false;
				} else if (e.character == ' ' || e.character == '\t'
						|| e.keyCode == SWT.CR
						|| (txaEdit.getText().trim().length() < 1))
					useCompletions = true;
				else
					useCompletions = false;

				if (Run_Query)
					RunQueryAction = true;
				else
					RunQueryAction = false;
			}
		});

		txaEdit.addFocusListener(new FocusListener() {
			public void focusGained(FocusEvent e) {
				// itemCut.setEnabled(true);
				// itemPaste.setEnabled(true);
				ApplicationActionBarAdvisor.cutAction.setEnabled(true);
				ApplicationActionBarAdvisor.pasteAction.setEnabled(true);
			}

			public void focusLost(FocusEvent e) {
				// itemCut.setEnabled(false);
				// itemPaste.setEnabled(false);
				ApplicationActionBarAdvisor.cutAction.setEnabled(false);
				ApplicationActionBarAdvisor.pasteAction.setEnabled(false);
			}
		});

		DropTarget dt = new DropTarget(txaEdit, DND.DROP_MOVE);
		dt.setTransfer(new Transfer[] { TextTransfer.getInstance() });
		dt.addDropListener(new DropTargetAdapter() {
			public void drop(DropTargetEvent event) {
				if (!event.data.equals(" "))
					txaEdit.append(createDropEventQuery(event.data.toString())
							+ MainConstants.NEW_LINE);
			}
		});
	}

	/**
	 * This method initializes tabMiddle
	 * 
	 */
	private void createTabMiddle() {
		tabMiddle = new TabFolder(sashTop, SWT.NONE);
		sashBottom = new SashForm(tabMiddle, SWT.VERTICAL);
		sashBottom.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		createTable();
		createCmpTail();
		sashBottom.setWeights(new int[] { 75, 25 });
		tabResult = new TabItem(tabMiddle, SWT.NONE);
		tabResult.setText(Messages.getString("QEDIT.RESULT"));
		tabResult.setControl(sashBottom);
		TableColumn column = new TableColumn(tblResult, SWT.NONE);
		column.setWidth(60);

		tabMiddle
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (curResult.size() > 0) {
							QueryExecuter qe = null;
							if (logTabResult != null) {
								if (tabMiddle.getSelectionIndex() == 0) {
									tabResult = logTabResult;
									tblResult = null;
									return;
								} else {
									qe = (QueryExecuter) curResult
											.get(tabMiddle.getSelectionIndex() - 1);
								}
							} else {
								qe = (QueryExecuter) curResult.get(tabMiddle
										.getSelectionIndex());
							}
							tabResult = tabMiddle.getItem(tabMiddle
									.getSelectionIndex());
							tblResult = qe.tblResult;
						} else if (logTabResult != null) {
							tabResult = logTabResult;
							tblResult = null;
						}
					}
				});
		setDropTraget(tblResult);
	}

	/**
	 * set drop target for table
	 * 
	 * @param table
	 */
	private void setDropTraget(Table table) {
		DropTarget dt = new DropTarget(table, DND.DROP_MOVE);
		dt.setTransfer(new Transfer[] { TextTransfer.getInstance() });
		dt.addDropListener(new DropTargetAdapter() {
			public void drop(DropTargetEvent event) {
				if (!event.data.equals(" "))
					// Set the buttons text to be the text being dropped
					runQuery(createDropEventQuery(event.data.toString()));
			}
		});
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
		if (font != null)
			txaEdit.setFont(font);
		txaRunQuery.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		StyledText txaMessagesArea = new StyledText(sashTail, SWT.MULTI
				| SWT.WRAP | SWT.V_SCROLL | SWT.READ_ONLY);
	}

	/**
	 * This method initializes toolBar1
	 * 
	 */
	private void createToolBar1() {
		toolBar1 = new ToolBar(cmpHead, SWT.FLAT);
		toolBar1.setLayoutData(new GridData());

		ToolItem itemOpen = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemOpen.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_open.png"));
		itemOpen.setToolTipText(Messages.getString("QEDIT.OPEN"));
		itemOpen
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						openFile();
					}
				});

		ToolItem itemSave = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemSave.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_save.png"));
		itemSave.setToolTipText(Messages.getString("QEDIT.SAVE"));
		itemSave
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						saveFile();
					}
				});

		ToolItem itemSaveAs = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemSaveAs.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_saveas.png"));
		itemSaveAs.setToolTipText(Messages.getString("QEDIT.SAVEAS"));
		itemSaveAs
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						saveAsFile();
					}
				});

		new ToolItem(toolBar1, SWT.SEPARATOR);

		itemRun = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemRun.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_go.png"));
		itemRun.setToolTipText(Messages.getString("QEDIT.RUN")
				+ "(F5,Ctrl+Enter)");
		itemRun
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						RunAction(e);
					}
				});

		// ToolItem itemScript = new ToolItem(toolBar1, SWT.PUSH);
		// //TODO: Image
		// itemScript.setImage(CubridmanagerPlugin
		// .getImage("/image/QueryEditor/qe_script_go.png"));
		// itemScript.setToolTipText(Messages.getString("QEDIT.SCRIPTRUN"));
		// itemScript.addSelectionListener(new
		// org.eclipse.swt.events.SelectionAdapter() {
		// public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
		// scriptRun();
		// }
		// });

		// ToolItem itemImport = new ToolItem(toolBar1, SWT.PUSH);
		// //TODO: Image
		// itemImport.setImage(CubridmanagerPlugin
		// .getImage("/image/QueryEditor/qe_import.png"));
		// itemImport.setToolTipText(Messages.getString("QEDIT.IMPORT"));
		// itemImport.addSelectionListener(new
		// org.eclipse.swt.events.SelectionAdapter() {
		// public void widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
		// importFile();
		// }
		// });

		new ToolItem(toolBar1, SWT.SEPARATOR);

		itemCommit = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemCommit.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_commit_enable.png"));
		itemCommit.setDisabledImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_commit_disable.png"));
		itemCommit.setToolTipText(Messages.getString("QEDIT.COMMIT"));
		itemCommit.setEnabled(false);
		itemCommit
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							conn.commit();
							setActive(false);
						} catch (SQLException ex) {
							CommonTool.ErrorBox(ex.getErrorCode()
									+ MainConstants.NEW_LINE + ex.getMessage());
							itemCommit.setEnabled(false);
							itemRollback.setEnabled(false);
							CommonTool.debugPrint(ex);
						}
					}
				});

		itemRollback = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemRollback.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_rollback_enable.png"));
		itemRollback.setDisabledImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_rollback_disable.png"));
		itemRollback.setToolTipText(Messages.getString("QEDIT.ROLLBACK"));
		itemRollback.setEnabled(false);
		itemRollback
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						try {
							conn.rollback();
							setActive(false);
						} catch (SQLException ex) {
							CommonTool.ErrorBox(ex.getErrorCode()
									+ MainConstants.NEW_LINE + ex.getMessage());
							itemCommit.setEnabled(false);
							itemRollback.setEnabled(false);
							CommonTool.debugPrint(ex);
						}
					}
				});

		final ToolItem itemAutocommit = new ToolItem(toolBar1, SWT.CHECK);
		// TODO: Image
		itemAutocommit.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_autocommit_enable.png"));
		itemAutocommit.setToolTipText(Messages.getString("QEDIT.AUTOCOMMIT"));
		itemAutocommit.setSelection(isAutocommit);
		itemAutocommit
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (checkActive())
							return;
						setAutocommit(itemAutocommit.getSelection());
					}
				});

		new ToolItem(toolBar1, SWT.SEPARATOR);

		/*
		 * ToolItem itemSchemaNavi = new ToolItem(toolBar1, SWT.PUSH);
		 * itemSchemaNavi.setWidth(25); //TODO: Image
		 * itemSchemaNavi.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_schema_navi.png"));
		 * itemSchemaNavi.setToolTipText(Messages.getString("QEDIT.SCHEMANAVIGATOR"));
		 * itemSchemaNavi.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { try { new
		 * SchemaNavigator(conn); } catch (SQLException e1) {
		 * msgQueryEditor(Messages.getString("QEDIT.ERROR"),
		 * e1.getErrorCode()+NEW_LINE+e1.getMessage(), true);
		 * CommonTool.debugPrint(e1); } } });
		 * 
		 * ToolItem itemOIDNavi = new ToolItem(toolBar1, SWT.PUSH); //TODO:
		 * Image itemOIDNavi.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_oid_navi.png"));
		 * itemOIDNavi.setToolTipText(Messages.getString("QEDIT.OIDNAVIGATOR"));
		 * itemOIDNavi.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { // TODO new
		 * OIDNavigator(conn); } });
		 * 
		 * new ToolItem(toolBar1, SWT.SEPARATOR);
		 * 
		 * final ToolItem itemQueryPlanEnable = new ToolItem(toolBar1,
		 * SWT.CHECK); //TODO: Image
		 * itemQueryPlanEnable.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_queryplan_enable.png"));
		 * itemQueryPlanEnable.setToolTipText(Messages.getString("TOOLTIP.QUERYPLANENABLE"));
		 * itemQueryPlanEnable.setSelection(doesGetQueryPlan); // Defalut value
		 * is false itemQueryPlanEnable.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) {
		 * doesGetQueryPlan = itemQueryPlanEnable.getSelection(); } });
		 */

		itemQueryPlan = new ToolItem(toolBar1, SWT.PUSH);
		// TODO: Image
		itemQueryPlan.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_queryplan.png"));
		itemQueryPlan.setDisabledImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/disable/qe_queryplan.png"));
		itemQueryPlan.setToolTipText(Messages.getString("TOOLTIP.QUERYPLAN")
				+ "(Ctrl+L)");
		itemQueryPlan
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						if (!isQueryPlanDlgOpen) {
							OnlyQueryPlan = true;
							RunAction(e);

							isQueryPlanDlgOpen = true;
							QueryPlan dlg = new QueryPlan(qe,
									Application.mainwindow.getShell());
							dlg.open(vectorQueryPlans);
						}
					}
				});

		itemQueryPlan.setEnabled(MainRegistry.queryEditorOption.getqueryplan);

		new ToolItem(toolBar1, SWT.SEPARATOR);

		/*
		 * ToolItem itemQueryHistory = new ToolItem(toolBar1, SWT.PUSH);
		 * itemQueryHistory.setWidth(25); //TODO: Image
		 * itemQueryHistory.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_history.png"));
		 * itemQueryHistory.setToolTipText(Messages.getString("QEDIT.QUERYHISTORY"));
		 * itemQueryHistory.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { // TODO new
		 * QueryHistory(); } });
		 * 
		 * ToolItem itemPrevQuery = new ToolItem(toolBar1, SWT.PUSH);
		 * itemPrevQuery.setWidth(25); //TODO: Image
		 * itemPrevQuery.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/empty.png"));
		 * itemPrevQuery.setToolTipText(Messages.getString("QEDIT.PREVIOUSQUERY"));
		 * 
		 * ToolItem itemNextQuery = new ToolItem(toolBar1, SWT.PUSH);
		 * itemNextQuery.setWidth(25); //TODO: Image
		 * itemNextQuery.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/empty.png"));
		 * itemNextQuery.setToolTipText(Messages.getString("QEDIT.NEXTQUERY"));
		 */
	}

	/**
	 * This method initializes toolBar2
	 * 
	 */
	private void createToolBar2() {
		toolBar2 = new ToolBar(cmpHead, SWT.FLAT);
		toolBar2.setLayoutData(new GridData());

		ToolItem itemUndo = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemUndo.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_undo.png"));
		itemUndo.setToolTipText(Messages.getString("TOOLTIP.QEDIT.UNDO")
				+ "(Ctrl+Z)");
		itemUndo
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						undo();
					}
				});

		ToolItem itemRedo = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemRedo.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_redo.png"));
		itemRedo.setToolTipText(Messages.getString("TOOLTIP.QEDIT.REDO")
				+ "(Ctrl+Y)");
		itemRedo
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						redo();
					}
				});

		new ToolItem(toolBar2, SWT.SEPARATOR);
		/*
		 * ToolItem itemCopy = new ToolItem(toolBar2, SWT.PUSH); //TODO: Image
		 * itemCopy.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_copy.png"));
		 * itemCopy.setToolTipText(Messages.getString("QEDIT.COPY"));
		 * itemCopy.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { copy(); }
		 * });
		 * 
		 * itemCut = new ToolItem(toolBar2, SWT.PUSH); //TODO: Image
		 * itemCut.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_cut.png"));
		 * itemCut.setToolTipText(Messages.getString("QEDIT.CUT"));
		 * itemCut.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { cut(); }
		 * });
		 * 
		 * itemPaste = new ToolItem(toolBar2, SWT.PUSH); //TODO: Image
		 * itemPaste.setImage(CubridmanagerPlugin
		 * .getImage("/image/QueryEditor/qe_paste.png"));
		 * itemPaste.setToolTipText(Messages.getString("QEDIT.PASTE"));
		 * itemPaste.addSelectionListener(new
		 * org.eclipse.swt.events.SelectionAdapter() { public void
		 * widgetSelected(org.eclipse.swt.events.SelectionEvent e) { paste(); }
		 * });
		 * 
		 * new ToolItem(toolBar2, SWT.SEPARATOR);
		 */
		ToolItem itemFind = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemFind.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_find.png"));
		itemFind.setToolTipText(Messages.getString("TOOLTIP.QEDIT.FIND")
				+ "(Ctrl+F)");
		itemFind
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						find();
					}
				});

		ToolItem itemFindNext = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemFindNext.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_findnext.png"));
		itemFindNext.setToolTipText(Messages
				.getString("TOOLTIP.QEDIT.FINDNEXT")
				+ "(F3)");
		itemFindNext
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						findNext();
					}
				});

		ToolItem itemReplace = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemReplace.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_replace.png"));
		itemReplace.setToolTipText(Messages.getString("TOOLTIP.QEDIT.REPLACE"));
		itemReplace
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						replace();
					}
				});

		new ToolItem(toolBar2, SWT.SEPARATOR);

		ToolItem itemComment = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemComment.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_comment_input.png"));
		itemComment.setToolTipText(Messages.getString("TOOLTIP.QEDIT.COMMENT"));
		itemComment
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						comment();
					}
				});

		ToolItem itemUncomment = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemUncomment.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_comment_delete.png"));
		itemUncomment.setToolTipText(Messages
				.getString("TOOLTIP.QEDIT.UNCOMMENT"));
		itemUncomment
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						uncomment();
					}
				});

		new ToolItem(toolBar2, SWT.SEPARATOR);

		ToolItem itemUnindent = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemUnindent.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_indent_remove.png"));
		itemUnindent.setToolTipText(Messages
				.getString("TOOLTIP.QEDIT.UNINDENT"));
		itemUnindent
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						unindent();
					}
				});

		ToolItem itemIndent = new ToolItem(toolBar2, SWT.PUSH);
		// TODO: Image
		itemIndent.setImage(CubridmanagerPlugin
				.getImage("/image/QueryEditor/qe_indent.png"));
		itemIndent.setToolTipText(Messages.getString("TOOLTIP.QEDIT.INDENT"));
		itemIndent
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						indent();
					}
				});
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

	public void openFile() {
		FileDialog dialog = new FileDialog(Application.mainwindow.getShell(),
				SWT.OPEN | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		File curdir = new File(".");
		try {
			dialog.setFilterPath(curdir.getCanonicalPath());
		} catch (Exception e) {
			dialog.setFilterPath(".");
		}
		String result = dialog.open();
		if (result != null) {
			file = new File(result);
			try {
				BufferedReader br = new BufferedReader(new FileReader(file));
				StringBuffer buff = new StringBuffer();
				String line = br.readLine();
				while (line != null) {
					buff.append(line + MainConstants.NEW_LINE);
					line = br.readLine();
				}
				txaEdit.setText(buff.toString());
				br.close();
				fileName = dialog.getFileName();
				this.setPartName(WorkView.connector.getDBName() + " - "
						+ fileName);
				hasChanged = false;
			} catch (FileNotFoundException e1) {
				CommonTool.ErrorBox(e1.getMessage());
				CommonTool.debugPrint(e1);
			} catch (IOException e1) {
				CommonTool.ErrorBox(e1.getMessage());
				CommonTool.debugPrint(e1);
			}
		}
	}

	public void saveAsFile() {
		FileDialog dialog = new FileDialog(Application.mainwindow.getShell(),
				SWT.SAVE | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		String result = dialog.open();
		if (result != null) {
			file = new File(result);
			try {
				BufferedWriter bw = new BufferedWriter(new FileWriter(file));
				String text = txaEdit.getText();
				bw.write(text);
				bw.close();
				fileName = dialog.getFileName();
				this.setPartName(WorkView.connector.getDBName() + " - "
						+ fileName);
				hasChanged = false;
			} catch (FileNotFoundException e1) {
				CommonTool.ErrorBox(e1.getMessage());
				CommonTool.debugPrint(e1);
			} catch (IOException e1) {
				CommonTool.ErrorBox(e1.getMessage());
				CommonTool.debugPrint(e1);
			}
		}
	}

	public void saveFile() {
		if (file == null) {
			saveAsFile();
		} else {
			try {
				BufferedWriter bw = new BufferedWriter(new FileWriter(file));
				String text = txaEdit.getText();
				bw.write(text);
				bw.close();
				this.setPartName(WorkView.connector.getDBName() + " - "
						+ fileName);
				hasChanged = false;
			} catch (FileNotFoundException e1) {
				CommonTool.debugPrint(e1);
			} catch (IOException e1) {
				CommonTool.debugPrint(e1);
			}
		}
	}

	public void RunAction(org.eclipse.swt.events.TypedEvent e) {
		if ((long) (e.time & 0xFFFFFFFFL) < execEndTime) {
			return;
		}

		long startTime, endTime, timeInterval;
		startTime = System.currentTimeMillis();
		runQuery();
		endTime = System.currentTimeMillis();
		timeInterval = endTime - startTime;

		execEndTime = (long) (e.time & 0xFFFFFFFFL) + timeInterval;

		RunQueryAction = true;
	}

	public void RollBackAction() {
		try {
			conn.rollback();
			setActive(false);
		} catch (SQLException ex) {
			CommonTool.ErrorBox(ex.getErrorCode() + MainConstants.NEW_LINE
					+ ex.getMessage());
			itemCommit.setEnabled(false);
			itemRollback.setEnabled(false);
			CommonTool.debugPrint(ex);
		}
	}

	public void undo() {
		undoManager.undo();
	}

	public void redo() {
		undoManager.redo();
	}

	public void copy() {
		if (tblResult != null && tblResult.isFocusControl())
			QueryExecuter.tblItemCopy(tblResult.getSelection());
		else if (txaEdit.isFocusControl())
			txaEdit.copy();
	}

	public void cut() {
		if (txaEdit.isFocusControl())
			txaEdit.cut();
	}

	public void paste() {
		txaEdit.paste();
	}

	public void find() {
		openFindDlg(true);
	}

	public void findNext() {
		if (txaEdit.getSelectionCount() > 0)
			curFindStr = txaEdit.getSelectionText();
		if (curFindStr == null)
			return;
		if (!txtFind(curFindStr, -1, isCurWrap, isCurUp, isCurCaseSensitive,
				isWholeWord))
			CommonTool.InformationBox(Messages.getString("TOOLTIP.QEDIT.FIND"),
					Messages.getString("QEDIT.NOTFOUND"));

	}

	public void replace() {
		openFindDlg(false);
	}

	public void comment() {
		inputComment(true, true);
	}

	public void uncomment() {
		inputComment(true, false);
	}

	public void unindent() {
		removeTab();
	}

	public void indent() {
		inputTab();
	}

	public void scriptRun(String result) {
		if (result != null) {
			file = new File(result);
			try {
				BufferedReader br = new BufferedReader(new FileReader(file));
				StringBuffer buff = new StringBuffer();
				String line = br.readLine();
				while (line != null) {
					buff.append(line + MainConstants.NEW_LINE);
					line = br.readLine();
				}
				br.close();
				txaEdit.setText(buff.toString());
				fileName = file.getName();
				this.setPartName(Messages.getString("TITLE.SCRIPTRUN") + "- "
						+ file.getName());
				hasChanged = false;
				runQuery(buff.toString());
			} catch (FileNotFoundException e1) {
				CommonTool.ErrorBox(e1.getMessage());
				CommonTool.debugPrint(e1);
			} catch (IOException e1) {
				CommonTool.ErrorBox(e1.getMessage());
				CommonTool.debugPrint(e1);
			}
		}

	}

	public void scriptRun() {
		FileDialog dialog = new FileDialog(Application.mainwindow.getShell(),
				SWT.OPEN | SWT.APPLICATION_MODAL);
		dialog.setFilterExtensions(new String[] { "*.sql", "*.txt", "*.*" });
		dialog.setFilterNames(new String[] { "SQL File", "Text File", "All" });
		String result = dialog.open();

		scriptRun(result);
	}

	/*
	 * public void importFile() { if (checkActive()) return; try {
	 * conn.commit(); setActive(false); } catch (SQLException ex) {
	 * CommonTool.ErrorBox(ex.getErrorCode()+MainConstants.NEW_LINE+ex.getMessage());
	 * itemCommit.setEnabled(false); itemRollback.setEnabled(false);
	 * CommonTool.debugPrint(ex); return; } new Import(conn); }
	 */

	private boolean confirmExit() {
		if (hasChanged) {
			MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
					SWT.ICON_QUESTION | SWT.YES | SWT.NO | SWT.CANCEL);
			mb.setText(Messages.getString("QEDIT.FILESAVE"));
			mb.setMessage(Messages.getString("QEDIT.FILESAVEMSG2"));
			int state = mb.open();
			if (state == SWT.YES) {
				saveFile();
			} else if (state == SWT.CANCEL) {
				return false;
			}
		}
		isClosing = true;

		try {
			if (conn != null)
				conn.close();
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		} finally {
			conn = null;
		}
		return true;
	}

	private void clearResult() {
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

	private Vector queriesToQuery(String queries) {
		char[] buffer = queries.toCharArray();
		boolean sglQuote = false;
		boolean dblQuote = false;
		boolean isLineComment = false;
		boolean isBlockComment = false;
		char prevChar = '\0';
		Vector qVector = new Vector();
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
				queryOneLine[j] = queryOneLine[j].substring(queryOneLine[j]
						.indexOf("*/") + 2);
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

	private void setAutocommit(boolean autocommit) {
		isAutocommit = autocommit;
		setActive(false);
		try {
			conn.setAutoCommit(isAutocommit);
		} catch (SQLException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETAUTOCOMMITERR")
					+ MainConstants.NEW_LINE + e.getErrorCode()
					+ MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
		}
	}

	private void setConnection(String conStr) {
		try {
			Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
			conn = DriverManager.getConnection(conStr);
			if (MainRegistry.isProtegoBuild()) {
				conKey = ((CUBRIDConnection) conn)
						.Login(MainRegistry.UserSignedData);
				CUBRIDKeyTable.putValue(conKey);
			}

			conn.setAutoCommit(isAutocommit);

		} catch (SQLException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ MainConstants.NEW_LINE + e.getErrorCode()
					+ MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
		} catch (ClassNotFoundException e) {
			CommonTool.ErrorBox(Messages.getString("QEDIT.SETCONNERR")
					+ MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
		}
	}

	private void runQuery() {
		String queries = new String();
		if (txaEdit.getSelectionCount() > 0)
			queries = txaEdit.getSelectionText();
		else
			queries = txaEdit.getText();

		if (OnlyQueryPlan)
			runQuery_PlanOnly(queries);
		else
			runQuery(queries);
	}

	private void runQuery_PlanOnly(String queries) {
		itemRun.setEnabled(false);
		itemQueryPlan.setEnabled(false);
		Vector qVector = null;
		vectorQueryPlans.clear();
		CUBRIDStatement statement = null;

		qVector = queriesToQuery(queries);
		int i = 0;
		try {
			for (i = 0; i < qVector.size(); i++) {
				String sql = qVector.get(i).toString();
				statement = (CUBRIDStatement) conn.createStatement();
				vectorQueryPlans.add(new StructQueryPlan(sql, statement
						.getQueryplan(sql)));
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
					conn.rollback();
				} catch (SQLException e1) {
				}

			txtFind((String) qVector.get(i), 0, false, false, true, false);
			int line = txaEdit.getLineAtOffset(txaEdit.getSelection().x) + 1;

			errmsg += Messages.getString("QEDIT.RUNERR") + errorCode
					+ MainConstants.NEW_LINE + line
					+ Messages.getString("QEDIT.ERRWHERE")
					+ MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + ee.getMessage();
			if (logTabResult != null) {
				tabMiddle.setSelection(0);
				String logMessage = logMessagesArea.getText();
				if (logMessage != null && logMessage.length() > 0) {
					logMessage += MainConstants.NEW_LINE;
				}
				logSqlText.setText(logSqlText.getText()
						+ MainConstants.NEW_LINE + qVector.get(i).toString());
				logSqlText.setTopIndex(logSqlText.getLineCount() - 1);
				logMessagesArea.setText(logMessage + MainConstants.NEW_LINE
						+ errmsg);
				logMessagesArea.setTopIndex(logMessagesArea.getLineCount() - 1);

			} else {
				while (tabMiddle.getItemCount() > 0) {
					tabMiddle.getItem(0).dispose();
				}
				makeLogResult(qVector.get(i).toString(), errmsg);
				for (int j = 0; j < curResult.size(); j++) {
					makeResult((QueryExecuter) curResult.get(j));
				}
				tabMiddle.setSelection(0);
			}
			CommonTool.debugPrint(ee);
		} finally {
			QueryUtil.freeQuery(statement);
		}
		itemRun.setEnabled(true);
		itemQueryPlan.setEnabled(true);
		OnlyQueryPlan = false;
	}

	private void runQuery(final String queries) {
		clearResult();
		logTabResult = null;
		itemRun.setEnabled(false);
		itemQueryPlan.setEnabled(false);
		if (runJob != null) {
			runJob.cancel();
			runJob = null;
		}
		runJob = new Job(Messages.getString("PROGRESSMONITOR.RUNQUERY")) {
			public IStatus run(IProgressMonitor monitor) {
				final Vector qVector = queriesToQuery(queries);
				monitor.beginTask("", qVector.size() * 2 + 2);
				vectorQueryPlans.clear();
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
				QueryExecuter result = null;
				String multiQuerySql = null;
				try {
					if (qVector.size() > 0 && !monitor.isCanceled())
						isIsolationHigher = isIsolationHigherThanRepeatableRead(conn, isActive);
					monitor.worked(1);
					for (i = 0; i < qVector.size() && !monitor.isCanceled(); i++) {
						String sql = qVector.get(i).toString();
						monitor.subTask(i + 1 + Messages.getString("TASK.QUERYDESC"));
						if (MainRegistry.queryEditorOption.recordlimit > 0) {
							multiQuerySql = SqlParser.parse(sql);
						}
						if (multiQuerySql == null) {
							beginTimestamp = System.currentTimeMillis();
							stmt = (CUBRIDPreparedStatement) conn.prepareStatement(sql, ResultSet.TYPE_FORWARD_ONLY,
							        (MainRegistry.queryEditorOption.oidinfo) ? ResultSet.CONCUR_UPDATABLE
							                : ResultSet.CONCUR_READ_ONLY, ResultSet.HOLD_CURSORS_OVER_COMMIT);
						}
						monitor.worked(1);
						if (multiQuerySql != null) {
							result = new QueryExecuter(QueryEditor.this, cntResults, "");
							result.setMultiQuerySql(multiQuerySql);
							result.setQueryMsg((i + 1) + Messages.getString("QEDIT.QUERYSEQ") + MainConstants.NEW_LINE);
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
								if (MainRegistry.isProtegoBuild()) {
									CUBRIDKeyTable.putValue(conKey);
								}
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
							result = new QueryExecuter(QueryEditor.this, cntResults, sql);
							result.makeTable(rs);
							String queryMsg = (i + 1) + Messages.getString("QEDIT.QUERYSEQ") + "[ " + elapsedTimeStr
							        + " " + Messages.getString("QEDIT.SECOND") + " , "
							        + Messages.getString("MSG.TOTALROWS") + " : " + result.cntRecord + " ]"
							        + MainConstants.NEW_LINE;
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
							threadExecResult = 0;
							try {
								threadExecResult = stmt.executeUpdate();
								endTimestamp = System.currentTimeMillis();
							} catch (SQLException ee) {
								throw ee;
							}
							elapsedTime = (endTimestamp - beginTimestamp) * 0.001;
							logs += (i + 1) + Messages.getString("QEDIT.QUERYSEQ") + " ";
							int cntModify = threadExecResult;
							noSelectSql += sql + MainConstants.NEW_LINE;
							hasModifyQuery = true;
							switch (execType) {
								case CUBRIDCommandType.CUBRID_STMT_ALTER_CLASS:
								case CUBRIDCommandType.CUBRID_STMT_ALTER_SERIAL:
								case CUBRIDCommandType.CUBRID_STMT_RENAME_CLASS:
								case CUBRIDCommandType.CUBRID_STMT_RENAME_TRIGGER:
									logs += Messages.getString("QEDIT.ALTEROK");
									break;
								case CUBRIDCommandType.CUBRID_STMT_CREATE_CLASS:
								case CUBRIDCommandType.CUBRID_STMT_CREATE_INDEX:
								case CUBRIDCommandType.CUBRID_STMT_CREATE_TRIGGER:
								case CUBRIDCommandType.CUBRID_STMT_CREATE_SERIAL:
									logs += Messages.getString("QEDIT.CREATEOK");
									break;
								case CUBRIDCommandType.CUBRID_STMT_DROP_DATABASE:
								case CUBRIDCommandType.CUBRID_STMT_DROP_CLASS:
								case CUBRIDCommandType.CUBRID_STMT_DROP_INDEX:
								case CUBRIDCommandType.CUBRID_STMT_DROP_LABEL:
								case CUBRIDCommandType.CUBRID_STMT_DROP_TRIGGER:
								case CUBRIDCommandType.CUBRID_STMT_DROP_SERIAL:
								case CUBRIDCommandType.CUBRID_STMT_REMOVE_TRIGGER:
									logs += Messages.getString("QEDIT.DROPOK");
									break;
								case CUBRIDCommandType.CUBRID_STMT_INSERT:
									logs += cntModify + " " + Messages.getString("QEDIT.INSERTOK");
									break;
								case CUBRIDCommandType.CUBRID_STMT_SELECT:
									break;
								case CUBRIDCommandType.CUBRID_STMT_UPDATE:
									logs += cntModify + " " + Messages.getString("QEDIT.UPDATEOK2");
									break;
								case CUBRIDCommandType.CUBRID_STMT_DELETE:
									logs += cntModify + " " + Messages.getString("QEDIT.DELETEOK");
									break;
								/* others are 'Successfully execution' */
								/*
								 * Under two line works disable button when
								 * query's last command is commit/rollback
								 */
								case CUBRIDCommandType.CUBRID_STMT_COMMIT_WORK:
								case CUBRIDCommandType.CUBRID_STMT_ROLLBACK_WORK:
									hasModifyQuery = false;
								default:
									logs += Messages.getString("QEDIT.QUERYOK");
									break;
							}
							String elapsedTimeStr = nf.format(elapsedTime);
							if (elapsedTime < 0.001) {
								elapsedTimeStr = "0.000";
							}
							logs += "[" + elapsedTimeStr + " " + Messages.getString("QEDIT.SECOND") + "]"
							        + MainConstants.NEW_LINE;
							if (MainRegistry.queryEditorOption.getqueryplan)
								vectorQueryPlans.add(new StructQueryPlan(sql, ""));
						}
						QueryUtil.freeQuery(stmt, rs);
						monitor.worked(1);
					}
					if (isAutocommit && !monitor.isCanceled())
						conn.commit();
				} catch (final SQLException e) {
					try {
						if (isAutocommit && !monitor.isCanceled())
							conn.rollback();
					} catch (SQLException e1) {
					}
					if (multiQuerySql != null && result != null) {
						noSelectSql += result.getQuerySql();
						logs += result.getQueryMsg();
					} else {
						final String errorSql = (String) qVector.get(i);
						Application.mainwindow.getShell().getDisplay().syncExec(new Runnable() {
							public void run() {
								if (txaEdit != null && !txaEdit.isDisposed()) {
									txtFind(errorSql, 0, false, false, true, false);
									line = txaEdit.getLineAtOffset(txaEdit.getSelection().x) + 1;
								}
							}
						});
						noSelectSql += errorSql;
						logs += Messages.getString("QEDIT.RUNERR") + e.getErrorCode() + MainConstants.NEW_LINE + line
						        + Messages.getString("QEDIT.ERRWHERE") + MainConstants.NEW_LINE
						        + Messages.getString("QEDIT.ERRORHEAD") + e.getMessage();
						CommonTool.debugPrint(e);
					}
				} finally {
					final String logsBak = logs;
					final String noSelectSqlBak = noSelectSql;
					final int cntResultsBak = i;
					final boolean hasModifyQueryBak = hasModifyQuery;
					final boolean isIsolationHigherBak = isIsolationHigher;
					Application.mainwindow.getShell().getDisplay().syncExec(new Runnable() {
						public void run() {
							if (tabMiddle != null && !tabMiddle.isDisposed()) {
								if (cntResultsBak < 1 && logsBak.trim().length() <= 0)
									makeEmptyResult();
								else {
									if (logsBak.trim().length() > 0) {
										makeLogResult(noSelectSqlBak, logsBak);
									}
									for (int j = 0; j < curResult.size(); j++) {
										makeResult((QueryExecuter) curResult.get(j));
									}
								}

								if (!hasModifyQueryBak && !isIsolationHigherBak) {
									try {
										if (conn != null && !conn.isClosed())
											conn.commit();
									} catch (SQLException e) {
										CommonTool.debugPrint(e);
									}
									setActive(false);
								} else
									setActive(true);
								itemRun.setEnabled(true);
								if (MainRegistry.queryEditorOption.getqueryplan)
									itemQueryPlan.setEnabled(true);
							}
						}
					});
					QueryUtil.freeQuery(stmt, rs);
					monitor.worked(1);
					monitor.done();
				}
				return Status.OK_STATUS;
			}
		};
		runJob.schedule();
	}

	private void makeResult(QueryExecuter result) {
		if (tabMiddle == null || tabMiddle.isDisposed()) {
			return;
		}
		ViewForm viewForm = new ViewForm(tabMiddle, SWT.NONE);
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
		Text sqlText = new Text(tailSash, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		sqlText.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		StyledText messagesArea = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);

		result.makeResult(tbl, sqlText, messagesArea);
		bottomSash.setWeights(new int[] { 70, 30 });
		TabItem tab = new TabItem(tabMiddle, SWT.NONE);
		tab.setText(Messages.getString("QEDIT.RESULT") + (result.idx + 1));
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

	private void makeLogResult(String sqlStr, String messageStr) {
		SashForm tailSash = new SashForm(tabMiddle, SWT.NONE);
		tailSash.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_GRAY));
		logSqlText = new Text(tailSash, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		logSqlText.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		logSqlText.setText(sqlStr);
		logSqlText.setTopIndex(logSqlText.getLineCount() - 1);
		logMessagesArea = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		logMessagesArea.setText(messageStr);
		logMessagesArea.setTopIndex(logMessagesArea.getLineCount() - 1);
		logTabResult = new TabItem(tabMiddle, SWT.NONE);
		logTabResult.setText(Messages.getString("QEDIT.LOGSRESULT"));
		logTabResult.setControl(tailSash);
	}

	private void makeEmptyResult() {
		SashForm bottomSash = new SashForm(tabMiddle, SWT.VERTICAL);
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
		Text sqlText = new Text(tailSash, SWT.MULTI | SWT.WRAP | SWT.V_SCROLL
				| SWT.READ_ONLY);
		sqlText.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_INFO_BACKGROUND));
		StyledText messagesArea = new StyledText(tailSash, SWT.MULTI | SWT.WRAP
				| SWT.V_SCROLL | SWT.READ_ONLY);
		bottomSash.setWeights(new int[] { 70, 30 });
		TabItem tab = new TabItem(tabMiddle, SWT.NONE);
		tab.setText(Messages.getString("QEDIT.RESULT"));
		tab.setControl(bottomSash);
	}

	// private void moreResult() {
	// PreparedStatement stmt = null;
	// QueryExecuter qe =
	// (QueryExecuter)curResult.get(tabMiddle.getSelectionIndex());
	// try {
	// stmt = conn.prepareStatement(qe.query,
	// ResultSet.TYPE_FORWARD_ONLY,
	// ResultSet.CONCUR_UPDATABLE,
	// ResultSet.HOLD_CURSORS_OVER_COMMIT);
	// qe.rs = (CUBRIDResultSet) stmt.executeQuery();
	// qe.makeTuple(qe.cursor);
	// toolBar4.setVisible(qe.isNotEndOfTuple);
	// setActive(true);
	// } catch (SQLException e) {
	// msgQueryEditor (Messages.getString("QEDIT.ERROR"),
	// e.getErrorCode()+NEW_LINE+e.getMessage(), true);
	// CommonTool.debugPrint(e);
	// }
	// }

	public void updateResult(String strOid, String column, String value)
			throws SQLException {
		CUBRIDOID oid = CUBRIDOID.getNewInstance((CUBRIDConnection) conn,
				strOid);
		if (value.equals(STR_NULL))
			value = null;
		oid.setValues(new String[] { column }, new Object[] { value });

		if (isAutocommit)
			conn.commit();

		setActive(true);

		CommonTool.InformationBox(Messages.getString("QEDIT.UPDATE"), Messages
				.getString("QEDIT.UPDATEOK1"));
	}

	public void deleteResult(String[] strOid) throws SQLException {
		int i = 0;
		for (i = 0; i < strOid.length; i++)
			CUBRIDOID.getNewInstance((CUBRIDConnection) conn, strOid[i])
					.remove();

		if (isAutocommit)
			conn.commit();

		setActive(true);

		CommonTool.InformationBox(Messages.getString("QEDIT.DELETE"), i + " "
				+ Messages.getString("QEDIT.DELETEOK"));
	}

	public Connection getConnection() {
		return conn;
	}

	private String createDropEventQuery(String table) {
		String query = new String();
		String columns = new String();
		try {
			DatabaseMetaData dbmd = conn.getMetaData();
			ResultSet column = dbmd.getColumns(null, null, table, null);
			boolean isFirst = true;
			while (column.next()) {
				if (!isFirst) {
					columns += ", ";
				}
				isFirst = false;
				columns += "\"" + column.getString("column_name") + "\"";
			}
			if (columns.trim().equals(""))
				columns = "*";
			query = "select " + columns + " from ";
			query += "\"" + table + "\";";
		} catch (SQLException e) {

			CommonTool.ErrorBox(e.getErrorCode() + MainConstants.NEW_LINE
					+ Messages.getString("QEDIT.ERRORHEAD") + e.getMessage());
			CommonTool.debugPrint(e);
		}
		return query;
	}

	public void setActive(boolean isActive) {
		if (!isAutocommit) {
			this.isActive = isActive;
			itemRollback.setEnabled(isActive);
			itemCommit.setEnabled(isActive);
		} else {
			this.isActive = false;
			itemRollback.setEnabled(false);
			itemCommit.setEnabled(false);
		}
	}

	private void openFindDlg(boolean isFind) {
		if (!MainRegistry.isFindDlgOpen) {
			MainRegistry.isFindDlgOpen = true;
			(new QueryFindDialog(isFind)).open(txaEdit.getSelectionText());
		}
	}

	/**
	 * @param strFind:
	 *            Find String
	 * @param curOffsetStart:
	 *            Start offset
	 * @param isWrap:
	 *            Return at end of stream
	 * @param isUp:
	 *            reverse search
	 * @param isCaseSensitive:
	 *            Case sensitive
	 * @param isWholeWord:
	 *            Whole word
	 * @return boolean: Success : true, Nothing or faild : false
	 */
	public boolean txtFind(String strFind, int curOffsetStart, boolean isWrap,
			boolean isUp, boolean isCaseSensitive, boolean isWholeWord) {
		IRegion region = null;
		try {
			if (curOffsetStart < 0) {
				Point pt = textViewer.getSelectedRange();

				// Get the current offset
				curOffsetStart = pt.x + pt.y;

				// If something is currently selected, and they're searching
				// backwards,
				// move offset to beginning of selection. Otherwise, repeated
				// backwards
				// finds will only find the same text
				if (isUp) {
					if (pt.x != pt.y) {
						curOffsetStart = pt.x - 1;
					}
				}
			}

			// Perform the find
			region = frda.find(curOffsetStart, strFind, !isUp, isCaseSensitive,
					isWholeWord, false /* regex */);
		} catch (BadLocationException e) {
			// Ignore
		}
		// Update the viewer with found selection
		if (region != null) {
			textViewer.setSelectedRange(region.getOffset(), region.getLength());
			textViewer.revealRange(region.getOffset(), region.getLength());
			return true;
		} else {
			if (!isWrap) {
				return false;
			} else {
				return txtFind(strFind, isUp ? frda.length() - 1 : 0, false,
						isUp, isCaseSensitive, isWholeWord);
			}
		}
	}

	public boolean txtReplace(String strFind, String strReplace,
			boolean isWrap, boolean isUp, boolean isCaseSensitive,
			boolean isWholeWord) {
		int curOffsetStart = textViewer.getSelectedRange().x;
		int curOffsetLength = textViewer.getSelectedRange().y;
		if (curOffsetLength > 0) {
			txaEdit.replaceTextRange(curOffsetStart, curOffsetLength,
					strReplace);
		}

		return txtFind(strFind, isUp ? curOffsetStart : curOffsetStart
				+ strReplace.length(), isWrap, isUp, isCaseSensitive,
				isWholeWord);
	}

	public int txtReplaceAll(String strFind, String strReplace,
			boolean isCaseSensitive) {
		int cnt = 0;
		int curOffsetStart = 0;

		while (txtFind(strFind, curOffsetStart, false, false, isCaseSensitive,
				isWholeWord)) {
			curOffsetStart = textViewer.getSelectedRange().x;
			txaEdit.replaceTextRange(curOffsetStart, textViewer
					.getSelectedRange().y, strReplace);
			curOffsetStart += strReplace.length();
			cnt++;
			// There's no sense why it doesn't warp search
			// if ((curOffsetStart + strReplace.length()) > frda.length())
			// break;
		}

		return cnt;
	}

	public void setFindOption(String strFind, boolean isWrap, boolean isUp,
			boolean isCaseSensitive, boolean isWholeWord) {
		this.curFindStr = strFind;
		this.isCurCaseSensitive = isCaseSensitive;
		this.isCurUp = isUp;
		this.isCurWrap = isWrap;
		this.isWholeWord = isWholeWord;
	}

	void inputComment(boolean isForce) {
		inputComment(false, false);
	}

	void inputComment(boolean isForce, boolean isComment) {
		int startOffset = txaEdit.getSelection().x;
		int endOffset = txaEdit.getSelection().y;

		int startLine = txaEdit.getLineAtOffset(startOffset);
		int endLine = txaEdit.getLineAtOffset(endOffset);

		if (txaEdit.getSelectionText().endsWith(MainConstants.NEW_LINE))
			endLine--;

		int currLineOffset;

		if (!isForce) {
			isComment = false; // if isComment == true, adding comment

			for (int i = startLine; i <= endLine; i++) {
				currLineOffset = txaEdit.getOffsetAtLine(i);
				if (!txaEdit.getText().substring(currLineOffset).trim()
						.startsWith("//")) {
					isComment |= true;
				}
			}
		}

		if (startOffset == endOffset) {
			currLineOffset = txaEdit.getOffsetAtLine(startLine);
			if (isComment) {
				txaEdit.replaceTextRange(currLineOffset, 0, "//");
			} else {
				int line_start_offset = txaEdit.getOffsetAtLine(startLine);
				if ((line_start_offset + 2 <= txaEdit.getText().length())
						&& (txaEdit.getText().substring(line_start_offset,
								line_start_offset + 2).equals("//"))) {
					currLineOffset = txaEdit.getText().indexOf("//",
							currLineOffset);
					txaEdit.replaceTextRange(currLineOffset, 2, "");
				}
			}
		} else {
			if (isComment) {
				for (int i = startLine; i <= endLine; i++) {
					currLineOffset = txaEdit.getOffsetAtLine(i);
					txaEdit.replaceTextRange(currLineOffset, 0, "//");
				}
				startOffset += 2;
				endOffset += (endLine - startLine + 1) * 2;
			} else {
				for (int i = startLine; i <= endLine; i++) {
					int line_start_offset = txaEdit.getOffsetAtLine(i);

					if ((line_start_offset + 2 <= txaEdit.getText().length())
							&& (txaEdit.getText().substring(line_start_offset,
									line_start_offset + 2).equals("//"))) {
						currLineOffset = txaEdit.getText().indexOf("//",
								txaEdit.getOffsetAtLine(i));
						txaEdit.replaceTextRange(currLineOffset, 2, "");
						if (i == startLine)
							startOffset -= 2;
						endOffset -= 2;
					}
				}
			}
			txaEdit.setSelection(startOffset, endOffset);
		}
	}

	public void inputTab() {
		int startOffset = txaEdit.getSelection().x;
		int endOffset = txaEdit.getSelection().y;

		int startLine = txaEdit.getLineAtOffset(startOffset);
		int endLine = txaEdit.getLineAtOffset(endOffset);

		if (endLine > startLine) {
			if (txaEdit.getSelectionText().endsWith(MainConstants.NEW_LINE))
				endLine--;
			for (int i = startLine; i <= endLine; i++)
				txaEdit.replaceTextRange(txaEdit.getOffsetAtLine(i), 0, "\t");
			startOffset++;
			endOffset += (endLine - startLine + 1);
		} else if (txaEdit.getSelectionCount() > 0) {
			txaEdit.replaceTextRange(startOffset, txaEdit.getSelectionCount(), "\t");
			startOffset++;
			endOffset = startOffset;
		} else {
			txaEdit.insert("\t");
			startOffset++;
			endOffset++;
		}
		txaEdit.setSelection(startOffset, endOffset);
	}

	public void removeTab() {
		int startOffset = txaEdit.getSelection().x;
		int endOffset = txaEdit.getSelection().y;

		int startLine = txaEdit.getLineAtOffset(startOffset);
		int endLine = txaEdit.getLineAtOffset(endOffset);

		if (endLine > startLine) {
			if (txaEdit.getSelectionText().endsWith(MainConstants.NEW_LINE))
				endLine--;
			if (txaEdit.getText().substring(txaEdit.getOffsetAtLine(startLine)).startsWith("\t")) {
				startOffset--;
			}
			int offset = 0;
			for (int i = startLine; i <= endLine; i++) {
				if (!txaEdit.getText().substring(txaEdit.getOffsetAtLine(i)).startsWith("\t"))
					continue;
				txaEdit.replaceTextRange(txaEdit.getText().indexOf("\t", txaEdit.getOffsetAtLine(i)), 1, "");
				offset++;
			}
			endOffset -= offset;
		} else if (txaEdit.getSelectionCount() > 0) {
			txaEdit.replaceTextRange(startOffset, txaEdit.getSelectionCount(), "\t");
			startOffset++;
			endOffset = startOffset;
		} else {
			txaEdit.insert("\t");
			startOffset++;
			endOffset++;
		}
		txaEdit.setSelection(startOffset, endOffset);
	}

	public static QueryEditor getApp() {
		return APP;
	}

	/**
	 * Gets the color manager
	 * 
	 * @return ColorManager
	 */
	public ColorManager getColorManager() {
		return colorManager;
	}

	/**
	 * Gets the code scanner
	 * 
	 * @return QueryCodeScanner
	 */
	public QueryCodeScanner getCodeScanner() {
		return codeScanner;
	}

	public IUndoManager getUndoManager() {
		return undoManager;
	}

	private boolean checkActive() {
		if (!isAutocommit && isActive) {
			MessageBox mb = new MessageBox(Application.mainwindow.getShell(),
					SWT.OK | SWT.ICON_WARNING);
			mb.setText(Messages.getString("QEDIT.INFORMATION"));
			mb.setMessage(Messages.getString("QEDIT.TRANSACTIONACTIVE"));
			mb.open();
			// itemAutocommit
			toolBar1.getItem(10).setSelection(false);
			return true;
		}
		return false;
	}

	/**
	 * Decide transaction close using by connection's isolation level for Select
	 * query.
	 * 
	 * @param conn
	 *            isolation level validation connection
	 * @param isActive
	 *            Transaction is exist? (if transaction is exist, return value
	 *            is always true.)
	 * @return boolean
	 *         <ul>
	 *         <li>true: keep transaction </li>
	 *         <li>false: close transaction</li>
	 *         </ul>
	 */
	public static boolean isIsolationHigherThanRepeatableRead(Connection conn,
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
		} catch (SQLException e) {
			CommonTool.ErrorBox(e.getErrorCode() + MainConstants.NEW_LINE
					+ e.getMessage());
			CommonTool.debugPrint(e);
			return false;
		}
	}
}
