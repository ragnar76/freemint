/*
 * This file has been modified as part of the FreeMiNT project. See
 * the file Changes.MH for details and dates.
 */

/*
 * Copyright 1990,1991,1992 Eric R. Smith.
 * Copyright 1992,1993,1994 Atari Corporation.
 * All rights reserved.
 */

/* signal.c: signal handling routines */

# include "signal.h"
# include "global.h"

# include "libkern/libkern.h"
# include "mint/asm.h"
# include "mint/basepage.h"
# include "mint/signal.h"

# include "arch/context.h"	/* save_context, restore_context */
# include "arch/kernel.h"
# include "arch/mprot.h"
# include "arch/syscall.h"

# include "dosmem.h"
# include "proc.h"
# include "update.h"
# include "util.h"


void (*sig_routine)();	/* used in intr.s */
short sig_exc;		/* used in intr.s */


/* send_sig: Send signal SIG to process P. If PRIV is non-zero then
 * the signal is generated by the kernel and no permissions should
 * be checked.
 * 
 * It is the callers responsibility to call check_sigs() if it is
 * possible that the target process and the sending process are
 * the same.
 * 
 * All references to the below function post_sig() should vanish
 * in order to perform permission checks only here.  The rest of
 * post_sig() can then move in here.
 */
static long
send_sig (PROC* p, ushort sig, int priv)
{
	if (curproc->euid == 0)
	{
		priv = 1;
	}
	else if (curproc->euid == p->suid
		|| curproc->euid == p->ruid
		|| curproc->ruid == p->suid
		|| curproc->ruid == p->ruid)
	{
		priv = 1;
	}
	
	if (!priv && (sig == SIGCONT))
	{
		/* Every process has a permanent ticket to send SIGCONT
		 * to other members of the same process group.
		 */
		if (curproc->pgrp == p->pgrp)
			priv = 1;
	}
	
	if (!priv)
		return EACCES;	/* The library should set errno to EPERM!  */
	
	if (sig == 0)
		return 0;	/* Ignore */
	
	if (p->sighandle[sig] == SIG_IGN && !p->ptracer && sig != SIGCONT)
		return 0;
	
	/* R. I. P. */
	if (p->wait_q == ZOMBIE_Q || p->wait_q == TSR_Q)
		return 0;
	
	/* Init cannot be killed or stopped.  */
	if (p->pid == 1 && (sig == SIGKILL || sig == SIGSTOP))
	{
		if (_mint_strcmp ("AESSYS", p->name) != 0)
			/* Ignore! */
			return 0;
	}
	
	/* Also avoid killing our update daemon.  What about "sld" started
	 * by the socket device driver?
	 */
	if (p->ppid == 0
		&& (sig == SIGKILL || sig == SIGSTOP)
		&& _mint_strcmp ("update", p->name) == 0)
	{
		/* Ignore! */
		return 0;
	}
	
	/* Mark the signal as being pending */	
	post_sig (p, sig);
	
	return 0;
}

/*
 * killgroup(pgrp, sig, priv): send a signal to all members of a process group
 * returns 0 on success, or an error code on failure
 * priv is non-zero if the signal is generated by the kernel, otherwise
 * access privileges are checked
 * 
 * Changed by Guido: Don't check for real user id, the effective user
 * id is the one we should obey.
 */

long _cdecl
killgroup (int pgrp, ushort sig, int priv)
{
	PROC *p;
	int found = 0;
	long ret = ENOENT;
	
	if (pgrp <= 0)
		return EINTERNAL;
	
	if (sig >= NSIG)
		return EINVAL;
	
	for (p = proclist; p; p = p->gl_next)
	{
		if (p->wait_q == ZOMBIE_Q || p->wait_q == TSR_Q)
			continue;
		
		if (p->pgrp == pgrp)
		{
			long last_error;
			
			last_error = send_sig (p, sig, priv);
			if (last_error)
				ret = last_error;
			else
				found = 1;
		}
	}
	
	if (found)
		return E_OK;
	
	return ret;
}

