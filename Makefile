CC?=gcc
DEPS=jsmin.h

all: libjsmin.a

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

libjsmin.a: jsmin.o
	$(AR) rc $@ $<

clean:
	rm -f *.o *.a
