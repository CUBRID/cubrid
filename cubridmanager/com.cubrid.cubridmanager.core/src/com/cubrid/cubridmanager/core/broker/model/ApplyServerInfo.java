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
package com.cubrid.cubridmanager.core.broker.model;

/**
 * A java bean which include all the information of the applied server
 * 
 * @author lizhiqiang
 * @version 1.0 - 2009-5-19 created by lizhiqiang
 */
public class ApplyServerInfo implements Cloneable{
	private String as_id;
	private String as_pid;
	private String as_c;//req
	private String as_psize;
	private String as_status;
	private String as_cpu;
	private String as_ctime;
	private String as_lat;
	private String as_cur;//job info
	private String as_num_query;
	private String as_num_tran;
	private String as_long_query;
	private String as_long_tran;
	private String as_error_query;
	private String as_dbname;
	private String as_dbhost;
	private String as_lct;
	
	
	public String getAs_id() {
		return as_id;
	}

	public void setAs_id(String as_id) {
		this.as_id = as_id;
	}

	public String getAs_pid() {
		return as_pid;
	}

	public void setAs_pid(String as_pid) {
		this.as_pid = as_pid;
	}

	public String getAs_c() {
		return as_c;
	}

	public void setAs_c(String as_c) {
		this.as_c = as_c;
	}

	public String getAs_psize() {
		return as_psize;
	}

	public void setAs_psize(String as_psize) {
		this.as_psize = as_psize;
	}

	public String getAs_status() {
		return as_status;
	}

	public void setAs_status(String as_status) {
		this.as_status = as_status;
	}

	public String getAs_cpu() {
		return as_cpu;
	}

	public void setAs_cpu(String as_cpu) {
		this.as_cpu = as_cpu;
	}

	public String getAs_ctime() {
		return as_ctime;
	}

	public void setAs_ctime(String as_ctime) {
		this.as_ctime = as_ctime;
	}

	public String getAs_lat() {
		return as_lat;
	}

	public void setAs_lat(String as_lat) {
		this.as_lat = as_lat;
	}

	public String getAs_cur() {
		return as_cur;
	}

	public void setAs_cur(String as_cur) {
		this.as_cur = as_cur;
	}

	public String getAs_num_query() {
		return as_num_query;
	}

	public void setAs_num_query(String as_num_query) {
		this.as_num_query = as_num_query;
	}

	public String getAs_num_tran() {
		return as_num_tran;
	}

	public void setAs_num_tran(String as_num_tran) {
		this.as_num_tran = as_num_tran;
	}

	public String getAs_long_query() {
		return as_long_query;
	}

	public void setAs_long_query(String as_long_query) {
		this.as_long_query = as_long_query;
	}

	public String getAs_long_tran() {
		return as_long_tran;
	}

	public void setAs_long_tran(String as_long_tran) {
		this.as_long_tran = as_long_tran;
	}

	public String getAs_error_query() {
		return as_error_query;
	}

	public void setAs_error_query(String as_error_query) {
		this.as_error_query = as_error_query;
	}

	public String getAs_dbname() {
		return as_dbname;
	}

	public void setAs_dbname(String as_dbname) {
		this.as_dbname = as_dbname;
	}

	public String getAs_dbhost() {
		return as_dbhost;
	}

	public void setAs_dbhost(String as_dbhost) {
		this.as_dbhost = as_dbhost;
	}

	public String getAs_lct() {
		return as_lct;
	}

	public void setAs_lct(String as_lct) {
		this.as_lct = as_lct;
	}
    public ApplyServerInfo clone(){
    	try{
    		return (ApplyServerInfo)super.clone();
    	}catch(CloneNotSupportedException e){ 		
    	}
    	return null;
    }
}