/*
 * post_sig: post a signal as being pending. It is assumed that the
 * caller has already verified that "sig" is a valid signal, and
 * moreover it is the caller's responsibility to call check_sigs()
 * if it's possible that p == curproc
 */

void
post_sig (PROC *p, ushort sig)
{
	ulong sigm;
	
	/* just to be sure */
	assert (sig < NSIG);
	
	/* if process is ignoring this signal, do nothing
	 * also: signal 0 is SIGNULL, and should never be delivered through
	 * the normal channels (indeed, it's filtered out in dossig.c,
	 * but the extra sanity check here is harmless). The kernel uses
	 * signal 0 internally for some purposes, but it is handled
	 * specially (see supexec() in xbios.c, for example).
	 */
	
	/* If the process is traced, the tracer should always be notified */
	if (sig == 0
		|| (p->sighandle[sig] == SIG_IGN
			&& !p->ptracer
			&& sig != SIGCONT))
	{
		return;
	}
	
	/* if the process is already dead, do nothing */
	if (p->wait_q == ZOMBIE_Q || p->wait_q == TSR_Q)
		return;
	
	/* Init cannot be killed or stopped.  */
	if (p->pid == 1 && (sig == SIGKILL || sig == SIGSTOP))
	{
		/* Ignore! */
		return;
	}
	
	/* Also avoid killing our update daemon.  What about "sld" started
	 * by the socket device driver?
	 */
	/* Hmm... there can be also other processes which should not
	 * be killed off, e.g. every SLB. So, a more general solution
	 * would be necessary perhaps, i.e. a flag in the proc struct?
	 */
# ifdef SYSUPDATE_DAEMON
	if (p->pid == update_pid && (sig == SIGKILL || sig == SIGSTOP))
	{
		/* Ignore! */
		return;
	}
# endif
	
	/* mark the signal as pending */
	sigm = (1L << (ulong) sig);
	p->sigpending |= sigm;
	
	/* if the signal is masked, do nothing further
	 * 
	 * note: some signals can't be masked, and we handle those elsewhere so
	 * that p->sigmask is always valid. SIGCONT is among the unmaskable
	 * signals
	 */
	if ((p->sigmask & sigm) != 0)
		return;
	
	/* otherwise, make sure the process is awake */
	{
		ushort sr = spl7 ();
		if (p->wait_q && p->wait_q != READY_Q)
		{
			rm_q (p->wait_q, p);
			add_q (READY_Q, p);
		}
		spl (sr);
	}
}

/*
 * special version of kill that can be called from an interrupt
 * handler or device driver
 * it also accepts negative numbers to send signals to groups
 */

long _cdecl
ikill (int pid, ushort sig)
{
	PROC *p;
	long r;
	
	if (sig >= NSIG)
		return EBADARG;
	
	if (pid < 0)
		r = killgroup (-pid, sig, 1);
	else if (pid == 0)
		r = killgroup (curproc->pgrp, sig, 1);
	else
	{
		p = pid2proc (pid);
		if (p == 0 || p->wait_q == ZOMBIE_Q || p->wait_q == TSR_Q)
			return ENOENT;
		
		/* if the user sends signal 0, don't deliver it -- for users,
		 * signal 0 is a null signal used to test the existence of a
		 * process
		 */
		if (sig != 0)
			post_sig (p, sig);
		
		r = E_OK;
	}
	
	return r;
}

/*
 * check_sigs: see if we have any signals pending. if so,
 * handle them.
 */

