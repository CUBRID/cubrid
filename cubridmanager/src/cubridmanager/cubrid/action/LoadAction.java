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

package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.Application;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.MainConstants;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WaitingMsgBox;
import cubridmanager.cubrid.AuthItem;
import cubridmanager.cubrid.dialog.COMMAND_RESULTDialog;
import cubridmanager.cubrid.dialog.LoadDbDialog;
import cubridmanager.cubrid.view.CubridView;
import java.util.ArrayList;

public class LoadAction extends Action {
	public static AuthItem ai = null;

	public static ArrayList unloaddb = new ArrayList();

	public static StringBuffer resultMsg = new StringBuffer();

	public LoadAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("LoadAction");
		if (img != null) {
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
			setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img.replaceFirst("icons",
							"disable_icons")));
		}
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell(Application.mainwindow.getShell());
		ai = MainRegistry.Authinfo_find(CubridView.Current_db);
		if (ai == null) {
			CommonTool.ErrorBox(shell, Messages.getString("MSG.SELECTDB"));
			return;
		}
		if (ai.status != MainConstants.STATUS_STOP) {
			CommonTool.ErrorBox(shell, Messages
					.getString("ERROR.RUNNINGDATABASE"));
			return;
		}
		ClientSocket cs = new ClientSocket();
		if (cs.Connect()) {
			if (cs.Send(shell, "", "unloadinfo")) {
				WaitingMsgBox wdlg = new WaitingMsgBox(shell);
				wdlg.run(Messages.getString("WAITING.GETTINGUNLOAD"));
				if (cs.ErrorMsg != null) {
					CommonTool.ErrorBox(shell, cs.ErrorMsg);
					return;
				}

				LoadDbDialog dlg = new LoadDbDialog(shell);

				if (dlg.doModal()) {
					COMMAND_RESULTDialog cdlg = new COMMAND_RESULTDialog(shell);
					cdlg.doModal();
				}
			} else {
				CommonTool.ErrorBox(shell, cs.ErrorMsg);
				return;
			}
		} else {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
	}
}

