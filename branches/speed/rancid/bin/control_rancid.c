/*
 * Copyright (C) 1997-2002 by Henry Kilmer, Erik Sherk and Pete Whiting.
 * All rights reserved.
 *
 * This software may be freely copied, modified and redistributed without
 * fee for non-commerical purposes provided that this copyright notice is
 * preserved intact on all copies and modified copies.
 *
 * There is no warranty or other guarantee of fitness of this software.
 * It is provided solely "as is". The author(s) disclaim(s) all
 * responsibility and liability with respect to this software's usage
 * or its effect upon hardware, computer systems, other software, or
 * anything else.
 *
 *
 * usage: control_rancid $GROUP
 *
 * provides basic control of rancid.
 */

#define DFLT_TO	60				/* default timeout */

#include <config.h>
#include <version.h>

#include <stdio.h>
#include <time.h>

#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <regex.h>

#include <termios.h>

extern char	**environ;

typedef struct {
	int	n;				/* proc n of n_opt processes */
	pid_t	pid;				/* child pid */
	char	*logfname;			/* logfile name */
	FILE	*logfile;
	int	logfd;				/* logfile FD */
	pid_t	xpid;				/* xterm child pid */
	/* XXX: int	rxp[2], txp[2];			/* rx/tx pipes */
} child;

char		*progname;
child		*progeny;
int		debug = 0,
		chld_wait = 0;			/* there are children */
int		devnull;			/* /dev/null */

/* args */
int		e_opt = 0,
		f_opt = 0,
		i_opt = 0,
		n_opt = 3,
		p_opt = 0,
		q_opt = 0,
		x_opt = 0,
		ifile = 0;			/* argv index to input files */
char		*c_opt = NULL,
		*l_opt = "par.log";

FILE		*errfp,				/* stderr fp */
		*logfile;			/* logfile */
sigset_t	set_chld;

int		arg_replace __P((char *, char **, char **, char ***));
int		execcmd __P((child *, char **));
void		arg_free __P((char ***));
void		reopenfds __P((child *));
int		shcmd __P((child *, char **));
int		xtermcmd __P((child *, char **));
void		usage __P((void));
void		vers __P((void));
void		xtermlog __P((child *));
RETSIGTYPE	reapchild __P((void));
RETSIGTYPE	handler __P((int));

