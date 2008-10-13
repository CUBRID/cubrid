package cubridmanager.diag.dialog;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Dialog;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Text;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Tree;
import org.eclipse.swt.widgets.Combo;

public class DiagDiagReportTemplateDialog extends Dialog {
	private int DiagEndCode = Window.CANCEL;
	private Shell sShell = null; // @jve:decl-index=0:visual-constraint="7,11"
	private Composite composite = null;
	private Group group = null;
	private Tree tree = null;
	private Tree tree1 = null;
	private Tree tree2 = null;
	private Tree tree3 = null;
	private Tree tree4 = null;
	private Tree tree5 = null;
	private Button button = null;
	private Button button1 = null;
	private Group group1 = null;
	private Button button2 = null;
	private Label label = null;
	private Label label1 = null;
	private Text text = null;
	private Combo combo = null;
	private Button button3 = null;
	private Button button4 = null;

	public DiagDiagReportTemplateDialog(Shell parent) {
		super(parent);
		// TODO Auto-generated constructor stub
	}

	public DiagDiagReportTemplateDialog(Shell parent, int style) {
		super(parent, style);
		// TODO Auto-generated constructor stub
	}

	public int doModal(String tName) {
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
		sShell = new Shell();
		sShell.setText("Diag templete setting");
		createComposite();
		sShell.setSize(new org.eclipse.swt.graphics.Point(405, 508));
		button3 = new Button(sShell, SWT.NONE);
		button3.setBounds(new org.eclipse.swt.graphics.Rectangle(306, 449, 82,
				23));
		button3.setText("\ub2eb\uae30");
		button3.addSelectionListener(new org.eclipse.swt.events.SelectionAdapter() {
					public void widgetSelected(
							org.eclipse.swt.events.SelectionEvent e) {
						sShell.dispose();
					}
				});
		button4 = new Button(sShell, SWT.NONE);
		button4.setBounds(new org.eclipse.swt.graphics.Rectangle(208, 449, 82,
				23));
		button4.setText("\ub3c4\uc6c0\ub9d0");
	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		composite = new Composite(sShell, SWT.NONE);
		createGroup();
		createGroup1();
		composite.setBounds(new org.eclipse.swt.graphics.Rectangle(1, -1, 397,
				447));
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(composite, SWT.NONE);
		group.setText("\uc9c4\ub2e8 \uac1c\uccb4");
		createTree();
		createTree1();
		createTree2();
		createTree3();
		createTree4();
		createTree5();
		group.setBounds(new Rectangle(7, 10, 383, 345));
		button = new Button(group, SWT.NONE);
		button.setBounds(new Rectangle(162, 149, 54, 21));
		button.setText("\ucd94\uac00 >");
		button1 = new Button(group, SWT.NONE);
		button1.setBounds(new Rectangle(162, 184, 54, 21));
		button1.setText("< \uc81c\uac70");
	}

	/**
	 * This method initializes tree
	 * 
	 */
	private void createTree() {
		tree = new Tree(group, SWT.NONE);
		tree.setBounds(new Rectangle(10, 18, 141, 91));
	}

	/**
	 * This method initializes tree1
	 * 
	 */
	private void createTree1() {
		tree1 = new Tree(group, SWT.NONE);
		tree1.setBounds(new Rectangle(10, 127, 141, 91));
	}

	/**
	 * This method initializes tree2
	 * 
	 */
	private void createTree2() {
		tree2 = new Tree(group, SWT.NONE);
		tree2.setBounds(new Rectangle(10, 236, 141, 91));
	}

	/**
	 * This method initializes tree3
	 * 
	 */
	private void createTree3() {
		tree3 = new Tree(group, SWT.NONE);
		tree3.setBounds(new Rectangle(228, 18, 141, 91));
	}

	/**
	 * This method initializes tree4
	 * 
	 */
	private void createTree4() {
		tree4 = new Tree(group, SWT.NONE);
		tree4.setBounds(new Rectangle(228, 127, 141, 91));
	}

	/**
	 * This method initializes tree5
	 * 
	 */
	private void createTree5() {
		tree5 = new Tree(group, SWT.NONE);
		tree5.setBounds(new Rectangle(228, 236, 141, 91));
	}

	/**
	 * This method initializes group1
	 * 
	 */
	private void createGroup1() {
		group1 = new Group(composite, SWT.NONE);
		group1.setText("\ud15c\ud50c\ub9bf");
		group1.setBounds(new Rectangle(8, 363, 380, 76));
		button2 = new Button(group1, SWT.NONE);
		button2.setBounds(new Rectangle(266, 13, 84, 23));
		button2.setText("\uc124\uc815 \uc800\uc7a5");
		label = new Label(group1, SWT.NONE);
		label.setBounds(new Rectangle(15, 19, 52, 13));
		label.setText("\uc774\ub984");
		label1 = new Label(group1, SWT.NONE);
		label1.setBounds(new Rectangle(16, 48, 55, 15));
		label1.setText("\uc124\uba85");
		text = new Text(group1, SWT.BORDER);
		text.setBounds(new Rectangle(74, 47, 275, 21));
		createCombo();
	}

	/**
	 * This method initializes combo
	 * 
	 */
	private void createCombo() {
		combo = new Combo(group1, SWT.NONE);
		combo.setBounds(new Rectangle(75, 15, 164, 20));
	}

}
