/*
  Copyright 2011-2014 James Hunt <james@jameshunt.us>

  This file is part of Clockwork.

  Clockwork is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Clockwork is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Clockwork.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "clockwork.h"
#include "pendulum.h"
#include "pendulum_funcs.h"

#include <zmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <netdb.h>
#include <getopt.h>

#include "policy.h"

#define MODE_RUN  0
#define MODE_DUMP 1

#define PROTOCOL_VERSION         1
#define PROTOCOL_VERSION_STRING "1"

typedef struct {
	void *zmq;
	void *listener;
	const char *fqdn;
	const char *gatherers;

	int   mode;
	int   trace;
	int   daemonize;

	char *masters[8];
	int   nmasters;
	int   current_master;
	int   timeout;

	struct {
		int64_t next_run;
		int     interval;
	} schedule;
} client_t;

static char* s_myfqdn(void)
{
	char nodename[1024];
	nodename[1023] = '\0';
	gethostname(nodename, 1023);

	struct addrinfo hints, *info, *p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family   = AF_UNSPEC; /*either IPV4 or IPV6*/
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_CANONNAME;

	int rc = getaddrinfo(nodename, NULL, &hints, &info);
	if (rc != 0) {
		cw_log(LOG_ERR, "Failed to lookup %s: %s", nodename, gai_strerror(rc));
		return NULL;
	}

	char *ret = NULL;
	for (p = info; p != NULL; p = p->ai_next) {
		if (strcmp(p->ai_canonname, nodename) == 0) continue;
		ret = strdup(p->ai_canonname);
		break;
	}

	freeaddrinfo(info);
	return ret ? ret : strdup(nodename);
}

static cw_pdu_t *s_sendto(void *socket, cw_pdu_t *pdu, int timeout)
{
	if (cw_pdu_send(socket, pdu) != 0) {
		cw_log(LOG_ERR, "Failed to send %s PDU to remote peer: %s",
			pdu->type, zmq_strerror(errno));
		return NULL;
	}

	zmq_pollitem_t socks[] = {{ socket, 0, ZMQ_POLLIN, 0 }};
	int rc = zmq_poll(socks, 1, timeout);
	if (rc == 1)
		return cw_pdu_recv(socket);
	return NULL;
}

static void s_cogd_run(client_t *c)
{
	cw_pdu_t *pdu, *reply;
	pdu = cw_pdu_recv(c->listener);
	assert(pdu);

	if (strcmp(pdu->type, "INFO") == 0) {
		char *code = cw_pdu_text(pdu, 1);
		cw_log(LOG_INFO, "inbound INFO request:\n%s", code);

		reply = cw_pdu_make(pdu->src, 2, "OK", "");
		cw_pdu_send(c->listener, reply);

	} else {
		cw_log(LOG_INFO, "unrecognized PDU: '%s'", pdu->type);
		reply = cw_pdu_make(pdu->src, 2, "ERROR", "Unrecognized PDU");
		cw_pdu_send(c->listener, reply);
	}
}

static void *s_connect(client_t *c)
{
	void *client = zmq_socket(c->zmq, ZMQ_DEALER);
	if (!client) return NULL;

	cw_pdu_t *ping = cw_pdu_make(NULL, 2, "PING", PROTOCOL_VERSION_STRING);
	cw_pdu_t *pong;

	char endpoint[256] = "tcp://";
	int i;
	for (i = (c->current_master + 1) % 8; ; i = (i + 1) % 8) {
		if (!c->masters[i]) continue;

		strcat(endpoint+6, c->masters[i]);
		cw_log(LOG_DEBUG, "Attempting to connect to %s", endpoint);
		int rc = zmq_connect(client, endpoint);
		assert(rc == 0);

		/* send the PING */
		pong = s_sendto(client, ping, c->timeout);
		if (pong) {
			if (strcmp(pong->type, "PONG") != 0) {
				cw_log(LOG_ERR, "Unexpected %s response from master %i (%s) - expected PONG",
					pong->type, i+1, c->masters[i]);

			} else if (atoi(cw_pdu_text(pong, 1)) != PROTOCOL_VERSION) {
				cw_log(LOG_ERR, "Upstream server speaks protocol %s (we want %i)",
					cw_pdu_text(pong, 1), PROTOCOL_VERSION);
			} else break;

		} else {
			if (errno == 0)
				cw_log(LOG_ERR, "No response from master %i (%s)",
					i+1, c->masters[i]);
			else
				cw_log(LOG_ERR, "Unexpected error talking to master %i (%s): %s",
					i+1, c->masters[i], zmq_strerror(errno));
		}

		if (i == c->current_master) {
			cw_log(LOG_ERR, "No masters were reachable; giving up");
			return NULL;
		};
	}

	cw_log(LOG_DEBUG, "setting current master idx to %i", i);
	c->current_master = i;
	return client;
}

