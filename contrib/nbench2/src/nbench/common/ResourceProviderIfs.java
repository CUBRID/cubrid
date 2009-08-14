package nbench.common;

public interface ResourceProviderIfs {
	ResourceIfs getResource(String resource) throws Exception;

	ResourceIfs newResource(String resource) throws Exception;

	void removeResource(String resource) throws Exception;
}
