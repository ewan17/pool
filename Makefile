CC	= gcc
W = -W -Wall -g
CFLAGS = $(W)
TESTS = testkiddie testmem testwork


clean:
	rm -rf $(TESTS) *.[ao] *.[ls]o

test:	$(TESTS)

testwork: testwork.o	kiddie.a
testmem: testmem.o	kiddie.a
testkiddie:	testkiddie.o	kiddie.a

%:	%.o
	$(CC) $(CFLAGS) $^ -o $@

%.o:	%.c kiddiepool.h
	$(CC) $(CFLAGS) -c $< 

kiddie.a:	kiddiepool.o
	$(AR) rs $@ kiddiepool.o

kiddiepool.o: kiddiepool.c kiddiepool.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c kiddiepool.c
