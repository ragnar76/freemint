/*
 * $Id$
 * 
 * XaAES - XaAES Ain't the AES (c) 1992 - 1998 C.Graham
 *                                 1999 - 2003 H.Robbers
 *                                        2004 F.Naumann & O.Skancke
 *
 * A multitasking AES replacement for FreeMiNT
 *
 * This file is part of XaAES.
 *
 * XaAES is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * XaAES is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with XaAES; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "xa_appl.h"
#include "xa_global.h"

#include "app_man.h"
#include "c_window.h"
#include "desktop.h"
#include "k_main.h"
#include "k_mouse.h"
#include "k_keybd.h"
#include "messages.h"
#include "menuwidg.h"
#include "sys_proc.h"
#include "taskman.h"
#include "util.h"
#include "widgets.h"
#include "xa_graf.h"
#include "xa_types.h"
#include "xa_evnt.h"
#include "xa_rsrc.h"
#include "xa_user_things.h"
#include "version.h"
#include "mint/fcntl.h"


bool
is_client(struct xa_client *client)
{
	return client && client != C.Aes;
}

static void
pname_to_string(char *to, unsigned long tolen, const char *name)
{
	int i = 8;

	strncpy(to, name, tolen);
	to[tolen-1] = '\0';

	while (i)
	{
		if (to[i] == ' ' || to[i] == 0)
			to[i] = '\0';
		else
			break;
		i--;
	}
}

static void
get_app_options(struct xa_client *client)
{
	struct opt_list *op = S.app_options;
	char proc_name[10];

	pname_to_string(proc_name, sizeof(proc_name), client->proc_name);

	DIAGS(("get_app_options '%s'", client->proc_name));

	while (op)
	{
		if (   stricmp(proc_name,        op->name) == 0
		    || stricmp(client->name + 2, op->name) == 0)
		{
			DIAGS(("Found '%s'", op->name));
			client->options = op->options;
			break;
		}

		op = op->next;
	}
}

static void
proc_is_now_client(struct xa_client *client)
{
	/* Ozk:
	 * Since processes not yet called appl_init() can call wind_update()
	 * we need to check and set stuff here... if only programmers
	 * could follow Atari's documentation!!!!
	 */
	if (C.update_lock && C.update_lock == client->p)
		client->fmd.lock |= SCREEN_UPD;
	if (C.mouse_lock && C.mouse_lock == client->p)
		client->fmd.lock |= MOUSE_UPD;

}

/*
 * Ozk: New scheme; We keep the shel_info extension throughout the processes
 * lifetime. Reason for this is that applications may call appl_init()/appl_exit()
 * more than once. And that means we need the shel_info on the second init() too.
 * So, shel_info extension is not freed until we are absolutely sure the process
 * is about to terminate...
 */
struct xa_client *
init_client(enum locks lock)
{
	struct xa_client *client;
	struct proc *p = get_curproc();
	long f;

	/* if attach_extension succeed it returns a pointer
	 * to kmalloc'ed and *clean* memory area of the given size
	 */
	client = attach_extension(NULL, XAAES_MAGIC, sizeof(*client), &xaaes_cb_vector);
	if (!client)
	{
		ALERT(("attach_extension for %u failed, out of memory?", p->pid));
		return NULL;
	}

	bzero(client, sizeof(*client));

	client->md_head = client->mdb;
	client->md_tail = client->mdb;
	client->md_end = client->mdb + CLIENT_MD_BUFFERS;
	client->md_head->clicks = -1;

	client->ut = umalloc(xa_user_things.len);
	client->mnu_clientlistname = umalloc(strlen(mnu_clientlistname)+1);

	if (!client->ut || !client->mnu_clientlistname)
	{
		ALERT(("umalloc for %u failed, out of memory?", p->pid));

		if (client->ut)
			ufree(client->ut);

		if (client->mnu_clientlistname)
			ufree(client->mnu_clientlistname);

		detach_extension(NULL, XAAES_MAGIC);
		return NULL;
	}

	/* 
	 * add to client list
	 * add to app list
	 */
	CLIENT_LIST_INSERT_END(client);
	APP_LIST_INSERT_END(client);

	/* remember process descriptor */
	client->p = p;

	/* initialize trampoline
	 */
	bcopy(&xa_user_things, client->ut, xa_user_things.len);
	/* relocate relative addresses */
	client->ut->progdef_p  += (long)client->ut;
	client->ut->userblk_pp += (long)client->ut;
	client->ut->ret_p      += (long)client->ut;
	client->ut->parmblk_p  += (long)client->ut;
	/* make sure data cache is flushed */
	cpush(client->ut, xa_user_things.len);

	client->cmd_tail = "\0";

	/* Ozk: About the fix_menu() thing; This is just as bad as it
	 * originally was, the client should have an attachment with
	 * umalloced space for such things as this. Did it like this
	 * temporarily...
	 * When changing this, also change it in k_init.c for the AESSYS
	 */
	strcpy(client->mnu_clientlistname, mnu_clientlistname);

	strncpy(client->proc_name, client->p->name, 8);
	f = strlen(client->proc_name);
	while (f < 8)
	{
		client->proc_name[f] = ' ';
		f++;
	}
	client->proc_name[8] = '\0';
	strnupr(client->proc_name, 8);
	DIAGS(("proc_name for %d: '%s'", client->p->pid, client->proc_name));

	client->options = default_options;

	/* Individual option settings. */
	get_app_options(client);
	if (client->options.inhibit_hide)
		client->swm_newmsg |= NM_INHIBIT_HIDE;

	/* awaiting menu_register */
	sprintf(client->name, sizeof(client->name), "  %s", client->proc_name);

	/* update the taskmanager if open */
	update_tasklist(lock);

	DIAGS(("appl_init: checking shel info (pid %i)", client->p->pid));
	{
		struct shel_info *info;

		info = lookup_extension(client->p, XAAES_MAGIC_SH);
		if (info)
		{
			DIAGS(("appl_init: shel_write started"));
			DIAGS(("appl_init: type %i", info->type));
			DIAGS(("appl_init: cmd_name '%s'", info->cmd_name));
			DIAGS(("appl_init: home_path '%s'", info->home_path));

			client->type = info->type;
			/*
			 * The 'real' parent ID, which is the client that called
			 * shel_write() to start us..
			 */
			client->rppid = info->rppid;
			client->cmd_tail = info->cmd_tail;
			strcpy(client->cmd_name, info->cmd_name);
			strcpy(client->home_path, info->home_path);
		}
	}

	/* Get the client's home directory (where it was started)
	 * - we use this later to load resource files, etc
	 */
	if (!*client->home_path)
	{
		int drv = d_getdrv();
		client->home_path[0] = (char)drv + 'A';
		client->home_path[1] = ':';
		d_getcwd(client->home_path + 2, drv + 1, sizeof(client->home_path) - 3);

		DIAG((D_appl, client, "[1]Client %d home path = '%s'",
			client->p->pid, client->home_path));
	}
	else /* already filled out by launch() */
	{
		DIAG((D_appl, client, "[2]Client %d home path = '%s'",
			client->p->pid, client->home_path));
	}

	proc_is_now_client(client);
	
	app_in_front(lock, client);

	/* Reset the AES messages pending list for our new application */
	client->msg = NULL;
	/* Initially, client isn't waiting on any event types */
	cancel_evnt_multi(client, 0);
	/* Initial settings for the clients mouse cursor */
	client->mouse = ARROW;		/* Default client mouse cursor is an arrow */
	client->mouse_form = NULL;
	client->save_mouse = client->mouse;
	client->save_mouse_form = client->mouse_form;
	
	return client;
}

