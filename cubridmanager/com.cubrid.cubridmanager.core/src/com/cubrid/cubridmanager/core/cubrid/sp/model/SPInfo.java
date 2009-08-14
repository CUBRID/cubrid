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
package com.cubrid.cubridmanager.core.cubrid.sp.model;

import java.util.ArrayList;
import java.util.List;

/**
 * 
 * This class is responsible to store stored procudure information
 * 
 * @author pangqiren
 * @version 1.0 - 2009-5-8 created by pangqiren
 */
public class SPInfo {

	private String spName;
	private SPType spType;
	private String returnType;
	private String language;
	private String owner;
	private String target;
	private List<SPArgsInfo> argsInfoList = new ArrayList<SPArgsInfo>();

	public SPInfo(String spName) {
		this.spName = spName;
	}

	public SPInfo(String spName, SPType spType, String returnType,
			String language, String owner, String target) {
		super();
		this.spName = spName;
		this.spType = spType;
		this.returnType = returnType;
		this.language = language;
		this.owner = owner;
		this.target = target;
	}

	public String getSpName() {
		return spName;
	}

	public void setSpName(String spName) {
		this.spName = spName;
	}

	public SPType getSpType() {
		return spType;
	}

	public void setSpType(SPType spType) {
		this.spType = spType;
	}

	public String getReturnType() {
		return returnType;
	}

	public void setReturnType(String returnType) {
		this.returnType = returnType;
	}

	public String getLanguage() {
		return language;
	}

	public void setLanguage(String language) {
		this.language = language;
	}

	public String getOwner() {
		return owner;
	}

	public void setOwner(String owner) {
		this.owner = owner;
	}

	public String getTarget() {
		return target;
	}

	public void setTarget(String target) {
		this.target = target;
	}

	public List<SPArgsInfo> getArgsInfoList() {
		return argsInfoList;
	}

	public void setArgsInfoList(List<SPArgsInfo> argsInfoList) {
		this.argsInfoList = argsInfoList;
	}

	public void addSPArgsInfo(SPArgsInfo spArgsInfo) {
		if (this.argsInfoList == null) {
			argsInfoList = new ArrayList<SPArgsInfo>();
		}
		if (!argsInfoList.contains(spArgsInfo)) {
			argsInfoList.add(spArgsInfo);
		}
	}

	public void removeSPArgsInfo(SPArgsInfo spArgsInfo) {
		if (this.argsInfoList != null) {
			argsInfoList.remove(spArgsInfo);
		}
	}

}
