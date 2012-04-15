#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>

#include <libusb.h>

double get_secs (void);

unsigned char bank0ref[] = {
	0x49, 0x5c, 0x07, 0xbc, 0x81, 0x1a, 0x00, 0x49,
	0x5c, 0x0a, 0xc8, 0x85, 0x1a, 0x00, 0x49, 0x5e,
	0xd1, 0xa4, 0x85, 0x2e, 0x2c, 0x85, 0x36, 0x39,
	0x86, 0x2a, 0x1e, 0x87, 0x1e, 0x13, 0x49, 0x5f,
	0x84, 0xf0, 0x85, 0x0a, 0x00, 0x85, 0x18, 0x00,
	0x49, 0x5f, 0xc7, 0xf8, 0x85, 0x0a, 0x00, 0x85,
	0x1a, 0x00
};

unsigned char bank1ref[] = {
	0x00, 0x59, 0x5d, 0x49, 0x60, 0x38, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0xaa, 0x5e, 0x49, 0x40, 0x38, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x20, 0xd4, 0x5f, 0x49, 0x24, 0x33, 0x5b, 0x01,
	0x00, 0x00, 0x2a, 0x8c, 0x03, 0x00, 0x14, 0x00,
	0x36, 0xd4, 0x5f, 0x49, 0x2e, 0x33, 0x5b, 0x01,
	0x00, 0x00, 0x2a, 0x8c, 0x03, 0x00, 0x14, 0x00,
	0x5e, 0xd4, 0x5f, 0x49, 0x2e, 0x33, 0x5b, 0x01,
	0x00, 0x00, 0x2a, 0x8c, 0x03, 0x00, 0x14, 0x00
};

int current_packet_id;

void print_bank2 (unsigned char *rbuf, int n);


