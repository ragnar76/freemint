/*
 * $Id$
 *
 * This file belongs to FreeMiNT. It's not in the original MiNT 1.12
 * distribution. See the file CHANGES for a detailed log of changes.
 *
 *
 * Copyright 2003 Konrad M.Kokoszkiewicz <draco@atari.org>
 * All rights reserved.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * Author:  Konrad M.Kokoszkiewicz
 * Started: 15-01-2003
 *
 * please send suggestions, patches or bug reports to me or
 * the MiNT mailing list
 *
 * Note:
 *
 * 1) the shell is running as separate process, not in kernel context;
 * 2) the shell is running in user mode;
 * 3) the shell is very minimal, so don't expect much ;-)
 *
 * TODO in random order:
 *
 * - cp, mv;
 * - i/o redirection;
 * - filename completion;
 * - make crunch() expand wildcards;
 * - make possible to execute some sort of init script (shellrc or something);
 * - after the code stops changing so quickly, move all the messages to info.c;
 * - get rid of static buffers and Cconrs();
 *
 */

# ifdef BUILTIN_SHELL

# include <stdarg.h>

# define _SHELL_THREAD

# include "libkern/libkern.h"

# include "mint/basepage.h"	/* BASEPAGE struct */
# include "mint/signal.h"	/* SIGCHLD etc. */
# include "mint/stat.h"		/* struct stat */

# include "cnf.h"		/* init_env */
# include "dosmem.h"		/* m_xalloc(), m_free(), m_shrink() */
# include "info.h"		/* national stuff */
# include "k_exec.h"		/* sys_pexec() */

# ifdef DEBUG_INFO
# include "mint/proc.h"		/* curproc (for DEBUG) */
# endif

# ifdef _SHELL_THREAD
# include "arch/startup.h"	/* _base */
# include "console.h"		/* c_conin(), c_conrs() */
# include "dos.h"		/* p_umask(), p_domain() */
# include "dosdir.h"		/* d_opendir(), d_readdir(), d_closedir(), f_symlink(), f_stat64(), ...*/
# include "dosfile.h"		/* f_write() */
# include "dossig.h"		/* p_kill() */
# include "k_kthread.h"		/* kthread_create(), kthread_exit() */
# include "proc.h"		/* struct proc */
# include "time.h"
# else
# include "arch/cpu.h"		/* setstack() */
# include "arch/syscall.h"	/* Pexec(), Cconrs() et cetera */
# include "mint/mem.h"		/* F_FASTLOAD et contubernales */
# endif

# include "mis.h"

# define STDIN		0
# define STDOUT		1
# define STDERR		2

# ifndef _SHELL_THREAD
# define SHELL_FLAGS	(F_FASTLOAD | F_ALTLOAD | F_ALTALLOC | F_PROT_P)
# endif
# define SHELL_UMASK	~(S_ISUID | S_ISGID | S_ISVTX | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

# define SH_VER_MAIOR	1
# define SH_VER_MINOR	0

# define COPYCOPY	"(c) 2003 Konrad M.Kokoszkiewicz (draco@atari.org)\r\n"

# define SHELL_ARGS	1024L		/* number of pointers in the argument vector table (i.e. 4K) */
# define SHELL_MAXPATH	1024L		/* max length of a pathname */
/* maximum usage is so far about a half ot this */
# define SHELL_STACK	((SHELL_ARGS * sizeof(long)) + (SHELL_MAXPATH * 3 * sizeof(long)) + 16384L)	/* 32768 */

/* this is an average number of seconds in Gregorian year
 * (365 days, 6 hours, 11 minutes, 15 seconds).
 */
# define SEC_OF_YEAR	31558275L

# define LINELEN	254

/* Some help for the optimizer */
static EXITING shell(void) NORETURN;

/* Global variables */
# ifdef _SHELL_THREAD
static struct proc *p;
# endif
static BASEPAGE *shell_base;
static short xcommands;		/* if 1, the extended command set is active */

/* Utility routines */

static void
shell_fprintf(long handle, const char *fmt, ...)
{
# ifdef _SHELL_THREAD
	static char buf[SHELL_MAXPATH << 1];
# else
	char buf[SHELL_MAXPATH << 1];
# endif
	va_list args;

	va_start(args, fmt);
	vsprintf(buf, sizeof (buf), fmt, args);
	va_end(args);

# ifdef _SHELL_THREAD
	f_write(handle, strlen(buf), buf);
# else
	Fwrite(handle, strlen(buf), buf);
# endif
}

