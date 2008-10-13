package cubridmanager.dialog;

import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;

import cubridmanager.CommonTool;
import cubridmanager.Messages;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.browser.Browser;
import org.eclipse.swt.SWT;

public class HelpDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="41,8"
	private static boolean isAlreadyOpened = false;
	private Browser browser = null;

	public HelpDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public HelpDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public void doModal() {
		if (isAlreadyOpened)
			return;
		else
			isAlreadyOpened = true;

		try {
			createSShell();

			// sShell.pack();
			CommonTool.centerShell(sShell);
			sShell.open();
		} catch (Exception e) {
			CommonTool.debugPrint(e);
		}

		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell();
		sShell.setText(Messages.getString("TITLE.HELP"));
		sShell.setLayout(new FillLayout());
		createBrowser();
		sShell.setImage(new Image(null, cubridmanager.CubridmanagerPlugin
				.getImageDescriptor("/icons/help.png").getImageData()));
		sShell.setSize(new org.eclipse.swt.graphics.Point(1024, 768));
		sShell.addDisposeListener(new DisposeListener() {
			public void widgetDisposed(DisposeEvent e) {
				isAlreadyOpened = false;
			}
		});
	}

	/**
	 * This method initializes browser
	 * 
	 */
	private void createBrowser() {
		browser = new Browser(sShell, SWT.NONE);
		String url = null;
		try {
			String[] tmp;
			if (System.getProperty("os.name").startsWith("Window"))
				tmp = System.getProperty("java.class.path").split(";");
			else
				tmp = System.getProperty("java.class.path").split(":");

			int indexOfStartup = -1;
			for (int i = 0; i < tmp.length; i++) {
				indexOfStartup = tmp[i].indexOf("startup.jar");
				if (indexOfStartup < 0)
					continue;
				else {
					url = tmp[i].substring(0, indexOfStartup);
					break;
				}
			}
			url = url.concat("../Documents/Index.htm");
		} catch (Exception e) {
			CommonTool.ErrorBox(sShell, "Sorry. Cannot find help.");
			CommonTool.debugPrint(e);
		} catch (Error e) {
			CommonTool.ErrorBox(sShell, "Sorry. Cannot find help.");
			CommonTool.debugPrint(e);
		}
		browser.setUrl(url);
	}
}
