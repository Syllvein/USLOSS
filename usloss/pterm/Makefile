COBJS = pterm.o
CC=gcc
CFLAGS  = -DBSD_COMP -g

pterm : $(COBJS)
	$(CC) $(CFLAGS) -o pterm $(COBJS)
clean:
	rm -f $(COBJS) pterm