void
check_sigs (void)
{
	ulong sigs, sigm;
	int i;
	short deliversig;
	
	if (curproc->pid == 0)
		return;
top:
	sigs = curproc->sigpending;
	
	/* Always notify the tracer about signals sent */
	if (!curproc->ptracer || curproc->sigpending & 1L)
		sigs &= ~(curproc->sigmask);
	
	if (sigs)
	{
		sigm = 2;
		
		/* with tracing we need a mechanism to allow a signal to be
		 * delivered to the child (curproc); Fcntl(...TRACEGO...)
		 * passes a SIGNULL to indicate that we should really deliver
		 * the signal, hence its always safe to remove it from pending.
		 */
		deliversig = (curproc->sigpending & 1L);
		curproc->sigpending &= ~1L;
		
		for (i = 1; i < NSIG; i++)
		{
			if (sigs & sigm)
			{
				curproc->sigpending &= ~sigm;
				if (curproc->ptracer && !deliversig
					&& (i != SIGCONT) && (i != SIGKILL))
				{
					TRACE (("tracer being notified of signal %d", i));
					stop (i);
					
					/* the parent may reset our pending
					 * signals, so check again
					 */
					goto top;
				}
				else
				{
					ulong omask;
					
					omask = curproc->sigmask;
					
					/* sigextra gives which extra signals
					 * should also be masked
					 */
					curproc->sigmask |= curproc->sigextra[i] | sigm;
					handle_sig(i);
					
/*
 * POSIX.1-3.3.4.2(723) "If and when the user's signal handler returns
 * normally, the original signal mask is restored."
 *
 * BUG?: This unmasking could unmask a pending signal which we will not
 * see this time around (if the signal number is less than i) and which
 * was not pending when we started; should we detect this condition and
 * loop around for a second try? POSIX only guarantees delivery of
 * one signal per kernel entry, so this shouldn't really be a problem.
 */
					/* unmask signals */
					curproc->sigmask = omask;
				}
			}
			
			sigm = sigm << 1;
		}
	}
}

/*
 * raise: cause a signal to be raised in the current process
 */

void
raise (ushort sig)
{
	post_sig (curproc, sig);
	check_sigs ();
}

# ifdef EXCEPTION_SIGS
/* exception numbers corresponding to signals
 */
char excep_num [NSIG] =
{
	0,
	0,
	0,
	0,
	4,		/* SIGILL  == illegal instruction */
	9,		/* SIGTRAP == trace trap */
	4,		/* pretend SIGABRT is also illegal instruction */
	8,		/* SIGPRIV == privileged instruction exception */
	5,		/* SIGFPE  == divide by zero */
	0,		
	2,		/* SIGBUS  == bus error */
	3		/* SIGSEGV == address error */
	
	/* everything else gets zeros */
};

/* a "0" means we don't print a message when it happens -- typically the
 * user is expecting a synchronous signal, so we don't need to report it
 */
const char *signames [NSIG] =
{
	0,
	0,
	0,
	0,
	"ILLEGAL INSTRUCTION",
	"TRACE TRAP",
	0 /* "SIGABRT" */,
	"PRIVILEGE VIOLATION",
	"DIVISION BY ZERO",
	0,
	"BUS ERROR",
	"ADDRESS ERROR",
	"BAD SYSTEM CALL",
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	"CPU TIME EXHAUSTED",
	"FILE TOO BIG",
	0,
	0,
	0,
	0,
	0,
	/* Don't be smart and put "POWER FAILURE RESTART" here.  The init
	 * daemon will already take care of informing our users before the
	 * lights go out
	 */
	0
};

/*
 * replaces the TOS "show bombs" routine: for now, print the name of the
 * interrupt on the console, and save info on the crash in the appropriate
 * system area
 */

