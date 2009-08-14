/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  - Neither the name of the <ORGANIZATION> nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
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

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

import org.apache.log4j.Logger;
import org.eclipse.core.commands.ExecutionException;
import org.eclipse.core.commands.IHandler;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.text.BadLocationException;
import org.eclipse.jface.text.FindReplaceDocumentAdapter;
import org.eclipse.jface.text.IDocument;
import org.eclipse.jface.text.IDocumentPartitioner;
import org.eclipse.jface.text.IRegion;
import org.eclipse.jface.text.TextViewerUndoManager;
import org.eclipse.jface.text.contentassist.ContentAssistant;
import org.eclipse.jface.text.rules.FastPartitioner;
import org.eclipse.jface.text.source.CompositeRuler;
import org.eclipse.jface.text.source.ISourceViewer;
import org.eclipse.jface.text.source.LineNumberRulerColumn;
import org.eclipse.jface.text.source.SourceViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.custom.VerifyKeyListener;
import org.eclipse.swt.events.ModifyEvent;
import org.eclipse.swt.events.ModifyListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.VerifyEvent;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.ToolItem;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.handlers.IHandlerActivation;
import org.eclipse.ui.handlers.IHandlerService;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.query.IEditorActionDefinitionIds;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.TextViewerOperationHandler;
import com.cubrid.cubridmanager.ui.query.action.CopyAction;
import com.cubrid.cubridmanager.ui.query.action.CutAction;
import com.cubrid.cubridmanager.ui.query.action.FindReplaceAction;
import com.cubrid.cubridmanager.ui.query.action.PasteAction;
import com.cubrid.cubridmanager.ui.query.action.RedoAction;
import com.cubrid.cubridmanager.ui.query.action.ShowSchemaAction;
import com.cubrid.cubridmanager.ui.query.action.SqlFormatAction;
import com.cubrid.cubridmanager.ui.query.action.UndoAction;
import com.cubrid.cubridmanager.ui.query.dialog.QueryFindDialog;
import com.cubrid.cubridmanager.ui.query.editor.CubridKeyWordContentAssistProcessor;
import com.cubrid.cubridmanager.ui.query.editor.DocumentConfig;
import com.cubrid.cubridmanager.ui.query.editor.PersistentDocument;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorSourceViewerConfiguration;
import com.cubrid.cubridmanager.ui.query.editor.QueryPartitionScanner;
import com.cubrid.cubridmanager.ui.query.editor.QuerySyntax;
import com.cubrid.cubridmanager.ui.query.editor.WordTracker;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;

/**
 * A composite to show the query source
 * 
 * @author wangsl 2009-3-11
 */