INLINE void
shell_print(const char *text)
{
	shell_fprintf(STDOUT, text);
}

/* Simple conversion of a pathname from the DOS to Unix format,
 * used mainly for displaying symlinks in `correct' (i.e. Unix) form.
 */
static void
dos2unix(char *p)
{
	if (p[1] == ':')
	{
		if (toupper(p[0]) == 'U')
			strcpy(p, p + 2);	/* eat off a prefix like 'u:' */
		else
		{
			p[1] = p[0];
			p[0] = '/';
		}
	}

	while (*p)
	{
		if (*p == '\\')
			*p = '/';
		p++;
	}
}

/* Helper routines for manipulating environment */
static long
env_size(char *var)
{
	long count = 0;

	if (var)
	{
		while (*var)
		{
			while (*var)
			{
				var++;
				count++;
			}
			var++;
			count++;
		}
	}

	return ++count;		/* plus trailing NULL */
}

static char *
env_append(char *where, char *what)
{
	strcpy(where, what);
	where += strlen(where);

	return ++where;
}

/* shell_delenv() idea is borrowed from mintlib's del_env() */
static void
shell_delenv(const char *strng)
{
	char *name, *var;

	/* find the tag in the environment */
	var = _mint_getenv(shell_base, strng);
	if (!var)
		return;

	/* if it's found, move all the other environment variables down by 1 to
   	 * delete it
         */
	var -= strlen(strng);
	name = var + strlen(var);

	var--;

	do
	{
		while (*name)
			*var++ = *name++;
		*var++ = *name++;
	} while (*name);

	*var = 0;

# ifdef _SHELL_THREAD
	m_shrink(0, (long)shell_base->p_env, env_size(shell_base->p_env));
# else
	Mshrink((void *)shell_base->p_env, env_size(shell_base->p_env));
# endif
}

/* XXX try to avoid reallocation whenever possible */
static void
shell_setenv(const char *var, char *value)
{
	char *env_str = shell_base->p_env;
	char *new_env, *old_var, *es, *ne;
	long new_size;

	new_size = env_size(env_str) + strlen(value) + 2;	/* '=' and trailing zero */
	old_var = _mint_getenv(shell_base, var);

	if (old_var)
		new_size -= strlen(old_var);	/* this is the VALUE of the var */
	else
		new_size += strlen(var);	/* this is its NAME */

# ifdef _SHELL_THREAD
	new_env = (char *)m_xalloc(new_size, 3);
# else
	new_env = (char *)Mxalloc(new_size, 3);
# endif
	if (new_env == NULL)
		return;

	es = env_str;
	ne = new_env;

	/* If it already exists, delete it */
	shell_delenv(var);

	/* Copy old env to new place */
	while (*es)
	{
		while (*es)
			*ne++ = *es++;
		*ne++ = *es++;
	}

	strcpy(ne, var);
	strcat(ne, "=");
	strcat(ne, value);

	ne += strlen(ne);
	*++ne = 0;

	shell_base->p_env = new_env;

# ifdef _SHELL_THREAD
	m_free((long)env_str);
# else
	Mfree(env_str);
# endif
}

/* Split command line into separate strings, put their pointers
 * into argv[], return argc.
 *
 * XXX add wildcard expansion (at least the `*'), see fnmatch().
 */
INLINE long
crunch(char *cmd, char **argv)
{
	char *start, *endquote = NULL;
	long idx = 0;
	short special;

	while (*cmd)
	{
		/* Kill leading spaces */
		while (*cmd && isspace(*cmd))
			cmd++;

		/* If the argument is an enviroment variable, evaluate it.
		 * It is assumed, that a space or NULL terminates the argument.
		 */
		if (cmd[0] == '$' && isupper(cmd[1]))
		{
			start = cmd + 1;

			while (*cmd && !isspace(*cmd))
				cmd++;

			if (*cmd)
				*cmd++ = 0;		/* terminate the tag for getenv() */

			start = _mint_getenv(shell_base, start);
			if (start)
				argv[idx++] = start;
		}
		else
		{
			/* Check if there may be a quoted argument. A quoted argument will begin with
			 * a quote and end with a quote. Quotes do not belong to the argument.
			 * Everything between quotes is litterally taken as argument, without any
			 * internal interpretation.
			 */
			if (*cmd == '\'')
			{
				cmd++;
				endquote = cmd;

				while (*endquote && *endquote != '\'')
					endquote++;

				if (!*endquote)
					return -1;		/* unbalanced quotes, syntax error */
			}

			start = cmd;

			if (endquote == NULL)
			{
				/* Search for the ending separator. In a non-quoted argument any space
				 * is understood as a separator except when preceded by the backslash.
				 */
				while (*cmd && !isspace(*cmd))
				{
					special = (*cmd == '\\');
					if (special && cmd[1])
						strcpy(cmd, cmd + 1);	/* physically remove the backslash */
					cmd++;
				}
			}
			else
			{
				cmd = endquote;
				endquote = NULL;
			}

			if (*cmd)
				*cmd++ = 0;

			argv[idx++] = start;
		}
	}

	argv[idx] = NULL;

	return idx;
}

