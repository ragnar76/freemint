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

#ifndef _menuwidg_h
#define _menuwidg_h

#include "global.h"
#include "xa_types.h"

TASK click_form_popup_entry, click_popup_entry, do_scroll_menu;
void	popout(Tab *tab);
void	do_popup(Tab *tab, XA_TREE *wt, int item, TASK *click, short rdx, short rdy);
bool	is_attach(struct xa_client *client, XA_TREE *wt, int item, XA_MENU_ATTACHMENT **pat);
int	inquire_menu(enum locks lock, struct xa_client *client, XA_TREE *wt, int item, XAMENU *mn);
int	attach_menu(enum locks lock, struct xa_client *client, XA_TREE *wt, int item, XAMENU *mn);
int	detach_menu(enum locks lock, struct xa_client *client, XA_TREE *wt, int item);
void	free_attachments(struct xa_client *client);
void	remove_attachments(enum locks lock, struct xa_client *client, XA_TREE *wt);
void	set_menu_widget(struct xa_window *wind, struct xa_client *owner, XA_TREE *menu);
void	fix_menu(struct xa_client *client, OBJECT *root, bool);

Tab *	collapse(Tab *from, Tab *upto);
Tab *	find_pop(Tab *start, short x, short y);


INLINE	struct xa_widget * get_menu_widg(void) { return &root_window->widgets[XAW_MENU]; }
//INLINE XA_TREE *get_menu(void) { return root_window->widgets[XAW_MENU].stuff; }
INLINE	XA_TREE *get_menu(void) { return root_window->widgets[XAW_MENU].stuff; }
INLINE	struct xa_client *menu_owner(void) { return get_menu()->owner; }

#endif /* _menuwidg_h */
