CC=gcc
LDFLAGS=-m64 
LDSO=-rdynamic -ldl
INCLUDES=-I ../../src/broker/

SRC=get_shard_id.c
TARGET=getshardid

$(TARGET) : $(SRC)
	$(CC) $(INCLUDES) $(LDSO) $(LDFLAGS) -o $@ $<

clean :
	rm -rf $(TARGET)
