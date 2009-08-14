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
package com.cubrid.cubridmanager.plugin;

import org.eclipse.jface.action.IContributionItem;
import org.eclipse.jface.action.ICoolBarManager;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.window.ApplicationWindow;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IPageListener;
import org.eclipse.ui.IPerspectiveDescriptor;
import org.eclipse.ui.IPerspectiveListener;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;

import com.cubrid.cubridmanager.ui.spi.LayoutManager;
import com.cubrid.cubridmanager.ui.spi.SWTResourceManager;

/**
 * 
 * This class is singleton which is responsible to add menu and toolbar action
 * related with CUBRID Manager to main menubar and main coolbar
 * 
 * @author pangqiren
 * @version 1.0 - 2009-6-4 created by pangqiren
 */
public class ActionHelper {

	private IMenuManager[] standaloneMenu = null;
	private IToolBarManager[] standaloneToolBar = null;
	private IPerspectiveListener perspectiveListener = null;
	private IPageListener pageListener = null;
	private boolean isInit = false;
	private static ActionHelper instance = null;

	/**
	 * The private constructor
	 */
	private ActionHelper() {
	}

	/**
	 * Get the only instance of ActionHelper
	 * 
	 * @return ActionHelper
	 */
	public synchronized static ActionHelper getInstance() {
		if (instance == null) {
			instance = new ActionHelper();
		}
		return instance;
	}

	/**
	 * Set the CUBRID Menu and ToolBar visibility in eclipse
	 * 
	 * @param isVisible
	 */
	private void setVisible(boolean isVisible) {
		initAction();
		if (standaloneMenu != null) {
			for (int i = 0; i < standaloneMenu.length; ++i) {
				if (standaloneMenu[i] != null) {
					standaloneMenu[i].setVisible(isVisible);
				}
			}
		}
		if (standaloneToolBar != null) {
			for (int i = 0; i < standaloneToolBar.length; ++i) {
				IContributionItem[] items = standaloneToolBar[i].getItems();
				for (int j = 0; j < items.length; j++) {
					if (items[j] != null) {
						items[j].setVisible(isVisible);
					}
				}
				standaloneToolBar[i].update(true);
			}
		}
		ApplicationWindow window = (ApplicationWindow) PlatformUI.getWorkbench().getActiveWorkbenchWindow();
		if (window != null) {
			IMenuManager workbenchMenuManager = window.getMenuBarManager();
			workbenchMenuManager.update(true);
			ICoolBarManager coolBarManager = window.getCoolBarManager2();
			coolBarManager.update(true);
		}
	}

	/**
	 * 
	 * Create menu and toolBar of CUBRID Manager
	 * 
	 */
	private void initAction() {
		if (!isInit) {
			ApplicationWindow window = (ApplicationWindow) PlatformUI.getWorkbench().getActiveWorkbenchWindow();
			if (window == null) {
				return;
			}
			IMenuManager menuManager = window.getMenuBarManager();
			ICoolBarManager coolBarManager = window.getCoolBarManager2();
			ActionBuilder builder = new ActionBuilder();
			builder.makeActions((IWorkbenchWindow) window);
			standaloneMenu = builder.buildMenu(menuManager);
			standaloneToolBar = builder.buildToolBar(coolBarManager);
			this.isInit = true;
		}
	}

	/**
	 * This method is responsible for staring to listen to cubrid perspecitive
	 * if it is current active perspective to determinte whether show menu and
	 * toolbar related with CUBRID Manager
	 */
	public void startMonitorPerspective() {
		Display.getDefault().asyncExec(new Runnable() {
			public void run() {
				final IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();

				IWorkbenchPage activePage = window.getActivePage();
				if (activePage != null) {
					IPerspectiveDescriptor desc = activePage.getPerspective();
					if (desc != null && desc.getId().equals(Perspective.ID)) {
						setVisible(true);
					}
				}
				pageListener = new IPageListener() {
					public void pageActivated(IWorkbenchPage page) {

					}

					public void pageClosed(IWorkbenchPage page) {
						IPerspectiveDescriptor desc = page.getPerspective();
						if (desc != null && desc.getId().equals(Perspective.ID)) {
							setVisible(false);
							LayoutManager.getInstance().clearStatusLine();
						}
					}

					public void pageOpened(IWorkbenchPage page) {
						IPerspectiveDescriptor desc = page.getPerspective();
						if (desc != null && desc.getId().equals(Perspective.ID)) {
							setVisible(true);
						}
					}
				};
				window.addPageListener(pageListener);
				perspectiveListener = new IPerspectiveListener() {
					public void perspectiveActivated(IWorkbenchPage page,
							IPerspectiveDescriptor perspective) {
						if (perspective != null
								&& perspective.getId().equals(Perspective.ID)) {
							setVisible(true);
						} else {
							setVisible(false);
							LayoutManager.getInstance().clearStatusLine();
						}
					}

					public void perspectiveChanged(IWorkbenchPage page,
							IPerspectiveDescriptor perspective, String changeId) {

					}
				};
				window.addPerspectiveListener(perspectiveListener);
			}
		});
	}

	/**
	 * Stop listening to CUBRID Manager perspective
	 */
	public void stopMonitorPerspective() {
		Display.getDefault().asyncExec(new Runnable() {
			public void run() {
				final IWorkbenchWindow window = PlatformUI.getWorkbench().getActiveWorkbenchWindow();
				if (window != null && pageListener != null)
					window.removePageListener(pageListener);
				if (window != null && perspectiveListener != null)
					window.removePerspectiveListener(perspectiveListener);
				SWTResourceManager.dispose();
			}
		});
	}
}