/*
 * Application initialise - appl_init()
 */
unsigned long
XA_appl_init(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct aes_global *globl = (struct aes_global *)pb->global;
	struct proc *p = get_curproc();
	//long f;

	CONTROL(0,1,0);

	DIAG((D_appl, client, "appl_init for %d", p->pid));

	if (client)
	{
		if (client->forced_init_client)
		{
			client->globl_ptr = globl;
			client->forced_init_client = false;
		}
	}
	else
	{
		if ((client = init_client(lock)))
		{
			/* Preserve the pointer to the globl array
			 * so we can fill in the resource address later
			 */
			client->globl_ptr = globl;
		}
	}

	if (client)
	{
		/* fill out global */
		globl->version = 0x0410;		/* Emulate AES 4.1 */
		globl->count = -1;			/* unlimited applications XXX -> mint process limit */
		globl->id = p->pid;			/* appid -> pid */
		globl->pprivate = NULL;
		globl->ptree = NULL;			/* Atari: pointer to pointerarray of trees in rsc. */
		globl->rshdr = NULL;			/* Pointer to resource header. */
		globl->lmem = 0;
		globl->nplanes = screen.planes;
		globl->res1 = 0;
		globl->res2 = 0;
		globl->c_max_h = screen.c_max_h;	/* AES 4.0 extensions */
		globl->bvhard = 4;

		pb->intout[0] = p->pid;
	}
	else
		pb->intout[0] = -1;

	return XAC_DONE;
}

static void
send_ch_exit(struct xa_client *client, short pid, int code)
{
	if (client)
	{
		send_app_message(NOLOCKS, NULL, client, AMQ_NORM, 0,
				 CH_EXIT, 0,0, pid,
				 code, 0,0,0);
	}
}

/*
 * Clean up client-indipendant stuff, since XaAES must handle
 * some things even when the process is not yet (or ever) a
 * client.
 */
void
exit_proc(enum locks lock, struct proc *p, int code)
{
	struct shel_info *info;
	struct xa_client *clnt = lookup_extension(p, XAAES_MAGIC);

	/* Unlock mouse & screen */
	if (update_locked() == p)
		free_update_lock();
	if (C.update_lock == p)
	{
		C.update_lock = NULL;
		C.updatelock_count = 0;
	}
	if (mouse_locked() == p)
		free_mouse_lock();
	if (C.mouse_lock == p)
	{
		C.mouse_lock = NULL;
		C.mouselock_count = 0;
	}
	
	
	/* NOT FINISHED WITH THESE COMMENTS, THEY ARE INCORRECT NOW */
	/* If both xaaes and shel_info extensions are attached, we rely on
	 * the facts that either;
	 *
	 * a -	if both xaaes and shel_info extension are attached, exit_proc()
	 *	is called from exit_client(), and does not necessarily mean that
	 *	the process terimnates (may be an appl_exit())
	 *
	 * b -	Only shel_info extension present - the application either crashed
	 *	before doing appl_init(), or it terminates without ever being an
	 *	AES client, or terminates normally after doing an appl_exit().
	 *	In this case we can be sure we are terminating, because exit_proc()
	 *	is only called from exit_client() or on_exit() module callback.
	 *	exit_client() will remove both extensions if it is called on real
	 *	process termination.
	 *
	 * This makes us able to find out our 'real' AES parent and send it a CH_EXIT
	 * AES message.
	 */
	if ((info = lookup_extension(p, XAAES_MAGIC_SH)) && (!clnt || (clnt && clnt->pexit)) )
	{
		struct xa_client *client = pid2client(info->rppid);
		
		send_ch_exit(client, p->pid, code);
#if GENERATE_DIAGS
		if (client)
		{
			DIAGS(("Sent CH_EXIT (premature client exit) to (pid %d)%s for (pid %d)%s",
				client->p->pid, client->name, p->pid, p->name));
		}
		else
			DIAGS(("No real parent client"));
#endif
		remove_shel_info(p);
	}
}