int
main(int argc, char **argv, char **envp)
{
    extern char		*optarg;
    extern int		optind;
    char		ch;
    time_t		t;
    int			i;
    FILE		*fp;

    /* get just the basename() of our exec() name and strip a .* off the end */
    if ((progname = strrchr(argv[0], '/')) != NULL)
	progname += 1;
    else
	progname = argv[0];
    if (strrchr(progname, '.') != NULL)
	*(strrchr(progname, '.')) = '\0';

    /* make sure stderr points to something */
    if ((devnull = open("/dev/null", O_RDWR, 0666)) == -1) {
	printf("Error: can not open /dev/null: %s\n", strerror(errno));
	exit(EX_OSERR);
    }
    if (stderr == NULL) {
	if ((errfp = fdopen(devnull, "w+")) == NULL) {
	    printf("Error: can not open /dev/null: %s\n", strerror(errno));
	    exit(EX_OSERR);
	}
    } else
	errfp = stderr;

    while ((ch = getopt(argc, argv, "defhiqxvc:e:l:n:p:")) != -1 )
	switch (ch) {
	case 'c':	/* command to run */
	    c_opt = optarg;
	    break;
	case 'd':
	    debug++;
	    break;
	case 'e':	/* exec args split by spaces rather than using sh -c */
	    e_opt = 1;
	    break;
	case 'f':	/* no file or stdin, just run quantity of command */
	    f_opt = 1;
	    break;
	case 'l':	/* logfile */
	    if (q_opt) {
		fprintf(errfp, "Warning: -q non-sensical with -x or -l\n");
		q_opt = 0;
	    }
	    l_opt = optarg;
	    break;
	case 'i':	/* run commands through interactive xterms */
			/* XXX: -i doesnt make sense with -e */
	    i_opt = 1;
	    break;
	case 'n':	/* number of processes to run to run at once, dflt 3 */
	    n_opt = atoi(optarg);
	    if (n_opt < 1) {
		fprintf(errfp, "Warning: -n < 1 is non-sensical\n");
		n_opt = 3;
	    }
	    break;
	case 'p':	/* pause # seconds between forks, dflt 0 */
	    p_opt = atoi(optarg);
	    break;
	case 'q':	/* quiet mode (dont log anything to logfiles */
	    if (x_opt) {
		fprintf(errfp, "Warning: -q non-sensical with -x or -l\n");
		x_opt = 0;
	    }
	    q_opt = 1;
	    break;
	case 'x':	/* view par logs as they run through an xterm */
	    if (q_opt) {
		fprintf(errfp, "Warning: -q non-sensical with -x or -l\n");
		q_opt = 0;
	    }
	    x_opt = 1;
	    break;
	case 'v':
	    vers();
	    return(EX_OK);
	case 'h':
	default:
	    usage();
	    return(EX_USAGE);
	}

    /* -f requires -c */
    if (f_opt && ! c_opt) {
	fprintf(errfp, "usage: -f requires -c option\n");
	usage();
	return(EX_USAGE);
    }
    /* args after options are input file(s) */
    if (c_opt == NULL && argc - optind == 0) {
		/* XXX: what about input file from stdin? */
	fprintf(errfp, "usage: either -c or a command file must be supplied\n");
	usage();
	return(EX_USAGE);
    } else
	ifile = optind;

    /* grab space to keep track of our children */
    if ((progeny = (child *) malloc(sizeof(child) * n_opt)) == NULL) {
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	exit(EX_TEMPFAIL);
    } else
	bzero((void *)progeny, sizeof(child) * n_opt);

    /* set-up child structures */
    t = time(NULL);
    if (! q_opt) {
	for (i = 0; i < n_opt; i++) {
	    /* open the logfile or use stderr */
	    if (asprintf(&progeny[i].logfname, "%s.%lu.%d", l_opt, t, i) < 1) {
		fprintf(errfp, "Error: could not allocate space for process "
			"%d's log filename\n", i);
		exit(EX_TEMPFAIL);
	    }
	    if ((progeny[i].logfd = open(progeny[i].logfname,
					O_WRONLY | O_CREAT, 0666)) == -1) {
		fprintf(errfp, "Error: could not open %s for writing: %s\n",
			progeny[i].logfname, strerror(errno));
		exit(EX_CANTCREAT);
	    }
	    if ((progeny[i].logfile =
				fdopen(progeny[i].logfd, "w")) == NULL) {
		fprintf(errfp, "Error: could not open %s for writing: %s\n",
			progeny[i].logfname, strerror(errno));
		exit(EX_CANTCREAT);
	    }

	    /* start an xterm for log file */
	    if (x_opt)
		xtermlog(&progeny[i]);
	}
    }

    /* prepare to accept signals */
    signal(SIGHUP, (void *) handler);
    signal(SIGINT, (void *) handler);
    signal(SIGTERM, (void *) handler);
    signal(SIGQUIT, (void *) handler);
    signal(SIGCHLD, (void *) reapchild);
    sigemptyset(&set_chld);
    sigaddset(&set_chld, SIGCHLD);

    /* if argv[ifile] is NULL and stdin is closed, then f_opt is implicit */
    if (argv[ifile] == NULL && stdin == NULL)
	f_opt = 1;

    /*
     * command dispatch loop starts here.
     *
     * -f means just run the -c command -n times.  any args become "".
     */
    if (f_opt) {
	char	*nullopts[] = { NULL, NULL },
		**args;

	/* replace {}s in c_opt */
	if (arg_replace(c_opt, NULL, NULL, &args)) {
		/* XXX: arg_replace error! */
		;
	} else {
	    for (i = 0; i < n_opt; i++) {
		progeny[i].n = i + 1;
			/* XXX: check retcode from *cmd()? */
		if (i_opt) {
		    xtermcmd(&progeny[i], args);
		} else if (e_opt) {
		    execcmd(&progeny[i], args);
		} else {
		    shcmd(&progeny[i], args);
		}
		if (p_opt) {
		    sigprocmask(SIG_BLOCK, &set_chld, NULL);
		    sleep(p_opt);
		    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
		}
	    }
	    arg_free(&args);
	}
    } else {
	/*
	 * if input files were specified on the command-line, use these as
	 * input, else read stdin.
	 *
	 * if -c was not supplied, the command is the first line of each file.
	 * if that line begins with a # and no -c was supplied, then each line
	 * is a separate command with no argument replacement.
	 */
		/* XXX: replace {}s in c_opt */
    }

    /*
     * after all the work is assigned we wait for all the children to
     * finish and be reaped.  ie: so all the pid's in the child structure
     * will be zero.
     */
    if (debug)
	fprintf(errfp, "All work assigned.  Waiting for remaining processes."
		"\n");
    while (1) {
	/* block sigchld while we search the child table */
	sigprocmask(SIG_BLOCK, &set_chld, NULL);

	for (i = 0; i < n_opt; i++) {
	    if (progeny[i].pid != 0) {
		chld_wait = 1;
		break;
            }
	}

	sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (chld_wait == 1) {
	    pause();
	} else if (chld_wait == 0)
	    break;
	chld_wait = 0;
    }

#if 0
for($i=0; ($no_file || ($_=<>)) ;$i++) {
    chop;
    if (/^\#/){$i--;next;}
    if ($opt_c == "" && /^:(.*)$/) {
	$command=$1;$i--;next;
    }
    if ($i<$procs) {
	$logfile="running.$i"; $logfile="$parlog.$i" if (!$opt_q);
    } else {
	$logfile=finish;
    }
    last if $signalled;
    if ($logfile) {
        $cmd = $command;
        $cmd =~ s/\{\}/$_/g;
	$cmd = "xterm -e $cmd" if ($opt_i);
        $id=start($cmd,$logfile);
	watchf($logfile) if($opt_x);
        $log{$id} = $logfile;
    }
    print STDERR "$i/$procs: $_: id=$id, log=$log{$id}\n" if ($debug);
    sleep($pause_time) if ($pause_time);
}
#endif
#if 0
    local($cmd,$logfile)=@_;
    unless ($id=fork) {
	if (!$opt_q) {
	    local($date)=scalar localtime;
	    open(LOG,">>$logfile");
	    print(LOG "!!!!!!!\n!$date: $cmd\n!!!!!!!\n");
	    close(LOG);
	    exec "($cmd) >>$logfile";
	} else {
            if($opt_e) {
                # Dont use sh -c.
	        exec split(/\s+/, $cmd);
            }
	    exec "($cmd)";
	}
        exit 0;
    }
    print STDERR "Starting $cmd: process id=$id logfile=$logfile\n" if ($debug);
    $id;
if($signalled && !eof) {
    $i--;
    print STDERR "Signalled - not running these:\n$_\n";
    while(<>){print STDERR;}
}
#endif

    /* close the log files */
    for (i = 0; i < n_opt; i++) {
	if (progeny[i].logfile != NULL)
	    fclose(progeny[i].logfile);
    }

    if (debug)
	fprintf(errfp, "Complete\n");

    return(EX_OK);
}

/*
 * arg to sh -c used in shcmd() must be 1 argument.  returns 0 else errno.
 *
 * NOTE: dst[][] is assumed to have 2 spaces of which [0][] will be used. the
 *	 caller will have to free dst[0].
 */
int
arg_mash(dst, src)
    char	**dst,
		**src;
{
    int		i = 0,
		len = 0;
    char	*ptr;

    while (src[i] != NULL)
	len += strlen(src[i++]);
    len += i + 1;

    if ((dst[0] = (char *) malloc(sizeof(char *) * len)) == NULL)
	return(errno);
    bzero(dst[0], len);

    i = 0; ptr = dst[0];
    while (src[i] != NULL) {
	len = strlen(src[i]);
	bcopy(src[i++], ptr, len);
	ptr += len;
	if (src[i] != NULL)
	    *ptr++ = ' ';
    }

    return(0);
}

/*
 * takes a command string (cmd) like "echo {}" whose args are stored in a
 * NULL terminated args[][] and sequentially replaces {}s from args[][]. any
 * {}s not matching up to an arg are replaced with "".
 *
 * args found in tail[][] are concatenated to the end newargs[][] without any
 * interpretation.
 *
 * it returns an argv[][] which begins with the command followed by the args
 * and is null terminated in newargs that is suitable for execvp() and 0 or
 * errno.
 *
 * NOTE: if arg_replace() succeeds (returns 0), it is the callers
 *	  responsibility to free the space with arg_free().
 */
int
arg_replace(cmd, args, tail, newargs)
    char	*cmd,
		**args,
		**tail,
		***newargs;
{
    int		argn = 0,
		len = 0,			/* length of cmd - leading sp */
		linemax = LINE_MAX,
		nargs,				/* # of args in args[][] */
		quotes,				/* " quoted string toggle */
		spaces = 0;			/* number of space in cmd */
    char	*lcmd,				/* local copy of cmd */
		*tick = NULL,			/* ' quoted string */
		*ptr;
    register int	c, l;

    /* if newargs is null, that is an internal error */
    if (newargs == NULL || (cmd == NULL && args == NULL))
	abort();

    if (args != NULL)
	while (args[nargs] != NULL)
	    nargs++;

    /* temporary buffer */
    len = strlen(cmd);
    linemax = linemax > (len + 1) ? linemax : linemax * 2;
    if ((lcmd = (char *) malloc(linemax)) == NULL) {
		/* XXX: error msg ??? */
	fprintf(errfp, "Error: memory allocation failed: %s", strerror(errno));
	return(ENOMEM);
    }
    bzero(lcmd, linemax);

    /* realloc lcmd space */
#define	RE_MALLOC(a, b, n)	{				\
	if ((b = (char *)realloc(&a, n + LINE_MAX)) == NULL) {	\
		free(a);					\
		return(ENOMEM);					\
	    }					 		\
	    a = b;						\
	    bzero(a + n - 1, LINE_MAX);		 		\
	    n += LINE_MAX;					\
	}

    /* if cmd is NULL, just catenate args into a lcmd to be separated below */
    if (cmd == NULL) {
	l = c = 0;
	while (args[c] != NULL) {
	    len = strlen(args[c]);
	    while (l + len + 1 > linemax)
		RE_MALLOC(lcmd, ptr, linemax);
	    bcopy(args[c], &lcmd[c], len);
	    l += len;
	}
	spaces = nargs;
    } else {
	/* skip leading whitespace, look for {}s to replace spaces & NULL
	 * spaces.  ugly
	 */
	quotes = c = l = 0;
	while (cmd[c] == ' ' || cmd[c] == '\t')
	    c++;

    while (cmd[c] != '\0') {
	while (l + 3 > linemax)
	    RE_MALLOC(lcmd, ptr, linemax);
	switch (cmd[c]) {
	case '\\':
	    if (quotes) {
		lcmd[l++] = cmd[c++];
		lcmd[l++] = cmd[c++];
	    } else {
		if (cmd[c+1] == '\n') {
		    lcmd[l++] = '\n';
		    c += 2;
		} else if (cmd[c+1] == '\t') {
		    lcmd[l++] = '\t';
		    c += 2;
		} else {
		    lcmd[l++] = cmd[c++];
		    lcmd[l++] = cmd[c++];
		}
	    }
	    break;
	case '\'':
	    /* shell preserves the meaning of all chars in '', incl \ */
	    c++;
	    if ((tick = index(cmd + c, '\'')) == NULL) {
		/* unmatched quotes */
		free(lcmd);
		return(EX_DATAERR);
	    }
#if 0
	    /* this would make it possible to do \' in a '-d string */
	      else if (*(tick - 1) == '\\') {
		for (;;;)
		    tick++;
		    if (*tick == '\0') {
			/* unmatched quotes */
			free(lcmd);
			return(EX_DATAERR);
		    }
		    if (*tick == '\'')
			break;
		}
	    }
#endif
	    len = tick - cmd;
	    while (l + len + 1 > linemax)
		RE_MALLOC(lcmd, ptr, linemax);
	    bcopy(&cmd[c], &lcmd[l], len);
	    c += len; l += len;
	    break;
	case '"':
	    /* the shell would recognize $, `, and \ in double-" strings.  by
	     * the time we get it, these chars should not exist and thus we
	     * we ignore them.  all we deal with are \" and \n.
	     */
	    quotes ^= 1;
	    c++;
	    break;
	case '\t':
	case ' ':
	    if (!quotes) {
		lcmd[l++] = '\0';
		spaces++;
		c++;
	    } else
		lcmd[l++] = cmd[c++];
	    break;
	case '{':
	    /* the shell would replace $x in quotes, so we do the same w/ {}s */
	    if (cmd[c + 1] == '}') {
		if (argn < nargs) {
		    len = strlen(args[argn]);
		    while (l + len + 1 > linemax)
			RE_MALLOC(lcmd, ptr, linemax);
		    bcopy(cmd + c, lcmd + l, len);
		    l += len;
		}
		c += 2;
	    } else
		lcmd[l++] = cmd[c++];
	    break;
	default:
	    lcmd[l++] = cmd[c++];
	}
    }
    }

    /* concatenate tail[][] on the end of lcmd */
    if (tail != NULL) {
	c = 0;
	if (lcmd[l] == '\0')
	    l++;
	while (tail[c] != NULL) {
	    len = strlen(tail[c]);
	    while (l + len + 1 > linemax)
		RE_MALLOC(lcmd, ptr, linemax);
	    bcopy(tail[c++], &lcmd[l], len);
	    l += len + 1;
	}
	spaces += c;
    }

    /* create space for first_arg + spaces + last_arg + NULL terminator */
    if ((*newargs = (char **) malloc(sizeof(char *) * (spaces + 3))) == NULL) {
	if (lcmd != NULL)
	   free(lcmd);
	return(ENOMEM);
    }
    bzero(*newargs, sizeof(char *) * (spaces + 3));

    /* fill in newargs[][] */
    c = argn = 0;
    while (c < l) {
	if (lcmd[c] != '\0') {
	    (*newargs)[argn++] = lcmd + c;
	    c += strlen(lcmd + c) + 1;
	} else
	    /* skip double+ spaces */
	    c++;
    }

    return(0);
}

