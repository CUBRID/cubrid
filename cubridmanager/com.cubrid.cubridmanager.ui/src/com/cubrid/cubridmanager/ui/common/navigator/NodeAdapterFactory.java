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

import org.eclipse.core.runtime.IAdapterFactory;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.jobs.ISchedulingRule;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.ui.model.IWorkbenchAdapter;
import org.eclipse.ui.progress.IDeferredWorkbenchAdapter;
import org.eclipse.ui.progress.IElementCollector;

import com.cubrid.cubridmanager.ui.common.Messages;
import com.cubrid.cubridmanager.ui.spi.model.ICubridNode;

/**
 * 
 * This is CUBRID node adaptor factory for adapting to CUBRID node
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class NodeAdapterFactory implements
		IAdapterFactory {
	/**
	 * 
	 * CUBRID node adaptor
	 * 
	 * @author pangqiren 2009-3-2
	 */

	private static class CubridNodeAdapter implements
			IDeferredWorkbenchAdapter {

		/**
		 * Called by a job run in a separate thread to fetch the children of
		 * this adapter.
		 */
		public void fetchDeferredChildren(Object object,
				IElementCollector collector, IProgressMonitor monitor) {
			if (object instanceof ICubridNode) {
				ICubridNode node = (ICubridNode) object;
				monitor.beginTask(Messages.bind(Messages.msgLoadingChildren,
						node.getLabel()), IProgressMonitor.UNKNOWN);
				ICubridNode[] nodeArr = node.getChildren(monitor);
				if (monitor.isCanceled()) {
					return;
				}
				if (nodeArr != null && nodeArr.length > 0)
					collector.add(nodeArr, monitor);
				collector.done();
			}
			monitor.done();
		}

		public Object[] getChildren(Object object) {
			return null;
		}

		public ImageDescriptor getImageDescriptor(Object object) {
			return null;
		}

		public String getLabel(Object obj) {
			if (obj instanceof ICubridNode) {
				ICubridNode node = (ICubridNode) obj;
				return node.getLabel();
			}
			return null;
		}

		public Object getParent(Object obj) {
			if (obj instanceof ICubridNode) {
				ICubridNode node = (ICubridNode) obj;
				return node.getParent();
			}
			return null;
		}

		public ISchedulingRule getRule(final Object object) {
			return null;
		}

		public boolean isContainer() {
			return true;
		}

	}

	private CubridNodeAdapter cubridNodeAdapter = new CubridNodeAdapter();

	@SuppressWarnings("unchecked")
	public Object getAdapter(Object object, Class type) {
		if (object instanceof ICubridNode) {
			if (type == ICubridNode.class
					|| type == IDeferredWorkbenchAdapter.class
					|| type == IWorkbenchAdapter.class)
				return cubridNodeAdapter;
		}
		return null;
	}

	@SuppressWarnings("unchecked")
	public Class[] getAdapterList() {
		return new Class[] { ICubridNode.class,
				IDeferredWorkbenchAdapter.class, IWorkbenchAdapter.class };
	}
}
