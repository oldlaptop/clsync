/*
    clsync - file tree sync utility based on fanotify and inotify

    Copyright (C) 2013  Dmitry Yu Okunev <xai@mephi.ru> 0x8E30679C

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include "output.h"
#include "sync.h"
#include "malloc.h"

#define VERSION_MAJ	0
#define VERSION_MIN	0
#include "revision.h"

static struct option long_options[] =
{
	{"background",		no_argument,		NULL,	BACKGROUND},
	{"pthread",		no_argument,		NULL,	PTHREAD},	// Not implemented, yet
	{"collectdelay",	required_argument,	NULL,	DELAY},
	{"commondelay",		required_argument,	NULL,	COMMONDELAY},
	{"outlistsdir",		required_argument,	NULL,	OUTLISTSDIR},
	{"rsync",		no_argument,		NULL,	RSYNC},
	{"rsyncinclimit",	required_argument,	NULL,	RSYNCINCLIMIT},
	{"rsyncpreferexclude",	no_argument,		NULL,	RSYNC_PREFEREXCLUDE},
	{"dontunlinklists",	no_argument,		NULL,	DONTUNLINK},
	{"bigfilethreshold",	required_argument,	NULL,	BFILETHRESHOLD},
	{"bigfilecollectdelay",	required_argument,	NULL,	BFILEDELAY},
	{"verbose",		no_argument,		NULL,	VERBOSE},
	{"debug",		no_argument,		NULL,	DEBUG},
	{"quite",		no_argument,		NULL,	QUITE},
#ifdef FANOTIFY_SUPPORT
	{"fanotify",		no_argument,		NULL,	FANOTIFY},
#endif
	{"inotify",		no_argument,		NULL,	INOTIFY},
	{"label",		no_argument,		NULL,	LABEL},
	{"help",		no_argument,		NULL,	HELP},
	{"version",		no_argument,		NULL,	VERSION},
	{0,			0,			0,	0}
};

int syntax() {
	printf("syntax: clsync [flags] <watch dir> <action script> [file with rules regexps]\npossible options:\n");
	int i=0;
	while(long_options[i].name != NULL) {
		printf("\t--%-24s-%c\n", long_options[i].name, long_options[i].val);
		i++;
	}
	exit(0);
}

int version() {
	printf("clsync v%i.%i.%i\n\t"AUTHOR"\n", VERSION_MAJ, VERSION_MIN, REVISION);
	exit(0);
}

int parse_arguments(int argc, char *argv[], struct options *options) {
	int c;
	int option_index = 0;
	while(1) {
#ifdef FANOTIFY_SUPPORT
		c = getopt_long(argc, argv, "bT:B:d:t:l:pw:qvDhaVRUI:Ef", long_options, &option_index);
#else
		c = getopt_long(argc, argv, "bT:B:d:t:l:pw:qvDhaVRUI:E",  long_options, &option_index);
#endif
	
		if (c == -1) break;
		switch (c) {
			case '?':
			case 'h':
				syntax();
				break;
			case 'd':
				options->listoutdir   = optarg;
				break;
			case 'l':
				options->label        = optarg;
				break;
			case 'w':
				options->commondelay  = (unsigned int)atol(optarg);
			case 't':
				options->_queues[QUEUE_NORMAL].collectdelay = (unsigned int)atol(optarg);
				break;
			case 'T':
				options->_queues[QUEUE_BIGFILE].collectdelay = (unsigned int)atol(optarg);
				break;
			case 'B':
				options->bfilethreshold = (unsigned long)atol(optarg);
				break;
#ifdef FANOTIFY_SUPPORT
			case 'f':
				options->notifyengine = NE_FANOTIFY;
				break;
#endif
			case 'i':
				options->notifyengine = NE_INOTIFY;
				break;
			case 'I':
				options->rsyncinclimit = (unsigned int)atol(optarg);
				break;
			case 'V':
				version();
				break;
			default:
				options->flags[c]++;
				break;
		}
	}
	if(optind+1 >= argc)
		syntax();

	options->actfpath = argv[optind+1];
	if(optind+2 < argc)
		options->rulfpath = argv[optind+2];

	options->watchdir = argv[optind];
	return 0;
}

int parse_rules_fromfile(const char *rulfpath, rule_t *rules) {
	char buf[BUFSIZ];
	char *line_buf=NULL;
	FILE *f = fopen(rulfpath, "r");
	
	if(f == NULL) {
		printf_e("Error: Cannot open \"%s\" for reading: %s (errno: %i).\n", rulfpath, strerror(errno), errno);
		return errno;
	}

	int i=0;
	size_t linelen, size=0;
	while((linelen = getline(&line_buf, &size, f)) != -1) {
		if(linelen>1) {
			char *line = line_buf;
			rule_t *rule;

			rule = &rules[i];
			line[--linelen] = 0; 
			switch(*line) {
				case '+':
					rule->action = RULE_ACCEPT;
					break;
				case '-':
					rule->action = RULE_REJECT;
					break;
				case '#':	// Comment?
					continue;
				default:
					printf_e("Error: Wrong rule action <%c>.\n", *line);
					return EINVAL;
			}

			line++;
			linelen--;

			*line |= 0x20;	// lower-casing
			switch(*line) {
				case '*':
					rule->objtype = 0;	// "0" - means "of any type"
					break;
#ifdef DETAILED_FTYPE
				case 's':
					rule->objtype = S_IFSOCK;
					break;
				case 'l':
					rule->objtype = S_IFLNK;
					break;
				case 'f':
					rule->objtype = S_IFREG;
					break;
				case 'b':
					rule->objtype = S_IFBLK;
					break;
				case 'd':
					rule->objtype = S_IFDIR;
					break;
				case 'c':
					rule->objtype = S_IFCHR;
					break;
				case 'p':
					rule->objtype = S_IFIFO;
					break;
#else
				case 'f':
					rule->objtype = S_IFREG;
					break;
				case 'd':
					rule->objtype = S_IFDIR;
					break;
#endif
			}

			line++;
			linelen--;

			printf_d("Debug2: Rule #%i <%c> <%c> pattern <%s> (length: %i).\n", i+1, line[-2], line[-1], line, linelen);
			int ret;
			if(i >= MAXRULES) {
				printf_e("Error: Too many rules (%i >= %i).\n", i, MAXRULES);
				rule->action = RULE_END;
				return ENOMEM;
			}
			if((ret = regcomp(&rule->expr, line, REG_EXTENDED | REG_NOSUB))) {
				regerror(ret, &rule->expr, buf, BUFSIZ);
				printf_e("Error: Invalid regexp pattern <%s>: %s (regex-errno: %i).\n", line, buf, ret);
				rule->action = RULE_END;
				return ret;
			}
			i++;
		}
	}
	if(size)
		free(line_buf);

	fclose(f);

	rules[i].action = RULE_END;	// Terminator. End of rules' chain.
	return 0;
}

int becomedaemon() {
	int pid;
	signal(SIGPIPE, SIG_IGN);
	switch((pid = fork())) {
		case -1:
			printf_e("Error: Cannot fork(): %s (errno: %i).\n", strerror(errno), errno);
			return(errno);
		case 0:
			setsid();
			break;
		default:
			printf_d("Debug: fork()-ed, pid is %i.\n", pid);
			exit(0);
	}
	return 0;
}

int main_cleanup(options_t *options_p) {
	int i=0;
	while((i < MAXRULES) && (options_p->rules[i].action != RULE_END))
		regfree(&options_p->rules[i++].expr);

	return 0;
}

int main_rehash(options_t *options_p) {
	printf_ddd("Debug3: main_rehash()\n");
	int ret=0;

	main_cleanup(options_p);

	if(options_p->rulfpath != NULL)
		ret = parse_rules_fromfile(options_p->rulfpath, options_p->rules);
	else
		options_p->rules[0].action = RULE_END;

	return ret;
}

int main(int argc, char *argv[]) {
	struct options options;
	int ret = 0, nret;
	memset(&options, 0, sizeof(options));
	options.notifyengine 			   = DEFAULT_NOTIFYENGINE;
	options.commondelay 			   = DEFAULT_COMMONDELAY;
	options._queues[QUEUE_NORMAL].collectdelay = DEFAULT_COLLECTDELAY;
	options._queues[QUEUE_BIGFILE].collectdelay= DEFAULT_BFILECOLLECTDELAY;
	options._queues[QUEUE_INSTANT].collectdelay= COLLECTDELAY_INSTANT;
	options.bfilethreshold			   = DEFAULT_BFILETHRESHOLD;
	options.label				   = DEFAULT_LABEL;
	options.rsyncinclimit			   = DEFAULT_RSYNC_INCLUDELINESLIMIT;

	parse_arguments(argc, argv, &options);
	out_init(options.flags);
	if(options.flags[RSYNC] && (options.listoutdir == NULL)) {
		printf_e("Error: Option \"--rsync\" cannot be used without \"--outlistsdir\".\n");
		return EINVAL;
	}
	if(options.flags[RSYNC_PREFEREXCLUDE] && (!options.flags[RSYNC])) {
		printf_e("Error: Option \"--rsyncpreferexclude\" cannot be used without \"--rsync\".\n");
		return EINVAL;
	}
	if(options.flags[DEBUG])
		debug_print_flags();

#ifdef FANOTIFY_SUPPORT
	if(options.notifyengine != NE_INOTIFY) {
		printf_e("Warning: fanotify is not fully supported, yet!\n");
	}
#endif

	nret=main_rehash(&options);
	if(nret)
		ret = nret;

	if(access(options.actfpath, X_OK) == -1) {
		printf_e("Error: \"%s\" is not executable: %s (errno: %i).\n", options.actfpath, strerror(errno), errno);
		ret = errno;
	}

	if(options.flags[BACKGROUND]) {
		nret = becomedaemon();
		if(nret)
			ret = nret;
	}

	options.watchdirlen = strlen(options.watchdir);
	if(ret == 0)
		ret = sync_run(&options);

	main_cleanup(&options);

	out_flush();
	printf_d("Debug: finished, exitcode: %i.\n", ret);
	out_flush();
	return ret;
}