void
remove_shel_info(struct proc *p)
{
	struct shel_info *info = lookup_extension(p, XAAES_MAGIC_SH);

	DIAGS((" -- removing shel_info=%lx from pid %d (%s)",
		info, p->pid, p->name));

	if (info)
	{
		if (info->tail_is_heap && info->cmd_tail)
		{
			kfree(info->cmd_tail);
			info->cmd_tail = NULL;
			info->tail_is_heap = false;
		}
		/*
		 * Ozk: we trust the kernel to release the extensions.
		 * If that changes, release shel_info here.
		 */
	}
}

/*
 * Ozk: check if other clients reference us, and remove or fix it
 */
static void
remove_client_crossrefs(struct xa_client *client)
{
	struct xa_client *cl;

	FOREACH_CLIENT(cl)
	{
		if (cl->nextclient == client)
		{
			cl->nextclient = NEXT_CLIENT(client);
			break;
		}
	}
}

/*
 * close and free all client resources
 * 
 * Called on appl_exit() or on process termination (if app crashed
 * or exit without appl_exit). With other words, this is the final
 * routine *before* the process becomes invalid and should do any
 * cleanup related to this client.
 */
void
exit_client(enum locks lock, struct xa_client *client, int code, bool pexit)
{
	struct xa_client *top_owner;
	long redraws;

	DIAG((D_appl, NULL, "XA_client_exit: %s", c_owner(client)));

	S.clients_exiting++;

	/*
	 * Clear if client was exclusively waiting for mouse input
	 */
	client->status |= CS_EXITING;

	cancel_mutimeout(client);
	/*
	 * Figure out which client to make active
	 */
	top_owner = C.Aes;
	if (cfg.next_active == 1)
	{
		top_owner = APP_LIST_START;

		if (top_owner == client)
			top_owner = previous_client(lock);
	}
	else if (cfg.next_active == 0)
	{
		struct xa_window *tw = get_topwind(lock, client, window_list, true, XAWS_OPEN|XAWS_HIDDEN, XAWS_OPEN);
		if (!tw)
			tw = get_topwind(lock, client, window_list, true, XAWS_OPEN, XAWS_OPEN);
		if (tw)
			top_owner = tw->owner;
	}

	if (!C.next_menu || (C.next_menu && C.next_menu == client))
		C.next_menu = top_owner;
	
	if ((client->p == menustruct_locked()) ||
	    (TAB_LIST_START && (TAB_LIST_START)->client == client))
	{
		popout(TAB_LIST_START);
	}

	exit_proc(lock, client->p, code);

	if (S.wait_mouse == client)
	{
		S.wm_count = 0;
		S.wait_mouse = NULL;
	}
	
	/*
	 * It is no longer interested in button released packet
	 */
	if (C.button_waiter == client)
		C.button_waiter = NULL;

	/*
	 * Check and remove if client set its own mouse-form
	 */
	if (C.mouse_owner == client || C.realmouse_owner == client)
		graf_mouse(ARROW, NULL, NULL, false);

	remove_widget_active(client);

	/*
	 * Go through and check that all windows belonging to this
	 * client are closed
	 */
	remove_all_windows(lock, client);

	redraws = cancel_app_aesmsgs(client);
	cancel_cevents(client);
	cancel_keyqueue(client);

	client->rsrc = NULL;
	FreeResources(client, NULL);

	/* Free name *only if* it is malloced: */
	if (client->tail_is_heap)
	{
		kfree(client->cmd_tail);
		client->cmd_tail = NULL;
		client->tail_is_heap = false;
	}

	/* Ozk:
	 * sending CH_EXIT is done by proc_exit()
	 */
	 
	/*
	 * remove any references
	 */
	
	if (client->desktop)
	{
		struct xa_client *newdt;
		if (get_desktop() == client->desktop)
		{
			if (top_owner->desktop && !(top_owner->status & CS_EXITING))
			{
				newdt = top_owner;
			}
			else
			{
				newdt = pid2client(C.DSKpid);
				if (!(newdt && newdt != client && !(newdt->status & CS_EXITING) && newdt->desktop))
					newdt = NULL;
			}
			if (!newdt)
				newdt = C.Aes;
			
			set_desktop(newdt->desktop);
		}
		client->desktop = NULL;
	}

	app_in_front(lock, top_owner);

	/*
	 * remove from client list
	 * remove from app list
	 */
	CLIENT_LIST_REMOVE(client);
	APP_LIST_REMOVE(client);
		
	if (redraws)
	{
		C.redraws -= redraws - 1;
		kick_mousemove_timeout();
	}
	
	/* if taskmanager is open the tasklist will be updated */
	update_tasklist(lock);

	/*
	 * free remaining resources
	 */

	free_attachments(client);
	free_xa_data_list(&client->xa_data);
	/*
	 * Free wt list last
	 */
	free_wtlist(client);

	/* free the quart screen buffer */
	if (client->half_screen_buffer)
		ufree(client->half_screen_buffer);

	if (client->ut)
		ufree(client->ut);

	if (client->mnu_clientlistname)
		ufree(client->mnu_clientlistname);


	client->cmd_tail = "\0";

	/* Ozk:
	 * If we are called to really terminate the process, we detach
	 * AES extensions here and now. If not, we leave detaching to
	 * our caller(s)...
	 */
	if (pexit)
	{
		struct proc *p = client->p;
		
		
		DIAG((D_appl, NULL, "Process terminating - detaching extensions..."));
		client->pexit = true;
		/* Ozk:
		 * We trust the kernel to detach the client extension.
		 * If that changes, detach client here (means extensions must be detachable
		 * by its callbacks).
		 */
		remove_shel_info(p);
	}
	
	/* zero out; just to be sure */
	bzero(client, sizeof(*client));

	remove_client_crossrefs(client);
	
	S.clients_exiting--;

	DIAG((D_appl, NULL, "client exit done"));
}

