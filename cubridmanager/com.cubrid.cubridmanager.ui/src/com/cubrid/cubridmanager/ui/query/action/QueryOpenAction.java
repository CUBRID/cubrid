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
package com.cubrid.cubridmanager.ui.query.action;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;

import org.apache.log4j.Logger;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.FileDialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.query.Messages;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.query.editor.QueryUnit;
import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;

/**
 * action for create new query editor
 * 
 * @author wangsl 2009-3-17
 */
public class QueryOpenAction extends
		SelectionAction {

	private static final Logger logger = LogUtil.getLogger(QueryOpenAction.class);

	public static String ID = QueryOpenAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	protected QueryOpenAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(shell, provider, text, icon);
		setId(ID);
		setToolTipText(text);
		setEnabled(true);
		setActionDefinitionId("com.cubrid.cubridmanager.ui.cubrid.action.query.open");
	}

	public QueryOpenAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	@Override
	protected void selectionChanged(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			setEnabled(true);
			return;
		}
		super.selectionChanged(selection);
	}

	@Override
	public void run() {
		super.run();
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null || window.getActivePage() == null) {
			return;
		}
		IEditorPart editor = window.getActivePage().getActiveEditor();
		if (editor != null && editor.isDirty()) {
			int confirm = CommonTool.openMsgBox(editor.getSite().getShell(),
					MessageDialog.WARNING, Messages.saveResource,
					Messages.bind(Messages.saveConfirm, editor.getTitle()),
					new String[] { Messages.btnYes, Messages.btnNo,
							Messages.cancel });
			switch (confirm) {
			case 0:
				editor.doSave(null);
				break;
			case 1:
				break;
			default:
				return;
			}
		}
		File file = openFile();
		if (file != null) {
			String script = getScript(file);
			if (window == null) {
				return;
			}
			IEditorInput input = new QueryUnit();
			try {
				if (editor == null) {
					editor = window.getActivePage().openEditor(input,
							QueryEditorPart.ID);
				}
				if (editor == null)
					return;
				QueryEditorPart queryEditor = (QueryEditorPart) editor;
				queryEditor.setScript(script);
				queryEditor.setFile(file);
				Object[] selected = getSelectedObj();
				if (editor != null && selected != null && selected.length == 1
						&& selected[0] instanceof CubridDatabase) {
					queryEditor.connect((CubridDatabase) selected[0]);
				}
			} catch (PartInitException e) {
				logger.error(e.getMessage());
			} finally {
			}
		}
	}

	public File openFile() {
		FileDialog dialog = QueryEditorPart.openFileOpenDialog();
		String result = dialog.open();
		if (result != null) {
			return new File(result);
		}
		return null;
	}

	private String getScript(File file) {
		BufferedReader br = null;
		try {
			br = new BufferedReader(new FileReader(file));
			StringBuffer buff = new StringBuffer();
			String line = br.readLine();
			while (line != null) {
				buff.append(line + CommonTool.getLineSeparator());
				line = br.readLine();
			}
			return buff.toString();
		} catch (FileNotFoundException e1) {
			logger.error(e1.getMessage());
		} catch (IOException e1) {
			logger.error(e1.getMessage());
		} finally {
			try {
				if (br != null)
					br.close();
			} catch (IOException e) {
				logger.error(e.getMessage());
			}
		}
		return null;
	}

	public boolean allowMultiSelections() {
		return true;
	}

	public boolean isSupported(Object obj) {
		return true;
	}

}
