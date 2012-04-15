#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <time.h>
#include <ctype.h>

void print_bank0 (unsigned char *rbuf, int n);
void print_bank1 (unsigned char *rbuf, int n);
void print_bank2 (unsigned char *rbuf, int n);

void process_bank0 (unsigned char *data, int len);

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

void
usage (void)
{
	fprintf (stderr, "usage: parsedata file\n");
	exit (1);
}

#define MAXBANKS 10
struct bank {
	unsigned char data[100 * 1024];
	int used;
};

struct bank banks[MAXBANKS];

time_t file_timestamp;

int
main (int argc, char **argv)
{
	int c;
	char *inname;
	FILE *inf;
	char buf[1000];
	struct tm tm;
	int off;
	struct bank *bp;
	int banknum;
	char *p;
	int val;

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind >= argc)
		usage ();

	inname = argv[optind++];

	if (optind != argc)
		usage ();

	if ((inf = fopen (inname, "r")) == NULL) {
		fprintf (stderr, "can't open %s\n", inname);
		exit (1);
	}

	if (fgets (buf, sizeof buf, inf) == NULL) {
		fprintf (stderr, "unexpected EOF\n");
		exit (1);
	}

	memset (&tm, 0, sizeof tm);
	if (sscanf (buf, "fitbit-log-%d-%d-%d-%2d%2d%2d",
		    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
		fprintf (stderr, "bad file format\n");
		exit (1);
	}
	tm.tm_year -= 1900;
	tm.tm_mon--;
	tm.tm_isdst = -1;
	file_timestamp = mktime (&tm);

	off = 0;
	bp = NULL;
	while (fgets (buf, sizeof buf, inf) != NULL) {
		if (sscanf (buf, "bank%d", &banknum) == 1) {
			if (banknum < 0 || banknum >= MAXBANKS) {
				fprintf (stderr, "invalid banknum %d\n",
					 banknum);
				exit (1);
			}
			bp = &banks[banknum];
			continue;
		}
		p = buf;
		while (sscanf (p, "%2x", &val) == 1) {
			if (bp->used >= sizeof bp->data) {
				fprintf (stderr, "bank %d overflow\n", 
					 (int)(bp-banks));
				exit (1);
			}
			bp->data[bp->used++] = val;
			p += 2;
		}
	}
		
	if (0) {
		print_bank0 (banks[0].data, banks[0].used);
		print_bank1 (banks[1].data, banks[1].used);
		print_bank2 (banks[2].data, banks[2].used);
		exit (0);
	}

	process_bank0 (banks[0].data, banks[0].used);

	return (0);
}

struct stepdata {
	struct stepdata *next;
	time_t device_timestamp;
	int steps;
	double activity;
};

void
process_bank0 (unsigned char *data, int len)
{
	FILE *df;
	char buf[1000], last[1000];
	struct tm tm;
	char tbuf[100];
	char *p;
	time_t device_lasttime, device_rectime;
	int off;
	struct stepdata *steps, *sp, **tailp, *lastsp;
	int time_offset;
	time_t t;

	steps = NULL;
	tailp = &steps;
	lastsp = NULL;

	off = 0;
	device_rectime = 0;
	while (off < len) {
		if ((data[off] & 0x80) == 0) {
			if (off + 4 > len)
				break;
			device_rectime = (data[off] << 24)
				| (data[off+1] << 16)
				| (data[off+2] << 8)
				| data[off+3];
			off += 4;
		} else {
			if ((sp = calloc (1, sizeof *sp)) == NULL) {
				fprintf (stderr, "out of memory\n");
				exit (1);
			}

			off++;
			sp->device_timestamp = device_rectime;
			sp->activity = (data[off++] - 10) / 10.0;
			sp->steps = data[off++];

			*tailp = sp;
			tailp = &sp->next;

			lastsp = sp;

			device_rectime += 60;
		}
	}

	time_offset = file_timestamp - lastsp->device_timestamp;
	
	last[0] = 0;
	if ((df = fopen ("fitbit.csv", "r")) != NULL) {
		while (fgets (buf, sizeof buf, df) != NULL)
			strcpy (last, buf);
		fclose (df);
	}
	p = last;
	while (*p && ! isdigit (*p))
		p++;
	device_lasttime = strtoul (p, NULL, 10);

	df = NULL;
	for (sp = steps; sp; sp = sp->next) {
		if (sp->device_timestamp > device_lasttime) {
			if (df == NULL) {
				if ((df = fopen ("fitbit.csv", "a")) == NULL) {
					fprintf (stderr, "can't write file\n");
					exit (1);
				}
			}

			t = sp->device_timestamp + time_offset;
			tm = *localtime (&t);
			strftime (tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", &tm);
					  
			fprintf (df, "%u,%s,%d,%.1f\n",
				 (unsigned int)sp->device_timestamp,
				 tbuf, sp->steps, sp->activity);
			device_lasttime = sp->device_timestamp;
		}
	}
	if (df)
		fclose (df);
}

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
	int val;

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

		val = up[0] | (up[1] << 8) | (up[2] << 16) | (up[3] << 24);
		printf (" [%d ?]", val);

		for (i = 0; i < 6; i++)
			printf (" %02x", *up++);
		printf ("\n");
		off += 16;
	}
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
