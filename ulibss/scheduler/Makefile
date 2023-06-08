CC=gcc
FLAGS=
TARGETS=scheduler cli_demo

all: $(TARGETS)

$(TARGETS): %: %.c
	$(CC) -o $@ $^ $(FLAGS)

clean:
	rm  -f $(TARGETS)
	rm  -f *.o