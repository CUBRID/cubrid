package com.cubrid.cubridmanager.core.cubrid.dbspace.model;

import com.cubrid.cubridmanager.core.common.model.IModel;

public class AddVolumeDbInfo  implements IModel {
    private String dbname;
    private String volname;
    private String purpose;
    private String path;
    private String numberofpage;
    private String size_need_mb;
	
	public String getDbname() {
    	return dbname;
    }

	public void setDbname(String dbname) {
    	this.dbname = dbname;
    }

	public String getVolname() {
    	return volname;
    }

	public void setVolname(String volname) {
    	this.volname = volname;
    }

	public String getPurpose() {
    	return purpose;
    }

	public void setPurpose(String purpose) {
    	this.purpose = purpose;
    }

	public String getPath() {
    	return path;
    }

	public void setPath(String path) {
    	this.path = path;
    }

	public String getNumberofpage() {
    	return numberofpage;
    }

	public void setNumberofpage(String numberofpage) {
    	this.numberofpage = numberofpage;
    }

	public String getSize_need_mb() {
    	return size_need_mb;
    }

	public void setSize_need_mb(String size_need_mb) {
    	this.size_need_mb = size_need_mb;
    }

	public String getTaskName() {
		return "addvoldb";
	}
	

}