void
bombs (ushort sig)
{
	long *procinfo = (long *) 0x380L;
	int i;
	CONTEXT *crash;
	
	if (sig >= NSIG)
	{
		ALERT ("bombs(%d): sig out of range", sig);
	}
	else if (signames[sig])
	{
		if (!no_mem_prot && sig == SIGBUS)
		{
		    /* already reported by report_buserr */
		}
		else
		{
			/* uk: give some more information in case of a crash, so that a
			 *     progam which shared text can be debugged better.
			 */
			BASEPAGE *base = curproc->base;
			long ptext = 0;
			long pdata = 0;
			long pbss = 0;
			
			/* can it happen, that base == NULL???? */
			if (base)
			{
				ptext = base->p_tbase;
				pdata = base->p_dbase;
				pbss = base->p_bbase;
			}
			
			if (sig == SIGSEGV || sig == SIGBUS)
			{
				ALERT ("%s: User PC=%lx, Address: %lx (basepage=%lx, text=%lx, data=%lx, bss=%lx)",
					signames[sig],
					curproc->exception_pc, curproc->exception_addr,
					curproc->base, ptext, pdata, pbss);
			}
			else
			{
				ALERT ("%s: User PC=%lx (basepage=%lx, text=%lx, data=%lx, bss=%lx)",
					signames[sig],
					curproc->exception_pc,
					curproc->base, ptext, pdata, pbss);
			}
		}
		
		/* save the processor state at crash time
		 * 
		 * assumes that "crash time" is the context
		 * curproc->ctxt[SYSCALL]
		 * 
		 * BUG: this is not true if the crash happened in the kernel;
		 * in the latter case, the crash context wasn't saved anywhere.
		 */
		crash = &curproc->ctxt[SYSCALL];
		*procinfo++ = 0x12345678L;	/* magic flag for valid info */
		for (i = 0; i < 15; i++)
			*procinfo++ = crash->regs[i];
		*procinfo++ = curproc->exception_ssp;
		*procinfo++ = ((long) excep_num[sig]) << 24L;
		*procinfo = crash->usp;
		
		/* we're also supposed to save some info from the supervisor
		 * stack. it's not clear what we should do for MiNT, since
		 * most of the stuff that used to be on the stack has been
		 * put in the CONTXT struct. Moreover, we don't want to crash
		 * because of an attempt to access illegal memory. Hence, we
		 * do nothing here...
		 */
	}
	else
	{
		TRACE (("bombs(%d)", sig));
	}
}
# endif

/*
 * handle_sig: do whatever is appropriate to handle a signal
 */

long unwound_stack = 0;

struct sigcontext
{
	ulong	sc_pc;
	ulong	sc_usp;
	ushort	sc_sr;
};

