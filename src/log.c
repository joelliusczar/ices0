/* log.c
 * - Functions for logging in ices
 * Copyright (c) 2000 Alexander Hav�ng
 * Copyright (c) 2001 Brendan Cully
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include "definitions.h"
#include <sys/stat.h>

extern ices_config_t ices_config;
static const int LOG_FILENAME_LEN = 1024;
static const int ERR_BUFF_LEN = 1024;

/* Private function declarations */
static void ices_log_string(char *format, char *string);
static int ices_log_open_logfile(void);
static int ices_log_close_logfile(void);
static int ices_get_logfile_name(char *filename, int len);
static int ices_setup_output_redirects(void);

static char lasterror[BUFSIZE];
/* Public function definitions */

/* Initialize the log module, creates log file */
void ices_log_initialize(void) {
	if(ices_config.daemon) {
		ices_log_daemonize();
	}
	else {
#ifdef REDIRECT_LOGGING
	ices_setup_output_redirects();
#else
	if (!ices_log_open_logfile())
		ices_log("%s", ices_log_get_error());
#endif
	}
	ices_log("Logfile opened");
}

/* Shutdown the log module, close the logfile */
void ices_log_shutdown(void) {
	if (!ices_log_close_logfile())
		ices_log("%s", ices_log_get_error());
}

/* Close everything, start up with clean slate when
 * run as a daemon */
void ices_log_daemonize(void) {
	freopen("/dev/null", "r", stdin);
//#ifdef REDIRECT_LOGGING
	char namespace[LOG_FILENAME_LEN];
	if(ices_get_logfile_name(namespace, LOG_FILENAME_LEN) != 1) {
		return;
	}
	fflush(stdout);
	FILE *logfp = freopen(namespace,"a",stdout);
	if(ices_config.logfile) {
		struct stat finfo;
		if(fstat(fileno(ices_config.logfile), &finfo) != -1) {
			if(S_ISFIFO(finfo.st_mode)) {
					pclose(ices_config.logfile);
			}
			else {
				fclose(ices_config.logfile);
			}
			ices_config.logfile = NULL;

		}
	}

	if (!logfp) {
		ices_log_error("Error while opening %s, error: %s", namespace,
			ices_util_strerror(errno, buf, ERR_BUFF_LEN));
		return 0;
	}
	//redirect stderr into stdout so that it also goes to the log file
	int res = dup2(fileno(logfp), fileno(stderr));
	if(res == -1) {
		ices_log("can't redirect stderr to pipe");
	}
	ices_config.logfile = logfp;

//#else

	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);

	ices_log_reopen_logfile();
//#endif
}

/* Cycle the logfile, usually called from the SIGHUP handler */
int ices_log_reopen_logfile(void) {
	ices_log_close_logfile();
	return ices_log_open_logfile();
}

/* Log only if verbose mode is set. Prepend output with DEBUG:  */
void ices_log_debug(const char *fmt, ...) {
	char buff[BUFSIZE];
	va_list ap;

	if (!ices_config.verbose)
		return;

	va_start(ap, fmt);
#ifdef HAVE_VSNPRINTF
	vsnprintf(buff, BUFSIZE, fmt, ap);
#else
	vsprintf(buff, fmt, ap);
#endif
	va_end(ap);

	ices_log_string("DEBUG: %s\n", buff);
}

/* Log to console and file */
void ices_log(const char *fmt, ...) {
	char buff[BUFSIZE];
	va_list ap;

	va_start(ap, fmt);
#ifdef HAVE_VSNPRINTF
	vsnprintf(buff, BUFSIZE, fmt, ap);
#else
	vsprintf(buff, fmt, ap);
#endif
	va_end(ap);

	ices_log_string("%s\n", buff);
}

/* Store error information in module memory */
void ices_log_error(const char *fmt, ...) {
	char buff[BUFSIZE];
	va_list ap;

	va_start(ap, fmt);
#ifdef HAVE_VSNPRINTF
	vsnprintf(buff, BUFSIZE, fmt, ap);
#else
	vsprintf(buff, fmt, ap);
#endif
	va_end(ap);

	strncpy(lasterror, buff, BUFSIZE);
}

