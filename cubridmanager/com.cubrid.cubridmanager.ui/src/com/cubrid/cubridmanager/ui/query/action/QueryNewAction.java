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

import org.apache.log4j.Logger;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorInput;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.core.common.log.LogUtil;
import com.cubrid.cubridmanager.ui.query.editor.QueryEditorPart;
import com.cubrid.cubridmanager.ui.query.editor.QueryUnit;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.ISchemaNode;

/**
 * action for create new query editor
 * 
 * @author wangsl 2009-3-9
 */
public class QueryNewAction extends
		SelectionAction {

	private static final Logger logger = LogUtil.getLogger(QueryNewAction.class);

	public static String ID = QueryNewAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public QueryNewAction(Shell shell, String text,
			ImageDescriptor enabledIcon, ImageDescriptor disabledIcon) {
		this(shell, null, text, enabledIcon, disabledIcon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param enabledIcon
	 * @param disabledIcon
	 */
	public QueryNewAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor enabledIcon,
			ImageDescriptor disabledIcon) {
		super(shell, provider, text, enabledIcon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
		this.setDisabledImageDescriptor(disabledIcon);
	}

	public boolean allowMultiSelections() {
		return true;
	}

	public boolean isSupported(Object obj) {
		return true;
	}

	@Override
	public void run() {
		IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window == null) {
			return;
		}
		IEditorInput input = new QueryUnit();
		try {
			IEditorPart editor = window.getActivePage().openEditor(input,
					QueryEditorPart.ID);
			Object[] selected = getSelectedObj();
			if (editor != null && selected != null && selected.length == 1
					&& selected[0] instanceof ISchemaNode) {
				((QueryEditorPart) editor).connect(((ISchemaNode) selected[0]).getDatabase());
			}
		} catch (PartInitException e) {
			logger.error(e.getMessage());
		} finally {
		}
	}

	@Override
	protected void selectionChanged(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			setEnabled(true);
			return;
		}
		super.selectionChanged(selection);
	}

}
