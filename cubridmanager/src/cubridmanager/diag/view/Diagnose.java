package cubridmanager.diag.view;

import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.layout.RowData;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.widgets.Group;

public class Diagnose extends ViewPart {

	public static final String ID = "workview.Diagnose";
	private Composite top = null;
	private Composite composite = null;
	private Label label4 = null;
	private Group group = null;
	private Label label = null;
	private Label label1 = null;
	private Label label2 = null;
	private Label label3 = null;
	private Label label5 = null;
	private Label label6 = null;
	private Label label7 = null;
	private Label label8 = null;
	private Label label9 = null;
	private Label label10 = null;
	private Label label11 = null;
	private Label label12 = null;
	private Label label13 = null;
	private Label label14 = null;
	private Label label15 = null;

	public Diagnose() {
		super();
		// TODO Auto-generated constructor stub
	}

	public void createPartControl(Composite parent) {
		// TODO Auto-generated method stub
		top = new Composite(parent, SWT.NONE);
		top.setBackground(Display.getCurrent().getSystemColor(SWT.COLOR_WHITE));
		top.setLayout(new RowLayout());
		createComposite();
	}

	public void setFocus() {
		// TODO Auto-generated method stub

	}

	/**
	 * This method initializes composite
	 * 
	 */
	private void createComposite() {
		RowData rowData = new org.eclipse.swt.layout.RowData();
		rowData.height = 360;
		rowData.width = 600;
		composite = new Composite(top, SWT.NONE);
		composite.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		composite.setLayoutData(rowData);
		label4 = new Label(composite, SWT.NONE);
		label4
				.setBounds(new org.eclipse.swt.graphics.Rectangle(27, 12, 483,
						29));
		label4.setFont(new Font(Display.getDefault(), "\uad74\ub9bc\uccb4", 18,
				SWT.NORMAL));
		label4.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label4.setText("localhost diagnosis information");
		createGroup();
		label = new Label(composite, SWT.NONE);
		label
				.setBounds(new org.eclipse.swt.graphics.Rectangle(74, 319, 297,
						18));
		label.setBackground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_WHITE));
		label.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label.setText(" defect unit added.");
		label1 = new Label(composite, SWT.NONE);
		label1.setBounds(new org.eclipse.swt.graphics.Rectangle(74, 258, 350,
				18));
		label1.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label1.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label1.setText(" diag report added");
		label2 = new Label(composite, SWT.NONE);
		label2.setBounds(new org.eclipse.swt.graphics.Rectangle(74, 197, 350,
				18));
		label2.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label2.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label2.setText(" newer status caution raised.");
		label3 = new Label(composite, SWT.NONE);
		label3.setBounds(new org.eclipse.swt.graphics.Rectangle(74, 136, 351,
				18));
		label3.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label3.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label3.setText(" active tracking progress.");
		label5 = new Label(composite, SWT.NONE);
		label5
				.setBounds(new org.eclipse.swt.graphics.Rectangle(74, 75, 354,
						18));
		label5.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label5.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label5.setText(" status monitor progress.");
		label6 = new Label(composite, SWT.NONE);
		label6
				.setBounds(new org.eclipse.swt.graphics.Rectangle(56, 75, 18,
						20));
		label6.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label6.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label6.setText("2");
		label7 = new Label(composite, SWT.NONE);
		label7
				.setBounds(new org.eclipse.swt.graphics.Rectangle(426, 75, 87,
						20));
		label7.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label7.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label7.setForeground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_BLUE));
		label7.setText("Shortcut to");
		label8 = new Label(composite, SWT.NONE);
		label8.setBounds(new org.eclipse.swt.graphics.Rectangle(426, 136, 87,
				20));
		label8.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label8.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label8.setForeground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_BLUE));
		label8.setText("Shortcut to");
		label9 = new Label(composite, SWT.NONE);
		label9.setBounds(new org.eclipse.swt.graphics.Rectangle(426, 197, 87,
				20));
		label9.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label9.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label9.setForeground(Display.getCurrent()
				.getSystemColor(SWT.COLOR_BLUE));
		label9.setText("Shortcut to");
		label10 = new Label(composite, SWT.NONE);
		label10.setBounds(new org.eclipse.swt.graphics.Rectangle(426, 258, 87,
				20));
		label10.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label10.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label10.setForeground(Display.getCurrent().getSystemColor(
				SWT.COLOR_BLUE));
		label10.setText("Shortcut to");
		label11 = new Label(composite, SWT.NONE);
		label11.setBounds(new org.eclipse.swt.graphics.Rectangle(426, 319, 87,
				20));
		label11.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label11.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label11.setForeground(Display.getCurrent().getSystemColor(
				SWT.COLOR_BLUE));
		label11.setText("Shortcut to");
		label12 = new Label(composite, SWT.NONE);
		label12.setBounds(new org.eclipse.swt.graphics.Rectangle(56, 136, 18,
				20));
		label12.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label12.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label12.setText("1");
		label13 = new Label(composite, SWT.NONE);
		label13.setBounds(new org.eclipse.swt.graphics.Rectangle(56, 197, 18,
				20));
		label13.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label13.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label13.setText("1");
		label14 = new Label(composite, SWT.NONE);
		label14.setBounds(new org.eclipse.swt.graphics.Rectangle(56, 258, 18,
				20));
		label14.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label14.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label14.setText("2 ");
		label15 = new Label(composite, SWT.NONE);
		label15.setBounds(new org.eclipse.swt.graphics.Rectangle(56, 319, 18,
				20));
		label15.setBackground(Display.getCurrent().getSystemColor(
				SWT.COLOR_WHITE));
		label15.setFont(new Font(Display.getDefault(), "\uad74\ub9bc", 12,
				SWT.NORMAL));
		label15.setText("1");
	}

	/**
	 * This method initializes group
	 * 
	 */
	private void createGroup() {
		group = new Group(composite, SWT.NONE);
		group.setBounds(new org.eclipse.swt.graphics.Rectangle(19, 52, 573, 2));
	}

}