/* Execute an external program */
INLINE long
execvp(char **argv)
{
	char *var, *new_env, *new_var, *t, *path, *np;
# ifdef _SHELL_THREAD
	static char oldcmd[128], npath[SHELL_MAXPATH + 256];
# else
	char oldcmd[128], npath[SHELL_MAXPATH + 256];
# endif
	long count, x, y, r = -1L, blanks = 0;

	/* Calculate the size of the environment */
	var = shell_base->p_env;
	count = env_size(var) + sizeof("ARGV=NULL:");

	/* Add space for the ARGV strings.
	 */
	for (x = 0; argv[x]; x++)
		count += strlen(argv[x]) + 1;	/* trailing NULL */

	count <<= 1;				/* make this twice as big (will be shrunk later) */

# ifdef _SHELL_THREAD
	new_env = (char *)m_xalloc(count, 3);
# else
	new_env = (char *)Mxalloc(count, 3);
# endif

	if (!new_env)
		return -40;

	var = shell_base->p_env;
	new_var = new_env;

	/* Copy variables from `var' to `new_var', but don't copy ARGV strings
	 */
	if (var)
	{
		while (*var)
		{
			if (strncmp(var, "ARGV=", 5) == 0)
				break;
			while (*var)
				*new_var++ = *var++;
			*new_var++ = *var++;
		}
	}

	/* Append new ARGV strings.
	 *
	 * First check if there are NULL arguments.
	 */
	for (x = 0; !blanks && argv[x]; x++)
		blanks = (argv[x][0] == 0);

	/* Now create the ARGV variable.
	 */
	if (blanks)
	{
		new_var = env_append(new_var, "ARGV=NULL:");
		new_var--;

		for (x = 0; argv[x]; x++)
		{
			if (argv[x][0] == 0)
			{
				ksprintf(new_var, 8, "%ld,", x);
				new_var += strlen(new_var);
			}
		}
		new_var[-1] = 0;	/* kill the last comma */
	}
	else
		new_var = env_append(new_var, "ARGV=");

	/* Append the argument strings.
	 */
	for (x = 0; argv[x]; x++)
	{
		if (argv[x][0] == 0)
			new_var = env_append(new_var, " ");
		else
			new_var = env_append(new_var, argv[x]);
	}

	*new_var = 0;

# ifdef _SHELL_THREAD
	(void)m_shrink(0, (long)new_env, env_size(new_env));
# else
	(void)Mshrink((void *)new_env, env_size(new_env));
# endif

	/* Since crunch() evaluates some arguments, we have now to
	 * re-create the old GEMDOS style command line string.
	 */
	bzero(oldcmd, sizeof(oldcmd));

	x = 1;			/* must skip the first argument (program name) */
	y = 0;
	while (y < (sizeof(oldcmd) - 2) && argv[x])
	{
		oldcmd[y++] = ' ';

		if (argv[x][0] == 0)
			strncpy_f(oldcmd + y, "''", sizeof(oldcmd) - 2 - y);
		else
			strncpy_f(oldcmd + y, argv[x], sizeof(oldcmd) - 2 - y);

		y += strlen(oldcmd + y);
		x++;
	}
	oldcmd[0] = 0x7f;

	/* $PATH searching. Don't do it, if the user seems to
	 * have specified the explicit pathname.
	 */
	t = strrchr(argv[0], '/');

	path = "";

	if (t == NULL)
	{
		path = _mint_getenv(shell_base, "PATH");
		if (path == NULL)
			path = "./";
	}

	do
	{
		np = npath;

		while (*path && *path != ';' && *path != ',')
			*np++ = *path++;
		if (*path)
			path++;			/* skip the separator */

		*np = 0;
		/* t == NULL means that we really search through $PATH */
		if (t == NULL)
			strcat(npath, "/");
		strcat(npath, argv[0]);

# ifdef _SHELL_THREAD
		r = sys_pexec(0, npath, oldcmd, new_env);
# else
		r = Pexec(0, npath, oldcmd, new_env);
# endif

	} while (*path && r < 0);

# ifdef _SHELL_THREAD
	m_free((long)new_env);
# else
	Mfree(new_env);
# endif

	return r;
}