/*
 * Application Exit
 */
unsigned long
XA_appl_exit(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(0,1,0)

	DIAG((D_appl, client, "appl_exit for %d", client->p->pid));

	/* Which process are we? It'll be a client pid */
	pb->intout[0] = client->p->pid;

	/* XXX ??? */
	/*
	 * Ozk: Weirdest thing; imgc4cd and ic4plus calls appl_exit() directly
	 * after it calls appl_init() and ignoring the appl_exit() make'em work!
	 */
	if (	strnicmp(client->proc_name, "wdialog", 7) == 0 ||
		strnicmp(client->proc_name, "imgc4cd", 7) == 0 ||
		strnicmp(client->proc_name, "ic4plus", 7) == 0
	   )
	{
		DIAG((D_appl, client, "appl_exit ignored for %s", client->name));
		return XAC_DONE;
	}

	/* we assume correct termination */
	exit_client(lock, client, 0, false);

	/* and decouple from process context */
	detach_extension(NULL, XAAES_MAGIC);

	return XAC_DONE;
}

/*
 * Free timeslice.
 */
unsigned long
XA_appl_yield(enum locks lock, struct xa_client *client, AESPB *pb)
{
	CONTROL(0,1,0)

	yield();

	pb->intout[0] = 1; /* OK */
	
	return XAC_DONE;
}

/*
 * AES 4.0 compatible appl_search
 */
unsigned long
XA_appl_search(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct xa_client *next = NULL;
	bool lang = false;
	bool spec = false;
	char *fname = (char *)pb->addrin[0];
	short *o = pb->intout;
	short cpid = pb->intin[0];

	CONTROL(1,3,1)

	o[0] = 1;
	o[1] = 0;

	DIAG((D_appl, client, "appl_search for %s cpid=0x%x", c_owner(client), cpid));

	if (cpid < 0)
	{
		cpid = -cpid;
		if (cpid < 1 || cpid > MAXPID)
			cpid = 1;

		next = pid2client(cpid);
		lang = true;
	}
	else if (cpid == APP_DESK)
	{
		/* N.AES: short name of desktop program */
		next = pid2client(C.DSKpid);
	}
	else
	{
		/* XaAES extension. */
		spec = (cpid & APP_TASKINFO) != 0;
		cpid &= ~APP_TASKINFO;

		if (spec)
			lang = true;

		Sema_Up(clients);

		if (cpid == APP_FIRST)
		{
			/* simply the first */
			next = CLIENT_LIST_START;
			client->nextclient = NEXT_CLIENT(next);
		}
		else if (cpid == APP_NEXT)
		{
			next = client->nextclient;
			if (next)
			{
				client->nextclient = NEXT_CLIENT(next);
			}
		}
		Sema_Dn(clients);
	}

	if (!next)
	{
		/* No more clients or no active clients */
		o[0] = 0;
	}
	else
	{
		cpid = next->p->pid;

		/* replies according to the compendium */		
		if (cpid == C.AESpid)
			o[1] = APP_SYSTEM;
		else if (cpid == C.DSKpid)
			o[1] = APP_APPLICATION | APP_SHELL;
		else
			o[1] = next->type;

		/* XaAES extensions. */
		if (spec)
		{
			if (any_hidden(lock, next, NULL))
				o[1] |= APP_HIDDEN;
			if (focus_owner() == next)
				o[1] |= APP_FOCUS;

			DIAG((D_appl, client, "   --   o[1] --> 0x%x", o[1]));
		}

		o[2] = cpid;

		if (lang)
		{
			strcpy(fname, next->name);
			DIAGS((" -- nicename '%s'", fname));
		}
		else
		{
			strncpy(fname, next->proc_name, 8);
			fname[8] = '\0';
			DIAGS((" -- procname '%s'", fname));
		}
	}
	return XAC_DONE;
}

