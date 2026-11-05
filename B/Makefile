FLAGS=-O3 -ffp-contract=off -Wall -fopenmp
EXEC=pulse
CC=gcc
all: $(EXEC)

$(EXEC): $(EXEC).c kern.S
	$(CC) $(FLAGS) $(EXEC).c -c -o $(EXEC).o -lm
	$(CC) -c kern.S -o kern.o
	$(CC) $(FLAGS) $(EXEC).o kern.o -o $(EXEC) -lm

clean:
	rm -f *.o $(EXEC)
