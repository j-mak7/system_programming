#all: myls.c
#	gcc -DFL=0 myls.c -o ls
#asm: myls.c
#	gcc -DFL=1 myls.c -o ls
#clean:
#	rm ls
CC=gcc
CFLAGS=-Wall -g
ASFLAGS=-DF=1
SOURCES=myls.c wrapper.c print.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=ls

all: $(SOURCES) $(EXECUTABLE)

clean:
	rm -rf $(EXECUTABLE) $(OBJECTS)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.c.o:
	$(CC) $(ASFLAGS) $< -o $@
