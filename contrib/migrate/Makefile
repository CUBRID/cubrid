CC = gcc

INCDIR=$(CUBRID)/include
BIN = migrate
OBJ = $(BIN).o
SRC = $(BIN).c
CFLAG = -g

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAG) -o $(BIN) $(OBJ) -ldl

$(OBJ): $(SRC)
	$(CC) $(CFLAG) -c $(SRC) -I$(INCDIR)

clean:
	rm -rf $(BIN) $(OBJ) 
