package nbench.common;

import java.util.Map;

public interface UserHostVarIfs {
	void prepareSetup(Map<String, String> map, ResourceProviderIfs rp)
			throws Exception;
}
