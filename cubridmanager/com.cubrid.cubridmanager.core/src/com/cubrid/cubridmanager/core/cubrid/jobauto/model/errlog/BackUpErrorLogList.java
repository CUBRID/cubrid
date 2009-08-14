package com.cubrid.cubridmanager.core.cubrid.jobauto.model.errlog;

import java.util.ArrayList;
import java.util.List;

import com.cubrid.cubridmanager.core.common.model.IModel;

public class BackUpErrorLogList implements IModel {

	List<BackUpErrorLog> errorLogList;


	public String getTaskName() {
	    return "getautobackupdberrlog";
    }
	
	public List<BackUpErrorLog> getErrorLogList() {
    	return errorLogList;
    }

	public void setErrorLogList(List<BackUpErrorLog> errorLogList) {
    	this.errorLogList = errorLogList;
    }
	
	/*******add the model to list  by reflect method*********/
	public void addError(BackUpErrorLog bean) {
		if (errorLogList == null)
			errorLogList = new ArrayList<BackUpErrorLog>();
		this.errorLogList.add(bean);
	}

	
}
