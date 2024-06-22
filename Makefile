CC	= gcc
W = -W -Wall -g
INCLUDE = include
CFLAGS = $(W) $(addprefix -I, $(INCLUDE))

BIN = bin

TESTS = testpool

.PHONY: clean bench

clean:
	rm -rf $(BIN) $(TESTS) *.[ao] *.[ls]o

test:	$(TESTS)

testpool: testpool.o	pool.a
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $(BIN)/$@ $^
	chmod +x ./test/val.sh
	./test/val.sh $(BIN)/$@

bench:
	@echo "bench mark"

%.o:	src/%.c
	$(CC) $(CFLAGS) -c $<

%.o:	test/%.c
	$(CC) $(CFLAGS) -c $<

pool.a:	pool.o
	$(AR) rs $@ pool.o

pool.o: src/pool.c
	$(CC) $(CFLAGS) -c $< 
