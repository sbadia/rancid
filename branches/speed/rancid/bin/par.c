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
 * PAR - parallel processing of command
 *
 * par runs a command N times in parallel.  It will accept a list of arguments
 * for command-line replacement in the command.  If the list entry begins
 * with a ":" the remainder of the line is the command to run ("{}" will be
 * replaced with each subsequent item in the list).  If the list entry begins
 * with a "#", the entry is ignored.  If a command is defined (either with
 * the -c or with a : line) any entry thereafter will be applied to the
 * command by replacing the {} brackets.  If no cammand is defined, then each
 * line is assumed to be a command to be run.
 */

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
sigset_t	set_chld;			/* SIGCHLD {un}blocking */

void		arg_free     __P((char ***));
int		arg_replace  __P((char *, char **, char **, char ***));
int		dispatch_cmd __P((char *, char **));
int		execcmd      __P((child *, char **));
int		line_split   __P((const char *, char ***));
int		read_input   __P((char*, FILE **, int *, char **, char ***));
void		reopenfds    __P((child *));
int		run_cmd      __P((child *, char **));
int		shcmd        __P((child *, char **));
void		usage        __P((void));
void		vers         __P((void));
int		xtermcmd     __P((child *, char **));
void		xtermlog     __P((child *));

RETSIGTYPE	handler      __P((int));
RETSIGTYPE	reapchild    __P((int));