/*
 * start a command via an exec, after breaking up the arguments
 */
int
execcmd(c, args)
    child	*c;
    char	**args;
{
    char	*cmd = "",
		*mashed[] = { NULL, NULL },
		**newargs;
    int		status;
    pid_t	pid;
    time_t	t;

    /* XXX: is this right? */
    if (args == NULL)
	return(ENOEXEC);

    /* build newargs */
    if ((pid = arg_mash(&mashed, args))) {		/* simply for the log */
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(pid);
    }
    if ((pid = arg_replace(cmd, NULL, args, &newargs))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(pid);
    }

    /* block sigchld so we quickly reap it ourselves */
    sigprocmask(SIG_BLOCK, &set_chld, NULL);

    if (c->logfile) {
	t = time(NULL);
		/* XXX: build a complete cmd line */
	cmd = ctime(&t); cmd[strlen(cmd) - 1] = '\0';
	fprintf(c->logfile, "!!!!!!!\n!%s: %s\n!!!!!!!\n", cmd, mashed[0]);
	fflush(c->logfile);
    }

    if ((pid = fork()) == 0) {
	/* child */
	signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (debug > 1) {
		/* XXX: build a complete cmd line */
	    if (c->logfile != NULL)
		fprintf(c->logfile, "fork(sh -c %s ...) pid %d\n", mashed[0],
								getpid());
	    fprintf(errfp, "fork(sh -c %s ...) pid %d\n", mashed[0],
								getpid());
	}
	/* reassign stdout and stderr to the log file, stdin to /dev/null */
	reopenfds(c);

	execvp(newargs[0], newargs);

	/* not reached, unless exec() fails */
	fprintf(errfp, "Error: exec(xterm failed): %s\n", strerror(errno));
	exit(EX_UNAVAILABLE);
    } else {
	if (debug)
		/* XXX: build a complete cmd line */
	    if (c->logfname == NULL)
		fprintf(errfp, "\nStarting %d/%d %s: process id=%d\n",
			c->n, n_opt, mashed[0], pid);
	    else
		fprintf(errfp, "\nStarting %d/%d %s: process id=%d "
			"logfile=%s\n", c->n, n_opt, mashed[0], pid,
			c->logfname);
	if (pid == -1) {
	    fprintf(errfp, "Error: exec(sh -c) failed: %s\n", strerror(errno));
	    waitpid(c->pid, &status, WNOHANG);
	} else
	    c->pid = pid;
    }

    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return(0);
}