void dbg (char const *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void vdbg (char const *fmt, va_list args);

int xflag;
int vflag;

void dump_msg (unsigned char *msg, int msglen);

int getrandom (void);

int device_number, pairing_request, device_type, transmission_type;

void reset_tracker (void);
void ping_tracker (void);
void get_tracker_info (void);
int run_opcode (unsigned char *op, int oplen,
		unsigned char *payload, int plen,
		unsigned char *retbuf, int retlen);
void send_tracker_packet (unsigned char *buf, int len);
int get_data_bank (unsigned char *retbuf, int retlen);
int check_tracker_data_bank (int bank_id, int cmd, unsigned char *buf, int len);
int get_tracker_burst (unsigned char *retbuf, int retlen);
int run_data_bank_opcode (int idx, unsigned char *rbuf, int rbufsize);

int receive_message (unsigned char *msg, int avail, int timeout);

void soak (double duration);

void fitbit_ant_init (void);
void init_tracker_for_transfer (void);
void init_fitbit (void);
void init_device_channel (int device_number,
			  int pairing_request, int device_type,
			  int transmission_type);
void send_message (unsigned char *data, int n);


void protocol_reset (void);
void protocol_set_network_key (int network, unsigned char *key, int keylen);
void protocol_assign_channel (int channel_type, int network_number);
void protocol_set_channel_period (double secs);
void protocol_set_channel_frequency (int channel_freq);
void protocol_set_transmit_power (int power);
void protocol_set_search_timeout (double secs);
void protocol_set_channel_id (int device_number, int pairing_request,
			      int device_type, int transmission_type);
void protocol_open_channel (void);
int protocol_send_acknowledged_data (unsigned char *msg, int len);
int protocol_check_tx_response (void);
void protocol_close_channel (void);
int protocol_receive_acknowledged_reply (unsigned char *rbuf, int rbuflen);
int protocol_send_acknowledged_data (unsigned char *msg, int len);
int protocol_check_burst_response (unsigned char *retbuf, int retlen);

void check_ok_response (int op);

void wait_for_beacon (void);

int protocol_channel; /* self._chan */

void
dump (void *buf, int n)
{
	int i;
	int j;
	int c;

	for (i = 0; i < n; i += 16) {
		printf ("%04x: ", i);
		for (j = 0; j < 16; j++) {
			if (i+j < n)
				printf ("%02x ", ((unsigned char *)buf)[i+j]);
			else
				printf ("   ");
		}
		printf ("  ");
		for (j = 0; j < 16; j++) {
			c = ((unsigned char *)buf)[i+j] & 0x7f;
			if (i+j >= n)
				putchar (' ');
			else if (c < ' ' || c == 0x7f)
				putchar ('.');
			else
				putchar (c);
		}
		printf ("\n");

	}
}

void try_requests (void);


void
usage (void)
{
	fprintf (stderr, "usage: fitbit\n");
	exit (1);
}

libusb_context *ctx;

libusb_device *device;
struct libusb_device_descriptor desc;
libusb_device_handle *dev;
struct libusb_device_descriptor desc;

char *
errstr (int val)
{
	switch (val) {
	case LIBUSB_SUCCESS: return ("(no error)");
	case LIBUSB_ERROR_IO: return ("LIBUSB_ERROR_IO");
	case LIBUSB_ERROR_INVALID_PARAM: return ("LIBUSB_ERROR_INVALID_PARAM");
	case LIBUSB_ERROR_ACCESS: return ("LIBUSB_ERROR_ACCESS");
	case LIBUSB_ERROR_NO_DEVICE: return ("LIBUSB_ERROR_NO_DEVICE");
	case LIBUSB_ERROR_NOT_FOUND: return ("LIBUSB_ERROR_NOT_FOUND");
	case LIBUSB_ERROR_BUSY: return ("LIBUSB_ERROR_BUSY");
	case LIBUSB_ERROR_TIMEOUT: return ("LIBUSB_ERROR_TIMEOUT");
	case LIBUSB_ERROR_OVERFLOW: return ("LIBUSB_ERROR_OVERFLOW");
	case LIBUSB_ERROR_PIPE: return ("LIBUSB_ERROR_PIPE");
	case LIBUSB_ERROR_INTERRUPTED: return ("LIBUSB_ERROR_INTERRUPTED");
	case LIBUSB_ERROR_NO_MEM: return ("LIBUSB_ERROR_NO_MEM");
	case LIBUSB_ERROR_NOT_SUPPORTED: return ("LIBUSB_ERROR_NOT_SUPPORTED");
	case LIBUSB_ERROR_OTHER: return ("LIBUSB_ERROR_OTHER");
	default: return ("unknown error");
	}
}

void
die (char *str, int rc)
{
	fprintf (stderr, "die %s %d: %s\n", str, rc, errstr (rc));
	exit (1);
}

int intarg;

void
print_bank0 (unsigned char *data, int len)
{
	int i;
	time_t last_date_time, record_date;
	int time_index;
	struct tm tm;
	int steps, not_sure;
	double active_score;

	i = 0;
	last_date_time = 0;
	time_index = 0;
	while (i < len) {
		if ((data[i] & 0x80) == 0) {
			last_date_time = data[i+3]
				| (data[i+2]<<8)
				| (data[i+1]<<16)
				| (data[i]<<24);
			tm = *localtime (&last_date_time);
			printf ("Time: %d-%02d-%02d %02d:%02d:%02d\n",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			i += 4;
			time_index = 0;
		} else {
			record_date = last_date_time + 60 * time_index;
			tm = *localtime (&record_date);
			steps = data[i+2];
			active_score = (data[i+1] - 10) / 10.0;
			not_sure = data[i] - 0x81;

			printf ("%04d-%02d-%02d %02d:%02d:%02d ",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			printf ("unk %d; active %g; steps %d\n",
				not_sure, active_score, steps);
			i += 3;
			time_index++;
		}
	}
}

void
print_bank1 (unsigned char *data, int len)
{
	int off;
	time_t t;
	unsigned char *up;
	struct tm tm;
	int i;
	int steps;

	dump (data, len);

	off = 0;
	while (off + 16 < len) {
		up = data + off;
		t = up[0] | (up[1] << 8) | (up[2] << 16) | (up[3] << 24);
		up += 4;
		
		tm = *localtime (&t);
		printf ("%04d-%02d-%02d %02d:%02d:%02d ",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		printf (" %02x", *up++);
		printf (" %02x", *up++);

		steps = up[0] | (up[1] << 8) | (up[2] << 16) | (up[3] << 24);
		up += 4;
		printf (" [%d steps] ", steps);

		for (i = 0; i < 6; i++)
			printf (" %02x", *up++);
		printf ("\n");
		off += 16;
	}
}

void
print_bank4 (unsigned char *data, int len)
{
	/* strings for front panel display */
	dump (data, len);
}

FILE *outf;

void
output_bank (int bank, unsigned char *data, int len)
{
	int i;

	fprintf (outf, "bank%d\n", bank);
	for (i = 0; i < len; i++) {
		fprintf (outf, "%02x", data[i]);
		if ((i % 32) == 31)
			fprintf (outf, "\n");
	}
	if (i % 32)
		fprintf (outf, "\n");
	fprintf (outf, "\n");
}

void
intr (int sig)
{
	dbg ("alarm timeout\n");
	exit (1);
}

int
main (int argc, char **argv)
{
	int c;
	int rc;
	unsigned char rbuf[10000];
	int n;
	char filename[1000];
	time_t t;
	struct tm tm;
	char cmd[1000];

	if (0) {
		print_bank1 (bank1ref, sizeof bank1ref);
		exit (0);
	}

	while ((c = getopt (argc, argv, "xv")) != EOF) {
		switch (c) {
		case 'v':
			vflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage ();
		}
	}

	if (optind < argc)
		intarg = atoi (argv[optind++]);

	if (optind != argc)
		usage ();

	signal (SIGALRM, intr);
	alarm (14 * 60);

	t = time (NULL);
	tm = *localtime (&t);
	sprintf (filename, "fitbit-log-%04d-%02d-%02d-%02d%02d%02d",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		 tm.tm_hour, tm.tm_min, tm.tm_sec);

	if (libusb_init (&ctx) != 0) {
		fprintf (stderr, "libusb_init error\n");
		exit (1);
	}

	libusb_set_debug (ctx, 3);

	dev = libusb_open_device_with_vid_pid (ctx, 0x10c4, 0x84c4);
	device = libusb_get_device (dev);

	if ((rc = libusb_get_device_descriptor (device, &desc)) != 0)
		die ("get_desc", rc);

	if (0) {
		printf ("bLength = %d\n", desc.bLength);
		printf ("bDescriptorType = %d\n", desc.bDescriptorType);
		/* 0x110 = USB 1.1 */
		printf ("bcdUSB = 0x%x\n", desc.bcdUSB);
		/* class = 0 means class per interface */
		printf ("bDeviceClass = %d\n", desc.bDeviceClass);
		printf ("bDeviceSubClass = %d\n", desc.bDeviceSubClass);
		printf ("bDeviceProtocol = %d\n", desc.bDeviceProtocol);
		printf ("bMaxPacketSize0 = %d\n", desc.bMaxPacketSize0);
		printf ("idVendor = 0x%x\n", desc.idVendor);
		printf ("idProduct = 0x%x\n", desc.idProduct);
		printf ("bcdDevice = 0x%x\n", desc.bcdDevice);
		printf ("iManufacturer = %d\n", desc.iManufacturer);
		printf ("iProduct = %d\n", desc.iProduct);
		printf ("iSerialNumber = %d\n", desc.iSerialNumber);
		printf ("bNumConfigurations = %d\n", desc.bNumConfigurations);
	}

	if ((rc = libusb_set_configuration (dev, -1)) != 0)
		die ("set_config", rc);

	if ((rc = libusb_set_configuration (dev, 1)) != 0)
		die ("set_config", rc);

	if ((rc = libusb_claim_interface (dev, 0)) != 0)
		die ("claim", rc);

	// try_requests ();

	dbg ("*** fitbit_ant_init\n");
	fitbit_ant_init ();
	init_tracker_for_transfer ();

	get_tracker_info ();

	if ((outf = fopen (filename, "w")) == NULL) {
		fprintf (stderr, "can't create %s\n", filename);
		exit (1);
	}
	fprintf (outf, "%s\n", filename);

	n = run_data_bank_opcode (0, rbuf, sizeof rbuf);
	print_bank0 (rbuf, n);
	output_bank (0, rbuf, n);
		
	n = run_data_bank_opcode (1, rbuf, sizeof rbuf);
	print_bank1 (rbuf, n);
	output_bank (1, rbuf, n);

	n = run_data_bank_opcode (2, rbuf, sizeof rbuf);
	print_bank2 (rbuf, n);
	output_bank (2, rbuf, n);

	dbg ("resetting\n");
	if ((rc = libusb_reset_device (dev)) != 0)
		die ("libusb_reset_device", rc);

	fclose (outf);
	sprintf (cmd, "./parsedata %s", filename);
	dbg ("running: %s\n", cmd);


	rc = system (cmd);
	if (rc)
		dbg ("error: 0x%x\n", rc);

	dbg ("ok\n");


	return (0);
}

void
print_bank2 (unsigned char *rbuf, int n)
{
	int off;
	unsigned char *up;
	time_t start;
	time_t t;
	struct tm tm;
	int i;
	int val;

	printf ("bank2\n");
	off = 0;
	while (off + 15 <= n) {
		up = rbuf + off;
		t = up[0] | (up[1] << 8) | (up[2] << 16) | (up[3] << 24);
		if (off == 0)
			start = t;
		up += 4;
		tm = *localtime (&t);
		printf ("%04d-%02d-%02d %02d:%02d:%02d (%8.3f) ",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec,
			(t - start) / 3600.0);
		for (i = 0; i < 11; i++) {
			val = *up++;
			if (val == 0)
				printf (" %2s", ".");
			else
				printf (" %02x", val);
		}
		printf ("\n");

		off += 15;
	}
	if (off != n)
		printf ("dregs %d\n", n - off);
}
	

int
control_read (int request, int val, int idx, void *data, int datalen)
{
	int request_type;
	int n;

	request_type = LIBUSB_ENDPOINT_IN
		| LIBUSB_REQUEST_TYPE_VENDOR;
	n = libusb_control_transfer (dev, request_type,
				     request, val, idx,
				     data, datalen,
				     100);
	return (n);
}

int
control_write (int request, int val, int idx, void *data, int datalen)
{
	int request_type;
	int n;

	request_type = LIBUSB_ENDPOINT_OUT
		| LIBUSB_REQUEST_TYPE_VENDOR;
	n = libusb_control_transfer (dev, request_type,
				     request, val, idx,
				     data, datalen,
				     100);
	if (n != datalen)
		dbg ("control_write: wrote %d, ret %d\n", datalen, n);
	return (n);
}

void
try_requests ()
{
	int request, val, idx;
	unsigned char data[128];
	int timeout;
	int n;

	for (request = 0; request < 256; request++) {
		val = 0;
		idx = 0;
		timeout = 100;
		n = control_read (request, val, idx, data, sizeof data);
		if (n >= 0) {
			dbg ("request 0x%x got %d\n", request, n);
			dump (data, n);
		}
	}
}

int
bulk_read (unsigned char *buf, int len, int timeout)
{
	int endpoint, transferred;
	int rc;

	/* wMaxPacketSize from lsusb -vv */
	if (len > 64)
		len = 64;

	endpoint = LIBUSB_ENDPOINT_IN | 1;
	transferred = 0;

	rc = libusb_bulk_transfer (dev, endpoint, buf, len,
				   &transferred, timeout);
	if (rc < 0)
		return (rc);
	return (transferred);
}

int
bulk_write (unsigned char *buf, int len)
{
	int endpoint, transferred, timeout;
	int rc;

	/* wMaxPacketSize from lsusb -vv */
	if (len > 64)
		len = 64;

	endpoint = LIBUSB_ENDPOINT_OUT | 1;
	transferred = 0;
	timeout = 100;

	rc = libusb_bulk_transfer (dev, endpoint, buf, len,
				   &transferred, timeout);
	if (rc < 0) {
		dbg ("bulk write error: %d %s\n", rc, errstr (rc));
		exit (1);
	}

	if (transferred != len) {
		dbg ("bulk write: wanted %d only transferred %d\n",
			len, transferred);
		exit (1);
	}

	return (transferred);
}

/* from bases.py FitBitAnt.init */
void
fitbit_ant_init (void)
{
	int n;
	unsigned char buf[128];

	control_write (0x00, 0xFFFF, 0x0, NULL, 0);
        control_write (0x01, 0x2000, 0x0, NULL, 0);
	/*
	 * At this point, we get a 4096 buffer, then start all over
	 * again? Apparently doesn't require an explicit receive
	 */
        control_write (0x00, 0x0, 0x0, NULL, 0);
        control_write (0x00, 0xFFFF, 0x0, NULL, 0);
	control_write (0x01, 0x2000, 0x0, NULL, 0);
        control_write (0x01, 0x4A, 0x0, NULL, 0);

	/* Receive 1 byte, should be 0x2 */
	n = control_read (0xff, 0x370b, 0, buf, sizeof buf);
	if (n < 1) {
		dbg ("fitbit_ant_init: first read %d %s\n", n, errstr (n));
		exit (1);
	}
	if (buf[0] != 2) {
		dbg ("fitbit_ant_init: first read expected 2 got 0x%x\n",
			buf[0]);
		exit (1);
	}

        control_write (0x03, 0x800, 0x0, NULL, 0);

	memset (buf, 0, sizeof buf);
	buf[0] = 0x08;
	buf[4] = 0x40;
        control_write (0x13, 0x0, 0x0, buf, 16);

	control_write (0x12, 0x0C, 0x0, NULL, 0);
	
	dbg ("initial soak...\n");
	soak (0.010);
	dbg ("done\n");

}

void
init_tracker_for_transfer (void)
{
	unsigned char data[100];
	int n;

	protocol_channel = 0;

	init_fitbit ();
	wait_for_beacon ();
	reset_tracker ();

	while (1) {
		device_number = getrandom () & 0xffff;
		if (device_number != 0 && device_number != 0xffff)
			break;
	}
	pairing_request = 0;
	device_type = 1;
	transmission_type = 1;

	dbg ("init_tracker_for_transfer: tell device to change dev num\n");
	n = 0;
	data[n++] = 0x78;
        data[n++] = 0x02;
	data[n++] = device_number;
	data[n++] = device_number >> 8;
	data[n++] = 0x00;
	data[n++] = 0x00;
	data[n++] = 0x00;
	data[n++] = 0x00;
	protocol_send_acknowledged_data (data, n);
	
	protocol_close_channel ();
	/* 
	 * we're supposed to wait for EVENT_CHANNEL_CLOSED but it never
	 * seems to come
	 */
	soak (.2);

	dbg ("hoping channel close has happened...\n");

	init_device_channel (device_number,
			     pairing_request, device_type,
			     transmission_type);

	wait_for_beacon ();
	ping_tracker ();
}

void
reset_tracker (void)
{
	unsigned char data[]
		= { 0x78, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	dbg ("reset_tracker\n");
	protocol_send_acknowledged_data (data, sizeof data);
}

void
ping_tracker (void)
{
	unsigned char data[]
		= { 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	dbg ("ping_tracker\n");
	protocol_send_acknowledged_data (data, sizeof data);
}

void
get_tracker_info (void)
{
	unsigned char data[] 
		= { 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	unsigned char buf[1000];
	int n;

	dbg ("\n");
	dbg ("\n");
	dbg ("get_tracker_info\n");
	n = run_opcode (data, sizeof data,
			NULL, 0,
			buf, sizeof buf);
	dbg ("tracker info %d\n", n);
	dump (buf, n);

	dbg ("serial %02x:%02x:%02x:%02x:%02x\n",
	     buf[0], buf[1], buf[2], buf[3], buf[4]);
	dbg ("firmware version %d (0x%x)\n", buf[5], buf[5]);
	dbg ("bsl major %d (0x%x)\n", buf[6], buf[6]);
	dbg ("bsl minor %d (0x%x)\n", buf[7], buf[7]);
	dbg ("app major %d (0x%x)\n", buf[8], buf[8]);
	dbg ("app minor %d (0x%x)\n", buf[9], buf[9]);
	dbg ("in_mode_bsl %d (0x%x)\n", buf[10], buf[10]);
	dbg ("on_charger %d (0x%x)\n", buf[11], buf[11]);
	

}

int
run_data_bank_opcode (int idx, unsigned char *rbuf, int rbufsize)
{
	unsigned char data[100];
	int n;

	dbg ("run_data_bank_opcode %d\n", idx);

	n = 0;
	data[n++] = 0x22;
	data[n++] = idx;
	data[n++] = 0x00;
	data[n++] = 0x00;
	data[n++] = 0x00;
	data[n++] = 0x00;
	data[n++] = 0x00;

	n = run_opcode (data, n,
			NULL, 0,
			rbuf, rbufsize);
	return (n);
}

int
run_opcode (unsigned char *op, int oplen,
	    unsigned char *payload, int plen,
	    unsigned char *retbuf, int retlen)
{
	int try;
	unsigned char rbuf[100];
	int n;
	char logbuf[1000], *logp;
	int i;
	
	logp = logbuf;
	logp += sprintf (logp, "run_opcode");
	for (i = 0; i < oplen; i++)
		logp += sprintf (logp, " %02x", op[i]);
	dbg ("%s\n", logbuf);

	try = 0;
	while (1) {
		try++;
		if (try > 4) {
			dbg ("run_opcode failed\n");
			exit (1);
		}

		send_tracker_packet (op, oplen);
		n = protocol_receive_acknowledged_reply (rbuf, sizeof rbuf);
		if (n > 0) {
			if ((rbuf[0] & 0x07) != (current_packet_id & 0x07))
				continue;
			switch (rbuf[1]) {
			case 0x42:
				return (get_data_bank (retbuf, retlen));
				break;
			case 0x61:
			case 0x41:
				dbg ("can't handle run_opcode response\n");
				exit (1);
			default:
				dbg ("ignore respose 0x%x\n", rbuf[1]);
				break;
			}
		}
	}
}

int
get_data_bank (unsigned char *retbuf, int retlen)
{
	int used, togo, cmd;
	int bank_id, n;

	dbg ("get_data_bank\n");

	used = 0;
	togo = retlen;

	cmd = 0x70;

	for (bank_id = 0; bank_id < 1; bank_id++) {
		dbg( "get_data_bank %d\n", bank_id);

		cmd = (bank_id == 0) ? 0x70 : 0x60;
		n = check_tracker_data_bank (bank_id, cmd, retbuf + used, togo);
		dbg ("got %d\n", n);
		if (n < 0) {
			dbg ("check_tracker_data_bank got error\n");
			exit (1);
		}
		if (n == 0)
			break;
		used += n;
		togo -= n;
	}

	dbg ("data bank size %d\n", used);
	return (used);
}

int
check_tracker_data_bank (int bank_id, int cmd,
			 unsigned char *retbuf, int retlen)
{
	unsigned char data[100];
	int n;

	n = 0;
	data[n++] = cmd;
        data[n++] = 0x00;
	data[n++] = 0x02;
	data[n++] = bank_id;
	data[n++] = 0x00;
	data[n++] = 0x00;
	data[n++] = 0x00;
	send_tracker_packet (data, n);
	return (get_tracker_burst (retbuf, retlen));
}

int
get_tracker_burst (unsigned char *retbuf, int retlen)
{
	unsigned char buf[1000];
	int n;
	int size;

	n = protocol_check_burst_response (buf, sizeof buf);

	if (n < 8) {
		dbg ("get_tracker_burst runt\n");
		exit (1);
	}

	if (buf[1] != 0x81) {
		dbg ("get_tracker_burst: buf[1] != 0x81\n");
		exit (1);
	}

	size = buf[2] | (buf[3] << 8);
	if (size == 0)
		return (0);

	if (size + 8 > n) {
		dbg ("get_tracker_burst: invalid size\n");
		exit (1);
	}

	memcpy (retbuf, buf + 8, size);
	return (size);
}

int
protocol_check_burst_response (unsigned char *retbuf, int retlen)
{
	unsigned char buf[100];
	int n;
	int timeout;
	int used, togo;
	int try;
	int thistime;

	timeout = 100;

	used = 0;
	togo = retlen;

	try = 0;
	while (1) {
		try++;
		if (try >= 128) {
			dbg ("protocol_check_burst_response: no end\n");
			exit (1);
		}
		n = receive_message (buf, sizeof buf, timeout);
		if (n > 5 && buf[2] == 0x40 && buf[5] == 0x4) {
			dbg ("protocol_check_burst_response got 0x40\n");
			exit (1);
		} else if (0 && n > 4 && buf[2] == 0x4f) {
			thistime = n - 4 - 1;
			if (thistime > togo)
				thistime = togo;
			memcpy (retbuf + used, buf + 4, thistime);
			used += thistime;
			togo -= thistime;
			dbg ("check_burst_response failed because 0x4f\n");
			return (used);
		} else if (n > 4 && buf[2] == 0x50) {
			thistime = n - 4 - 1;
			if (thistime > togo)
				thistime = togo;
			memcpy (retbuf + used, buf + 4, thistime);
			used += thistime;
			togo -= thistime;
			if (buf[3] & 0x80) {
				dbg ("check_burst_response success\n");
				return (used);
			}
		}
	}
}

int
gen_packet_id (void)
{
	int seq;

	seq = ++current_packet_id;

	return (0x38 | (seq & 0x07));
}

void
send_tracker_packet (unsigned char *buf, int len)
{
	unsigned char data[100];
	int n;

	n = 0;
	data[n++] = gen_packet_id ();
	memcpy (data + n, buf, len);
	n += len;
	protocol_send_acknowledged_data (data, n);
}

int
protocol_send_acknowledged_data (unsigned char *msg, int len)
{
	unsigned char data[100];
	int n;
	int try;

	n = 0;
	data[n++] = 0x4f;
	data[n++] = protocol_channel;
	memcpy (data + n, msg, len);
	n += len;

	try = 0;
	while (1) {
		try++;
		if (try > 8) {
			dbg ("send_acknoledged_data failed\n");
			exit (1);
			return (-1);
		}
		send_message (data, n);
		if (protocol_check_tx_response () >= 0)
			break;
		
	}

	return (0);
}

int
protocol_check_tx_response (void)
{
	int try;
	int timeout;
	int n;
	unsigned char msg[100];
	int code;

	try = 0;

	while (1) {
		try++;
		if (try > 16)
			return (-1);

		timeout = 100;
		n = receive_message (msg, sizeof msg, timeout);
		if (n >= 6 && msg[2] == 0x40) {
			dump_msg (msg, n);

			code = msg[5];

			if (code == 5) /* EVENT_TRANSFER_TX_COMPLETED */
				break;
			if (code == 6) {
				dbg ("tx failed\n");
				exit (1);
			}
		}
			
	}

	return (0);
}

void
protocol_close_channel (void)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("close channel\n");

	op = 0x4c;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	send_message (data, n);
	check_ok_response (op);
}

void
wait_for_beacon (void)
{
	unsigned char msg[100];
	double start, now;

	dbg ("wait_for_beacon\n");
	start = get_secs ();
	while (1) {
		now = get_secs ();
		if (now - start > 13 * 60) {
			dbg ("timeout waiting for beacon\n");
			exit (1);
		}
		if (receive_message (msg, sizeof msg, 100) > 0) {
			if (msg[2] == 0x4e)
				break;
		}
	}
	dbg ("beacon received\n\n");
}

void
init_fitbit (void)
{
	device_number = 0xffff;
	pairing_request = 0;
	device_type = 1;
	transmission_type = 1;

	init_device_channel (device_number,
			     pairing_request, device_type,
			     transmission_type);
}

int
getrandom (void)
{
	static int beenhere;
	static int fd;
	int val;
	if (beenhere == 0) {
		beenhere = 1;
		if ((fd = open ("/dev/urandom", O_RDONLY)) < 0) {
			fprintf (stderr, "can't open /dev/urandom\n");
			exit (1);
		}
	}
	read (fd, &val, sizeof val);
	val &= 0x7fffffff;
	return (val);
}

/* in fitbit.py */
void
init_device_channel (int device_number,
		     int pairing_request, int device_type,
		     int transmission_type)
{
	int network;
	unsigned char key[8];

	protocol_reset ();

	network = 0;
	memset (key, 0, 8);
	protocol_set_network_key (network, key, 8);
	protocol_assign_channel (0, 0);
	protocol_set_channel_period (0.125);
	protocol_set_channel_frequency (2);
	protocol_set_transmit_power (3);
	protocol_set_search_timeout (-1);
	protocol_set_channel_id (device_number, pairing_request,
				 device_type, transmission_type);
	protocol_open_channel ();
}

/* in protocol.py: reset */
void
protocol_reset (void)
{
	unsigned char data[100];
	int n;

	dbg ("ant reset requested\n");
	n = 0;
	data[n++] = 0x4a;
	data[n++] = 0;
	send_message (data, n);

	soak (0.100);
}

void
protocol_set_network_key (int network, unsigned char *key, int keylen)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("set_network_key\n");

	op = 0x46;

	n = 0;
	data[n++] = op;
	data[n++] = network;
	memcpy (data + n, key, keylen);
	n += keylen;
	send_message (data, n);
	check_ok_response (op);
}
	
void
protocol_assign_channel (int channel_type, int network_number)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("assign channel\n");

	op = 0x42;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	data[n++] = channel_type;
	data[n++] = network_number;
	send_message (data, n);
	check_ok_response (op);
}
	
int
protocol_receive_acknowledged_reply (unsigned char *rbuf, int rbuflen)
{
	int timeout;
	int try;
	int n;
	unsigned char rawbuf[100];
	int off, tail;

	timeout = 100;

	dbg ("receive_acknowledged_reply\n");
	try = 0;
	while (1) {
		try++;
		if (try >= 30) {
			dbg ("fail\n");
			exit (1);
		}
		n = receive_message (rawbuf, sizeof rawbuf, timeout);
		if (n > 4 && rawbuf[2] == 0x4f) {
			off = 4;
			tail = n - off;
			memcpy (rbuf, rawbuf + off, tail);
			return (tail);
		}
	}
}



void
protocol_set_channel_period (double secs)
{
	int op;
	unsigned char data[100];
	int n;
	int period;

	period = secs * 32768;

	dbg ("set_channel_period\n");

	op = 0x43;
	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	data[n++] = period;
	data[n++] = period >> 8;
	send_message (data, n);
	check_ok_response (op);
}

void
protocol_set_channel_frequency (int channel_freq)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("set_channel_frequency\n");

	op = 0x45;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	data[n++] = channel_freq;
	send_message (data, n);
	check_ok_response (op);
}

void
protocol_set_transmit_power (int power)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("set_transmit_power\n");

	op = 0x47;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	data[n++] = 0;
	data[n++] = power;
	send_message (data, n);
	check_ok_response (op);
}

void
protocol_set_search_timeout (double secs)
{
	int op;
	unsigned char data[100];
	int n;
	int code;

	if (secs < 0) {
		code = 0xff;
	} else {
		code = secs / 2.5;
		if (code > 0xff)
			code = 0xff;
	}

	dbg ("set_search_timeout\n");

	op = 0x44;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	data[n++] = code;
	send_message (data, n);
	check_ok_response (op);
}

void
protocol_set_channel_id (int device_number, int pairing_request,
			 int device_type, int transmission_type)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("set_channel_id\n");

	op = 0x51;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	data[n++] = device_number;
	data[n++] = device_number >> 8;
	data[n++] = (pairing_request ? 0x80 : 0) | device_type;
	data[n++] = transmission_type;
	send_message (data, n);
	check_ok_response (op);
}

void
protocol_open_channel (void)
{
	int op;
	unsigned char data[100];
	int n;

	dbg ("open_channel\n");

	op = 0x4b;

	n = 0;
	data[n++] = op;
	data[n++] = protocol_channel;
	send_message (data, n);
	check_ok_response (op);
}

/* in protocol.py: _send_message */
void
send_message (unsigned char *data, int n)
{
	unsigned char pkt[10000];
	int pktlen;
	int cksum;
	int i;
	char logbuf[1000];
	char *logp;
	
	pktlen = 0;
	pkt[pktlen++] = 0xa4;
	pkt[pktlen++] = n - 1;
	memcpy (pkt + pktlen, data, n);
	pktlen += n;
	cksum = 0;
	for (i = 0; i < pktlen; i++)
		cksum ^= pkt[i];
	pkt[pktlen++] = cksum;

	logp = logbuf;
	logp += sprintf (logp, ">>>");
	for (i = 0; i < pktlen; i++)
		logp += sprintf (logp, " %02x", pkt[i]);
	dbg ("%s\n", logbuf);

	n = bulk_write (pkt, pktlen);
	if (n != pktlen) {
		printf ("bulk_write error %d %d\n", pktlen, n);
		exit (1);
	}
}

unsigned char input_buffer[10000];
int input_buffer_used;

int
get_more_data (int timeout)
{
	int thistime, n;
	int i;
	char logbuf[1000];
	char *logp;

	thistime = sizeof input_buffer - input_buffer_used;
	if (thistime > 64)
		thistime = 64;
	n = bulk_read (input_buffer + input_buffer_used, thistime, timeout);
	if (n < 0)
		return (n);
	if (xflag) {
		logp = logbuf;
		logp += sprintf (logp, "<<<");
		for (i = 0; i < n; i++)
			logp += sprintf (logp, " %02x",
					 input_buffer[input_buffer_used + i]);
		dbg ("%s\n", logbuf);
	}
	input_buffer_used += n;
	return (input_buffer_used);
}

void
discard (int n)
{
	int tail;

	if (n > input_buffer_used) {
		dbg ("invalid arg to discard\n");
		exit (1);
	}
	tail = input_buffer_used - n;
	memmove (input_buffer, input_buffer + n, tail);
	input_buffer_used = tail;
}

int
receive_message (unsigned char *msg, int avail, int timeout)
{
	int off;
	int payload_size, total_pktsize;
	int cksum;
	int rc;
	char logbuf[1000], *logp;
	int i;

	while (1) {
		for (off = 0; off < input_buffer_used; off++) {
			if (input_buffer[off] == 0xa4
			    || input_buffer[off] == 0xa5)
				break;
		}
		if (off > 0)
			discard (off);

		if (input_buffer_used < 4)
			goto need_more;

		payload_size = input_buffer[1];
		if (payload_size > 32) {
			/* invalid length */
			discard (1);
			continue;
		}

		total_pktsize = 3 + payload_size + 1;
		if (input_buffer_used < total_pktsize)
			goto need_more;

		cksum = 0;
		for (off = 0; off < total_pktsize; off++)
			cksum ^= input_buffer[off];

		if (cksum != 0) {
			dbg ("invalid cksum\n");
			dump (input_buffer, total_pktsize);
			discard (total_pktsize);
			continue;
		}

		break;

	need_more:
		if ((rc = get_more_data (timeout)) < 0)
			return (rc);
	}

	memcpy (msg, input_buffer, total_pktsize);
	discard (total_pktsize);

	logp = logbuf;
	logp += sprintf (logp, "rcv");
	for (i = 0; i < total_pktsize; i++)
		logp += sprintf (logp, " %02x", msg[i]);
	dbg ("%s\n", logbuf);

	return (total_pktsize);
}	

double
get_secs (void)
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return (tv.tv_sec + tv.tv_usec / 1e6);
}