static void
handle_XaAES_msgs(enum locks lock, union msg_buf *msg)
{
	union msg_buf m = *msg;
	int mt = m.s.msg;

	DIAGS(("Message to AES %d", mt));
	m.s.msg = 0;

	switch (mt)
	{
		case XA_M_DESK:
		{
			DIAGS(("Desk %d, '%s'", m.s.m3, m.s.p2 ? m.s.p2 : "~~~"));
			if (m.s.p2 && m.s.m3)
			{
				strcpy(C.desk, m.s.p2);
				C.DSKpid = m.s.m3;
				m.s.msg = XA_M_OK;
			}
		}
		break;
	}

	{
		struct xa_client *dest_clnt;

		dest_clnt = pid2client(m.m[1]);
		if (dest_clnt)
			send_a_message(lock, dest_clnt, AMQ_NORM, QMF_CHKDUP, &m);
	}
}

/*
 * XaAES's current appl_write() only works for standard 16 byte messages
 */
unsigned long
XA_appl_write(enum locks lock, struct xa_client *client, AESPB *pb)
{
	int dest_id = pb->intin[0];
	int len = pb->intin[1];
	int rep = 1;
	union msg_buf *m = (union msg_buf *)pb->addrin[0];

	CONTROL(2,1,1)

	DIAGS(("appl_write: %d --> %d, len=%d msg = (%s) "
		"%04x,%04x,%04x,%04x,%04x,%04x,%04x,%04x\n",
		client->p->pid, dest_id, len,
		pmsg(m->m[0]),
		m->m[0], m->m[1], m->m[2], m->m[3], m->m[4], m->m[5], m->m[6], m->m[7]));

	if (len < 0)
		rep = 0;

	if (dest_id == C.AESpid)
	{
		handle_XaAES_msgs(lock, m);
	}
	else
	{
		short i = 50;
		struct xa_client *dest_clnt = NULL;

		/*
		 * Ozk: Problem; 1. aMail starts asock to do network stuff.
		 *               2. aMail sends asock an AES message.
		 *               3. Since asock didnt get the chance to do appl_init() yet,
		 *                  this message ends up in the great big void.
		 *     Solution; If pid2client returns NULL, we yield() and try again upto 50
		 *               times before giving up. Anyone else got better ideas? 
		 */ 
		dest_clnt = pid2client(dest_id);

		while (i && !dest_clnt)
		{
			yield();
			dest_clnt = pid2client(dest_id);
			i--;
		}

		if (dest_clnt)
		{
			short amq = AMQ_NORM;
			short qmf = QMF_NORM;

			if (m->m[0] == WM_REDRAW)
			{
				struct xa_window *wind = get_wind_by_handle(lock, m->m[3]);
				if (wind)
				{
					generate_redraws(lock, wind, (RECT *)(m->m + 4), RDRW_WA);
					m = NULL;
				}
				else
				{
					amq = AMQ_REDRAW;
					qmf = QMF_CHKDUP;
				}
			}
			if (m)
				send_a_message(lock, dest_clnt, amq, qmf, m);
			
			if (dest_clnt != client)
				yield();
		}
	}
	pb->intout[0] = rep;
	return XAC_DONE;
}

/*
 * Data table for appl_getinfo
 */
