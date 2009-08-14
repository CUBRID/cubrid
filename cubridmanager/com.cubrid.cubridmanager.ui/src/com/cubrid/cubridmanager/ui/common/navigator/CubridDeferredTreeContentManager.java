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

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.viewers.AbstractTreeViewer;
import org.eclipse.jface.viewers.ITreeContentProvider;
import org.eclipse.ui.progress.DeferredTreeContentManager;
import org.eclipse.ui.progress.WorkbenchJob;

import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.ActionManager;
import com.cubrid.cubridmanager.ui.spi.model.CubridBroker;
import com.cubrid.cubridmanager.ui.spi.model.CubridBrokerFolder;
import com.cubrid.cubridmanager.ui.spi.model.CubridDatabase;
import com.cubrid.cubridmanager.ui.spi.model.CubridServer;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This class extend DeferredTreeContentManager for restoring the previous tree
 * expanded status
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class CubridDeferredTreeContentManager extends
		DeferredTreeContentManager {

	private Object[] expandedElements = null;
	private AbstractTreeViewer treeViewer = null;

	/**
	 * The constructor
	 * 
	 * @param provider
	 * @param viewer
	 */
	public CubridDeferredTreeContentManager(ITreeContentProvider provider,
			AbstractTreeViewer viewer) {
		super(provider, viewer);
		treeViewer = viewer;
	}

	/**
	 * 
	 * Set expanded elements
	 * 
	 * @param expandedElements
	 */
	public void setExpandedElements(Object[] expandedElements) {
		this.expandedElements = expandedElements;
	}

	@Override
	protected void addChildren(final Object parent, final Object[] children,
			IProgressMonitor monitor) {
		WorkbenchJob updateJob = new WorkbenchJob(Messages.msgAddingChildren) {
			public IStatus runInUIThread(IProgressMonitor updateMonitor) {
				// Cancel the job if the tree viewer got closed
				if (treeViewer.getControl().isDisposed()
						|| updateMonitor.isCanceled()) {
					return Status.CANCEL_STATUS;
				}
				treeViewer.add(parent, children);
				if ((parent instanceof CubridDatabase)
						|| (parent instanceof CubridBrokerFolder)
						|| (parent instanceof CubridBroker)) {
					treeViewer.update(parent, null);
					ActionManager.getInstance().fireSelectionChanged(
							treeViewer.getSelection());
				}
				if ((parent instanceof CubridServer)
						&& expandedElements == null) {
					for (int i = 0; children != null && i < children.length; i++) {
						treeViewer.expandToLevel(children[i], 1);
					}
				} else {
					for (int i = 0; children != null && i < children.length; i++) {
						if ((children[i] instanceof ICubridNode)) {
							ICubridNode node = (ICubridNode) children[i];
							for (int j = 0; expandedElements != null
									&& j < expandedElements.length; j++) {
								if (expandedElements[j] instanceof ICubridNode) {
									ICubridNode node1 = (ICubridNode) expandedElements[j];
									if (node.getId().equals(node1.getId())) {
										treeViewer.expandToLevel(children[i], 1);
									}
								}
							}
						}
					}
				}
				return Status.OK_STATUS;
			}
		};
		updateJob.setSystem(true);
		updateJob.schedule();
	}
}