void
soak (double duration)
{
	int timeout;
	unsigned char msg[128];
	int n;
	double start, now;
	int i;
	char logbuf[1000];
	char *logp;

	start = get_secs ();

	while (1) {
		timeout = 10;
		n = receive_message (msg, sizeof msg, timeout);
		
		if (n > 0) {
			logp = logbuf;
			logp += sprintf (logp, "soak:");
			for (i = 0; i < n; i++)
				logp += sprintf (logp, " %02x", msg[i]);
			dbg ("%s\n", logbuf);
		}

		if (n >= 6 && msg[2] == 0x40) {
			dump_msg (msg, n);
		}

		if (n >= 2 && msg[2] == 0x6f) {
			int val;
			printf ("startup message:");
			val = msg[3];
			if (val == 0) {
				printf (" POWER_ON_RESET");
			} else {
				if (val & 0x01)
					printf (" HARDWARE_RESET_LINE");
				if (val & 0x02)
					printf (" WATCH_DOG_RESET");
				if (val & 0x20)
					printf (" COMMAND_RESET");
				if (val & 0x40)
					printf (" SYNCHRONOUS_RESET");
				if (val & 0x80)
					printf (" SUSPEND_RESET");
			}
			printf ("\n");
		}

		now = get_secs ();
		if (now > start + duration)
			break;
	}
}	

