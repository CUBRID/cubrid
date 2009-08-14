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
package com.cubrid.cubridmanager.ui.common.action;

import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IViewPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.ui.spi.CommonTool;
import com.cubrid.cubridmanager.ui.spi.CubridNodeTypeManager;
import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.action.SelectionAction;
import com.cubrid.cubridmanager.ui.spi.model.CubridNodeType;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This action is responsible to reload CUBRID navigator Node
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class RefreshAction extends
		SelectionAction {

	public static final String ID = RefreshAction.class.getName();

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param text
	 * @param icon
	 */
	public RefreshAction(Shell shell, String text, ImageDescriptor icon) {
		this(shell, null, text, icon);
	}

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	public RefreshAction(Shell shell, ISelectionProvider provider, String text,
			ImageDescriptor icon) {
		super(shell, provider, text, icon);
		this.setId(ID);
		this.setToolTipText(text);
		this.setActionDefinitionId(ID);
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#allowMultiSelections ()
	 */
	public boolean allowMultiSelections() {
		return false;
	}

	/**
	 * 
	 * @see com.cubrid.cubridmanager.ui.spi.action.ISelectionAction#isSupported(java
	 *      .lang.Object)
	 */
	public boolean isSupported(Object obj) {
		if (obj instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) obj;
			if (CubridNodeTypeManager.isCanRefresh(node.getType())) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Reload the selected CUBRID node
	 */
	public void run() {
		final Object[] obj = this.getSelectedObj();
		if (obj == null || obj.length > 0) {
			if (!isSupported(obj[0])) {
				return;
			}
		}
		ICubridNode cubridNode = (ICubridNode) obj[0];
		String editorId = cubridNode.getEditorId();
		String viewId = cubridNode.getViewId();
		if ((editorId != null && editorId.trim().length() > 0)
				|| (viewId != null && viewId.trim().length() > 0)) {
			IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
			if (window != null) {
				if (editorId != null && editorId.trim().length() > 0) {
					IEditorPart editorPart = LayoutManager.getInstance().getEditorPart(
							cubridNode);
					if (editorPart != null) {
						LayoutManager.getInstance().openEditorOrView(cubridNode);
					}
				} else if (viewId != null && viewId.trim().length() > 0) {
					IViewPart viewPart = LayoutManager.getInstance().getViewPart(
							cubridNode);
					if (viewPart != null) {
						LayoutManager.getInstance().openEditorOrView(cubridNode);
					}
				}
			}
		}
		CubridNodeType nodeType = cubridNode.getType();
		switch (nodeType) {
		case GENERIC_VOLUME_FOLDER:
		case DATA_VOLUME_FOLDER:
		case INDEX_VOLUME_FOLDER:
		case TEMP_VOLUME_FOLDER:
			cubridNode = cubridNode.getParent();
			break;
		case ACTIVE_LOG_FOLDER:
		case ARCHIVE_LOG_FOLDER:
			cubridNode = cubridNode.getParent() != null ? cubridNode.getParent().getParent()
					: null;
			break;
		}

		ISelectionProvider provider = this.getSelectionProvider();
		if ((provider instanceof TreeViewer) && cubridNode != null) {
			TreeViewer viewer = (TreeViewer) provider;
			CommonTool.refreshNavigatorTree(viewer, cubridNode);
		}

	}
}