static void
xcmdstate(void)
{
	shell_fprintf(STDOUT, MSG_shell_xcmd_info, xcommands ? MSG_shell_xcmd_on : MSG_shell_xcmd_off);
}

/* End utilities, begin commands */

static long
sh_ver(long argc, char **argv)
{
	shell_fprintf(STDOUT, MSG_shell_name, SH_VER_MAIOR, SH_VER_MINOR, COPYCOPY);

	return 0;
}

static long
sh_help(long argc, char **argv)
{
	shell_fprintf(STDOUT, MSG_shell_help, xcommands ? MSG_shell_xcmd_on : MSG_shell_xcmd_off);

	return 0;
}

/* Display all files in the current directory, with attributes et ceteris.
 * No wilcards filtering what to display, no sorted output, no nothing.
 */
static long
sh_ls(long argc, char **argv)
{
	struct stat st;
	struct timeval tv;
	short year, month, day, hour, minute;
	short i, all = 0, full = 0;
	long x, r, s, handle;
	char *dir;
# ifndef _SHELL_THREAD
	char path[SHELL_MAXPATH], link[SHELL_MAXPATH];
	char entry[256];
# else
	static char path[SHELL_MAXPATH], link[SHELL_MAXPATH];
	static char entry[256];
# endif

	dir = ".";

	/* Accept options like --help, -a and -l */
	for (x = 1; x < argc; x++)
	{
		if (argv[x][0] == '-')
		{
			if (strcmp(argv[x], "--help") == 0)
			{
				shell_fprintf(STDOUT, MSG_shell_ls_help, argv[0]);

				return 0;
			}
			else
			{
				for (i = 1; argv[x][i]; i++)
				{
					if (argv[x][i] == 'a')
						all = 1;
					else if (argv[x][i] == 'l')
						full = 1;
				}
			}
		}
		else
		{
			dir = argv[x];
			break;
		}
	}

# ifdef _SHELL_THREAD
	handle = d_opendir(dir, 0);
# else
	handle = Dopendir(dir, 0);
# endif
	if (handle < 0)
		return handle;

# ifdef _SHELL_THREAD
	t_gettimeofday(&tv, NULL);
# else
	Tgettimeofday(&tv, NULL);
# endif

	do
	{
# ifdef _SHELL_THREAD
		r = d_readdir(sizeof(entry), handle, entry);
# else
		r = Dreaddir(sizeof(entry), handle, entry);
# endif

		/* Display only names not starting with a dot, unless -a was specified */
		if (r == 0 && (entry[sizeof(long)] != '.' || all))
		{
			strcpy(path, dir);
			strcat(path, "/");
			strcat(path, entry + sizeof(long));

			/* `/bin/ls -l' doesn't dereference links, so we don't either */
# ifdef _SHELL_THREAD
			s = f_stat64(1, path, &st);
# else
			s = Fstat64(1, path, &st);
# endif
			if (s == 0)
			{
				if (S_ISLNK(st.mode))
				{
# ifdef _SHELL_THREAD
					s = f_readlink(sizeof(link), link, path);
# else
					s = Freadlink(sizeof(link), link, path);
# endif
					if (s == 0)
						dos2unix(link);
				}

				if (full)
				{
					/* Reuse the path[] space for attributes */
					strcpy(path, "?---------");

					if (S_ISLNK(st.mode))		/* file type */
						path[0] = 'l';
					else if (S_ISMEM(st.mode))
						path[0] = 'm';
					else if (S_ISFIFO(st.mode))
						path[0] = 'p';
					else if (S_ISREG(st.mode))
						path[0] = '-';
					else if (S_ISBLK(st.mode))
						path[0] = 'b';
					else if (S_ISDIR(st.mode))
						path[0] = 'd';
					else if (S_ISCHR(st.mode))
						path[0] = 'c';
					else if (S_ISSOCK(st.mode))
						path[0] = 's';

					/* access attibutes: user */
					if (st.mode & S_IRUSR)
						path[1] = 'r';
					if (st.mode & S_IWUSR)
						path[2] = 'w';
					if (st.mode & S_IXUSR)
						path[3] = (st.mode & S_ISUID) ? 's' : 'x';

					/* ... group */
					if (st.mode & S_IRGRP)
						path[4] = 'r';
					if (st.mode & S_IWGRP)
						path[5] = 'w';
					if (st.mode & S_IXGRP)
						path[6] = (st.mode & S_ISGID) ? 's' : 'x';

					/* ... others */
					if (st.mode & S_IROTH)
						path[7] = 'r';
					if (st.mode & S_IWOTH)
						path[8] = 'w';
					if (st.mode & S_IXOTH)
						path[9] = (st.mode & S_ISVTX) ? 't' : 'x';

					/* Now recalculate the time stamp */
					unix2calendar(st.mtime.time, &year, &month, &day, &hour, &minute, NULL);

					shell_fprintf(STDOUT, "%s %5d %8ld %8ld %8ld %s %2d ", \
							path, (short)st.nlink, st.uid, st.gid, \
							(long)st.size, months_abbr_3[month - 1], day);

					/* Here we decide whether the timestamp displayed should
					 * contain the year or the hour/minute of the modification.
					 *
					 * Basically everything that was modified 6 months ago or
					 * earlier will have the year, more recent stuff will get
					 * hour/minute. This is less or more what /bin/ls does.
					 *
					 * There can be up to 18 hours, 33 minutes and 45 seconds
					 * of error relative to the calendar time, but for this purpose
					 * it does not matter so much (and we avoid calling
					 * unix2calendar again).
					 */
					if ((tv.tv_sec - st.mtime.time) > (SEC_OF_YEAR >> 1))
						shell_fprintf(STDOUT, "%5d ", year);
					else
						shell_fprintf(STDOUT, "%02d:%02d ", hour, minute);
				}

				shell_fprintf(STDOUT, "%s", entry + sizeof(long));

				if (S_ISLNK(st.mode))
					shell_fprintf(STDOUT, " -> %s\r\n", link);
				else
					shell_print("\r\n");
			}
		}
	} while (r == 0);

# ifdef _SHELL_THREAD
	d_closedir(handle);
# else
	Dclosedir(handle);
# endif

	return 0;
}

