CPUC      = gcc
CFLAGS    = -O3 -Wall -pthread -ffp-contract=off
FFLAGS    = -O3 -Wall -ffp-contract=off -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables
FLINK     = -static -nostdlib -no-pie -Wl,--build-id=none -lgcc
DEFS      =
LIBS      = -Wl,--as-needed -lm

all:
	$(CPUC) -c p1asm.S -o p1asm.o
	$(CPUC) $(CFLAGS) $(DEFS) qt.c p1asm.o -o qtd $(LIBS) 2>/dev/null || $(CPUC) $(CFLAGS) $(DEFS) qt.c p1asm.o -o qtd -lm
	$(CPUC) $(FFLAGS) $(DEFS) qf.c p1asm.o -o qt $(FLINK) 2>/dev/null || $(CPUC) $(CFLAGS) $(DEFS) qt.c p1asm.o -o qt -lm

clean:
	rm -f qt qtd p1asm.o
