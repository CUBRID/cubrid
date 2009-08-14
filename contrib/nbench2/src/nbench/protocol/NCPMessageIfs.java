package nbench.protocol;

public interface NCPMessageIfs {
	int getID(String message) throws NCPException;
	String getName(int id) throws NCPException;
}