/* Change cwd to the given path. If none given, change to $HOME
 *
 * XXX crashes when entering /kern/1
 *
 */
static long
sh_cd(long argc, char **argv)
{
	long r;
	char *newdir, *pwd;
# ifdef _SHELL_THREAD
	static char cwd[SHELL_MAXPATH];
# else
	char cwd[SHELL_MAXPATH];
# endif

	if (argc >= 2)
	{
		if (strcmp(argv[1], "--help") == 0)
		{
			shell_fprintf(STDOUT, MSG_shell_cd_help, argv[0]);

			return 0;
		}
		else
			newdir = argv[1];
	}
	else
	{
		newdir = _mint_getenv(shell_base, "HOME");
		if (!newdir)
			newdir = "/";
	}

# ifdef _SHELL_THREAD
	r = d_setpath(newdir);
# else
	r = Dsetpath(newdir);
# endif

	if (r == 0)
	{
# ifdef _SHELL_THREAD
		d_getcwd(cwd, 0, sizeof(cwd));
# else
		Dgetcwd(cwd, 0, sizeof(cwd));
# endif

		if (*cwd == 0)
			strcpy(cwd, "/");
		else
			dos2unix(cwd);

		pwd = _mint_getenv(shell_base, "PWD");

		if (pwd && strcmp(pwd, cwd))
			shell_setenv("OLDPWD", pwd);

		shell_setenv("PWD", cwd);
	}

	return r;
}

