package cubridmanager;

import java.util.ArrayList;

public class TreeParent extends TreeObject {
	private ArrayList children;

	public TreeParent(String objid, String name, String image, String viewid) {
		super(objid, name, image, viewid);
		children = new ArrayList();
	}

	public void addChild(TreeObject child) {
		children.add(child);
		child.setParent(this);
	}

	public void removeChild(TreeObject child) {
		if (child instanceof TreeParent) {
			TreeObject[] childtree = ((TreeParent) child).getChildren();
			for (int ci = 0, cn = childtree.length; ci < cn; ci++) {
				((TreeParent) child).removeChild(childtree[ci]);
			}
		}
		children.remove(child);
		child.setParent(null);
	}

	public TreeObject[] getChildren() {
		return (TreeObject[]) children.toArray(new TreeObject[children.size()]);
	}

	public boolean hasChildren() {
		return children.size() > 0;
	}
}
