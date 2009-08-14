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
import org.eclipse.jface.window.IShellProvider;
import org.eclipse.swt.events.FocusEvent;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.PlatformUI;

/**
 * 
 * This is a abstract class for action to listen to focus changed event from
 * focus provider. The action which is related with focus control will extend
 * this class
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public abstract class FocusAction extends
		Action implements
		IFocusAction,
		IShellProvider {

	private Shell shell;
	private Control control;

	/**
	 * The constructor
	 * 
	 * @param shell
	 * @param focusProvider
	 * @param text
	 * @param icon
	 */
	protected FocusAction(Shell shell, Control focusProvider, String text,
			ImageDescriptor icon) {
		super(text);
		if (icon != null) {
			this.setImageDescriptor(icon);
		}
		setEnabled(false);
		this.shell = shell;
		if (focusProvider != null) {
			control = focusProvider;
			setFocusProvider(control);
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
	 * @see IFocusAction#getFocusProvider()
	 */
	public Control getFocusProvider() {
		return control;
	}

	/**
	 * @see IFocusAction#setFocusProvider(Control)
	 */
	public void setFocusProvider(Control focusProvider) {
		if (focusProvider != null) {
			if (control != null && !control.isDisposed()) {
				control.removeFocusListener(this);
			}
			control = focusProvider;
			control.addFocusListener(this);
			focusChanged();
		}
	}

	/**
	 * 
	 * Handle with focus event to determine this action's enabled status
	 * 
	 */
	protected void focusChanged() {
		if (control == null || control.isDisposed()) {
			setEnabled(false);
			return;
		}
		if (control.isFocusControl()) {
			setEnabled(true);
		} else {
			setEnabled(false);
		}
	}

	/**
	 * Notifies that the focus gained event
	 * 
	 * @param e an event containing information about the focus change
	 */
	public void focusGained(FocusEvent e) {
		focusChanged();
	}

	/**
	 * Notifies the focus lost event
	 * 
	 * @param e an event containing information about the focus change
	 */
	public void focusLost(FocusEvent e) {
		focusChanged();
	}
}
