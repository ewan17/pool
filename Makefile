CC	= gcc
AR	= ar
W = -W -Wall -g
INCLUDE = include
CFLAGS = $(W) $(addprefix -I, $(INCLUDE))

BIN = bin
TARGET = threadpool.a

TESTS = testpool 

.PHONY: clean bench bin

all:	bin	$(TARGET)

clean:
	rm -rf $(BIN) $(TARGET)

bench:
	@echo "bench mark"

bin:
	@mkdir -p $(BIN)

test:	bin	$(TESTS)

testpool:	testpool.o	$(TARGET)
	$(CC) $(CFLAGS) -o $(BIN)/$@ $(BIN)/$^
	chmod +x ./test/val.sh
	./test/val.sh $(BIN)/$@

%.o:	src/%.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

%.o:	test/%.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@

$(TARGET):	pool.o
	$(AR) rs $@ $(BIN)/pool.o

pool.o: src/pool.c
	$(CC) $(CFLAGS) -c $< -o $(BIN)/$@
