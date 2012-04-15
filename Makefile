CFLAGS = -g -Wall `pkg-config --cflags libusb-1.0`
LIBS = `pkg-config --libs libusb-1.0`

all: fitbit parsedata

fitbit: fitbit.o
	$(CC) $(CFLAGS) -o fitbit fitbit.o $(LIBS)

parsedata: parsedata.o
	$(CC) $(CFLAGS) -o parsedata parsedata.o

clean:
	rm -f *.o *~ fitbit parsedata
