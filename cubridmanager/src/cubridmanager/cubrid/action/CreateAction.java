package cubridmanager.cubrid.action;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.jface.wizard.IWizardPage;
import org.eclipse.jface.wizard.Wizard;
import org.eclipse.jface.wizard.WizardPage;

import cubridmanager.*;
import cubridmanager.cubrid.AddVols;
import cubridmanager.cubrid.dialog.CREATEDB_PAGE1Dialog;
import cubridmanager.cubrid.dialog.CREATEDB_PAGE2Dialog;
import cubridmanager.cubrid.dialog.CREATEDB_PAGE4Dialog;
import cubridmanager.cubrid.dialog.NEWDIRECTORYDialog;
import cubridmanager.cubrid.dialog.CHECKFILEDialog;
import cubridmanager.cubrid.view.CubridView;

public class CreateAction extends Action {
	public static String newdb = null;
	public static boolean bOverwriteConfigFile = true;
	public static boolean ret = false;

	public CreateAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("CreateAction");
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
		ret = false;
		Shell shell = new Shell(Application.mainwindow.getShell());
		CubridWizardDialog dialog = new CubridWizardDialog(shell,
				new CreateActionWizard());
		CommonTool.centerShell(shell);
		dialog.open();
		if (ret == true) {
			// CommonTool.MsgBox(shell, Messages.getString("MSG.SUCCESS"),
			// Messages.getString("MSG.CREATESUCCESS"));
			CubridView.Current_db = newdb;
			ApplicationActionBarAdvisor.startServerAction.run();
			CubridView.myNavi.SelectDB_UpdateView(newdb);
		}
	}
}

class CreateActionWizard extends Wizard {
	public CreateActionWizard() {
		super();
		setWindowTitle(Messages.getString("TITLE.CREATEDB"));
	}

	public void addPages() {
		addPage(new CREATEDB_PAGE1Dialog());
		addPage(new CREATEDB_PAGE2Dialog());
		addPage(new CREATEDB_PAGE4Dialog());
	}

