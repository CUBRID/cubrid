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
package com.cubrid.cubridmanager.ui.common.navigator;

import java.util.List;

import org.eclipse.jface.viewers.AbstractTreeViewer;
import org.eclipse.jface.viewers.ITreeContentProvider;
import org.eclipse.jface.viewers.Viewer;

import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * CUBRID manager navigator treeviewer content provider
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class DeferredContentProvider implements
		ITreeContentProvider {

	private CubridDeferredTreeContentManager contentManager = null;

	/**
	 * Return the elements to display in the tree viewer when its input is set
	 * to the given element.
	 * 
	 * @param inputElement the input element
	 * @return the array of elements to display in the viewer
	 */
	@SuppressWarnings("unchecked")
	public Object[] getElements(Object inputElement) {
		if (inputElement instanceof List) {
			List<ICubridNode> list = (List<ICubridNode>) inputElement;
			ICubridNode[] nodeArr = new ICubridNode[list.size()];
			return list.toArray(nodeArr);
		}
		return new Object[] {};
	}

	/**
	 * Return the child elements of the given parent element.
	 * 
	 * @param parentElement the parent element
	 * @return an array of child elements
	 */
	public Object[] getChildren(Object element) {
		if (contentManager != null) {
			Object[] children = contentManager.getChildren(element);
			if (children != null) {
				return children;
			}
		}
		return new Object[] {};
	}

	/**
	 * Return whether the given element has children.
	 * 
	 * 
	 * @param element the element
	 * @return <code>true</code> if the given element has children, and
	 *         <code>false</code> if it has no children
	 */
	public boolean hasChildren(Object element) {
		if (element instanceof CubridServer) {
			CubridServer server = (CubridServer) element;
			if (!server.isConnected()) {
				return false;
			} else {
				return true;
			}
		} else if (element instanceof CubridDatabase) {
			CubridDatabase database = (CubridDatabase) element;
			return database.isLogined();
		} else if (element instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) element;
			return node.isContainer();
		}
		return false;
	}

	/**
	 * Return the parent for the given element, or <code>null</code>
	 * 
	 * @param element the element
	 * @return the parent element, or <code>null</code> if it has none or if
	 *         the parent cannot be computed
	 */
	public Object getParent(Object element) {
		if (element instanceof ICubridNode) {
			ICubridNode node = (ICubridNode) element;
			return node.getParent();
		}
		return null;
	}

	/**
	 * Disposes of this content provider. This is called by the viewer when it
	 * is disposed.
	 */
	public void dispose() {
		// ignore
	}

	/**
	 * Notifies this content provider that the given viewer's input has been
	 * switched to a different element.
	 * 
	 * @param viewer the viewer
	 * @param oldInput the old input element, or <code>null</code> if the
	 *        viewer did not previously have an input
	 * @param newInput the new input element, or <code>null</code> if the
	 *        viewer does not have an input
	 */
	public void inputChanged(Viewer viewer, Object oldInput, Object newInput) {
		if (viewer instanceof AbstractTreeViewer) {
			contentManager = new CubridDeferredTreeContentManager(this,
					(AbstractTreeViewer) viewer);
		}
	}

	public CubridDeferredTreeContentManager getDeferredTreeContentManager() {
		return contentManager;
	}
}