/*
 * free space in a char[][] from arg_replace()
 */
void
arg_free(newargs)
    char	***newargs;
{
#if 0
    char	**ptr;
#endif

    if (newargs == NULL || *newargs == NULL)
	return;

    if (**newargs != NULL)
	free(**newargs);

    free(*newargs);

#if 0
    ptr = *newargs;

    while (ptr != NULL && *ptr != NULL)
	free(*ptr);

    if (ptr != NULL)
	free(ptr);
#endif

    *newargs = NULL;

    return;
}

/*
 * start a command whose output is concatenated to the childs logfile via
 * sh -c.
 */
int
shcmd(c, args)
    child	*c;
    char	**args;
{
    char	*cmd = "sh -c",
		*mashed[] = { NULL, NULL },
		**newargs;
    int		status;
    pid_t	pid;
    time_t	t;

    /* XXX: is this right? */
    if (args == NULL)
	return(ENOEXEC);

    /* build newargs */
    if ((pid = arg_mash(&mashed, args))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(pid);
    }
    if ((pid = arg_replace(cmd, NULL, mashed, &newargs))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(pid);
    }

    /* block sigchld so we quickly reap it ourselves */
    sigprocmask(SIG_BLOCK, &set_chld, NULL);

    if (c->logfile) {
	t = time(NULL);
		/* XXX: build a complete cmd line */
	cmd = ctime(&t); cmd[strlen(cmd) - 1] = '\0';
	fprintf(c->logfile, "!!!!!!!\n!%s: %s\n!!!!!!!\n", cmd, mashed[0]);
	fflush(c->logfile);
    }

    if ((pid = fork()) == 0) {
	/* child */
	signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (debug > 1) {
		/* XXX: build a complete cmd line */
	    if (c->logfile != NULL)
		fprintf(c->logfile, "fork(sh -c %s ...) pid %d\n", mashed[0],
								getpid());
	    fprintf(errfp, "fork(sh -c %s ...) pid %d\n", mashed[0], getpid());
	}
	/* reassign stdout and stderr to the log file, stdin to /dev/null */
	reopenfds(c);

	execvp(newargs[0], newargs);

	/* not reached, unless exec() fails */
	fprintf(errfp, "Error: exec(xterm failed): %s\n", strerror(errno));
	exit(EX_UNAVAILABLE);
    } else {
	if (debug)
		/* XXX: build a complete cmd line */
	    if (c->logfname == NULL)
		fprintf(errfp, "\nStarting %d/%d %s: process id=%d\n",
			c->n, n_opt, mashed[0], pid);
	    else
		fprintf(errfp, "\nStarting %d/%d %s: process id=%d "
			"logfile=%s\n", c->n, n_opt, mashed[0], pid,
			c->logfname);
	if (pid == -1) {
	    fprintf(errfp, "Error: exec(sh -c) failed: %s\n", strerror(errno));
	    waitpid(c->pid, &status, WNOHANG);
	} else {
	    c->pid = pid;
	}
    }

    if (mashed[0] != NULL)
	free(mashed[0]);

    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return(0);
}

