CC	= gcc
AR	= ar
W = -W -Wall -g
INCLUDE = include
CFLAGS = $(W) $(addprefix -I, $(INCLUDE)) -lpthread

BIN = bin
TARGET = threadpool.a

TESTS = testpool 

.PHONY: clean bin

all:	bin	$(TARGET)

clean:
	rm -rf $(BIN) $(TARGET)	

bin:
	@mkdir -p $(BIN)

test:	bin	$(TESTS)
bench:	bin benchpool

testpool:	testpool.o	$(TARGET)
	$(CC) $(CFLAGS) -o $(BIN)/$@ $(BIN)/$^
	chmod +x ./test/val.sh
	./test/val.sh $(BIN)/$@

benchpool:	benchpool.o	$(TARGET)
	$(CC) $(CFLAGS) -o $(BIN)/$@ $(BIN)/$^

%.o:	src/%.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

%.o:	test/%.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

$(TARGET):	pool.o
	$(AR) rs $@ $(BIN)/pool.o

pool.o: src/pool.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@