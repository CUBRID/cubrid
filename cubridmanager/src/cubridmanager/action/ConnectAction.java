/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.dnd.DND;
import org.eclipse.swt.dnd.DragSource;
import org.eclipse.swt.dnd.DragSourceAdapter;
import org.eclipse.swt.dnd.DragSourceEvent;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.Application;
import cubridmanager.WorkView;
import cubridmanager.MainRegistry;
import cubridmanager.cubrid.view.CubridView;
import cubridmanager.cubrid.view.DBSchema;
import cubridmanager.cubrid.view.DatabaseListInHost;
import cubridmanager.dialog.ConnectDialog;

public class ConnectAction extends Action {
	// private final IWorkbenchWindow window;

	public ConnectAction(String text, IWorkbenchWindow window) {
		super(text);
		// this.window = window;
		// The id is used to refer to the action in a menu or toolbar
		setId("ConnectAction");
		setActionDefinitionId("ConnectAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/connect.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/connect.png"));
	}

	public void run() {
		boolean save_con = MainRegistry.IsConnected;
		Shell shell = new Shell(Application.mainwindow.getShell());
		ConnectDialog dlg = new ConnectDialog(shell);
		dlg.doModal();
		if (save_con != MainRegistry.IsConnected) {
			WorkView.TopView(CubridView.ID);
			CubridView.refresh();
			MainRegistry.NaviDraw_CUBRID = true;
			WorkView
					.SetView(DatabaseListInHost.ID, DatabaseListInHost.ID, null);
			if (MainRegistry.IsConnected) {
				CubridView.source = new DragSource(CubridView.viewer
						.getControl(), DND.DROP_MOVE);
				CubridView.types = new Transfer[] { TextTransfer.getInstance() };
				CubridView.source.setTransfer(CubridView.types);
				CubridView.source.addDragListener(new DragSourceAdapter() {
					public void dragStart(DragSourceEvent event) {
						if (!CubridView.getSelobj().getID().equals(
								DBSchema.SYS_OBJECT)
								&& !CubridView.getSelobj().getID().equals(
										DBSchema.USER_OBJECT)) {
							event.doit = false;
						}
					}

					public void dragSetData(DragSourceEvent event) {
						if (CubridView.getSelobj() != null) {
							event.data = CubridView.getSelobj().getName();
						}
					}
				});
			}
		}
	}
}