static void s_cfm_run(client_t *c)
{
	cw_log(LOG_INFO, "Starting configuration run (%i > %i)",
		cw_time_ms(), c->schedule.next_run);
	c->schedule.next_run = cw_time_ms() + c->schedule.interval;

	cw_hash_t facts;
	memset(&facts, 0, sizeof(cw_hash_t));
	if (fact_gather(c->gatherers, &facts) != 0) {
		cw_log(LOG_CRIT, "Unable to gather facts frm %s", c->gatherers);
		goto maybe_next_time;
	}

	FILE *io = tmpfile();
	assert(io);

	int rc = fact_write(io, &facts);
	assert(rc == 0);

	fprintf(io, "%c", '\0');
	size_t len = ftell(io);
	fseek(io, 0, SEEK_SET);

	char *factstr = mmap(NULL, len, PROT_READ, MAP_SHARED, fileno(io), 0);
	if ((void *)factstr == MAP_FAILED) {
		cw_log(LOG_CRIT, "Failed to mmap fact data");
		goto maybe_next_time;
	}

	cw_log(LOG_DEBUG, "connecting to one of the masters");
	void *client = s_connect(c);
	if (!client)
		goto maybe_next_time;
	cw_log(LOG_DEBUG, "connected");

	cw_pdu_t *pdu, *reply;

	pdu = cw_pdu_make(NULL, 2, "HELLO", c->fqdn);
	reply = s_sendto(client, pdu, c->timeout);
	if (!reply) {
		cw_log(LOG_ERR, "failed: %s", zmq_strerror(errno));
		goto shut_it_down;
	}
	cw_log(LOG_INFO, "Received a '%s' PDU", reply->type);
	if (strcmp(reply->type, "ERROR") == 0) {
		char *e = cw_pdu_text(reply, 1);
		cw_log(LOG_ERR, "failed: %s", e);
		free(e);
		goto shut_it_down;
	}

	pdu = cw_pdu_make(NULL, 3, "POLICY", c->fqdn, factstr);
	reply = s_sendto(client, pdu, c->timeout);

	munmap(factstr, len);
	fclose(io);

	if (!reply) {
		cw_log(LOG_ERR, "failed: %s", zmq_strerror(errno));
		goto shut_it_down;
	}
	cw_log(LOG_INFO, "Received a '%s' PDU", pdu->type);
	if (strcmp(reply->type, "ERROR") == 0) {
		char *e = cw_pdu_text(reply, 1);
		cw_log(LOG_ERR, "failed: %s", e);
		free(e);
		goto shut_it_down;
	}

	pn_machine m;
	pn_init(&m);
	pendulum_funcs(&m, client);

	char *code = cw_pdu_text(reply, 1);
	io = tmpfile();
	fprintf(io, "%s", code);
	fseek(io, 0, SEEK_SET);

	pn_parse(&m, io);
	m.trace = c->trace;
	pn_run_safe(&m);
	fclose(io);

	pdu = cw_pdu_make(NULL, 1, "BYE");
	reply = s_sendto(client, pdu, c->timeout);
	if (!reply) {
		cw_log(LOG_ERR, "failed: %s", zmq_strerror(errno));
		goto shut_it_down;
	}
	cw_log(LOG_INFO, "Received a '%s' PDU", pdu->type);
	if (strcmp(reply->type, "ERROR") == 0) {
		char *e = cw_pdu_text(reply, 1);
		cw_log(LOG_ERR, "failed: %s", e);
		free(e);
		goto shut_it_down;
	}

shut_it_down:
	cw_zmq_shutdown(client, 0);
	cw_log(LOG_INFO, "closed connection");

maybe_next_time:
	cw_log(LOG_INFO, "Scheduled next configuration run at %i (in %i ms)",
		c->schedule.next_run, c->schedule.next_run - cw_time_ms());
}

