/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

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
