/*
 * Copyright (C) 2009 Search Solution Corporation. All rights reserved by Search
 * Solution.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: -
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. - Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. - Neither the name of the <ORGANIZATION> nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 */

package com.cubrid.cubridmanager.core.cubrid.table.model;

/**
 * class Authorization
 * 
 * @author robin
 * 2009-4-7
 */
public class ClassAuthorizations {
	private String className;
	private boolean selectPriv=false;
	private boolean insertPriv=false;
	private boolean updatePriv=false;
	private boolean alterPriv=false;
	private boolean deletePriv=false;
	private boolean indexPriv=false;
	private boolean executePriv=false;
	private boolean grantSelectPriv=false;
	private boolean grantInsertPriv=false;
	private boolean grantUpdatePriv=false;
	private boolean grantAlterPriv=false;
	private boolean grantDeletePriv=false;
	private boolean grantIndexPriv=false;
	private boolean grantExecutePriv=false;
	private boolean allPriv=false;

	public ClassAuthorizations(String p_Name, int authNum) {
		className = p_Name;
		
		
		for (int m1 = 0; m1 < 16; m1++) {
			if ((authNum & (1 << m1)) != 0) {
				switch (m1) {
				case 0:
					selectPriv = true;
					break;
				case 1:
					insertPriv = true;
					break;
				case 2:
					updatePriv = true;
					break;
				case 3:
					deletePriv = true;
					break;
				case 4:
					alterPriv = true;
					break;
				case 5:
					indexPriv = true;
					break;
				case 6:
					executePriv = true;
					break;
				case 8:
					grantSelectPriv = true;
					break;
				case 9:
					grantInsertPriv = true;
					break;
				case 10:
					grantUpdatePriv = true;
					break;
				case 11:
					grantDeletePriv = true;
					break;
				case 12:
					grantAlterPriv = true;
					break;
				case 13:
					grantIndexPriv = true;
					break;
				case 14:
					grantExecutePriv = true;
					break;
				}
			}
		}
	}
	public String getClassName() {
    	return className;
    }

	public void setClassName(String className) {
    	this.className = className;
    }

	public boolean isSelectPriv() {
    	return selectPriv;
    }

	public void setSelectPriv(boolean selectPriv) {
    	this.selectPriv = selectPriv;
    }

	public boolean isInsertPriv() {
    	return insertPriv;
    }

	public void setInsertPriv(boolean insertPriv) {
    	this.insertPriv = insertPriv;
    }

	public boolean isUpdatePriv() {
    	return updatePriv;
    }

	public void setUpdatePriv(boolean updatePriv) {
    	this.updatePriv = updatePriv;
    }

	public boolean isAlterPriv() {
    	return alterPriv;
    }

	public void setAlterPriv(boolean alterPriv) {
    	this.alterPriv = alterPriv;
    }

	public boolean isDeletePriv() {
    	return deletePriv;
    }

	public void setDeletePriv(boolean deletePriv) {
    	this.deletePriv = deletePriv;
    }

	public boolean isIndexPriv() {
    	return indexPriv;
    }

	public void setIndexPriv(boolean indexPriv) {
    	this.indexPriv = indexPriv;
    }

	public boolean isExecutePriv() {
    	return executePriv;
    }

	public void setExecutePriv(boolean executePriv) {
    	this.executePriv = executePriv;
    }

	public boolean isGrantSelectPriv() {
    	return grantSelectPriv;
    }

	public void setGrantSelectPriv(boolean grantSelectPriv) {
    	this.grantSelectPriv = grantSelectPriv;
    }

	public boolean isGrantInsertPriv() {
    	return grantInsertPriv;
    }

	public void setGrantInsertPriv(boolean grantInsertPriv) {
    	this.grantInsertPriv = grantInsertPriv;
    }

	public boolean isGrantUpdatePriv() {
    	return grantUpdatePriv;
    }

	public void setGrantUpdatePriv(boolean grantUpdatePriv) {
    	this.grantUpdatePriv = grantUpdatePriv;
    }

	public boolean isGrantAlterPriv() {
    	return grantAlterPriv;
    }

	public void setGrantAlterPriv(boolean grantAlterPriv) {
    	this.grantAlterPriv = grantAlterPriv;
    }

	public boolean isGrantDeletePriv() {
    	return grantDeletePriv;
    }

	public void setGrantDeletePriv(boolean grantDeletePriv) {
    	this.grantDeletePriv = grantDeletePriv;
    }

	public boolean isGrantIndexPriv() {
    	return grantIndexPriv;
    }

	public void setGrantIndexPriv(boolean grantIndexPriv) {
    	this.grantIndexPriv = grantIndexPriv;
    }

	public boolean isGrantExecutePriv() {
    	return grantExecutePriv;
    }

	public void setGrantExecutePriv(boolean grantExecutePriv) {
    	this.grantExecutePriv = grantExecutePriv;
    }

	public boolean isAllPriv() {
    	return allPriv;
    }

	public void setAllPriv(boolean allPriv) {
    	this.allPriv = allPriv;
    }
}