/*
 * class LoadActionWizard extends Wizard { public LoadActionWizard() { super();
 * setWindowTitle(Messages.getString("TITLE.LOADDB")); }
 * 
 * public void addPages() { addPage(new LOADDB_PAGE1Dialog()); addPage(new
 * LOADDB_PAGE2Dialog()); }
 * 
 * private boolean SendLoad(ClientSocket cs, LOADDB_PAGE1Dialog page1,
 * LOADDB_PAGE2Dialog page2) { String requestMsg=""; if
 * (!page2.CHECK_LOADDB_LOADSCHEMA.getSelection() &&
 * !page2.CHECK_LOADDB_LOADOBJECT.getSelection()) {
 * cs.ErrorMsg=Messages.getString("ERROR.NOUNLOADINFORMATION"); return false; }
 * 
 * requestMsg += "dbname:" + page1.EDIT_LOADDB_DBNAME.getText() + "\n";
 * 
 * if (page1.RADIO_LOADDB_SYNTAXONLY.getSelection()) requestMsg +=
 * "checkoption:syntax\n"; else if (page1.RADIO_LOADDB_LOADONLY.getSelection())
 * requestMsg += "checkoption:load\n"; else if
 * (page1.RADIO_LOADDB_BOTH.getSelection()) requestMsg += "checkoption:both\n";
 * 
 * requestMsg += "period:" + (page1.CHECK_COMMIT_PERIOD.getSelection() ?
 * page1.EDIT_LOADDB_PERIOD.getText() : "none") + "\n"; requestMsg += "user:" +
 * page1.EDIT_LOADDB_USER.getText() + "\n";
 * 
 * requestMsg += "estimated:" + (page1.CHECK_ESTIMATED_SIZE.getSelection() ?
 * page1.EDIT_ESTIMATED_SIZE.getText() : "none" ) + "\n"; requestMsg +=
 * "oiduse:" + (page1.CHECK_OID_IS_NOT_USE.getSelection() ? "no" : "yes") +
 * "\n"; requestMsg += "nolog:" + (page1.CHECK_NO_LOG.getSelection() ? "yes" :
 * "no") + "\n";
 *  // page 2... String tmp=null; if
 * (page2.RADIO_LOADDB_INSERTFILEPATH.getSelection()){ if
 * (page2.CHECK_LOADDB_LOADSCHEMA.getSelection()){
 * tmp=page2.EDIT_LOADDB_SCHEMA.getText(); if (tmp!=null) tmp=tmp.trim(); if
 * (tmp==null || tmp.length()<=0){
 * cs.ErrorMsg=Messages.getString("TOOLTIP.EDITSCHEMA"); return false; }
 * requestMsg += "schema:" + tmp + "\n"; } else { requestMsg += "schema:none\n"; }
 * 
 * if (page2.CHECK_LOADDB_LOADOBJECT.getSelection()){
 * tmp=page2.EDIT_LOADDB_OBJECT.getText(); if (tmp!=null) tmp=tmp.trim(); if
 * (tmp==null || tmp.length()<=0){
 * cs.ErrorMsg=Messages.getString("TOOLTIP.EDITOBJECT"); return false; }
 * requestMsg += "object:" + tmp + "\n"; } else { requestMsg += "object:none\n"; }
 * 
 * if (page2.CHECK_LOADDB_LOADINDEX.getSelection()){
 * tmp=page2.EDIT_LOADDB_INDEX.getText(); if (tmp!=null) tmp=tmp.trim(); if
 * (tmp==null || tmp.length()<=0){
 * cs.ErrorMsg=Messages.getString("TOOLTIP.EDITINDEX"); return false; }
 * requestMsg += "index:" + tmp + "\n"; } else { requestMsg += "index:none\n"; }
 * 
 * /* for future
 * if (page2.CHECK_LOADDB_LOADTRIGGER.getSelection()){
 * tmp=page2.EDIT_LOADDB_TRIGGER.getText(); if (tmp!=null) tmp=tmp.trim(); if
 * (tmp==null || tmp.length()<=0){
 * cs.ErrorMsg=Messages.getString("TOOLTIP.EDITTRIGGER"); return false; }
 * requestMsg += "trigger:" + tmp + "\n"; } else { requestMsg +=
 * "trigger:none\n"; } // * / } else{ if
 * (page2.COMBO_LOADDB_SRCDB.getSelectionIndex()==0) {
 * cs.ErrorMsg=Messages.getString("ERROR.SELECTDBTOLOAD"); return false; }
 * 
 * if (page2.CHECK_LOADDB_LOADSCHEMA.getSelection()){ TableItem[]
 * sels=page2.LIST_LOADDB_SCHEMA.getSelection(); if (sels==null || sels.length<=0) {
 * cs.ErrorMsg=Messages.getString("ERROR.SELECTSCHEMAFILETOLOAD"); return false; }
 * requestMsg += "schema:" + sels[0].getText(1) + "\n"; } else{ requestMsg +=
 * "schema:none\n"; }
 * 
 * if (page2.CHECK_LOADDB_LOADOBJECT.getSelection()){ TableItem[]
 * sels=page2.LIST_LOADDB_OBJECT.getSelection(); if (sels==null || sels.length<=0) {
 * cs.ErrorMsg=Messages.getString("ERROR.SELECTOBJECTFILETOLOAD"); return false; }
 * requestMsg += "object:" + sels[0].getText(1) + "\n"; } else{ requestMsg +=
 * "object:none\n"; }
 * 
 * if (page2.CHECK_LOADDB_LOADINDEX.getSelection()){ TableItem[]
 * sels=page2.LIST_LOADDB_INDEX.getSelection(); if (sels==null || sels.length<=0) {
 * cs.ErrorMsg=Messages.getString("ERROR.SELECTINDEXFILETOLOAD"); return false; }
 * requestMsg += "index:" + sels[0].getText(1) + "\n"; } else{ requestMsg +=
 * "index:none\n"; }
 * /* for future 
 * if (page2.CHECK_LOADDB_LOADTRIGGER.getSelection()){
 * TableItem[] sels=page2.LIST_LOADDB_TRIGGER.getSelection(); if (sels==null ||
 * sels.length<=0) {
 * cs.ErrorMsg=Messages.getString("ERROR.SELECTTRIGGERFILETOLOAD"); return
 * false; } requestMsg += "trigger:" + sels[0].getText(1) + "\n"; } else{
 * requestMsg += "trigger:none\n"; } //* / }
 * 
 * if (cs.Connect()) { if (cs.Send(super.getShell(), requestMsg, "loaddb")) {
 * WaitingMsgBox dlg = new WaitingMsgBox(super.getShell());
 * dlg.run(Messages.getString("WAITING.LOADDB")); if (cs.ErrorMsg!=null) {
 * return false; } } else { return false; } } else { return false; } return
 * true; }
 * 
 * public boolean performFinish() { LOADDB_PAGE1Dialog
 * page1=(LOADDB_PAGE1Dialog)getPage(LOADDB_PAGE1Dialog.PAGE_NAME);
 * LOADDB_PAGE2Dialog
 * page2=(LOADDB_PAGE2Dialog)getPage(LOADDB_PAGE2Dialog.PAGE_NAME); ClientSocket
 * cs=new ClientSocket(); cs=new ClientSocket(); if (!SendLoad(cs, page1,
 * page2)) { CommonTool.ErrorBox(super.getShell(), cs.ErrorMsg); return false; }
 * LoadAction.ret=true; return true; }
 * 
 * public boolean performCancel() { return true; }
 * 
 * public IWizardPage getNextPage(IWizardPage page) { IWizardPage nextPage =
 * super.getNextPage(page); if (nextPage instanceof LOADDB_PAGE2Dialog) {
 * ((WizardPage)nextPage).setPageComplete(true); } return nextPage; } }
 */