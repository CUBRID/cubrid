package cubridmanager.cas.action;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.wizard.IWizardPage;
import org.eclipse.jface.wizard.Wizard;
import org.eclipse.jface.wizard.WizardPage;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.TableItem;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.ClientSocket;
import cubridmanager.CommonTool;
import cubridmanager.CubridWizardDialog;
import cubridmanager.MainRegistry;
import cubridmanager.Messages;
import cubridmanager.WorkView;
import cubridmanager.cas.CASItem;
import cubridmanager.cas.BrokerParamInfo;
import cubridmanager.cas.dialog.BROKER_EDITORDialog;
import cubridmanager.cas.view.BrokerStatus;
import cubridmanager.cas.view.CASView;
import cubridmanager.cubrid.dialog.SqlxinitEditor;

public class SetParameterAction extends Action {
	public static String masterShmId = new String();

	public static BrokerParamInfo bpi = new BrokerParamInfo();

	public static boolean needRestart = true;

	public SetParameterAction(String text, String img) {
		super(text);
		// The id is used to refer to the action in a menu or toolbar
		setId("SetParameterAction");
		if (img != null)
			setImageDescriptor(cubridmanager.CubridmanagerPlugin
					.getImageDescriptor(img));
		setToolTipText(text);
	}

	public void run() {
		Shell shell = new Shell();
		ClientSocket cs = new ClientSocket();
		if (!cs.SendBackGround(shell, "confname:brokerconf", "getaddbrokerinfo",
				Messages.getString("WAITING.GETTINGSERVERINFO"))) {
			CommonTool.ErrorBox(shell, cs.ErrorMsg);
			return;
		}
		BROKER_EDITORDialog dlg = new BROKER_EDITORDialog(shell);
		dlg.doModal();		
	}
}

