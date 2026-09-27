CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -lpthread

all: planificador

planificador: src/planificador.c
	$(CC) $(CFLAGS) -o planificador src/planificador.c

clean:
	rm -f planificador
