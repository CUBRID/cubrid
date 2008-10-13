package cubridmanager;

public class TreeObject {
	private String objid;
	private String name;
	private String iconpath;
	private String viewid;
	private TreeParent parent;

	public TreeObject(String objid, String name, String image, String viewid) {
		this.objid = objid;
		this.name = name;
		this.iconpath = image;
		this.viewid = viewid;
	}

	public String getID() {
		return objid;
	}

	public String getName() {
		return name;
	}

	public String getImageName() {
		return iconpath;
	}

	public String getViewID() {
		return viewid;
	}

	public void setParent(TreeParent parent) {
		this.parent = parent;
	}

	public void setImage(String image) {
		iconpath = image;
	}

	public TreeParent getParent() {
		return parent;
	}

	public String toString() {
		return getName();
	}

	public static TreeObject FindName(TreeObject[] ary, boolean[] chkary,
			String Name) {
		for (int ci = 0, cn = ary.length; ci < cn; ci++) {
			if (ary[ci].getName().equals(Name)) {
				if (chkary != null)
					chkary[ci] = true;
				return ary[ci];
			}
		}
		return null;
	}

	public static TreeObject FindID(TreeObject[] ary, boolean[] chkary,
			String ID) {
		for (int ci = 0, cn = ary.length; ci < cn; ci++) {
			if (ary[ci].getID().equals(ID)) {
				if (chkary != null)
					chkary[ci] = true;
				return ary[ci];
			}
		}
		return null;
	}

	public static void FindRemove(TreeParent tp, TreeObject[] ary,
			boolean[] chkary) {
		for (int ci = 0, cn = ary.length; ci < cn; ci++) {
			if (chkary[ci] == false) {
				tp.removeChild(ary[ci]);
			}
		}
	}

	public static void FindClear(TreeParent tp, TreeObject[] ary) {
		for (int ci = 0, cn = ary.length; ci < cn; ci++) {
			tp.removeChild(ary[ci]);
		}
	}

	public static void FindReset(boolean[] chkary) {
		for (int ci = 0, cn = chkary.length; ci < cn; ci++) {
			chkary[ci] = false;
		}
	}
}