	private boolean CheckDirs(ClientSocket cs, CREATEDB_PAGE1Dialog page1,
			CREATEDB_PAGE2Dialog page2) {
		String requestMsg = "dir:" + page1.EDIT_CREATEDB_GENERICVOL.getText()
				+ "\n";
		String tmp = "dir:" + page1.EDIT_CREATEDB_LOGVOL.getText() + "\n";
		if (requestMsg.indexOf(tmp) < 0)
			requestMsg += tmp;
		for (int i = 0, n = page2.addvols.size(); i < n; i++) {
			AddVols avtmp = (AddVols) page2.addvols.get(i);
			tmp = "dir:" + avtmp.Path + "\n";
			if (requestMsg.indexOf(tmp) < 0)
				requestMsg += tmp;
		}

		if (cs.Connect()) {
			if (cs.Send(super.getShell(), requestMsg, "checkdir")) {
				WaitingMsgBox dlg = new WaitingMsgBox(super.getShell());
				dlg.run(Messages.getString("WAITING.CHECKINGDIRECTORY"));
				if (cs.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	private boolean CheckFiles(ClientSocket cs, CREATEDB_PAGE1Dialog page1,
			CREATEDB_PAGE2Dialog page2) {
		String requestMsg = "file:" + page1.EDIT_CREATEDB_GENERICVOL.getText()
				+ "/sqlx.init\n";
		requestMsg += "file:" + page1.EDIT_CREATEDB_GENERICVOL.getText()
				+ "/dbparm.ini\n";
		for (int i = 0, n = page2.addvols.size(); i < n; i++) {
			AddVols avtmp = (AddVols) page2.addvols.get(i);
			requestMsg += "file:" + avtmp.Path + "/" + avtmp.Volname + "\n";
		}
		requestMsg = requestMsg.replaceAll("//", "/");
		if (cs.Connect()) {
			if (cs.Send(super.getShell(), requestMsg, "checkfile")) {
				WaitingMsgBox dlg = new WaitingMsgBox(super.getShell());
				dlg.run(Messages.getString("WAITING.CHECKINGFILES"));
				if (cs.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	private boolean SendCreate(ClientSocket cs, CREATEDB_PAGE1Dialog page1,
			CREATEDB_PAGE2Dialog page2) {
		String msg = "dbname:" + CreateAction.newdb + "\n";
		msg += "numpage:" + page1.EDIT_CREATEDB_NUMPAGE.getText() + "\n";
		msg += "pagesize:" + page1.COMBO_CREATEDB_PAGESIZE.getText() + "\n";
		msg += "logsize:" + page1.EDIT_CREATEDB_LOGSIZE.getText() + "\n";

		msg += "genvolpath:" + page1.EDIT_CREATEDB_GENERICVOL.getText() + "\n";
		msg += "logvolpath:" + page1.EDIT_CREATEDB_LOGVOL.getText() + "\n";

		msg += "open:exvol\n";
		for (int i = 0, n = page2.addvols.size(); i < n; i++) {
			AddVols avtmp = (AddVols) page2.addvols.get(i);
			msg += avtmp.Volname + ":" + avtmp.Purpose + ";" + avtmp.Pages
					+ ";" + avtmp.Path + "\n";
		}
		msg += "close:exvol\n";

		msg += "overwrite_config_file:";
		msg += (CreateAction.bOverwriteConfigFile) ? "YES" : "NO";

		if (cs.Connect()) {
			if (cs.Send(super.getShell(), msg, "createdb")) {
				WaitingMsgBox dlg = new WaitingMsgBox(super.getShell());
				dlg.run(Messages.getString("WAITING.CREATINGDATABASE"));
				if (cs.ErrorMsg != null) {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}
		return true;
	}

	public boolean performFinish() {
		if (MainRegistry.Authinfo_find(CreateAction.newdb) != null) {
			CommonTool.ErrorBox(super.getShell(), Messages
					.getString("ERROR.DATABASEALREADYEXIST"));
			return false;
		}

		CREATEDB_PAGE1Dialog page1 = (CREATEDB_PAGE1Dialog) getPage(CREATEDB_PAGE1Dialog.PAGE_NAME);
		CREATEDB_PAGE2Dialog page2 = (CREATEDB_PAGE2Dialog) getPage(CREATEDB_PAGE2Dialog.PAGE_NAME);
		ClientSocket cs = new ClientSocket();
		if (!CheckDirs(cs, page1, page2)) {
			CommonTool.ErrorBox(super.getShell(), cs.ErrorMsg);
			return false;
		}
		super.getShell().update();
		if (MainRegistry.Tmpchkrst.size() > 0) { // create directory confirm
			NEWDIRECTORYDialog newdlg = new NEWDIRECTORYDialog(super.getShell());
			if (newdlg.doModal() == 0)
				return false; // cancel
		}
		ClientSocket cs2 = new ClientSocket();
		if (!CheckFiles(cs2, page1, page2)) {
			CommonTool.ErrorBox(super.getShell(), cs2.ErrorMsg);
			return false;
		}
		super.getShell().update();
		CreateAction.bOverwriteConfigFile = true;
		if (MainRegistry.Tmpchkrst.size() > 0) { // create directory confirm
			CHECKFILEDialog newdlg = new CHECKFILEDialog(super.getShell());
			if (newdlg.doModal() == 0)
				CreateAction.bOverwriteConfigFile = false; // cancel

		}
		ClientSocket cs3 = new ClientSocket();
		if (!SendCreate(cs3, page1, page2)) {
			CommonTool.ErrorBox(super.getShell(), cs3.ErrorMsg);
			return false;
		}
		CreateAction.ret = true;
		MainRegistry.addDBUserInfo(page1.EDIT_CREATEDB_NAME.getText(), "dba",
				"");
		MainRegistry.Authinfo_add(page1.EDIT_CREATEDB_NAME.getText(), "dba",
				page1.EDIT_CREATEDB_GENERICVOL.getText(),
				MainConstants.STATUS_STOP);
		MainRegistry.Authinfo_find(page1.EDIT_CREATEDB_NAME.getText()).isDBAGroup = true;
		return true;
	}

	public boolean performCancel() {
		return true;
	}

	public boolean canFinish() {
		CREATEDB_PAGE4Dialog page4 = (CREATEDB_PAGE4Dialog) getPage(CREATEDB_PAGE4Dialog.PAGE_NAME);

		if (page4.isActive())
			return true;
		else
			return false;
	}

	public IWizardPage getNextPage(IWizardPage page) {

		IWizardPage nextPage = super.getNextPage(page);

		if (nextPage instanceof CREATEDB_PAGE2Dialog) {
			CREATEDB_PAGE1Dialog page1 = (CREATEDB_PAGE1Dialog) getPage(CREATEDB_PAGE1Dialog.PAGE_NAME);
			if (page1.is_ready) {
				int page_num;
				CreateAction.newdb = page1.EDIT_CREATEDB_NAME.getText();
				if (CreateAction.newdb == null
						|| CreateAction.newdb.length() < 1) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.NODBNAME"));
					return page;
				}
				if (!CommonTool.isValidDBName(CreateAction.newdb)) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.INVALIDDBNAME"));
					return page;
				}
				if (MainRegistry.hostOsInfo.equals("NT")
						&& CreateAction.newdb.trim().length() > 6) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.NT_DBNAME_LENGTH_TOOLONG"));
					return page;
				}
				/* validate num_pages */
				try {
					page_num = Integer.parseInt(page1.EDIT_CREATEDB_NUMPAGE
							.getText());
				} catch (Exception ee) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.INVALIDCREATE_NUMPAGE"));
					return page;
				}

				if (page_num < 100) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.CREATE_NUMPAGE_TOO_SMALL"));
					return page;
				}

				/* validate log file size */
				int log_file_size;
				try {
					log_file_size = Integer
							.parseInt(page1.EDIT_CREATEDB_LOGSIZE.getText());
				} catch (Exception ee) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.INVALIDCREATE_LOGSIZE"));
					return page;
				}
				if (log_file_size < 0) {
					CommonTool.ErrorBox(super.getShell(), Messages
							.getString("ERROR.INVALIDCREATE_LOGSIZE"));
					return page;
				}

				CREATEDB_PAGE2Dialog page2 = (CREATEDB_PAGE2Dialog) getPage(CREATEDB_PAGE2Dialog.PAGE_NAME);
				page2.EDIT_CREATEDB_EXVOLPATH
						.setText(page1.EDIT_CREATEDB_GENERICVOL.getText());
				page2.SetDefaultVolname();
			} else {
				page1.is_ready = true;
				page1.isChangedGenericVol = false;
				page1.isChangedLogVol = false;
			}
		} else if (nextPage instanceof CREATEDB_PAGE4Dialog) {
			CREATEDB_PAGE1Dialog page1 = (CREATEDB_PAGE1Dialog) getPage(CREATEDB_PAGE1Dialog.PAGE_NAME);
			if (page1.is_ready) {
				CREATEDB_PAGE2Dialog page2 = (CREATEDB_PAGE2Dialog) getPage(CREATEDB_PAGE2Dialog.PAGE_NAME);

				String summary = null;
				summary = Messages.getString("MSG.DATABASENAMEIS") + " "
						+ CreateAction.newdb + "\n\n";
				summary += Messages.getString("MSG.SIZEOFAPAGEWILLBE") + " "
						+ page1.COMBO_CREATEDB_PAGESIZE.getText()
						+ " bytes\n\n";
				summary += Messages
						.getString("MSG.NUMBEROFPAGESOFDATABASEWILLBE")
						+ " "
						+ page1.EDIT_CREATEDB_NUMPAGE.getText()
						+ " pages\n\n";
				summary += Messages.getString("MSG.SIZEOFTHELOGFILEWILLBE")
						+ " " + page1.EDIT_CREATEDB_LOGSIZE.getText()
						+ " pages\n\n";
				if (page2.addvols.size() > 0) {
					summary += Messages.getString("MSG.ADDITIONALVOLUMES")
							+ "\n\n";
					for (int i = 0, n = page2.addvols.size(); i < n; i++) {
						AddVols avtmp = (AddVols) page2.addvols.get(i);
						summary += Messages.getString("MSG.VOLUMENAMEWILLBE")
								+ " " + avtmp.Volname + "\n";
						summary += Messages
								.getString("MSG.PURPOSEOFVOLUMEWILLBE")
								+ " " + avtmp.Purpose + " volume\n";
						summary += Messages
								.getString("MSG.PAGESIZEOFVOLUMEWILLBE")
								+ " " + avtmp.Pages + " pages\n";
						summary += Messages
								.getString("MSG.DIRECTORYPATHFORVOLUMEWILLBE")
								+ " " + avtmp.Path + "\n\n";
					}
				}

				CREATEDB_PAGE4Dialog page4 = (CREATEDB_PAGE4Dialog) getPage(CREATEDB_PAGE4Dialog.PAGE_NAME);
				page4.EDIT_CREATEDB_SUMMARY.setText(summary);
				((WizardPage) nextPage).setPageComplete(true);
			}
		}

		return nextPage;
	}
}
