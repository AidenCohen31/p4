CC     := gcc
CFLAGS := -Wall -Werror 

SRCS   := client.c \
	server.c  \
	mkfs.c \

OBJS   := ${SRCS:c=o}
PROGS  := ${SRCS:.c=}

.PHONY: all
all: ${PROGS}

${PROGS} : % : %.o Makefile
	${CC} $< -o $@ udp.c

clean:
	rm -f ${PROGS} ${OBJS}

%.o: %.c Makefile
	${CC} ${CFLAGS} -c $<

libmfs.so: libmfs.o
	gcc -shared -Wl,-soname,libmfs.so -o libmfs.so libmfs.o -lc

libmfs.o: libmfs.c
	gcc -fPIC -g -c -Wall libmfs.c

main:
	gcc -o main main.c -Wall -L. -lmfs