static short info_tab[][4] =
{
	/* 0 large font */
	{
		0,
		0,
		0,
		0
	},
	/* 1 small font */
	{
		0,
		0,
		0,
		0
	},
	/* 2 colours */
	{
		1,		/* Getrez() */
		16,		/* no of colours */
		1,		/* colour icons */
		1		/* extended rsrc file format */
	},
	/* 3 language (english) */
	{
		0,
		0,
		0,
		0
	},
	/* 4 processes */
	{
		1,		/* preemptive */
		1,		/* convert mint id <--> aes id */
		1,		/* appl_search */
		1		/* rsrc_rcfix */
	},
	/* 5 PC_GEM (none!) */
	{
		0,		/* objc_xfind */
		0,
		0,		/* menu_click */
		0		/* shel_rdef/wdef */
	},
	/* 6 extended inquiry */
	{
		0,		/* -1 not a valid app id for appl_read */
		0,		/* -1 not a valid length parameter to shel_get */
		1,		/* -1 is a valid mode parameter to menu_bar */
		1		/* MENU_INSTL is not a valid mode parameter to menu_bar */
	},
	/* 7 MagiC specific */
	{
		/* bit: 0 wdlg_xx(), 1 lbox_xx(), 2 fnts_xx(), 3 fslx_xx(), 4 pdlg_xx() */
		(WDIALOG_WDLG ? 0x01 : 0) |
		(WDIALOG_LBOX ? 0x02 : 0) |
		(WDIALOG_FNTS ? 0x04 : 0) |
		(WDIALOG_FSLX ? 0x08 : 0) |
		(WDIALOG_PDLG ? 0x10 : 0),
		0,
		0,
		0
	},
	/* 8 mouse support */
	{
		1,		/* modes 258 - 260 applicable */
		1,		/* mouse form maintained per application */
		1,		/* mouse wheels support */
		0
	},
	/* 9 menu support */
	{
		1,		/* sub menus */
		1,		/* popup menus */
		1,		/* scrollable menus */
		1		/* MN_SELECTED provides object tree information */
	},
	/*10 AES shell support */
	{
		0x3f07,		/* supported extended bits + highest mode */
		0,		/* 0 launch mode */
		0,		/* 1 launch mode */
		1		/* ARGV style via wiscr to shel_write supported */
	},
	/*11 window functions
	 * 
	 * WF_COLOR and WF_DCOLOR are not completely supported. Especially not changing them.
	 * So the bits are off, although wind_get() will supply default values.
	 * 
	 * These values are'nt bits, so I dont think this is correct
	 * WF_TOP + WF_NEWDESK + WF_OWNER + WF_BEVENT + WF_BOTTOM + WF_ICONIFY + WF_UNICONIFY
	 * bit 9 WF_WHEEL */
#define AGI_WF_TOP		0x0001
#define AGI_WF_NEWDESK		0x0002
#define AGI_WF_COLOR		0x0004
#define AGI_WF_DCOLOR		0x0008
#define AGI_WF_OWNER		0x0010
#define AGI_WF_BEVENT		0x0020
#define AGI_WF_BOTTOM		0x0040
#define AGI_WF_ICONIFY		0x0080
#define AGI_WF_UNICONIFY	0x0100
#define AGI_WF_WHEEL		0x0200	/* wind_set(handle, WF_WHEEL, mode, whl_mode, 0,0) available */
#define AGI_WF_FIRSTAREAXYWH	0x0400	/* wind_get(handle, WF_FIRSTAREAXYWH, clip_x, clip_y, clip_w, clip_h) available */
#define AGI_WF_OPTS		0x0800	/* wind_set(handle, WF_OPTS, wopt0, wopt1, wopt2) available */
#define AGI_WF_MENU		0x1000	/* wind_set(handle, WF_MENU) exists */

#define AGI_WF_WIDGETS		0x0001

#define AGI_ICONIFIER		0x0001
#define AGI_BACKDROP		0x0002
#define AGI_BDSHIFTCLICK	0x0004
#define AGI_HOTCLOSER		0x0008

#define AGI_WINDUPD_CAS		0x0001

	{
		0
	|	AGI_WF_TOP
	|	AGI_WF_NEWDESK
	//|	AGI_WF_COLOR
	//|	AGI_WF_DCOLOR
	|	AGI_WF_OWNER
	|	AGI_WF_BEVENT
	|	AGI_WF_BOTTOM
	|	AGI_WF_ICONIFY
	|	AGI_WF_UNICONIFY
	|	AGI_WF_WHEEL		/*01763, see above */
	|	AGI_WF_FIRSTAREAXYWH
	|	AGI_WF_OPTS
	|	AGI_WF_MENU
		,
		0
		,
		0
	|	AGI_ICONIFIER
	//|	AGI_BACKDROP
	|	AGI_BDSHIFTCLICK
	//|	AGI_HOTCLOSER		/*5,	window behaviours iconifier & click for bottoming */
		,
		0
	|	AGI_WINDUPD_CAS		/*1	wind_update(): check and set available (mode + 0x100) */
	},

#define AGI_WM_NEWTOP		0x0001
#define AGI_WM_UNTOPPED		0x0002
#define AGI_WM_ONTOP		0x0004
#define AGI_AP_TERM		0x0008
#define AGI_CHRES		0x0010
#define AGI_CH_EXIT		0x0020
#define AGI_WM_BOTTOMED		0x0040
#define AGI_WM_ICONIFY		0x0080
#define AGI_WM_UNICONIFY	0x0100
#define AGI_WM_ALLICONIFY	0x0200
#define AGI_WM_REPOSED		0x0400

	/*12 messages
	 * WM_UNTOPPED + WM_ONTOP + AP_TERM + CH_EXIT (HR) + WM_BOTTOMED +
	 * WM_ICONIFY + WM_UNICONIFY
	 */
	{
		0
	//|	AGI_WM_NEWTOP
	|	AGI_WM_UNTOPPED
	|	AGI_WM_ONTOP
	|	AGI_AP_TERM
	//|	AGI_CHRES
	|	AGI_CH_EXIT
	|	AGI_WM_BOTTOMED
	|	AGI_WM_ICONIFY
	|	AGI_WM_UNICONIFY
	|	AGI_WM_ALLICONIFY	/*0756,	see above */ /* XXX is this correct? 0756 is octal */
	|	AGI_WM_REPOSED
		,
		0
		,
		1
		,		/* WM_ICONIFY gives coordinates */
		0
	},
	/*13 objects */
	{
		1,		/* 3D objects */
		1,		/* objc_sysvar */
		0,		/* GDOS fonts */
		014		/* extended objects (0x8 G_SHORTCUT, 0x4 WHITEBAK objects)
				                     0x2 G_POPUP,    0x1 G_SWBUTTON */
	},
	/*14 form support (form_xdo, form_xdial) */
	{
		0,		/* MagiC flydials */
		0,		/* MagiC keyboard tables */
		0,		/* return last cursor position */
		0
	},
	/*15 <-- 64 */
	{
		0,		/* shel_write and AP_AESTERM */
		0,		/* shel_write and SHW_SHUTDOWN/SHW_RESCHANGE */
		3,		/* appl_search with long names and additive mode APP_TASKINFO available. */
		1		/* form_error and all GEMDOS errorcodes */
	},
	/*16 <-- 65 */
	{
		1,		/* appl_control exists. */
		APC_INFO,	/* highest opcode for appl_control */
		0,		/* shel_help exists. */
		0		/* wind_draw exists. */
	},
	/*17 <-- 22360 Winx */
	{
		0x0008,			/* WF_SHADE is supportd */
		(0x0002|0x0004),	/* WM_SHADED/WM_UNSHADED supported */
		0,
		0
	},

	/* 18 <-- 96 AES_VERSION */
	/* AES version information */
	{
		VER_MAJOR,	/* Major */
		VER_MINOR,	/* Minor */
		DEV_STATUS,	/* Status */ 
		ARCH_TARGET	/* Target */
	},
};