static inline client_t* s_client_new(int argc, char **argv)
{
	char *s;
	client_t *c = cw_alloc(sizeof(client_t));
	c->daemonize = 1;

	LIST(config);
	cw_cfg_set(&config, "listen",          "*:2304");
	cw_cfg_set(&config, "timeout",         "5");
	cw_cfg_set(&config, "gatherers",       CW_GATHER_DIR "/*");
	cw_cfg_set(&config, "interval",        "300");
	cw_cfg_set(&config, "syslog.ident",    "cogd");
	cw_cfg_set(&config, "syslog.facility", "daemon");
	cw_cfg_set(&config, "syslog.level",    "error");
	cw_cfg_set(&config, "pidfile",         "/var/run/cogd.pid");

	cw_log_open(cw_cfg_get(&config, "syslog.ident"), "stderr");
	cw_log_level(0, (getenv("COGD_DEBUG") ? "debug" : "error"));
	cw_log(LOG_DEBUG, "default configuration:");
	cw_log(LOG_DEBUG, "  listen          %s", cw_cfg_get(&config, "listen"));
	cw_log(LOG_DEBUG, "  timeout         %s", cw_cfg_get(&config, "timeout"));
	cw_log(LOG_DEBUG, "  gatherers       %s", cw_cfg_get(&config, "gatherers"));
	cw_log(LOG_DEBUG, "  interval        %s", cw_cfg_get(&config, "interval"));
	cw_log(LOG_DEBUG, "  syslog.ident    %s", cw_cfg_get(&config, "syslog.ident"));
	cw_log(LOG_DEBUG, "  syslog.facility %s", cw_cfg_get(&config, "syslog.facility"));
	cw_log(LOG_DEBUG, "  syslog.level    %s", cw_cfg_get(&config, "syslog.level"));
	cw_log(LOG_DEBUG, "  pidfile         %s", cw_cfg_get(&config, "pidfile"));


	cw_log(LOG_DEBUG, "processing command-line options");
	const char *short_opts = "h?vqVcFdT";
	struct option long_opts[] = {
		{ "help",       no_argument,       NULL, 'h' },
		{ "verbose",    no_argument,       NULL, 'v' },
		{ "quiet",      no_argument,       NULL, 'q' },
		{ "version",    no_argument,       NULL, 'V' },
		{ "config",     required_argument, NULL, 'c' },
		{ "foreground", no_argument,       NULL, 'F' },
		{ "dump",       no_argument,       NULL, 'd' },
		{ "trace",      no_argument,       NULL, 'T' },
		{ 0, 0, 0, 0 },
	};
	int verbose = -1;
	const char *config_file = "/etc/clockwork/cogd.conf";
	int opt, idx = 0;
	while ( (opt = getopt_long(argc, argv, short_opts, long_opts, &idx)) != -1) {
		switch (opt) {
		case 'h':
		case '?':
			cw_log(LOG_DEBUG, "handling -h/-?/--help");
			break;

		case 'v':
			if (verbose < 0) verbose = 0;
			verbose++;
			cw_log(LOG_DEBUG, "handling -v/--verbose (modifier = %i)", verbose);
			break;

		case 'q':
			verbose = 0;
			cw_log(LOG_DEBUG, "handling -q/--quiet (modifier = %i)", verbose);
			break;

		case 'V':
			cw_log(LOG_DEBUG, "handling -V/--version");
			printf("cogd (Clockwork) %s runtime v%04x\n"
			       "Copyright (C) 2014 James Hunt\n",
			       PACKAGE_VERSION, PENDULUM_VERSION);
			exit(0);
			break;

		case 'c':
			cw_log(LOG_DEBUG, "handling -c/--config; replacing '%s' with '%s'",
				config_file, argv[optind]);
			config_file = argv[optind];
			break;

		case 'F':
			cw_log(LOG_DEBUG, "handling -F/--foreground; turning off daemonize behavior");
			c->daemonize = 0;
			break;

		case 'd':
			cw_log(LOG_DEBUG, "handling -d/--dump; switching to <dump-config> mode");
			c->mode = MODE_DUMP;
			break;

		case 'T':
			cw_log(LOG_DEBUG, "handling -T/--trace");
			cw_log(LOG_DEBUG, "TRACE option will be set on all Pendulum runs");
			c->trace = 1;
			break;
		}
	}
	cw_log(LOG_DEBUG, "option processing complete");


	cw_log(LOG_DEBUG, "parsing cogd configuration file '%s'", config_file);
	FILE *io = fopen(config_file, "r");
	if (!io) {
		cw_log(LOG_WARNING, "Failed to read configuration from %s: %s",
			config_file, strerror(errno));
		cw_log(LOG_WARNING, "Using default configuration");

	} else {
		if (cw_cfg_read(&config, io) != 0) {
			cw_log(LOG_ERR, "Unable to parse %s");
			exit(1);
		}
		fclose(io);
		io = NULL;
	}


	cw_log(LOG_DEBUG, "determining adjusted log level/facility");
	if (verbose < 0) verbose = 0;
	s = cw_cfg_get(&config, "syslog.level");
	cw_log(LOG_DEBUG, "configured log level is '%s', verbose modifier is %+i", s, verbose);
	int level = cw_log_level_number(s);
	if (level < 0) {
		cw_log(LOG_WARNING, "'%s' is not a recognized log level, falling back to 'error'", s);
		level = LOG_ERR;
	}
	level += verbose;
	cw_log(LOG_DEBUG, "adjusted log level is %s (%i)",
		cw_log_level_name(level), level);
	if (!c->daemonize) {
		cw_log(LOG_DEBUG, "Running in --foreground mode; forcing all logging to stdout");
		cw_cfg_set(&config, "syslog.facility", "stdout");
	}
	if (c->mode == MODE_DUMP) {
		cw_log(LOG_DEBUG, "Running in --dump mode; forcing all logging to stderr");
		cw_cfg_set(&config, "syslog.facility", "stderr");
	}
	cw_log(LOG_DEBUG, "redirecting to %s log as %s",
		cw_cfg_get(&config, "syslog.facility"),
		cw_cfg_get(&config, "syslog.ident"));

	cw_log_open(cw_cfg_get(&config, "syslog.ident"),
	            cw_cfg_get(&config, "syslog.facility"));
	cw_log_level(level, NULL);


	cw_log(LOG_INFO, "cogd starting up");
	c->schedule.next_run = cw_time_ms();


	cw_log(LOG_DEBUG, "detecting fully-qualified domain name of local node");
	c->fqdn = s_myfqdn();
	if (!c->fqdn) exit(1);
	cw_log(LOG_INFO, "detected my FQDN as '%s'", c->fqdn);


	cw_log(LOG_DEBUG, "parsing master.* definitions into endpoint records");
	int n; char *key;
	c->nmasters = 0;
	memset(c->masters, 0, sizeof(c->masters));
	for (key = cw_string("master.%i", (n = 1));
	     n <= 8 && key != NULL && (s = cw_cfg_get(&config, key)) != NULL;
	     key = cw_string("master.%i", ++n)) {

		cw_log(LOG_DEBUG, "found a master: %s", s);
		c->masters[c->nmasters++] = s;
	}


	if (c->mode == MODE_DUMP) {
		printf("listen          %s\n", cw_cfg_get(&config, "listen"));
		int i;
		for (i = 0; i < c->nmasters; i++)
			printf("master.%i        %s\n", i+1, c->masters[i]);
		printf("timeout         %s\n", cw_cfg_get(&config, "timeout"));
		printf("gatherers       %s\n", cw_cfg_get(&config, "gatherers"));
		printf("interval        %s\n", cw_cfg_get(&config, "interval"));
		printf("syslog.ident    %s\n", cw_cfg_get(&config, "syslog.ident"));
		printf("syslog.facility %s\n", cw_cfg_get(&config, "syslog.facility"));
		printf("syslog.level    %s\n", cw_cfg_get(&config, "syslog.level"));
		printf("pidfile         %s\n", cw_cfg_get(&config, "pidfile"));
		exit(0);
	}


	if (c->nmasters == 0) {
		cw_log(LOG_ERR, "No masters defined in %s", config_file);
		exit(2);
	}

	c->gatherers = cw_cfg_get(&config, "gatherers");
	c->schedule.interval  = 1000 * atoi(cw_cfg_get(&config, "interval"));
	c->timeout            = 1000 * atoi(cw_cfg_get(&config, "timeout"));

	s = cw_string("tcp://%s", cw_cfg_get(&config, "listen"));
	cw_log(LOG_DEBUG, "binding to %s", s);
	c->zmq = zmq_ctx_new();
	c->listener = zmq_socket(c->zmq, ZMQ_ROUTER);
	zmq_bind(c->listener, s);
	free(s);

	return c;
}

int main(int argc, char **argv)
{
	client_t *c = s_client_new(argc, argv);

	if (getuid() != 0 || geteuid() != 0) {
		fprintf(stderr, "%s must be run as root!\n", argv[0]);
		exit(9);
	}

	while (!cw_sig_interrupt()) {
		zmq_pollitem_t socks[] = {
			{ c->listener, 0, ZMQ_POLLIN, 0 },
		};

		int time_left = (int)((c->schedule.next_run - cw_time_ms()));
		if (time_left < 0) time_left = 0;
		cw_log(LOG_DEBUG, "zmq_poll for %ims", time_left);

		errno = 0;
		int rc = zmq_poll(socks, 1, time_left);
		if (rc == -1)
			break;

		if (socks[0].revents & ZMQ_POLLIN)
			s_cogd_run(c);

		if (cw_time_ms() >= c->schedule.next_run)
			s_cfm_run(c);
	}
	cw_log(LOG_INFO, "shutting down");

	cw_zmq_shutdown(c->listener, 500);
	zmq_ctx_destroy(c->zmq);
	return 0;
}
