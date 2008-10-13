package cubridmanager.diag.dialog;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.SWT;

import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.Table;

public class DiagStatusWarningDialog extends Dialog {

	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="54,11"
	private Button okButton = null;
	private Button cancelButton = null;
	private int DiagEndCode = Window.CANCEL;
	private Label descLabel = null;
	private Text descText = null;
	private Group objectListGroup = null;
	private Tree objectListTree = null;
	private Button addButton = null;
	private Button removeButton = null;
	private Table selectedListTable = null;
	private Button applyButton = null;
	private Label objectListLabel = null;
	private Label selectedObjectListLabel = null;
	private Group group = null;
	private Button button2 = null;
	private Button button3 = null;
	private Button button4 = null;
	private Button button5 = null;
	private Text text1 = null;
	private Text text2 = null;
	private Group group1 = null;
	private Label label = null;
	private Text text = null;
	private Button button = null;

	public DiagStatusWarningDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagStatusWarningDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal() {
		createSShell();
		sShell.open();
		Display display = sShell.getDisplay();
		while (!sShell.isDisposed()) {
			if (!display.readAndDispatch())
				display.sleep();
		}

		return DiagEndCode;
	}

	/**
	 * This method initializes sShell
	 * 
	 */
	private void createSShell() {
		sShell = new Shell(SWT.APPLICATION_MODAL | SWT.DIALOG_TRIM);
		sShell.setText("Status alert management");
		sShell.setSize(new org.eclipse.swt.graphics.Point(617, 522));
		okButton = new Button(sShell, SWT.NONE);
		okButton.setBounds(new org.eclipse.swt.graphics.Rectangle(289, 462, 85,
				22));
		okButton.setText("OK");
		okButton
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.OK;
						sShell.dispose();
					}
				});

		cancelButton = new Button(sShell, SWT.NONE);
		cancelButton.setBounds(new org.eclipse.swt.graphics.Rectangle(390, 462,
				85, 22));
		cancelButton.setText("CANCEL");
		cancelButton
				.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						DiagEndCode = Window.CANCEL;
						sShell.dispose();
					}
				});

		descLabel = new Label(sShell, SWT.NONE);
		descLabel.setBounds(new org.eclipse.swt.graphics.Rectangle(24, 417, 39, 19));
		descLabel.setText("Explanation");
		descText = new Text(sShell, SWT.BORDER);
		descText.setBounds(new org.eclipse.swt.graphics.Rectangle(77, 416, 512, 18));
		createObjectListGroup();
		applyButton = new Button(sShell, SWT.NONE);
		applyButton.setBounds(new org.eclipse.swt.graphics.Rectangle(489, 462, 85, 22));
		applyButton.setText("Apply");
		createGroup();
		createGroup1();
		label = new Label(sShell, SWT.NONE);
		label.setBounds(new org.eclipse.swt.graphics.Rectangle(25, 390, 187, 14));
		label.setText("If alerted, execute next program");
		text = new Text(sShell, SWT.BORDER);
		text.setBounds(new org.eclipse.swt.graphics.Rectangle(232, 386, 323, 20));
		button = new Button(sShell, SWT.NONE);
		button.setBounds(new org.eclipse.swt.graphics.Rectangle(558, 386, 31, 20));
		button.setText("...");
	}

	/**
	 * This method initializes objectListGroup
	 * 
	 */
	private void createObjectListGroup() {
		objectListGroup = new Group(sShell, SWT.NONE);
		objectListGroup.setText("Alert setting");
		createObjectListTree();
		objectListGroup.setBounds(new org.eclipse.swt.graphics.Rectangle(15, 9, 578, 278));
		addButton = new Button(objectListGroup, SWT.NONE);
		addButton.setBounds(new org.eclipse.swt.graphics.Rectangle(194, 96, 56, 22));
		addButton.setText("Add >");
		removeButton = new Button(objectListGroup, SWT.NONE);
		removeButton.setBounds(new org.eclipse.swt.graphics.Rectangle(193, 140, 58, 22));
		removeButton.setText("< Remove");
		createSelectedListTable();
		objectListLabel = new Label(objectListGroup, SWT.CENTER);
		objectListLabel.setBounds(new org.eclipse.swt.graphics.Rectangle(34, 252, 116, 18));
		objectListLabel.setText("Entire Object");
		selectedObjectListLabel = new Label(objectListGroup, SWT.CENTER);
		selectedObjectListLabel.setBounds(new org.eclipse.swt.graphics.Rectangle(338, 250, 165, 19));
		selectedObjectListLabel.setText("Selected Object information");
	}

	/**
	 * This method initializes objectListTree
	 * 
	 */
	private void createObjectListTree() {
		objectListTree = new Tree(objectListGroup, SWT.NONE);
		objectListTree.setBounds(new org.eclipse.swt.graphics.Rectangle(11, 20,
				168, 222));
	}

	/**
	 * This method initializes selectedListTable
	 * 
	 */
	private void createSelectedListTable() {
		selectedListTable = new Table(objectListGroup, SWT.NONE);
		selectedListTable.setHeaderVisible(true);
		selectedListTable.setLinesVisible(true);
		selectedListTable.setBounds(new org.eclipse.swt.graphics.Rectangle(268,
				17, 300, 222));
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(sShell, SWT.NONE);
		group.setText("Status alert time");
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(15, 295, 578, 84));
		button2 = new Button(group, SWT.CHECK);
		button2.setBounds(new org.eclipse.swt.graphics.Rectangle(21, 24, 103, 16));
		button2.setText("\uc2dc\uc791 \uc2dc\uac04 \uc124\uc815");
		button3 = new Button(group, SWT.CHECK);
		button3.setBounds(new org.eclipse.swt.graphics.Rectangle(20, 54, 105, 16));
		button3.setText("\uc885\ub8cc \uc2dc\uac04 \uc124\uc815");
		button4 = new Button(group, SWT.NONE);
		button4.setBounds(new org.eclipse.swt.graphics.Rectangle(362, 22, 88, 19));
		button4.setText("Start status alert");
		button5 = new Button(group, SWT.NONE);
		button5.setBounds(new org.eclipse.swt.graphics.Rectangle(363, 50, 88, 19));
		button5.setText("Stop status alert");
		text1 = new Text(group, SWT.BORDER);
		text1.setBounds(new org.eclipse.swt.graphics.Rectangle(128, 21, 202, 22));
		text2 = new Text(group, SWT.BORDER);
		text2.setBounds(new org.eclipse.swt.graphics.Rectangle(128, 48, 203, 22));
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		group1 = new Group(sShell, SWT.NONE);
		group1.setBounds(new org.eclipse.swt.graphics.Rectangle(18, 447, 573, 2));
	}

}
