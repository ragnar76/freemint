/*
 * Filename:     main.c
 * Project:      GlueSTiK
 * 
 * Note:         Please send suggestions, patches or bug reports to me
 *               or the MiNT mailing list <mint@fishpool.com>.
 * 
 * Copying:      Copyright 1999 Frank Naumann <fnaumann@freemint.de>
 * 
 * Portions copyright 1997, 1998, 1999 Scott Bigham <dsb@cs.duke.edu>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

# include <mint/osbind.h>
# include <mint/mintbind.h>
# include <mint/basepage.h>
# include <mint/ssystem.h>
# include <mint/cookie.h>

# include <stdio.h>
# include <signal.h>
# include <unistd.h>
# include <netdb.h>
# include <stdbool.h>
# include <string.h>

# include "gs.h"

# include "gs_config.h"
# include "gs_func.h"
# include "gs_mem.h"
# include "gs_stik.h"
# include "version.h"


# define MSG_VERSION	str (VER_MAJOR) "." str (VER_MINOR)
# define MSG_BUILDDATE	__DATE__

# define MSG_BOOT	\
	"\033p GlueSTiK\277 STiK emulator for MiNTnet version " \
	MSG_VERSION " \033q\r\n"

# define MSG_GREET	\
	"Redirect Daemon\r\n" \
	"\275 1996-98 Scott Bigham.\r\n" \
	"\275 2000-2010 Frank Naumann.\r\n"

# define MSG_ALPHA	\
	"\033p WARNING: This is an unstable version - alpha! \033q\7\r\n"

# define MSG_MINT	\
	"\7This program requires FreeMiNT!\r\n"

# define MSG_ALREADY	\
	"\7There is an active STiK Cookie!\r\n"

# define MSG_FAILURE	\
	"\7Sorry, driver NOT installed - initialization failed!\r\n\r\n"

static bool opt_force_install = false;

static void
cleanup (void)
{
	cleanup_config();
	cleanup_mem();
	cleanup_stik_if();
}

/* ------------------
   | Remove cookie |
   ------------------ */
static void
uninstall_cookie (void)
{
# ifndef S_DELCOOKIE
# define S_DELCOOKIE	26
# endif

	Ssystem(S_DELCOOKIE, C_STiK, 0L);
}

/* ------------------
   | Install cookie |
   ------------------ */
static int
install_cookie (void)
{
	long dummy;
	
	if (Ssystem (-1, 0, 0) == -32)
	{
		(void) Cconws (MSG_MINT);
		return 1;
	}

	if (opt_force_install == true) {
		uninstall_cookie();
	}
	if (Ssystem (S_GETCOOKIE, C_STiK, &dummy) == 0)
	{
		(void) Cconws (MSG_ALREADY);
		return 1;
	}
	
	if (Ssystem (S_SETCOOKIE, C_STiK, (long) &stik_driver) != 0)
	{
		return 1;
	}
	
	return 0;
}


static void
nothing (long sig)
{
	;
}

static void
end (long sig)
{
	Psigreturn ();
	
	cleanup ();
	uninstall_cookie ();
	
	exit (0);
}


/* ------------------
   | Remove cookie |
   ------------------ */
static void 
parse_cmdline(void)
{
	BASEPAGE *bp = _base;
	unsigned char c;
	
	c = bp->p_cmdlin[0];
	if (c == 0 || c >= 0x7f)
		return;

  	if (!strncmp(bp->p_cmdlin + 1, "--force", 7) || !strncmp(bp->p_cmdlin + 1, "-f", 2))
		opt_force_install = true; 
}

int
main (void)
{
	long r;
	
	r = Pfork();
	if (r < 0)
	{
		/* fork failed */
		return 1;
	}
	if (r > 0)
	{
		/* parent can exit */
		return 0;
	}
	parse_cmdline();

	(void) Cconws (MSG_BOOT);
	(void) Cconws (MSG_GREET);
# ifdef ALPHA
	(void) Cconws (MSG_ALPHA);
# endif
	(void) Cconws ("\r\n");
	
	if (init_stik_if() == 0 ||
		load_config_file() == 0 ||
		init_mem() == 0 ||
		install_cookie() != 0)
	{
		(void) Cconws (MSG_FAILURE);
		
		cleanup ();
		exit (1);
	}
	
	Psignal (SIGALRM, nothing);
	Psignal (SIGTERM, end);
	Psignal (SIGQUIT, end);
	Psignal (SIGHUP,  nothing);
	Psignal (SIGTSTP, nothing);
	Psignal (SIGINT,  nothing);
	Psignal (SIGABRT, nothing);
	Psignal (SIGUSR1, nothing);
	Psignal (SIGUSR2, nothing);
	
	for (;;)
	{
		PMSG pmsg;
		
		r = Pmsg (0, GS_GETHOSTBYNAME, &pmsg);
		if (r)
		{
			/* printf ("Pmsg wait fail!\n"); */
			break;
		}
		
		switch (pmsg.msg1)
		{
			case 1:
				pmsg.msg2 = (long) gethostbyname ((char *) pmsg.msg2);
				break;
			case 2:
				pmsg.msg2 = (long) malloc (pmsg.msg2);
				break;
			case 3:
				free ((void *) pmsg.msg2);
				break;
			case 4:
				/* pmsg.msg2 = realloc ((void *) pmsg.msg2); */
				break;
			case 5:
				
				break;
		}
		
		r = Pmsg (1, 0xffff0000L | pmsg.pid, &pmsg);
		if (r)
		{
			/* printf ("Pmsg back fail!\n"); */
		}
	}
	
	cleanup ();
	uninstall_cookie ();
	
	return 0;
}
