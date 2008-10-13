package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.wizard.IWizardPage;
import org.eclipse.jface.wizard.Wizard;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.CubridWizardDialog;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.dialog.BROKERADD_PAGE1Dialog;
import cubridmanager.cas.dialog.BROKERADD_PAGE2Dialog;
import cubridmanager.cas.dialog.BROKERADD_PAGE3Dialog;
import cubridmanager.cas.view.CASView;
import cubridmanager.cas.view.BrokerStatus;
import org.eclipse.swt.widgets.TableItem;

public class AddBrokerAction extends Action {
	public AddBrokerAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("AddBrokerAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendClientMessage(shell, "", "getaddbrokerinfo")) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}

		CubridWizardDialog dialog = new CubridWizardDialog(shell,
				new AddBrokerActionWizard());
		CommonTool.centerShell(shell);
		dialog.open();
	}
}

class AddBrokerActionWizard extends Wizard {
	public AddBrokerActionWizard() {
		super();
		setWindowTitle(Messages.getString("TITLE.ADDBROKER"));
	}

	public void addPages() {
		addPage(new BROKERADD_PAGE1Dialog());
		addPage(new BROKERADD_PAGE2Dialog());
		addPage(new BROKERADD_PAGE3Dialog());
	}

	public boolean performFinish() {
		String bName = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_BNAME.getText()
				.trim();
		if (bName.length() <= 0) {
			CommonTool.ErrorBox(super.getShell(), Messages
					.getString("ERROR.INVALIDBROKERNAME"));
			return false;
		}
		CASItem casrec;
		for (int i = 0, n = MainRegistry.CASinfo.size(); i < n; i++) {
			casrec = (CASItem) MainRegistry.CASinfo.get(i);
			if (casrec.broker_name.equals(bName)) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.EXISTBROKERNAME"));
				return false;
			}
		}
		for (int i = 0, n = MainRegistry.AddedBrokers.size(); i < n; i++) {
			if (bName.equals((String) MainRegistry.AddedBrokers.get(i))) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.EXISTBROKERNAME"));
				return false;
			}
		}
		for (int i = 0, n = MainRegistry.DeletedBrokers.size(); i < n; i++) {
			if (bName.equals((String) MainRegistry.DeletedBrokers.get(i))) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.EXISTBROKERNAME"));
				return false;
			}
		}
		int newport = CommonTool
				.atoi(BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_PORT.getText());
		for (int i = 0, n = MainRegistry.Tmpchkrst.size(); i < n; i += 3) {
			int bport = CommonTool.atoi((String) MainRegistry.Tmpchkrst
					.get(i + 1));
			if (bport == newport) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.BROKERPORTEXIST"));
				return false;
			}
		}

		String as = BROKERADD_PAGE1Dialog.COMBO_BROKER_ADD_ASTYPE.getText();
		String applroot = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_APPL_ROOT
				.getText().trim();
		if (!as.equals("CAS") && applroot.length() <= 0) {
			CommonTool.ErrorBox(super.getShell(), Messages
					.getString("ERROR.NOAPPLROOT"));
			return false;
		}
		int asmin = CommonTool.atoi(BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_ASMIN
				.getText());
		int asmax = CommonTool.atoi(BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_ASMAX
				.getText());
		if (asmin <= 0 || asmax <= 0 || asmin > asmax) {
			CommonTool.ErrorBox(super.getShell(), Messages
					.getString("ERROR.MINMAXAPSERVER"));
			return false;
		}

		String msg = "";
		msg = "bname:" + bName + "\n";

		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(super.getShell(), msg, "addbroker", Messages
				.getString("WAITING.ADDBROKER"))) {
			CommonTool.ErrorBox(super.getShell(), cs.ErrorMsg);
			return false;
		}

		MainRegistry.AddedBrokers.add(bName);

		msg += "open:params\n";
		msg += "BROKER_PORT:" + newport + "\n";
		msg += "APPL_SERVER:" + as + "\n";
		if (!as.equals("CAS")) {
			msg += "APPL_ROOT:" + applroot + "\n";
		}
		msg += "MIN_NUM_APPL_SERVER:" + asmin + "\n";
		msg += "MAX_NUM_APPL_SERVER:" + asmax + "\n";

		for (int i = 0; i < BROKERADD_PAGE2Dialog.LIST_BROKERADD_LIST1
				.getItemCount(); i++) {
			TableItem ti = BROKERADD_PAGE2Dialog.LIST_BROKERADD_LIST1
					.getItem(i);
			String name = ti.getText(0);
			String value = ti.getText(1);

			if ((name.equals("ACCESS_LIST") || name.equals("SOURCE_ENV"))
					&& value.equals("Not Specified")) {
				msg = msg + name + ":" + " " + "\n";
			} else {
				msg += name + ":" + value + "\n";
			}
		}
		msg += "close:params\n";
		cs = new ClientSocket();
		if (!cs.SendBackGround(super.getShell(), msg, "broker_setparam",
				Messages.getString("WAITING.BROKERPARASET"))) {
			CommonTool.ErrorBox(super.getShell(), cs.ErrorMsg);
			return false;
		}

		cs = new ClientSocket();
		if (!cs.SendClientMessage(super.getShell(), "", "getbrokersinfo")) {
			CommonTool.ErrorBox(super.getShell(), cs.ErrorMsg);
			return false;
		}
		if (MainRegistry.IsCASStart) {
			CommonTool.WarnBox(super.getShell(), Messages
					.getString("MSG.AFTERRESTARTCAS"));
		}

		CASView.myNavi.createModel();
		CASView.viewer.refresh();
		WorkView.SetView(BrokerStatus.ID, (String) null, BrokerStatus.ID);
		return true;
	}

	public boolean performCancel() {
		return true;
	}

	public IWizardPage getNextPage(IWizardPage page) {
		if (page instanceof BROKERADD_PAGE1Dialog) { // current page
			if (!BROKERADD_PAGE1Dialog.isready) {
				BROKERADD_PAGE1Dialog.isready = true;
				return page;
			}
			if (BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_BNAME == null)
				return page;
			String bname = BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_BNAME
					.getText().trim();
			if (bname.length() <= 0 || bname.indexOf(" ") >= 0) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.INVALIDBROKERNAME"));
				return page;
			}
			int newport = CommonTool
					.atoi(BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_PORT.getText());
			if (newport <= 0 || newport > 65535) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.INVALIDPORTNUMBER"));
				return page;
			}
			int asmin = CommonTool
					.atoi(BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_ASMIN.getText());
			int asmax = CommonTool
					.atoi(BROKERADD_PAGE1Dialog.EDIT_BROKER_ADD_ASMAX.getText());
			if (asmin <= 0 || asmax <= 0 || asmin > asmax) {
				CommonTool.ErrorBox(super.getShell(), Messages
						.getString("ERROR.MINMAXAPSERVER"));
				return page;
			}
		}

		IWizardPage nextPage = super.getNextPage(page);
		if (nextPage instanceof BROKERADD_PAGE2Dialog) {
			BROKERADD_PAGE2Dialog page2 = (BROKERADD_PAGE2Dialog) nextPage;
			page2.setinfo();
		}
		if (nextPage instanceof BROKERADD_PAGE3Dialog) {
			((WizardPage) nextPage).setPageComplete(true);
			BROKERADD_PAGE3Dialog page3 = (BROKERADD_PAGE3Dialog) nextPage;
			page3.setinfo();
		}
		return nextPage;
	}
}