/*
 * start a command in an interactive xterm.
 */
int
xtermcmd(c, args)
    child	*c;
    char	**args;
{
    char	*cmd = "xterm -e",
		*mashed[] = { NULL, NULL },
		**newargs;
    int		status;
    pid_t	pid;
    time_t	t;

    /* XXX: is this right? */
    if (args == NULL)
	return(ENOEXEC);

    /* build newargs */
    if ((pid = arg_mash(&mashed, args))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(pid);
    }
    if ((pid = arg_replace(cmd, NULL, args, &newargs))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(pid);
    }

    /* block sigchld so we quickly reap it ourselves */
    sigprocmask(SIG_BLOCK, &set_chld, NULL);

    if (c->logfile) {
	t = time(NULL);
		/* XXX: build a complete cmd line */
	cmd = ctime(&t); cmd[strlen(cmd) - 1] = '\0';
	fprintf(c->logfile, "!!!!!!!\n!%s: %s\n!!!!!!!\n", cmd, mashed[0]);
	fflush(c->logfile);
    }

    if ((pid = fork()) == 0) {
	/* child */
	signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (debug > 1) {
		/* XXX: build a complete cmd line */
	    if (c->logfile != NULL)
		fprintf(c->logfile, "fork(sh -c %s ...) pid %d\n", mashed[0],
								getpid());
	    fprintf(errfp, "fork(sh -c %s ...) pid %d\n", mashed[0], getpid());
	}
	/* reassign stdout and stderr to the log file, stdin to /dev/null */
	reopenfds(c);

	execvp(newargs[0], newargs);

	/* not reached, unless exec() fails */
	fprintf(errfp, "Error: exec(xterm failed): %s\n", strerror(errno));
	exit(EX_UNAVAILABLE);
    } else {
	if (debug)
		/* XXX: build a complete cmd line */
	    if (c->logfname == NULL)
		fprintf(errfp, "\nStarting %d/%d %s: process id=%d\n",
			c->n, n_opt, mashed[0], pid);
	    else
		fprintf(errfp, "\nStarting %d/%d %s: process id=%d "
			"logfile=%s\n", c->n, n_opt, mashed[0], pid,
			c->logfname);
	if (pid == -1) {
	    fprintf(errfp, "Error: exec(sh -c) failed: %s\n", strerror(errno));
	    waitpid(c->pid, &status, WNOHANG);
	} else {
	    c->pid = pid;
	}
    }

    if (mashed[0] != NULL)
	free(mashed[0]);

    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return(0);
}