public class QuerySourceViewer extends
		Composite {
	private static final Logger logger = LogUtil.getLogger(QuerySourceViewer.class);
	private Color lineNumColor;
	private SourceViewer textViewer;
	private StyledText text;

	PersistentDocument document;
	private TextViewerUndoManager undoManager;

	private FindReplaceDocumentAdapter frda;
	private QueryPartitionScanner scanner;
	private CombinedQueryComposite queryComposite;
	private boolean isWholeWord;
	private String curFindStr;
	private boolean isCurCaseSensitive;
	private boolean isCurUp;
	private boolean isCurWrap;
	protected boolean useCompletions = true;

	static Map<String, IHandler> handlers = new HashMap<String, IHandler>();
	// handler service
	IHandlerService handlerService = (IHandlerService) PlatformUI.getWorkbench().getService(
			IHandlerService.class);

	// command handler activations
	Map<IHandler, IHandlerActivation> handlerActivations = new HashMap<IHandler, IHandlerActivation>();
	private IHandler handler;

	public QuerySourceViewer(Composite parent, int style,
			CombinedQueryComposite combinedQueryComposite) {

		super(parent, style);
		this.queryComposite = combinedQueryComposite;
		setLayout(new FillLayout());
		setUpDocument();
		CompositeRuler ruler = new CompositeRuler();
		LineNumberRulerColumn lineCol = new LineNumberRulerColumn();
		if (lineNumColor == null) {
			lineNumColor = SWTResourceManager.getColor(new RGB(236, 233, 216));
		}
		lineCol.setBackground(lineNumColor);
		ruler.addDecorator(0, lineCol);

		textViewer = new SourceViewer(this, ruler, SWT.V_SCROLL | SWT.H_SCROLL);
		textViewer.configure(new QueryEditorSourceViewerConfiguration());
		textViewer.setDocument(document);

		undoManager.connect(textViewer);

		final ContentAssistant assistant = new ContentAssistant();
		final WordTracker tracker = new WordTracker(QuerySyntax.KEYWORDS);
		assistant.setContentAssistProcessor(
				new CubridKeyWordContentAssistProcessor(tracker),
				IDocument.DEFAULT_CONTENT_TYPE);
		assistant.install(textViewer);

		text = (StyledText) textViewer.getTextWidget();
		text.setIndent(6);
		MenuManager menuManager = new MenuManager();
		menuManager.setRemoveAllWhenShown(true);
		menuManager.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager manager) {
				IAction copyAction = ActionManager.getInstance().getAction(
						CopyAction.ID);
				if (copyAction != null) {
					manager.add(copyAction);
				}
				IAction cutAction = ActionManager.getInstance().getAction(
						CutAction.ID);
				if (cutAction != null) {
					manager.add(cutAction);
				}
				IAction pasteAction = ActionManager.getInstance().getAction(
						PasteAction.ID);
				if (pasteAction != null) {
					manager.add(pasteAction);
				}
				IAction findAction = ActionManager.getInstance().getAction(
						FindReplaceAction.ID);
				if (findAction != null) {
					manager.add(findAction);
				}
				IAction formatAction = ActionManager.getInstance().getAction(
						SqlFormatAction.ID);
				if (formatAction != null) {
					manager.add(formatAction);
				}
				IAction showSchemaViewAction = ActionManager.getInstance().getAction(
						ShowSchemaAction.ID);
				if (showSchemaViewAction != null) {
					manager.add(showSchemaViewAction);
				}
			}
		});
		Menu contextMenu = menuManager.createContextMenu(text);
		text.setMenu(contextMenu);
		text.addModifyListener(new ModifyListener() {

			IAction undoAction = ActionManager.getInstance().getAction(
					UndoAction.ID);
			IAction redoAction = ActionManager.getInstance().getAction(
					RedoAction.ID);
			QueryEditorPart editor = queryComposite.getEditor();

			public void modifyText(ModifyEvent e) {
				if (!editor.isDirty()) {
					editor.setDirty(true);
				}
				if (!undoAction.isEnabled()) {
					undoAction.setEnabled(true);
				}
				if (!redoAction.isEnabled()) {
					redoAction.setEnabled(true);
				}
			}

		});
		text.addVerifyKeyListener(new VerifyKeyListener() {
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
		text.addKeyListener(new org.eclipse.swt.events.KeyAdapter() {
			public void keyPressed(org.eclipse.swt.events.KeyEvent e) {

				QueryEditorPart editor = queryComposite.getEditor();
				if (e.keyCode == SWT.F5) // Run
				{
					ToolItem runItem = editor.getRunItem();
					if (runItem.isEnabled()) {
						runItem.notifyListeners(SWT.Selection, new Event());
					}
				} else if (e.keyCode == SWT.F6) {
					ToolItem runPlanItem = editor.getRunPlanItem();
					if (runPlanItem.isEnabled()) {
						runPlanItem.notifyListeners(SWT.Selection, new Event());
					}
				} else if (e.keyCode == SWT.F3) {
					if (text.getSelectionCount() > 0)
						curFindStr = text.getSelectionText();
					if (curFindStr == null)
						return;
					if ((e.stateMask & SWT.SHIFT) != 0) { // SHIFT F3 event
						if (!txtFind(curFindStr, -1, isCurWrap, !isCurUp,
								isCurCaseSensitive, isWholeWord))
							CommonTool.openInformationBox(
									Messages.TOOLTIP_QEDIT_FIND,
									Messages.QEDIT_NOTFOUND);
					} else { // F3 event
						if (!txtFind(curFindStr, -1, isCurWrap, isCurUp,
								isCurCaseSensitive, isWholeWord))
							CommonTool.openInformationBox(
									Messages.TOOLTIP_QEDIT_FIND,
									Messages.QEDIT_NOTFOUND);
					}
				} else if ((e.stateMask & SWT.CTRL) != 0
						&& (e.stateMask & SWT.SHIFT) == 0) {
					if (e.keyCode == '/')
						inputComment(false, false);
					else if (e.keyCode == 'z') {
						e.doit = false;
						undo();
					} else if (e.keyCode == 'y')
						redo();
					else if (e.keyCode == 'f')
						find();
				} else if ((e.stateMask & SWT.ALT) != 0) {
					if (e.keyCode == SWT.BS) {
					}
				} else if ((e.stateMask & SWT.SHIFT) != 0) {
					if (e.keyCode == SWT.TAB)
						unindent();
					else if ((e.stateMask & SWT.CTRL) != 0 && e.keyCode == 'f') {
						editor.format();
					}
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
						|| (text.getText().trim().length() < 1))
					useCompletions = true;
			}
		});
		text.addSelectionListener(new SelectionAdapter() {

			@Override
			public void widgetSelected(SelectionEvent e) {
				IAction copyAction = ActionManager.getInstance().getAction(
						CopyAction.ID);
				copyAction.setEnabled(true);
				IAction cutAction = ActionManager.getInstance().getAction(
						CutAction.ID);
				cutAction.setEnabled(true);

				// show schema info view with a selected text
				IAction showSchemaAction = ActionManager.getInstance().getAction(
						ShowSchemaAction.ID);
				showSchemaAction.setEnabled(false);
				if (e.getSource() != null
						&& e.getSource() instanceof StyledText) {
					StyledText stext = (StyledText) e.getSource();
					if (stext != null) {
						showSchemaAction.setEnabled(true);
						IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
						IEditorPart editor = window.getActivePage().getActiveEditor();
						((QueryEditorPart) editor).setCurrentSchemaName(stext.getSelectionText());
					}
				}
			}

		});

		createHandlers();
	}

	protected void createHandlers() {
		handler = new TextViewerOperationHandler(textViewer,
				ISourceViewer.FORMAT);
		handlers.put(IEditorActionDefinitionIds.FORMAT, handler);

		// activate handlers
		activateHandlers();
	}

	protected void activateHandlers() {
		// if handler service is null, return
		if (handlerService == null) {
			return;
		}
		// activate handlers if it is not active
		Iterator<String> i = handlers.keySet().iterator();
		while (i.hasNext()) {
			String id = i.next();
			IHandler handler = handlers.get(id);
			IHandlerActivation activation = handlerActivations.get(handler);
			if (activation == null) {
				activation = handlerService.activateHandler(id, handler);
				handlerActivations.put(handler, activation);
			}
		}
	}

	/**
	 * Sets up the document
	 */
	@SuppressWarnings("deprecation")
	protected void setUpDocument() {
		undoManager = new TextViewerUndoManager(50);

		// Create the document
		document = new PersistentDocument();

		// Create the partition scanner
		scanner = new QueryPartitionScanner();
		frda = new FindReplaceDocumentAdapter(document);

		// Create the partitioner
		IDocumentPartitioner partitioner = new FastPartitioner(scanner,
				QueryPartitionScanner.TYPES);

		// Connect the partitioner and document
		document.setDocumentPartitioner(DocumentConfig.QUERY_PARTITIONING,
				partitioner);
		partitioner.connect(document);
	}

	public void setScript(String script) {
		document.set(script);

	}

	/**
	 * do the undo operation
	 */
	public void undo() {
		undoManager.undo();
	}

	/**
	 * do the redo operation
	 */
	public void redo() {
		undoManager.redo();

	}

	public StyledText getText() {
		return text;
	}

	public void setFindOption(String strFind, boolean isWrap, boolean isUp,
			boolean isCaseSensitive, boolean isWholeWord) {
		this.curFindStr = strFind;
		this.isCurCaseSensitive = isCaseSensitive;
		this.isCurUp = isUp;
		this.isCurWrap = isWrap;
		this.isWholeWord = isWholeWord;
	}

	/**
	 * @param strFind: Find String
	 * @param curOffsetStart: Start offset
	 * @param isWrap: Return at end of stream
	 * @param isUp: reverse search
	 * @param isCaseSensitive: Case sensitive
	 * @param isWholeWord: Whole word
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
			logger.error(e);
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
			text.replaceTextRange(curOffsetStart, curOffsetLength, strReplace);
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
			text.replaceTextRange(curOffsetStart,
					textViewer.getSelectedRange().y, strReplace);
			curOffsetStart += strReplace.length();
			cnt++;
			// There's no sense why it doesn't warp search
			// if ((curOffsetStart + strReplace.length()) > frda.length())
			// break;
		}

		return cnt;
	}

	public String getQueries() {
		if (text.getSelectionCount() > 0) {
			return text.getSelectionText();
		}
		return text.getText();
	}

	public void setQueries(String query) {
		text.setText(query);
	}

	public void release() {
		if (document != null) {
			document.clear();
			document = null;
		}
	}

	/**
	 * 
	 * find key in source editor
	 */
	public void find() {
		QueryFindDialog dialog = QueryFindDialog.getFindDialog(this,
				text.getSelectionText());
		dialog.open();
	}

	public void findNext() {
		if (text.getSelectionCount() > 0)
			curFindStr = text.getSelectionText();
		if (curFindStr == null)
			return;
		if (!txtFind(curFindStr, -1, isCurWrap, isCurUp, isCurCaseSensitive,
				isWholeWord))
			CommonTool.openInformationBox(Messages.TOOLTIP_QEDIT_FIND,
					Messages.QEDIT_NOTFOUND);

	}

	/**
	 * 
	 * change current line sql script to comment
	 */
	public void comment() {
		inputComment(true, true);
	}

	/**
	 * 
	 * change comment to current line sql script
	 */
	public void uncomment() {
		inputComment(true, false);
	}

	/**
	 * 
	 * remove tab in script
	 */
	public void unindent() {
		removeTab();
	}

	/**
	 * 
	 * insert tab in script
	 */
	public void indent() {
		inputTab();
	}

	void inputComment(boolean isForce, boolean isComment) {
		int startOffset = text.getSelection().x;
		int endOffset = text.getSelection().y;

		int startLine = text.getLineAtOffset(startOffset);
		int endLine = text.getLineAtOffset(endOffset);

		if (text.getSelectionText().endsWith(CommonTool.getLineSeparator()))
			endLine--;

		int currLineOffset;

		if (!isForce) {
			isComment = false; // if isComment == true, adding comment

			for (int i = startLine; i <= endLine; i++) {
				currLineOffset = text.getOffsetAtLine(i);
				if (!text.getText().substring(currLineOffset).trim().startsWith(
						"--")) {
					isComment |= true;
				}
			}
		}

		if (startOffset == endOffset) {
			currLineOffset = text.getOffsetAtLine(startLine);
			if (isComment) {
				text.replaceTextRange(currLineOffset, 0, "--");
			} else {
				int line_start_offset = text.getOffsetAtLine(startLine);
				if ((line_start_offset + 2 <= text.getText().length())
						&& (text.getText().substring(line_start_offset,
								line_start_offset + 2).equals("--"))) {
					currLineOffset = text.getText().indexOf("--",
							currLineOffset);
					text.replaceTextRange(currLineOffset, 2, "");
				}
			}
		} else {
			if (isComment) {
				for (int i = startLine; i <= endLine; i++) {
					currLineOffset = text.getOffsetAtLine(i);
					text.replaceTextRange(currLineOffset, 0, "--");
				}
				startOffset += 2;
				endOffset += (endLine - startLine + 1) * 2;
			} else {
				for (int i = startLine; i <= endLine; i++) {
					int line_start_offset = text.getOffsetAtLine(i);

					if ((line_start_offset + 2 <= text.getText().length())
							&& (text.getText().substring(line_start_offset,
									line_start_offset + 2).equals("--"))) {
						currLineOffset = text.getText().indexOf("--",
								text.getOffsetAtLine(i));
						text.replaceTextRange(currLineOffset, 2, "");
						if (i == startLine)
							startOffset -= 2;
						endOffset -= 2;
					}
				}
			}
			text.setSelection(startOffset, endOffset);
		}
	}

	public void inputTab() {
		int startOffset = text.getSelection().x;
		int endOffset = text.getSelection().y;

		int startLine = text.getLineAtOffset(startOffset);
		int endLine = text.getLineAtOffset(endOffset);

		if (endLine > startLine) {
			if (text.getSelectionText().endsWith(CommonTool.getLineSeparator()))
				endLine--;
			for (int i = startLine; i <= endLine; i++)
				text.replaceTextRange(text.getOffsetAtLine(i), 0, "\t");
			startOffset++;
			endOffset += (endLine - startLine + 1);
		} else if (text.getSelectionCount() > 0) {
			text.replaceTextRange(startOffset, text.getSelectionCount(), "\t");
			startOffset++;
			endOffset = startOffset;
		} else {
			text.insert("\t");
			startOffset++;
			endOffset++;
		}
		text.setSelection(startOffset, endOffset);
	}

	public void removeTab() {
		int startOffset = text.getSelection().x;
		int endOffset = text.getSelection().y;

		int startLine = text.getLineAtOffset(startOffset);
		int endLine = text.getLineAtOffset(endOffset);

		if (endLine > startLine) {
			if (text.getSelectionText().endsWith(CommonTool.getLineSeparator()))
				endLine--;
			if (text.getText().substring(text.getOffsetAtLine(startLine)).startsWith(
					"\t")) {
				startOffset--;
			}
			int offset = 0;
			for (int i = startLine; i <= endLine; i++) {
				if (!text.getText().substring(text.getOffsetAtLine(i)).startsWith(
						"\t"))
					continue;
				text.replaceTextRange(text.getText().indexOf("\t",
						text.getOffsetAtLine(i)), 1, "");
				offset++;
			}
			endOffset -= offset;
		} else if (text.getSelectionCount() > 0) {
			text.replaceTextRange(startOffset, text.getSelectionCount(), "\t");
			startOffset++;
			endOffset = startOffset;
		} else {
			text.insert("\t");
			startOffset++;
			endOffset++;
		}
		text.setSelection(startOffset, endOffset);
	}

	/**
	 * format script
	 */
	public void format() {
		try {
			handler.execute(null);
		} catch (ExecutionException e) {
			logger.error(e);
		}
	}

}
