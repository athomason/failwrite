CFLAGS=-g -Os -Wall

all: failwrite_ptrace failwrite_preload failwrite_preload_lib.so

failwrite_preload: failwrite_preload.c

failwrite_ptrace: failwrite_ptrace.c

failwrite_preload_lib.so: failwrite_preload_lib.c
	$(CC) failwrite_preload_lib.c $(CFLAGS) $(LDFLAGS) -ldl -shared -O3 -o failwrite_preload_lib.so -fPIC

clean:
	rm -f failwrite_ptrace failwrite_preload failwrite_preload_lib.so