void
handle_sig (ushort sig)
{
	long oldstack, newstack;
	long *stack;
	struct sigcontext *sigctxt;
	CONTEXT *call, contexts[2];
# define newcurrent (contexts[0])
# define oldsysctxt (contexts[1])
	
	/* just to be sure */
	assert (sig < NSIG);
	
	curproc->last_sig = sig;
	if (curproc->sighandle[sig] == SIG_IGN)
		return;
	
	if (curproc->sighandle[sig] == SIG_DFL)
	{
_default:
		switch (sig)
		{
# if 0
		/* Note: SIGNULL is filtered out in dossig.c and is never
		 * actually delivered (its only purpose for the user is to
		 * test for the existence of a process, it isn't a real
		 * signal). The kernel uses SIGNULL internally, but all such
		 * code does the signal handling "by hand" and so no default
		 * handling is necessary.
		 */
		case SIGNULL:
# endif
		case SIGWINCH:
		case SIGCHLD:
		/* SIGFPE is divide by 0
		 * TOS ignores this, so we will too
		 */
		case SIGFPE:
			return;		/* do nothing */
		
		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			stop (sig);
			return;
		
		case SIGCONT:
			curproc->sigpending &= ~STOPSIGS;
			return;
		
		/* here are the fatal signals. for SIGINT, we use
		 * kernel_pterm(ENOSYS) so that TOS programs that catch ^C
		 * via the vector at 0x400 and which expect TOS's error code
		 * (ENOSYS) to be sent will work. For most other signals, we
		 * kernel_pterm with an error code; for SIGKILL, we don't want
		 * to allow the program any chance to recover, so we call
		 * terminate() directly to avoid calling through to the user's
		 * terminate vector.
		 */
		case SIGINT:		/* ^C */
			if (curproc->domain == DOM_TOS)
			{
				curproc->signaled = 1;
				kernel_pterm (curproc, ENOSYS);
				return;
			}
			/* otherwise, fall through */
		default:
			/* Mark the process as being killed
			 * by a signal.  Used by Pwaitpid.
			 */
			curproc->signaled = 1;
# ifdef EXCEPTION_SIGS
			/* tell the user what happened */
			bombs (sig);
# endif
			/* the "sigmask" check is in case a bus error happens
			 * in the user's term_vec code; we don't want to get
			 * stuck in an infinite loop!
			 */
			if ((curproc->sigmask & 1L) || sig == SIGKILL)
				terminate (curproc, sig << 8, ZOMBIE_Q);
			else
				kernel_pterm (curproc, sig << 8);
		}
	}
	else
	{
		/* user wants to handle it himself */
		
		/* another kludge: there is one case in which the p_sigreturn
		 * mechanism is invoked by the kernel, namely when the user
		 * calls Supexec() or when s/he installs a handler for the
		 * GEMDOS terminate vector (#0x102) and the program terminates.
		 * MiNT fakes the call to user code with signal 0 (SIGNULL);
		 * programs that longjmp out of the user function and are
		 * later sent back to it again (e.g. if ^C keeps getting
		 * pressed and a terminate vector has been installed) will
		 * grow the stack without bound unless we watch for this case.
		 *
		 * Solution (sort of): whenever Pterm() is called, we unwind
		 * the stack; otherwise, we let it grow, so that nested
		 * Supexec() calls work.
		 *
		 * Note that SIGNULL is thrown away when sent by user
		 * processes,  and the user can't mask it (it's UNMASKABLE),
		 * so there is is no possibility of confusion with anything
		 * the user does.
		 */
		
		if (sig == 0)
		{
			/* kernel_pterm() sets sigmask to let us know to do
			 * Psigreturn
			 */
			
			if (curproc->sigmask & 1L)
			{
				p_sigreturn();
				curproc->sigmask &= ~1L;
			}
			else
			{
				unwound_stack = 0;
			}
		}
		
		++curproc->nsigs;
		call = &curproc->ctxt[SYSCALL];
		
		/* what we do is build two fake stack frames; the top one is
		 * for a call to the user function, with (long)parameter being the
		 * signal number; the bottom one is for sig_return.
		 * When the user function returns, it returns to sig_return, which
		 * calls into the kernel to restore the context in prev_ctxt
		 * (thus putting us back here). We can then continue on our way.
		 */
		
		/* set a new system stack, with a bit of buffer space */
		oldstack = curproc->sysstack;
		newstack = ((long) &newcurrent) - 0x40 - 12;
		
		if (newstack < (long) curproc->stack + ISTKSIZE + 256)
		{
			ALERT ("stack overflow");
			goto _default;
		}
		else if ((long) curproc->stack + STKSIZE < newstack)
		{
			FATAL ("system stack not in proc structure");
		}
		
		oldsysctxt = *call;
		stack = (long *)(call->sr & 0x2000 ? call->ssp : call->usp);
		
		/* Hmmm... here's another potential problem for the signal 0
		 * terminate vector: if the program keeps returning back to
		 * user mode without worrying about the supervisor stack,
		 * we'll eventually overflow it. However, if the program is
		 * in supervisor mode itself, then we don't want to stop on
		 * its stack. Temporary solution: ignore the problem, the
		 * stack's only growing 12 bytes at a time.
		 * 
		 * in addition to the signal number we stuff the vector offset
		 * on the stack; if the user is interested they can sniff it,
		 * if not ignoring it needs no action on their part. Why do we
		 * need this? So that a single SIGFPE handler (for example)
		 * can discriminate amongst the multiple things which may get
		 * thrown its way
		 */
		stack -= 3;
		sigctxt = (struct sigcontext *) stack;
		sigctxt->sc_pc = oldsysctxt.pc;
		sigctxt->sc_usp = oldsysctxt.usp;
		sigctxt->sc_sr = oldsysctxt.sr;
		*(--stack) = (long) sigctxt;
		*(--stack) = (long) call->sfmt & 0xfff;
		*(--stack) = (long) sig;
		*(--stack) = (long) sig_return;
		if (call->sr & 0x2000)
			call->ssp = ((long) stack);
		else
			call->usp = ((long) stack);
		call->pc = (long) curproc->sighandle[sig];
		/* don't restart FPU communication */
		call->sfmt = call->fstate[0] = 0;
		
		if (curproc->sigflags[sig] & SA_RESET)
		{
			curproc->sighandle[sig] = SIG_DFL;
			curproc->sigflags[sig] &= ~SA_RESET;
		}
		
		if (save_context (&newcurrent) == 0)
		{
			/* go do the signal; eventually, we'll restore this
			 * context (unless the user longjmp'd out of his
			 * signal handler). while the user is handling the
			 * signal, it's masked out to prevent race conditions.
			 * p_sigreturn() will unmask it for us when the user
			 * is finished.
			 */
			newcurrent.regs[0] = CTXT_MAGIC;
				/* set D0 so next return is different */
			assert(curproc->magic == CTXT_MAGIC);
			
			/* unwound_stack is set by p_sigreturn() */
			if (sig == 0 && unwound_stack)
				stack = (long *) unwound_stack;
			else
				/* newstack points just below our current sp,
				 * much less than ISTKSIZE away so better set
				 * it up with interrupts off...  -nox
				 */
				stack = (long *) newstack;
			
			spl7 ();
			unwound_stack = 0;
			curproc->sysstack = (long) stack;
			++stack;
			*stack++ = FRAME_MAGIC;
			*stack++ = oldstack;
			*stack = sig;
			leave_kernel ();
			restore_context (call);
		}
		
		/* OK, we get here from p_sigreturn, via the user returning
		 * from the handler to sig_return. Restoring the stack and
		 * unmasking the signal have been done already for us by
		 * p_sigreturn. We should just restore the old system call
		 * context and continue with whatever it was we were doing.
		 */
		
		TRACE (("done handling signal"));
		
		oldsysctxt.pc = sigctxt->sc_pc;
		oldsysctxt.usp = sigctxt->sc_usp;
		oldsysctxt.sr &= 0xff00;
		oldsysctxt.sr |= sigctxt->sc_sr & 0xff;
		
		curproc->ctxt[SYSCALL] = oldsysctxt;
		assert (curproc->magic == CTXT_MAGIC);
	}
# undef oldsysctxt
# undef newcurrent
}