int
main(int argc, char **argv, char **envp)
{
    extern char		*optarg;
    extern int		optind;
    char		ch;
    time_t		t;
    int			i,
			line;
    char		*cmd,			/* cmd (c_opt or from input) */
			**args;			/* cmd argv */
    FILE		*F;			/* input file */

    /* get just the basename() of our exec() name and strip .* off the end */
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
	    if (i_opt) {
		fprintf(errfp, "Warning: -e non-sensical with -i\n");
	    }
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
	    i_opt = 1;
	    if (e_opt) {
		fprintf(errfp, "Warning: -i non-sensical with -e\n");
	    }
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
    } else if (argc - optind == 0) {
	/* if no -f and no cmd-line input files, read from stdin */
	ifile = -1;
    } else if (c_opt == NULL && argc - optind == 0) {
	/* args after options are input file(s) */
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
    signal(SIGHUP, handler);
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGQUIT, handler);
    signal(SIGCHLD, reapchild);
    sigemptyset(&set_chld);
    sigaddset(&set_chld, SIGCHLD);

    /* if argv[ifile] is NULL and stdin is closed, then f_opt is implicit */
    if (argv[ifile] == NULL && stdin == NULL)
	f_opt = 1;

    /*
     * command dispatch starts here.
     *
     * -f means just run the -c command -n times.  any args become "".
     */
    if (f_opt) {
	if ((i = arg_replace(c_opt, NULL, NULL, &args))) {
	    fprintf(errfp, "Error: failed to build command");
	    if (i == ENOMEM)
		fprintf(errfp, ": %s\n", strerror(errno));
	    else
		fprintf(errfp, "\n");
	} else {
	    for (i = 0; i < n_opt; i++) {
		progeny[i].n = i + 1;
			/* XXX: check retcode from *cmd()? */
		run_cmd(&progeny[i], args);
	    }
	    arg_free(&args);
	}
    } else if (ifile > 0) {
	/*
	 * input files were specified on the command-line, read each as input
	 */
	F = NULL; cmd = NULL;  args = NULL;
	for ( ; ifile < argc; ifile++) {
	    while ((i = read_input(argv[ifile], &F, &line, &cmd, &args)) == 0) {
		dispatch_cmd(cmd, args);
		if (args != NULL)
		    arg_free(&args);
	    }
	    /* XXX: if (i == EOF) */
	    if (cmd != NULL)
		free(cmd);
	    if (args != NULL)
		arg_free(&args);
	}
    } else {
	/* read stdin as input */
	F = stdin;
	line = 1; cmd = NULL;  args = NULL;
	while ((i = read_input("(stdin)", &F, &line, &cmd, &args)) == 0) {
	    dispatch_cmd(cmd, args);
	    if (args != NULL)
		arg_free(&args);
	}
	if (cmd != NULL)
	    free(cmd);
	if (args != NULL)
	    arg_free(&args);
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
		if (cmd[c+1] == 'n') {
		    lcmd[l++] = '\n';
		    c += 2;
		} else if (cmd[c+1] == 't') {
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
 * split a line into an arg[][] with shell escape/quoting semantics.
 */
int
line_split(line, args)
    const char	*line;
    char	***args;
{
    int		argn = 0,			/* current arg */
		b,
		c,
		llen,				/* length of line */
		len,
		nargs = 0;
    int		quotes = 0;			/* track double quotes */
    char	*tick;				/* ptr to single quote */

    /* temp buffer for a single arg. 2* as long as the buf used to read to
     * reduce the chance of expansion exceeding the length of the buf
     */
    char	buf[LINE_MAX * 2];

    if (args == NULL)
	abort;

    /* if line is NULL, just create arg[1][NULL] */
    if (line == NULL) {
        if ((*args = (char **) malloc(sizeof(char **))) == NULL)
	    return(errno);
	bzero(*args, sizeof(char **));
    } else {
	/* skip leading/trailing whitespace in line */
        while (*line == ' ' || *line == '\t')
	    line++;
	llen = strlen(line);
        while (line[llen] == ' ' || line[llen] == '\t' && llen > 0)
	    llen--;

	/*
	 * just count spaces for args[][] malloc, this will waste memory when
	 * spaces are in quoted args, but do not expect it to be a big deal.
	 */
	c = 0;
	while (c < llen) {
	    if (line[c] == ' ' || line[c] == '\t')
		nargs++;
	    c++;
	}
	/* gratuitous last arg and args[][NULL] */
	nargs += 2;

	if ((*args = (char **) malloc(sizeof(char **) * nargs)) == NULL)
	    return(errno);
	bzero(*args, sizeof(char **) * nargs);

	/*
	 * copy the args into place.
	 * unwrap shell style quoting as we go.
	 */
	b = c = 0;
	bzero(buf, LINE_MAX * 2);
	while (c <= llen) {
	    /* XXX: need to check buf len before anything */
	    if ((LINE_MAX * 2) - b < 2)
		return(ENOMEM);

	    switch(line[c]) {
	    case '\\':
		if (quotes) {
		    buf[b++] = line[c++];
		    buf[b++] = line[c++];
		} else {
		    if (line[c + 1] == 'n') {
			buf[b++] = '\n';
			c += 2;
		    } else if (line[c + 1] == 't') {
			buf[b++] = '\t';
			c += 2;
		    } else {
			buf[b++] = line[++c];
			c += 2;
		    }
		}
		break;
	    case '\'':
		/* shell preserves the meaning of all chars between single
		 * quotes, including backslashes.  so, it is not possible to
		 * put a single quote inside a single quoted string in shell.
		 */
		c++;
		if ((tick = index(line + c, '\'')) == NULL) {
		    /* unmatched quotes */
		    return(EX_DATAERR);
		}
		len = tick - (line + c);
		if ((b + len + 1) > (LINE_MAX * 2))
		    return(ENOMEM);
		bcopy(&line[c], &buf[b], len);
		c += len + 1; b += len;
		break;
	    case '"':
		/* the shell would recognize $, `, and \ in double-" strings.
		 * by the time we get it, these chars should not exist and
		 * thus we we ignore them.  all we deal with are \" and \n.
		 */
		quotes ^= 1;
		c++;
		break;
	    case '\t':
	    case ' ':
	    case '\0':
		/* the end of line, copy the last arg */
		if (!quotes) {
		    /* make a copy of the buffer for args[argn] */
		    buf[b++] = '\0';
		    if (asprintf(&((*args)[argn]), "%s", buf) == -1)
			return(errno);
		    argn++; c++; b = 0;
		    buf[0] = '\0';
		} else {
		    if (line[c] == '\0')
			/* unmatched quotes */
			return(EX_DATAERR);
		    buf[b++] = line[c++];
		}
		break;
	    default:
		buf[b++] = line[c++];
	    }
	}
    }

    return(0);
}

/*
 * make cmd and arg strings into an args[][] for exec(), find a process
 * slot (1 of n_opt) to run the command and use run_cmd() to start it.
 */
int
dispatch_cmd(cmd, args)
    char	*cmd,
		**args;
{
    int		i;
    static int	cmd_n = 1;

    for (i = 0; i < n_opt; i++) {
	if (progeny[i].pid != 0)
	    continue;

	progeny[i].n = cmd_n;
			/* XXX: check retcode from *cmd()? */
	return(run_cmd(&progeny[i], args));
    }

	/* XXX: is this right?  what could run_cmd() return? */
    return(-1);
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
    time_t	t;

    /* XXX: is this right? */
    if (args == NULL)
	return(ENOEXEC);

    /* build newargs */
    if ((status = arg_mash(&mashed, args))) {		/* simply for the log */
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(status);
    }
    if ((status = arg_replace(cmd, NULL, args, &newargs))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(status);
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

    if ((c->pid = fork()) == 0) {
	/* child */
	signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (debug > 1) {
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
	    fprintf(errfp, "\nStarting %d/%d %s: process id=%d\n",
					c->n, n_opt, mashed[0], c->pid);
	if (c->pid == -1) {
	    fprintf(errfp, "Error: exec(sh -c) failed: %s\n", strerror(errno));
	    waitpid(c->pid, &status, WNOHANG);
	    c->pid = 0;
	}
    }

    if (mashed[0] != NULL)
	free(mashed[0]);

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
    time_t	t;

    /* XXX: is this right? */
    if (args == NULL)
	return(ENOEXEC);

    /* build newargs */
    if ((status = arg_mash(&mashed, args))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(status);
    }
    if ((status = arg_replace(cmd, NULL, mashed, &newargs))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(status);
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

    if ((c->pid = fork()) == 0) {
	/* child */
	signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (debug > 1) {
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
	    fprintf(errfp, "\nStarting %d/%d %s: process id=%d\n",
					c->n, n_opt, mashed[0], c->pid);
	if (c->pid == -1) {
	    fprintf(errfp, "Error: exec(sh -c) failed: %s\n", strerror(errno));
	    waitpid(c->pid, &status, WNOHANG);
	    c->pid = 0;
	}
    }

    if (mashed[0] != NULL)
	free(mashed[0]);

    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return(0);
}

/*
 * if F is NULL, we open fname.
 *
 * read first line as a command and subsequent lines as either commands or
 * args depending upon c_opt and the first line.  basically;
 * - if the first char of the first line is ':', then what follows it is the
 *   command.
 * - if what follows is null, then the command is c_opt (-c) and subsequent
 *   lines are args
 * - if -c was not specified, then each subsequent line is a command without
 *   arg substitution
 * - if the ':' of the first line was ommitted, then we proceed as if it had
 *   been but the command was empty
 *
 * return 0 on success, EOF at end of file, else errno with an errmsg in cmd.
 * note: any space allocated for cmd or args must be free'd by the caller
 *	 with free() or arg_free().
 */
int
read_input(fname, F, line, cmd, args)
    char	*fname;
    FILE	**F;
    int		*line;
    char	**cmd;
    char	***args;
{
    int		e;
    char	buf[LINE_MAX + 1];

    if (cmd == NULL || args == NULL)
	abort();

    if (*F == NULL) {
	if (fname == NULL)
	    abort();

	if ((*F = fopen(fname, "r")) == NULL) {
	    e = errno;
	    asprintf(cmd, "Error: open(%s) failed: %s\n", fname, strerror(e));
	    return(e);
	}
	*cmd = NULL;  *args = NULL;
	*line = 1;
    }

    /* first line might be a command */
    if (*line == 1) {
	switch ((buf[0] = fgetc(*F))) {
	case EOF:
	    goto ERR;
	    break;
	case ':':
	    if (fgets(buf, LINE_MAX + 1, *F) == NULL)
		goto ERR;
	    *line++;
	    if (asprintf(cmd, "%s", buf) == -1) {
		e = errno;
		asprintf(cmd, "Error: %s\n", strerror(e));
		return(e);
	    }
	    break;
	default:
	    ungetc(buf[0], *F);
	    if (cmd == NULL && c_opt != NULL)
		if (asprintf(cmd, "%s", c_opt) == -1) {
		    e = errno;
		    asprintf(cmd, "Error: %s\n", strerror(e));
		    return(e);
		}
	}
    }
    if (fname == NULL)
	fname = "(null)";

NEXT:
    /* next line */
    if (fgets(buf, LINE_MAX + 1, *F) == NULL)
	goto ERR;
    *line++;
    if (buf[0] == '#')
	goto NEXT;

    /*
     * if there isnt a \n on the end, either we lacked buffer space or
     * the last line of the file lacks a \n
     */
    e = strlen(buf);
    if (buf[e - 1] == '\n') {
	buf[e - 1] = '\0';
    } /* else
	/* XXX: finish this */

    /* split the line into an args[][] */
    if ((e = line_split(buf, args))) {
	asprintf(cmd, "Error: parsing args: %s\n", strerror(e));
	return(e);
    }

    return(0);

ERR:
    /* handle FILE error */
    if (feof(*F)) {
	e = EOF;
    } else {
	e = errno;
	asprintf(cmd, "Error: read(%s) failed: %s\n", fname, strerror(e));
    }
    fclose(*F); *F = NULL;
    return(e);
}

/*
 * reassign stdout and stderr to the log file and stdin to /dev/null.
 * called before exec()s.
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

/*
 * start a command.
 */
int
run_cmd(c, args)
    child	*c;
    char	**args;
{
    int		e;

    if (c == NULL)
	return;

    if (i_opt) {
	e = xtermcmd(c, args);
    } else if (e_opt) {
	e = execcmd(c, args);
    } else {
	e = shcmd(c, args);
    }

    if (p_opt) {
	sigprocmask(SIG_BLOCK, &set_chld, NULL);
	sleep(p_opt);
	sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
    }

    return(e);
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
    time_t	t;

    /* XXX: is this right? */
    if (args == NULL)
	return(ENOEXEC);

    /* build newargs */
    if ((status = arg_mash(&mashed, args))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(status);
    }
    if ((status = arg_replace(cmd, NULL, args, &newargs))) {
	/* XXX: is this err msg always proper? will only ret true or ENOMEM? */
	fprintf(errfp, "Error: memory allocation failed: %s\n",
		strerror(errno));
	return(status);
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

    if ((c->pid = fork()) == 0) {
	/* child */
	signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_UNBLOCK, &set_chld, NULL);
	if (debug > 1) {
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
	    fprintf(errfp, "\nStarting %d/%d %s: process id=%d\n",
					c->n, n_opt, mashed[0], c->pid);
	if (c->pid == -1) {
	    fprintf(errfp, "Error: exec(sh -c) failed: %s\n", strerror(errno));
	    waitpid(c->pid, &status, WNOHANG);
	    c->pid = 0;
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
	 * logging xterms.  in theory, we want them for a reason, so we
	 * dont want them to die when par exits.
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
handler(sig)
    int		sig;
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
reapchild(sig)
    int		sig;
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
		if (debug > 1) {
		    if (progeny[i].logfile != NULL)
			fprintf(errfp, "%d log xterm finished\n", pid);
		    else
			fprintf(errfp, "%d log xterm finished (logfile %s)\n",
						pid, progeny[i].logfname);
		}
		break;
	    }
	}
    }

    signal(SIGCHLD, reapchild);
    sigprocmask(SIG_UNBLOCK, &set_chld, NULL);

    return;
}   

void
usage(void)
{
    fprintf(errfp,
"usage: %s [-dfiqx] [-n #] [-p n] [-l logfile] [-c command] [<command file>]\n",
	progname);
    return;
}

void
vers(void)
{
    fprintf(errfp, "%s: %s version %s\n", progname, package, version);
    return;
}
