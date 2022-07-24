# important directories
SRC = src
BIN = bin
OBJ = obj
INC = include

# compilation parameters
CC      = gcc
CFLAGS  = -I $(INC) -gdwarf-5 -O2 -Winline
LDFLAGS = -lSDL2 -lrt -lportaudio

# name of final binary
FINBIN = mvemu.chip8

# identify sources and construct target objects
SOURCES = $(wildcard $(SRC)/*.c)
OBJECTS = $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SOURCES))

# prevent deletion of intermediary files and directories
.SECONDARY:

# top level rule
build: $(BIN)/$(FINBIN)

# ephemeral directory generation rule
%/:
	@mkdir -p $@

# wrapper rule that generates compile_commands.json (for clangd)
bear:
	bear -- $(MAKE)

# final binary generation rule
$(BIN)/$(FINBIN): $(OBJECTS) | $(BIN)/
	$(CC) -o $@ $^ $(LDFLAGS)

# individual object generation rule
$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)/
	$(CC) -c $(CFLAGS) -o $@ $<

# clean rule
clean:
	rm -rf $(BIN) $(OBJ)