/*
 * the p_sigreturn system call
 * When called by the user from inside a signal handler, it indicates a
 * desire to restore the old stack frame prior to a longjmp() out of
 * the handler.
 * When called from the sig_return module, it indicates that the user
 * is finished a handler, and we should not only restore the stack
 * frame but also the old context we were working in (which is on the
 * system call stack -- see handle_sig).
 * The syscall pc is "pc_valid_return" in the second case.
 */

long _cdecl
p_sigreturn (void)
{
	CONTEXT *oldctxt;
	long *frame;
	long sig;

	unwound_stack = 0;
top:
	frame = (long *) curproc->sysstack;
	frame++;	/* frame should point at FRAME_MAGIC, now */
	sig = frame[2];
	if (*frame != FRAME_MAGIC || (sig < 0) || (sig >= NSIG))
	{
		FATAL("Psigreturn: system stack corrupted");
	}
	if (frame[1] == 0)
	{
		DEBUG (("Psigreturn: frame at %lx points to 0", frame-1));
		return E_OK;
	}
	unwound_stack = curproc->sysstack;
	TRACE (("Psigreturn(%d)", (int) sig));
	
	curproc->sysstack = frame[1];	/* restore frame */
	curproc->sigmask &= ~(1L<<sig); /* unblock signal */
	
	if (curproc->ctxt[SYSCALL].pc != (long) &pc_valid_return)
	{
		/* here, the user is telling us that a longjmp out of a signal
		 * handler is about to occur; so we should unwind *all* the
		 * signal frames
		 */
		goto top;
	}
	else
	{
		unwound_stack = 0;
		
		oldctxt = (CONTEXT *) (((long) &frame[2]) + 0x40);
		if (oldctxt->regs[0] != CTXT_MAGIC)
		{
			FATAL ("p_sigreturn: corrupted context");
		}
		assert (curproc->magic == CTXT_MAGIC);
		
		restore_context (oldctxt);
		
		/* dummy -- this isn't reached */
		return E_OK;
	}
}