/* chgrp, chown, chmod: change various attributes */
static long
sh_chgrp(long argc, char **argv)
{
	struct stat st;
	long r, gid;
	char *fname;

	if (argc < 3)
	{
		shell_fprintf(STDERR, "%s%s\r\n", argv[0], MSG_shell_missing_arg);

		return 0;
	}

	fname = argv[2];

# ifdef _SHELL_THREAD
	r = f_stat64(0, fname, &st);
# else
	r = Fstat64(0, fname, &st);
# endif
	if (r < 0)
		return r;

	gid = atol(argv[1]);

	if (gid == st.gid)
		return 0;			/* no change */

# ifdef _SHELL_THREAD
	return f_chown(fname, st.uid, gid);
# else
	return Fchown(fname, st.uid, gid);
# endif
}

static long
sh_chown(long argc, char **argv)
{
	struct stat st;
	long r, uid, gid;
	char *own, *fname, *grp;

	if (argc < 3)
	{
		shell_fprintf(STDERR, "%s%s\r\n", argv[0], MSG_shell_missing_arg);

		return 0;
	}

	own = argv[1];
	fname = argv[2];

# ifdef _SHELL_THREAD
	r = f_stat64(0, fname, &st);
# else
	r = Fstat64(0, fname, &st);
# endif
	if (r < 0)
		return r;

	uid = st.uid;
	gid = st.gid;

	/* cases like `chown 0 filename' and `chown 0. filename' */
	grp = strrchr(own, '.');
	if (!grp)
		grp = strrchr(own, ':');

	if (grp)
	{
		*grp++ = 0;
		if (isdigit(*grp))
			gid = atol(grp);
	}

	/* cases like `chown .0 filename' */
	if (isdigit(*own))
		uid = atol(own);

	if (uid == st.uid && gid == st.gid)
		return 0;			/* no change */

# ifdef _SHELL_THREAD
	return f_chown(fname, uid, gid);
# else
	return Fchown(fname, uid, gid);
# endif
}

static long
sh_chmod(long argc, char **argv)
{
	long d = 0;
	short c;
	char *s;

	if (argc < 3)
	{
		shell_fprintf(STDERR, "%s%s\r\n", argv[0], MSG_shell_missing_arg);

		return 0;
	}

	s = argv[1];

	while ((c = *s++) != 0 && isodigit(c))
	{
		d <<= 3;
		d += (c & 0x0007);
	}

	d &= S_IALLUGO;

# ifdef _SHELL_THREAD
	return f_chmod(argv[2], d);
# else
	return Fchmod(argv[2], d);
# endif
}

static long
sh_xcmd(long argc, char **argv)
{
	if (argc >= 2)
	{
		if (strcmp(argv[1], "on") == 0)		/* don't change this to `MSG_shell_xcmd_on' */
			xcommands = 1;
		else if (strcmp(argv[1], "off") == 0)
			xcommands = 0;
		else if (strcmp(argv[1], "--help") == 0)
			shell_fprintf(STDOUT, MSG_shell_xcmd_help, argv[0]);
	}
	xcmdstate();

	return 0;
}

static long
sh_exit(long argc, char **argv)
{
	short y;

	shell_print(MSG_shell_exit_q);

# ifdef _SHELL_THREAD
	y = (short)c_conin();
# else
	y = (short)Cconin();
# endif

	if (tolower(y & 0x00ff) == *MSG_init_menu_yes)
# ifndef _SHELL_THREAD
		Shutdown(2);
# else
		s_hutdown(2);
# endif

	shell_print("\r\n");

	return 0;
}

/* Variants:
 *
 * file -> file
 * file file file file ... -> directory
 */
static long
sh_cp(long argc, char **argv)
{
	shell_fprintf(STDOUT, "%s not implemented yet!\r\n", argv[0]);

	return -1;
}

static long
sh_mv(long argc, char **argv)
{
	shell_fprintf(STDOUT, "%s not implemented yet!\r\n", argv[0]);

	return -1;
}

static long
sh_rm(long argc, char **argv)
{
	long x, r;
	short force = 0;

	if (argc < 2)
	{
		shell_fprintf(STDERR, "%s%s\r\n", argv[0], MSG_shell_missing_arg);

		return 0;
	}

	for (x = 1; x < argc; x++)
	{
		if (argv[x][0] == '-')
		{
			if (strcmp(argv[x], "--help") == 0)
			{
				shell_fprintf(STDOUT, MSG_shell_rm_help, argv[0]);

				return 0;
			}
			else if (strcmp(argv[x], "-f") == 0)
				force = 1;
		}
		else
			break;
	}

	while (argv[x])
	{
# ifdef _SHELL_THREAD
		r = f_delete(argv[x++]);
# else
		r = Fdelete(argv[x++]);
# endif
		if (!force && r < 0)
			return r;
	}

	return 0;
}

