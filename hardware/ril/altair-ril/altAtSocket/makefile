OBJ_DIR = out
BIN_DIR = out


CFLAGS = -Wall 
SOURCES := atSocketTest.c
OBJS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
EXE = altAtTest

all : $(BIN_DIR)/$(EXE)

$(BIN_DIR)/$(EXE) : $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LFLAGS) $(LIBS)

$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $< -c -o $@