char *
response_str (int code)
{
	switch (code) {
	case 0: return ("noerr");
	case 1: return ("EVENT_RX_SEARCH_TIMEOUT");
	case 2: return ("EVENT_RX_FAIL");
	case 3: return ("EVENT_TX");
	case 4: return ("EVENT_TRANSFER_RX_FAILED");
	case 5: return ("EVENT_TRANSFER_TX_COMPLETED");
	case 6: return ("EVENT_TRANSFER_TX_FAILED");
	case 7: return ("EVENT_CHANNEL_CLOSED");
	case 8: return ("EVENT_RX_FAIL_GO_TO_SEARCH");
	case 9: return ("EVENT_CHANNEL_COLLISION");
	case 10: return ("EVENT_TRANSFER_TX_START");
	case 21: return ("CHANNEL_IN_WRONG_STATE");
	case 22: return ("CHANNEL_NOT_OPENED");
	case 24: return ("CHANNEL_ID_NOT_SET");
	case 25: return ("CLOSE_ALL_CHANNELS");
	case 31: return ("TRANSFER_IN_PROGRESS");
	case 32: return ("TRANSFER_SEQUENCE_NUMBER_ERROR");
	case 33: return ("TRANSFER_IN_ERROR");
	case 39: return ("MESSAGE_SIZE_EXCEEDS_LIMIT");
	case 40: return ("INVALID_MESSAGE");
	case 41: return ("INVALID_NETWORK_NUMBER");
	case 48: return ("INVALID_LIST_ID");
	case 49: return ("INVALID_SCAN_TX_CHANNEL");
	case 51: return ("INVALID_PARAMETER_PROVIDED");
	case 52: return ("EVENT_SERIAL_QUE_OVERFLOW");
	case 53: return ("EVENT_QUE_OVERFLOW");
	case 64: return ("NVM_FULL_ERROR");
	case 65: return ("NVM_WRITE_ERROR");
	case 112: return ("USB_STRING_WRITE_FAIL");
	case 174: return ("MESG_SERIAL_ERROR_ID");
	default: return ("");
	}
}