void ices_log_error_output(const char *fmt, ...) {
	char buff[BUFSIZE];
	va_list ap;

	va_start(ap, fmt);
#ifdef HAVE_VSNPRINTF
	vsnprintf(buff, BUFSIZE, fmt, ap);
#else
	vsprintf(buff, fmt, ap);
#endif
	va_end(ap);

	ices_log_error("%s",buff);
	ices_log_string("%s\n",buff);
}

/* Get last error from log module */
char *ices_log_get_error(void) {
	return lasterror;
}

/* redirect stdout and stderr to a pipe which tee uses to send
	data to the terminal and the logfile, so that we don't have to
	do this manual log to the screen and log to the file.
	Also, this should ensure we also catch the module errors in daemon mode
*/
int ices_setup_output_redirects(void) {
#ifdef REDIRECT_LOGGING
	char namespace[LOG_FILENAME_LEN];
	int cmdLen = 9;
	char cmd[LOG_FILENAME_LEN + cmdLen]
	if(ices_get_logfile_name(namespace, LOG_FILENAME_LEN) != 1) {
		return 0;
	}
	//invoke the system command to send output to log file and terminal
	sprintf(cmd, "tee -a '%s'", namespace);
	FILE* pipe = popen(cmd, "w");
	if(!pipe) {
		ices_log("can't create pipe");
		return 0;
	}
	int pno = fileno(pipe);
	int res1 = dup2(pno, fileno(stdout));
	if(res1 == -1) {
		pclose(pipe);
		ices_log("can't redirect stdout to pipe");
	}
	int res2 = dup2(pno, fileno(stderr));
	if(res2 == -1) {
		pclose(pipe);
		//revert standard out
		freopen("/dev/tty","w",stdout);
		ices_log("can't redirect stderr to pipe");
	}
	ices_log_close_logfile();
	ices_config.logfile = pipe;
#endif
	return 1;
}

/* Private function definitions */

/* Function to log string to both console and file */
static void ices_log_string(char *format, char *string) {
#ifdef REDIRECT_LOGGING
		printf(format, string);
		return;
#else
	if (ices_config.logfile) {
		fprintf(ices_config.logfile, format, string);
#ifndef HAVE_SETLINEBUF
		fflush(ices_config.logfile);
#endif
	}

	/* Don't log to console when daemonized */
	if (!ices_config.daemon)
		fprintf(stdout, format, string);
#endif
}

/* Open the ices logfile, create it if needed */
static int ices_log_open_logfile(void) {
	char namespace[LOG_FILENAME_LEN], buf[ERR_BUFF_LEN];
	FILE *logfp;

	if(ices_get_logfile_name(namespace, LOG_FILENAME_LEN) != 1) {
		return 0;
	}

	logfp = fopen(namespace, "a");

	if (!logfp) {
		ices_log_error("Error while opening %s, error: %s", namespace,
			ices_util_strerror(errno, buf, ERR_BUFF_LEN));
		return 0;
	}

	ices_config.logfile = logfp;
#ifdef HAVE_SETLINEBUF
	setlinebuf(ices_config.logfile);
#endif

	return 1;
}

/* Close ices' logfile */
static int ices_log_close_logfile(void) {
	if (ices_config.logfile)
		ices_util_fclose(ices_config.logfile);

	return 1;
}

static int ices_log_close_pipe(void) {
	if (ices_config.logfile)
		pclose(ices_config.logfile);
	ices_config.logfile = NULL;
	return 1;
}

static int ices_get_logfile_name(char *filename, int len) {
	if (!ices_config.base_directory ||
		strlen(ices_config.base_directory) > 1016
	) {
		ices_log_error("Base directory is invalid");
		return 0;
	}

	snprintf(filename, len, "%s/ices.log", ices_config.base_directory);

	return 1;
}
