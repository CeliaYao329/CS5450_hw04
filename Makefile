CC=g++
CFLAGS=-I. 
DEPS 			= process.h
OBJS 			= process.o
STOPOBJS		= stopall.o
ALLEXEC			= process stopall

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(ALLEXEC)

process: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

stopall: $(STOPOBJS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *.o $(ALLEXEC)