/*
 * stop a process because of signal "sig"
 */

void
stop (ushort sig)
{
	ushort code;
	ulong oldmask;
	
	/* just to be sure */
	assert (sig < NSIG);
	
	code = sig << 8;
	
	if (curproc->pid == 0)
	{
		FORCE ("attempt to stop MiNT");
		return;
	}
	
	/* notify parent */
	if (curproc->ptracer)
	{
		DEBUG (("stop (%i) SIGCHLD -> ptracer %lx", sig, curproc->ptracer));
		post_sig (curproc->ptracer, SIGCHLD);
	}
	else
	{
		PROC *p = pid2proc (curproc->ppid);
		if (p && !(p->sigflags[SIGCHLD] & SA_NOCLDSTOP))
		{
			ushort sr;
			
			post_sig (p, SIGCHLD);
			
			sr = splhigh ();
			if (p->wait_q == WAIT_Q && p->wait_cond == (long) p_waitpid)
			{
				rm_q (WAIT_Q, p);
				add_q (READY_Q, p);
			}
			spl (sr);
		}
	}
	
	oldmask = curproc->sigmask;
	
	if (!curproc->ptracer)
	{
		assert ((1L << sig) & STOPSIGS);
		
		/* mask out most signals */
		curproc->sigmask |= ~(UNMASKABLE | SIGTERM);
	}
	
	/* sleep until someone signals us awake */
	sleep (STOP_Q, (long) code | 0177);
	
	/* when we wake up, restore the signal mask */
	curproc->sigmask = oldmask;
	
	/* and discard any signals that would cause us to stop again */
	curproc->sigpending &= ~STOPSIGS;
}

/*
 * interrupt handlers to raise SIGBUS, SIGSEGV, etc. Note that for
 * really fatal errors we reset the handler to SIG_DFL, so that
 * a second such error kills us
 */

void
exception (ushort sig)
{
	/* just to be sure */
	assert (sig < NSIG);
	
	curproc->sigflags[sig] |= SA_RESET;
	DEBUG (("exception #%d raised [pc %lx]", sig, curproc->ctxt[SYSCALL].pc));
	
	raise (sig);
}

void
sigbus (void)
{
	if (curproc->sighandle[SIGBUS] == SIG_DFL)
		report_buserr ();
	
	exception (SIGBUS);
}

void
sigaddr (void)
{
	exception (SIGSEGV);
}

void
sigill (void)
{
	exception (SIGILL);
}

void
sigpriv (void)
{
	raise (SIGPRIV);
}

void
sigfpe (void)
{
	if (fpu)
	{
		CONTEXT *ctxt = &curproc->ctxt[SYSCALL];
		
		/* 0x1f38 is a Motorola magic cookie to detect a 68882 idle
		 * state frame
		 */
		if ((*(ushort *) ctxt->fstate == 0x1f38)
			&& ((ctxt->sfmt & 0xfff) >= 0xc0L)
			&& ((ctxt->sfmt & 0xfff) <= 0xd8L))
		{
			/* fix a bug in the 68882 - Motorola call it a
			 * feature :-)
			 */
			ctxt->fstate[ctxt->fstate[1]] |= 1 << 3;
		}
	}
	
	raise (SIGFPE);
}

void
sigtrap (void)
{
	raise (SIGTRAP);
}

void
haltformat (void)
{
	FATAL ("halt: invalid stack frame format");
}

void
haltcpv (void)
{
	FATAL ("halt: coprocessor protocol violation");
}