void
dump_msg (unsigned char *msg, int msglen)
{
	int chan, msgid, code;
	char logbuf[1000];
	char *logp;
	int i;

	logp = logbuf;
	logp += sprintf (logp, "msg");
	for (i = 0; i < msglen; i++)
		logp += sprintf (logp, " %02x", msg[i]);
	
	if (msglen >= 6 && msg[2] == 0x40) {
		chan = msg[3];
		msgid = msg[4];
		code = msg[5];
		logp += sprintf (logp, " chan %d; msgid 0x%x; code %d %s",
				 chan, msgid, code, response_str (code));
	}
	dbg ("%s\n", logbuf);
}

void
check_ok_response (int op)
{
	unsigned char msg[128];
	int n;
	int timeout;
	int chan, msgid, msgcode;

	while (1) {
		timeout = 100;
		if ((n = receive_message (msg, sizeof msg, timeout)) < 0) {
			dbg ("check_ok_response: no message\n");
			exit (1);
		}
		
		if (n < 6) {
			dbg ("check_ok_response: runt %d\n", n);
			continue;
		}

		if (msg[2] != 0x40) {
			dbg ("check_ok_response: bad pkt type\n");
			continue;
		}

		dump_msg (msg, n);

		chan = msg[3];
		msgid = msg[4];
		msgcode = msg[5];

		if (msgid != op) {
			dbg ("check_ok_response: wrong op\n");
			continue;
		}

		if (msgcode == 0)
			break;

		dbg ("error\n");
		exit (1);
	}

	dbg ("\n");
	return;
}

