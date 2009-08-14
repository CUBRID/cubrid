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
package com.cubrid.cubridmanager.ui.spi.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionProvider;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.window.IShellProvider;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.PlatformUI;

/**
 * 
 * This is a abstract class for action to listen to selected changed event from
 * selection provider. The action which is related with selection will extends
 * this class
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public abstract class SelectionAction extends
		Action implements
		ISelectionAction,
		IShellProvider {

	private ISelectionProvider provider;
	private Shell shell;

	/**
	 * The constuctor
	 * 
	 * @param shell
	 * @param provider
	 * @param text
	 * @param icon
	 */
	protected SelectionAction(Shell shell, ISelectionProvider provider,
			String text, ImageDescriptor icon) {
		super(text);
		if (icon != null) {
			this.setImageDescriptor(icon);
		}
		setEnabled(false);
		this.shell = shell;
		if (provider != null) {
			this.provider = provider;
			setSelectionProvider(provider);
		}
	}

	/**
	 * Return the current shell (or null if none). This return value may change
	 * over time, and should not be cached.
	 * 
	 * @return the current shell or null if none
	 */
	public Shell getShell() {
		if (shell == null) {
			shell = PlatformUI.getWorkbench().getActiveWorkbenchWindow().getShell();
		}
		return shell;
	}

	/**
	 * 
	 * Return the selected object array from selection provider
	 * 
	 * @return selected object array
	 */
	protected Object[] getSelectedObj() {
		if (provider != null && provider.getSelection() != null
				&& provider.getSelection() instanceof IStructuredSelection) {
			IStructuredSelection selection = (IStructuredSelection) provider.getSelection();
			return selection.toArray();
		}
		return new Object[] {};
	}

	/**
	 * 
	 * Return selection
	 * 
	 * @return
	 */
	protected ISelection getSelection() {
		if (provider != null) {
			return provider.getSelection();
		}
		return null;
	}

	/**
	 * Notifies that the selection has changed.
	 * 
	 * @param event event object describing the change
	 */
	public final void selectionChanged(SelectionChangedEvent event) {
		if (!(event.getSelection() instanceof IStructuredSelection))
			return;
		selectionChanged(event.getSelection());
	}

	/**
	 * @see ISelectionAction#getSelectionProvider()
	 */
	public ISelectionProvider getSelectionProvider() {
		return this.provider;
	}

	/**
	 * @see ISelectionAction#setSelectionProvider(ISelectionProvider)
	 */
	public void setSelectionProvider(ISelectionProvider provider) {
		if (provider != null) {
			if (this.provider != null)
				this.provider.removeSelectionChangedListener(this);
			this.provider = provider;
			this.provider.addSelectionChangedListener(this);
			selectionChanged(this.provider.getSelection());
		}
	}

	/**
	 * Handle with selection changed object to determine this action's enabled
	 * status,it is not intented to be override
	 * 
	 * @param selection
	 */
	protected void selectionChanged(ISelection selection) {
		if (selection == null || selection.isEmpty()) {
			setEnabled(false);
			return;
		}
		IStructuredSelection structuredSelection = (IStructuredSelection) selection;
		if (!allowMultiSelections()) {
			setEnabled(structuredSelection.size() == 1
					&& isSupported(structuredSelection.getFirstElement()));
		} else {
			setEnabled(true);
			for (Object object : structuredSelection.toArray()) {
				if (!isSupported(object)) {
					setEnabled(false);
					break;
				}
			}
		}
	}
}
