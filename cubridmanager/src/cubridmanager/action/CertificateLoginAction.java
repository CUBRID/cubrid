package cubridmanager.action;

import org.eclipse.jface.action.Action;
import org.eclipse.ui.IWorkbenchWindow;

import cubridmanager.ApplicationActionBarAdvisor;
import cubridmanager.MainRegistry;

public class CertificateLoginAction extends Action {

	public CertificateLoginAction(String text, IWorkbenchWindow window) {
		super(text);

		// The id is used to refer to the action in a menu or toolbar
		setId("CertificateLoginAction");
		setActionDefinitionId("CertificateLoginAction");
		setImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/event.png"));
		setDisabledImageDescriptor(cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/disable_icons/event.png"));
	}

	public void run() {
		MainRegistry.isCertificateLogin = true;
		ApplicationActionBarAdvisor.setCheckCertificationLogin(true);
	}
}