static long
sh_ln(long argc, char **argv)
{
	char *old, *new;

	if (argc < 3)
	{
		shell_fprintf(STDERR, "%s%s\r\n", argv[0], MSG_shell_missing_arg);

		return 0;
	}

	old = argv[1];
	new = argv[2];

# ifdef _SHELL_THREAD
	return f_symlink(old, new);
# else
	return Fsymlink(old, new);
# endif
}

static long
sh_setenv(long argc, char **argv)
{
	char *var = shell_base->p_env;

	if (argc < 2)
	{
		if (var)
		{
			while (*var)
			{
				shell_fprintf(STDOUT, "%s\r\n", var);
				while (*var)
					var++;
				var++;
			}
		}

	}
	else if (argc == 2)
	{
		if (strcmp(argv[1], "--help") == 0)
			shell_fprintf(STDOUT, MSG_shell_setenv_help, argv[0]);
		else
			shell_delenv(argv[1]);
	}
	else
		shell_setenv(argv[1], argv[2]);

	return 0;
}

static long
sh_export(long argc, char **argv)
{
	char *arg2;

	if (argc < 2)
		sh_setenv(argc, argv);
	else
	{
		if (strcmp(argv[1], "--help") == 0)
			shell_fprintf(STDOUT, MSG_shell_export_help, argv[0]);
		else
		{
			arg2 = strrchr(argv[1], '=');

			if (arg2 == NULL)
				shell_delenv(argv[1]);
			else
			{
				*arg2++ = 0;
				argv[2] = arg2;
				argv[3] = NULL;

				sh_setenv(argc + 1, argv);
			}
		}
	}

	return 0;
}

static long
sh_echo(long argc, char **argv)
{
	short x = 0;

	while (--argc)
		shell_fprintf(STDOUT, "%s ", argv[++x]);

	shell_fprintf(STDOUT, "\r\n");

	return 0;
}

/* End of the commands, begin control routines */

# define MAX_BASIC_CMD	8	/* i.e. echo, counting from exit == 1 */

typedef long FUNC();

static FUNC *cmd_routs[] =
{
	sh_exit, sh_ver, sh_cd, sh_help, sh_xcmd, sh_setenv, sh_export, sh_echo, \
	sh_ls, sh_cp, sh_mv, sh_rm, sh_ln, sh_chmod, sh_chown, sh_chgrp
};

static const char *commands[] =
{
	/* First eight commands are always active */
	"exit", "ver", "cd", "help", "xcmd", "setenv", "export", "echo", \
	/* The rest is switchable by `xcmd on' of `off' */
	"ls", "cp", "mv", "rm", "ln", "chmod", "chown", "chgrp", NULL
};

/* Execute the given command */
INLINE long
execute(char *cmdline)
{
# ifdef _SHELL_THREAD
	static char *argv[SHELL_ARGS];
# else
	char *argv[SHELL_ARGS];
# endif
	char *c;
	long cnt, cmdno, argc;

	c = cmdline;

	/* Convert possible control characters to spaces
	 * and double quotes to single quotes.
	 */
	while (*c)
	{
		if (iscntrl(*c))
			*c = ' ';
		if (*c == '"')
			*c = '\'';
		c++;
	}

	/* This destroys the cmdline string */
	argc = crunch(cmdline, argv);

	if (argc <= 0)				/* empty command line or parse error */
	{
		if (argc < 0)
			shell_fprintf(STDERR, MSG_shell_unmatched_quotes);

		return 0;
	}

	/* Result a zero if the string given is not an internal command,
	 * or the number of the internal command otherwise (the number is
	 * just their index in the commands[] table, plus one).
	 */
	cmdno = cnt = 0;

	while (commands[cnt])
	{
		if (strcmp(argv[0], commands[cnt]) == 0)
		{
			cmdno = cnt + 1;
			break;
		}
		cnt++;
	}

	/* If xcommands == 1 internal code is used for the commands
	 * below, or external programs are executed otherwise
	 */
	if ((commands[cnt] == NULL) || (!xcommands && cmdno > MAX_BASIC_CMD))
		cmdno = 0;

	return (cmdno == 0) ? execvp(argv) : cmd_routs[cnt](argc, argv);
}

