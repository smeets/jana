
JANA_CC=$(CXX)
JANA_LD=$(CXX)

FINAL_CXX_FLAGS=-std=c++14

JANA_CLI_NAME=jana-cli
JANA_CLI_OBJ=client.o

JANA_SERVER_NAME=jana-server
JANA_SERVER_OBJ=server.o

%.o: %.cpp
	$(JANA_CC) -c $< $(FINAL_CXX_FLAGS)

$(JANA_SERVER_NAME): $(JANA_SERVER_OBJ)
	$(JANA_LD) -o $@ $^

$(JANA_CLI_NAME): $(JANA_CLI_OBJ)
	$(JANA_LD) -o $@ $^

.PHONY: clean

clean:
	rm -f ./*.o
	rm -f ./$(JANA_SERVER_NAME)
	rm -f ./$(JANA_CLI_NAME)