/*
 * start a xterm with the command tail -f on the logfile
 */
void
xtermlog(c)
    child	*c;
{
    char	*args[] = {	"xterm", "-e", "tail", "-f", NULL, NULL };
    int		status;

    if (c->logfname == NULL || c->logfile == NULL) {
	fprintf(errfp, "Error: tried to start xterm to watch NULL log file "
		"for par process\n");
	return;
    }
    args[4] = c->logfname;

    if ((c->xpid = fork()) == 0) {
	/* child */
	if (debug > 1) {
	    fprintf(errfp, "fork(xterm tail %s) pid %d\n", c->logfname,
		getpid());
	}
	/*
	 * set the process group id so signals are not delivered to the
	 * logging xterms.  in theory, we want them for a reason.
	 */
	setpgid(0, getpid());

	execvp(args[0], args);

	/* not reached, unless exec() fails */
	fprintf(errfp, "Error: exec(xterm failed): %s\n", strerror(errno));
	exit(EX_UNAVAILABLE);
    } else {
	if (c->xpid == -1) {
	    fprintf(errfp, "Error: exec(xterm) failed: %s\n", strerror(errno));
	    waitpid(c->xpid, &status, WNOHANG);
	    c->xpid = 0;
	    return;
	}
    }

    return;
}

/*
 * we are being killed.  kill and reap our sub-processes, except for
 * logging xterms.  if we get a second signal, we give up.
 */