#define XA_VERSION_INFO	18

/*
 * appl_getinfo() handler
 *
 * Ozk: appl_getinfo() may be called by processes not yet called
 * appl_init(). So, it must not depend on client being valid!
 */
#ifndef AGI_WINX
#define AGI_WINX WF_WINX
#endif

static char *
mcs(char *d, char *s)
{
	while ((*d++ = *s++))
		;

	return d - 1;
}

void
init_apgi_infotab(void)
{
	char *s = info_string;
	
	info_tab[0][0] = screen.standard_font_height;
 	info_tab[0][1] = screen.standard_font_id;
	info_tab[1][0] = screen.small_font_height;
	info_tab[1][1] = screen.small_font_id;
	
	info_tab[2][0] = xbios_getrez();
	info_tab[2][1] = 256;
	info_tab[2][2] = 1;
	info_tab[2][3] = 1; // + 2;

	/*
	 * Build status string
	 */
	s = mcs(s, version);
	*s++ = 0x7c;

	if (DEV_STATUS & AES_FDEVSTATUS_STABLE)
		s = mcs(s, "Stable ");
	else
		s = mcs(s, "Unstable ");
		
	s = mcs(s, ASCII_DEV_STATUS);
	*s++ = 0x7c;
	s = mcs(s, ASCII_ARCH_TARGET);
	*s++ = 0x7c;
	s = mcs(s, BDATE);*s++ = 0x20;s = mcs(s, BTIME);
	*s++ = 0x7c;
	s = mcs(s, BCOMPILER);
	*s++ = 0;

	DIAGS(("Build status-string '%s'", info_string));
}

/*
 * Ozk: XA_appl_getinfo() may be called by processes not yet called
 * appl_init(). So, it must not depend on client being valid!
 */
unsigned long
XA_appl_getinfo(enum locks lock, struct xa_client *client, AESPB *pb)
{
	unsigned short gi_type = pb->intin[0];
	int i, n_intout = 5;

	CONTROL(1,5,0)

#if GENERATE_DIAGS
	/* Extremely curious to who's asking what. */
	if (client)
		DIAG((D_appl, client, "appl_getinfo %d for %s", gi_type, c_owner(client)));
	else
		DIAG((D_appl, client, "appl_getinfo %d for non AES process (pid %ld)", gi_type, p_getpid()));
#endif	
	if (gi_type > 14)
	{
		switch (gi_type)
		{
			case 64:
			case 65:
			{
				gi_type -= (64 - 15);
				break;
			}
			case AGI_WINX:
			{
				gi_type = 17;
				break;
			}
			case AES_VERSION:
			{
				if (pb->control[N_ADDRIN] >= 3)
				{
					char *d;
				
					if ((d = (char *)pb->addrin[0]))
					{
						for (i = 0; i < 8; i++)
							*d++ = aes_id[i];
					}
					if ((d = (char *)pb->addrin[1]))
						strcpy(d, long_name);
					if ((d = (char *)pb->addrin[2]))
						strcpy(d, info_string);
				}
				n_intout = pb->control[N_INTOUT];
				gi_type = XA_VERSION_INFO;
				break;
			}
			default:
			{
				gi_type = -1;
				break;
			}
		}
	}

	if (gi_type != -1)
	{
		for (i = 1; i < n_intout; i++)
			pb->intout[i] = info_tab[gi_type][i-1];
		
		gi_type = 1;
	}
	else
		gi_type = 0;

	pb->intout[0] = gi_type;

	return XAC_DONE;
}

/*
 * appl_find()
 *
 * Ozk: appl_find() may be called by processes not yet called
 * appl_init(). So, it must not depend on client being valid!
 */