/* XXX because of Cconrs() the command line cannot be longer than 254 bytes.
 */
INLINE char *
prompt(uchar *buffer, long buflen)
{
	char *lbuff;
# ifdef _SHELL_THREAD
	static char cwd[SHELL_MAXPATH];
# else
	char cwd[SHELL_MAXPATH];
# endif
	short idx;

	buffer[0] = LINELEN;
	buffer[1] = 0;

# ifdef _SHELL_THREAD
	d_getcwd(cwd, 0, sizeof(cwd));
# else
	Dgetcwd(cwd, 0, sizeof(cwd));
# endif

	if (*cwd == 0)
		strcpy(cwd, "/");
	else
		dos2unix(cwd);

	shell_fprintf(STDOUT, "mint:%s#", cwd);

# ifdef _SHELL_THREAD
	c_conrs(buffer);
# else
	Cconrs(buffer);
# endif

	/* "the string is not guaranteed to be NULL terminated"
	 * (Atari GEMDOS reference manual)
	 */
	lbuff = buffer + 2;
	idx = buffer[1];
	lbuff[--idx] = 0;

	return lbuff;
}

static EXITING
shell(void)
{
# ifdef _SHELL_THREAD
	static uchar linebuf[LINELEN + 2];
# else
	uchar linebuf[LINELEN + 2];
# endif
	uchar *lbuff;
	long r;

# ifdef _SHELL_THREAD
	(void)p_domain(1);			/* switch to MiNT domain */
	(void)f_force(2, -1);			/* redirect the stderr to console */
	(void)p_umask(SHELL_UMASK);		/* files created should be rwxr-xr-x */
# else
	long s;

	(void)Pdomain(1);			/* switch to MiNT domain */
	(void)Fforce(2, -1);			/* redirect the stderr to console */
	(void)Pumask(SHELL_UMASK);		/* files created should be rwxr-xr-x */
# endif
	shell_print("\ee\ev\r\n");		/* enable cursor, enable word wrap, make newline */
	sh_ver(0, NULL);
	xcmdstate();
	shell_print(MSG_shell_type_help);

# ifdef _SHELL_THREAD
	d_setdrv('U' - 'A');
# else
	Dsetdrv('U' - 'A');			/* force current drive to u: */
# endif

	sh_cd(1, NULL);				/* this sets $HOME as current dir */

	/* This loop restarts the shell when it dies cause of a bus error etc.
	 */
# ifndef _SHELL_THREAD
	for (;;)
	{
		s = Pvfork();

		if (s == 0)	/* Child here */
		{
# endif
			for (;;)
			{
				lbuff = prompt(linebuf, sizeof(linebuf));

				r = execute(lbuff);

				if (r < 0)
					shell_fprintf(STDERR, MSG_shell_error, lbuff, r);
			}
# ifndef _SHELL_THREAD
		}
		else		/* Parent here */
			Pwaitpid(s, 0, NULL);
	}
# endif
}

# ifndef _SHELL_THREAD
/* Notice that we cannot define local variables here due to setstack()
 * which changes the stack pointer value.
 */
static void
shell_start(BASEPAGE *bp)
{
	/* we must leave some bytes of `pad' behind the (sp)
	 * (see arch/syscall.S), this is why it is `-256L'.
	 */
	setstack((long)bp + SHELL_STACK - 256L);
	Mshrink((void *)bp, SHELL_STACK);
	shell();				/* this one doesn't return */
}
# endif

/* This below is running in kernel context, called from init.c
 */
long
startup_shell(void)
{
	long r;

# ifdef _SHELL_THREAD
	r = kthread_create((void *)shell, NULL, &p, "shell");
	if (r)
	{
		DEBUG(("can't create \"shell\" kernel thread"));

		return -1;
	}

	shell_base = _base;
	shell_base->p_env = init_env;

	return p->pid;
# else
	shell_base = (BASEPAGE *)sys_pexec(7, (char *)SHELL_FLAGS, "\0", init_env);

	/* Hope this never happens */
	if (!shell_base)
	{
		DEBUG(("Pexec(7) returned 0!\r\n"));

		return -1;
	}

	/* Start address */
	shell_base->p_tbase = (long)shell_start;

	r = sys_pexec(104, (char *)"shell", shell_base, 0L);

	m_free((long)shell_base);

	return r;
# endif
}

# endif /* BUILTIN_SHELL */

/* EOF */