void
dbg (char const *fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vdbg (fmt, args);
	va_end (args);
}

void
vdbg (char const *fmt, va_list args)
{
	char buf[10000];
	char *p;
	struct tm tm;
	struct timeval tv;
	time_t t;
	int binchars;
	static unsigned long last_millitime;
	unsigned long this_millitime;
	int delta;

	gettimeofday (&tv, NULL);
	t = tv.tv_sec;
	tm = *localtime (&t);

	p = buf;

	sprintf (p, "%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec,
		 (int)(tv.tv_usec / 1000));
	p += strlen (p);

	this_millitime = (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec) 
		* 1000 + tv.tv_usec / 1000;

	if (last_millitime == 0)
		last_millitime = this_millitime;

	delta = this_millitime - last_millitime;
	last_millitime = this_millitime;

	if (delta < 0)
		delta = 0;

	sprintf (p, "%5d ", delta);
	p += strlen (p);

	vsprintf (p, fmt, args);

	p += strlen (p);
	while (p != buf && (p[-1] == '\n' || p[-1] == '\r'))
		*--p = 0;

	binchars = 0;
	for (p = buf; *p && binchars < 20; p++) {
		int c = *p;
		if ((c >= ' ' && c < 0177) || c == '\t' || c == '\n') {
			putc (c, stdout);
		} else {
			binchars++;
			putc ('.', stdout);
		}
	}
	putc ('\n', stdout);
	fflush (stdout);
}