unsigned long
XA_appl_find(enum locks lock, struct xa_client *client, AESPB *pb)
{
	const char *name = (const char *)pb->addrin[0];

	CONTROL(0,1,1)

#if GENERATE_DIAGS
	if (client)
		DIAG((D_appl, client, "appl_find for %s", c_owner(client)));
	else
		DIAG((D_appl, NULL, "appl_find for non AES process (pid %ld)", c_owner(client), p_getpid()));
#endif

	/* default to error */
	pb->intout[0] = -1;

	if (name == NULL)
	{
		/* pid of current process */
		if (client)
		{
			pb->intout[0] = client->p->pid;
			DIAG((D_appl, client, "   Mode NULL"));
		}
		else
			pb->intout[0] = p_getpid();
	}
	else
	{
		short lo = (short)((unsigned long)name & 0xffff);
		short hi = (short)((unsigned long)name >> 16);

		switch (hi)
		{
		/* convert mint id -> aes id */
		case -1:
		/* convert aes id -> mint id */
		case -2:
		{
			struct xa_client *cl;

			DIAG((D_appl, client, "   Mode 0xfff%c, convert %i", hi == -1 ? 'f' : 'e', lo));

			Sema_Up(clients);

			FOREACH_CLIENT(cl)
			{
				if (cl->p->pid == lo)
				{
					pb->intout[0] = lo;
					break;
				}
			}

			Sema_Dn(clients);

			break;
		}
		/* pid of topped application */
		case -3:
		{
			struct xa_client *cl;

			DIAG((D_appl, client, "   Mode 0xfffd"));

			cl = focus_owner();
			if (cl)
				pb->intout[0] = cl->p->pid;
			else
				pb->intout[0] = -1;

			break;
		}
		/* search in client list */
		default:
		{
			if (strcmp(name, "?AGI") == 0)
			{
				/* Tell application we understand appl_getinfo()
				 * (Invented by Martin Osieka for his AES extension WINX;
				 * used by MagiC 4, too.)
				 */

				pb->intout[0] = 0; /* OK */
				DIAG((D_appl, client, "   Mode ?AGI"));
			}
			else
			{
				struct xa_client *cl;

				DIAG((D_appl, client, "   Mode search for '%s'", name));

				Sema_Up(clients);

				FOREACH_CLIENT(cl)
				{
					if (strnicmp(cl->proc_name, name, 8) == 0)
					{
						pb->intout[0] = cl->p->pid;
						break;
					}
				}

				Sema_Dn(clients);
			}
			break;
		}
		}
	}

	DIAG((D_appl, client, "   --> %d", pb->intout[0]));
	return XAC_DONE;
}

/*
 * Extended XaAES calls
 */
unsigned long
XA_appl_control(enum locks lock, struct xa_client *client, AESPB *pb)
{
	struct xa_client *cl = NULL;
	short pid = pb->intin[0];
	short f = pb->intin[1], ret;

	CONTROL(2,1,1)

	DIAG((D_appl, client, "appl_control for %s", c_owner(client)));

	if (pid == -1)
	{
		if (!(cl = find_focus(true, NULL, NULL, NULL)))
			cl = get_app_infront();
	}
	else
		cl = pid2client(pid);

	DIAG((D_appl, client, "  --    on %s, func %d, 0x%lx",
		c_owner(cl), f, pb->addrin[0]));

	/*
	 * note: When extending this switch be sure to update
                *       the appl_getinfo(65) mode corresponding structure
	 *  tip: grep APC_ * 
	 */

	ret = 1;
	switch (f)
	{
		case APC_SYSTEM:
		{
			if (cl)
			{
				cl->type = APP_SYSTEM;
			}
			else
				ret = 0;
			break;
		}
		case APC_HIDE:
		{
			if (cl && !(cl->swm_newmsg & NM_INHIBIT_HIDE))
			{
				hide_app(lock, cl);
			}
			else
				ret = 0;
			break;
		}
		case APC_SHOW:
		{
			if (pid == -1)
				unhide_all(lock, cl);
			else
				unhide_app(lock, cl);
			break;
		}
		case APC_TOP:
		{
			if (cl)
			{
				if (any_hidden(lock, cl, NULL))
					unhide_app(lock, cl);
				else
					app_in_front(lock, cl);

				if (cl->type & APP_ACCESSORY)
					send_app_message(lock, NULL, cl, AMQ_NORM, QMF_CHKDUP,
							 AC_OPEN, 0, 0, 0,
							 cl->p->pid, 0, 0, 0);
			}
			else
				ret = 0;
			break;
		}
		case APC_HIDENOT:
		{
			if (cl)
			{
				if (any_hidden(lock, cl, NULL))
					unhide_app(lock, cl);
				hide_other(lock, cl);
			}
			else
				ret = 0;
			break;
		}
		case APC_INFO:
		{
			if (cl)
			{
				short *ii = (short*)pb->addrin[0];

				if (ii)
				{
					*ii = 0;
					if (any_hidden(lock, cl, NULL))
						*ii |= APCI_HIDDEN;
					if (cl->std_menu)
						*ii |= APCI_HASMBAR;
					if (cl->desktop)
						*ii |= APCI_HASDESK;
				}
			}
			else
				ret = 0;
			break;
		}
		case APC_MENU:
		{
			OBJECT **m = (OBJECT **)pb->addrin[0];
			
			if (cl && m)
				*m = cl->std_menu ? cl->std_menu->tree : NULL;
			else if (m)
				*m = NULL;
			break;
		}
		default:
		{
			ret = 0;
		}
	}
	pb->intout[0] = ret;
	return XAC_DONE;
}

#if 0
unsigned long
XA_appl_trecord(enum locks lock, struct xa_client *client, AESPB *pb)
{
	EVNTREC *ap_tbuffer = (void *)pb->addrin[0];
	short ap_trcount = pb->intin[0];

	CONTROL(1,1,1)

	DIAG((D_appl, client, "appl_trecord for %s", c_owner(client)));

	/* error */
	pb->intout[0] = 0;

	return XAC_DONE;
}

unsigned long
XA_appl_tplay(enum locks lock, struct xa_client *client, AESPB *pb)
{
	EVNTREC *ap_tpmem = (void *)pb->addrin[0];
	short ap_tpnum = pb->intin[0];
	short ap_tpscale = pb->intin[1];

	CONTROL(2,1,1)

	DIAG((D_appl, client, "appl_tplay for %s", c_owner(client)));

	/* error */
	pb->intout[0] = 0;

	return XAC_DONE;
}
#endif
