CC	= gcc
AR	= ar
W = -W -Wall -g
INCLUDE = include
CFLAGS = $(W) $(addprefix -I, $(INCLUDE)) -lpthread

LIB = lib
BIN = bin
TARGET = $(LIB)/threadpool.a

TESTS = testpool 

.PHONY: clean bin

all:	bin	$(TARGET)

clean:
	rm -rf $(BIN) $(LIB) $(TARGET)	

bin:
	@mkdir -p $(BIN)
	@mkdir -p $(LIB)

test:	bin	$(TESTS)
bench:	bin benchpool

testpool:	testpool.o	$(TARGET)
	$(CC) $(CFLAGS) -o $(BIN)/$@ $(BIN)/$^
	chmod +x ./test/val.sh
	./test/val.sh $(BIN)/$@

benchpool:	benchpool.o $(LIB)/jhs.a $(TARGET)
	$(CC) -DNDEBUG $(CFLAGS) -o $(BIN)/$@ $(BIN)/$^
	./$(BIN)/$@

%.o:	src/%.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

%.o:	test/%.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

$(TARGET):	pool.o
	$(AR) rs -o $@ $(BIN)/$^

$(LIB)/jhs.a:	thpool.o
	$(AR) rs -o $@ $(BIN)/$^

thpool.o: test/jhs/thpool.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

pool.o: src/pool.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@