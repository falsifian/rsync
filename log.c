/* 
   Copyright (C) Andrew Tridgell 1998
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  logging and utility functions

  tridge, May 1998
  */
#include "rsync.h"

static FILE *logfile;


/****************************************************************************
  return the date and time as a string
****************************************************************************/
static char *timestring(void )
{
	static char TimeBuf[200];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

#ifdef HAVE_STRFTIME
	strftime(TimeBuf,sizeof(TimeBuf)-1,"%Y/%m/%d %T",tm);
#else
	strlcpy(TimeBuf, asctime(tm), sizeof(TimeBuf)-1);
#endif

	if (TimeBuf[strlen(TimeBuf)-1] == '\n') {
		TimeBuf[strlen(TimeBuf)-1] = 0;
	}

	return(TimeBuf);
}

static void logit(int priority, char *buf)
{
	if (logfile) {
		fprintf(logfile,"%s  %s", timestring(), buf);
		fflush(logfile);
	} else {
		syslog(priority, "%s", buf);
	}
}

void log_open(void)
{
	static int initialised;
	int options = LOG_PID;
	time_t t;
	char *logf;

	if (initialised) return;
	initialised = 1;

	logf = lp_log_file();
	if (logf && *logf) {
		logfile = fopen(logf, "a");
		return;
	}

#ifdef LOG_NDELAY
	options |= LOG_NDELAY;
#endif

#ifdef LOG_DAEMON
	openlog("rsyncd", options, lp_syslog_facility());
#else
	openlog("rsyncd", options);
#endif

#ifndef LOG_NDELAY
	logit(LOG_INFO,"rsyncd started\n");
#endif

	/* this looks pointless, but it is needed in order for the
	   C library on some systems to fetch the timezone info
	   before the chroot */
	t = time(NULL);
	localtime(&t);
}
		

/* this is the rsync debugging function. Call it with FINFO or FERROR */
void rprintf(int fd, const char *format, ...)
{
	va_list ap;  
	char buf[1024];
	int len;
	FILE *f=NULL;
	extern int am_daemon;
	/* recursion can happen with certain fatal conditions */
	static int depth;

	if (depth) return;

	depth++;

	va_start(ap, format);
	len = vslprintf(buf, sizeof(buf)-1, format, ap);
	va_end(ap);

	if (len < 0) exit_cleanup(1);

	if (len > sizeof(buf)-1) exit_cleanup(1);

	buf[len] = 0;

	if (fd == FLOG) {
		if (am_daemon) logit(LOG_INFO, buf);
		depth--;
		return;
	}

	if (am_daemon) {
		int priority = LOG_INFO;
		if (fd == FERROR) priority = LOG_WARNING;

		log_open();
		if (!io_multiplex_write(fd, buf, strlen(buf))) {
			logit(priority, buf);
		}

		depth--;
		return;
	}

	if (fd == FERROR) {
		f = stderr;
	} 

	if (fd == FINFO) {
		extern int am_server;
		if (am_server) 
			f = stderr;
		else
			f = stdout;
	} 

	if (!f) exit_cleanup(1);

	if (fwrite(buf, len, 1, f) != 1) exit_cleanup(1);

	if (buf[len-1] == '\r' || buf[len-1] == '\n') fflush(f);

	depth--;
}

void rflush(int fd)
{
	FILE *f = NULL;
	extern int am_daemon;
	
	if (am_daemon) {
		return;
	}

	if (fd == FERROR) {
		f = stderr;
	} 

	if (fd == FINFO) {
		extern int am_server;
		if (am_server) 
			f = stderr;
		else
			f = stdout;
	} 

	if (!f) exit_cleanup(1);
	fflush(f);
}


/* log the outgoing transfer of a file */
void log_send(struct file_struct *file)
{
	extern int module_id;
	if (lp_transfer_logging(module_id)) {
		rprintf(FLOG,"%d Sending %s [%s] %.0f %s\n",
			(int)getpid(), client_name(0), client_addr(0),
			(double)file->length, f_name(file));
	}
}

/* log the incoming transfer of a file */
void log_recv(struct file_struct *file)
{
	extern int module_id;
	if (lp_transfer_logging(module_id)) {
		rprintf(FLOG,"%d Receiving %s [%s] %.0f %s\n",
			(int)getpid(), client_name(0), client_addr(0),
			(double)file->length, f_name(file));
	}
}

/* called when the transfer is interrupted for some reason */
void log_exit(int code)
{
	if (code == 0) {
		extern struct stats stats;		
		rprintf(FLOG,"%d wrote %.0f bytes  read %.0f bytes  total size %.0f\n",
			(int)getpid(),
			(double)stats.total_written,
			(double)stats.total_read,
			(double)stats.total_size);
	} else {
		rprintf(FLOG,"%d transfer interrupted\n", (int)getpid());
	}
}

/* log the incoming transfer of a file for interactive use, this
   will be called at the end where the client was run */
void log_transfer(struct file_struct *file, char *fname)
{
	extern int verbose;

	if (!verbose) return;

	rprintf(FINFO,"%s\n", fname);
}