RETSIGTYPE 
handler(int sig)
{
    int		i;

    /* block sigchld for the moment, no big deal if it fails */
    sigprocmask(SIG_BLOCK, &set_chld, NULL);

    if (debug > 1)
	fprintf(errfp, "Received signal %d\n", sig);

    for (i = 0; i < n_opt; i++) {
	if (! progeny[i].pid)
	    continue;

	if (debug > 1)
	    fprintf(errfp, "kill(%d, SIGQUIT)\n", progeny[i].pid);

	if (kill(progeny[i].pid, SIGQUIT))
	    fprintf(errfp, "Error: kill(%d): %s\n", progeny[i].pid,
		strerror(errno));
    }

    /* if we rx 2 signals, just give up */
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return;
}

RETSIGTYPE 
reapchild(void)
{
    int		i,
		status;
    char	*str;
    pid_t	pid;
    time_t	t;

    /* block sigchld for the moment, no big deal if it fails */
    sigprocmask(SIG_BLOCK, &set_chld, NULL);

    if (debug > 1)
	fprintf(errfp, "Received signal SIGCHLD\n");

    /*
     * clean-up hook so we know whether we need to walk the child table
     * again at the end of main()
     */
    chld_wait = -1;
    while ((pid = wait3(&status, WNOHANG, NULL)) > 0) {
	if (debug > 1)
	    fprintf(errfp, "reaped %d\n", pid);

	t = time(NULL);

	/* search for the pid */
	for (i = 0; i < n_opt; i++) {
	    if (progeny[i].pid == pid) {
		if (debug) {
		    if (progeny[i].logfname == NULL)
			fprintf(errfp, "%d finished\n", pid);
		    else
			fprintf(errfp, "%d finished (logfile %s)\n", pid,
							progeny[i].logfname);
		}
		if (progeny[i].logfile != NULL) {
		    str = ctime(&t); str[strlen(str) - 1] = '\0';
		    fprintf(progeny[i].logfile, "Ending: %s: pid = %d\n",
							str, pid);
		}
		progeny[i].pid = 0;
		break;
	    } else if (i_opt && progeny[i].xpid == pid) {
		progeny[i].xpid = 0;
		if (debug > 1)
		    if (progeny[i].logfile != NULL)
			fprintf(errfp, "%d log xterm finished\n", pid);
		    else
			fprintf(errfp, "%d log xterm finished (logfile %s)\n",
						pid, progeny[i].logfname);
		break;
	    }
	}
    }

    signal(SIGCHLD, (void *) reapchild);
    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return;
}   

/*
 * reassign stdout and stderr to the log file and stdin to /dev/null
 */
void
reopenfds(c)
    child	*c;
{
    int		fd = STDERR_FILENO + 1;

    /* stderr */
    if (! q_opt) {
	if (c->logfd != STDERR_FILENO) {
	    if (dup2(c->logfd, STDERR_FILENO) == -1)
		abort();
	    close(c->logfd);
	    c->logfd = STDERR_FILENO;
	}
	/* stdout */
	if (dup2(c->logfd, STDOUT_FILENO) == -1)
	    abort();
    } else {
	/* stderr */
	if (dup2(devnull, STDERR_FILENO) == -1)
	    abort();
	/* stdout */
	if (dup2(devnull, STDOUT_FILENO) == -1)
	    abort();
    }
    /* stdin */
    if (dup2(devnull, STDIN_FILENO) == -1)
	abort();

    /* close anything about stderr */
    while (fd < 10)
	close(fd++);

    return;
}

void
usage(void)
{
    fprintf(errfp,
"usage: %s [-dfiqx] [-n #] [-p n] [-l logfile] [-c command] [<command file>]
", progname);
    return;
}

void
vers(void)
{
    fprintf(errfp,
"%s: %s version %s
", progname, package, version);
    return;
}
