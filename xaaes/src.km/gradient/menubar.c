/*
 * $Id$
 *
 * XaAES - XaAES Ain't the AES (c) 1992 - 1998 C.Graham
 *                                 1999 - 2003 H.Robbers
 *                                 2004 - 2006 F.Naumann & O.Skancke
 *                                 2009 - 2010 P.Wratt
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

/*// menubar: gradient only for menubar */

#include "../gradients.h"
short start = START;
/* ---------------------------------------------------------------------------------- */
/* -----------  Standard client window colour gradients --------------------------------- */
/* ---------------------------------------------------------------------------------- */
struct xa_gradient otop_vslide_gradient =
{
	(unsigned long)OTOP_VSLIDE_GRADIENT,
	-1, 0,
	 0, 16,

	1,-1, {0},
	{{400,400,500},{600,600,700}},
};
struct xa_gradient otop_hslide_gradient =
{
	(unsigned long)OTOP_HSLIDE_GRADIENT,
	 0, -1,
	16,  0,

	0, -1, {0},
	{{400,400,500},{600,600,700}},
};
struct xa_gradient otop_vslider_gradient =
{
	(unsigned long)OTOP_VSLIDER_GRADIENT,
	-1, 0,
	 0, 16,

	1, -1, {0},
	{{900,900,1000},{600,600,700}},
};
struct xa_gradient otop_hslider_gradient =
{
	(unsigned long)OTOP_HSLIDER_GRADIENT,
	 0, -1,
	16,  0,

	0, -1, {0},
	{{900,900,1000},{600,600,700}},
};

struct xa_gradient utop_vslide_gradient =
{
	(unsigned long)UTOP_VSLIDE_GRADIENT,
	-1, 0,
	 0, 16,

	1, -1, {0},
	{{300,300,300},{500,500,500}},
};
struct xa_gradient utop_hslide_gradient =
{
	(unsigned long)UTOP_HSLIDE_GRADIENT,
	 0, -1,
	16,  0,

	0, 0, {0},
	{{300,300,300},{500,500,500}},
};
struct xa_gradient utop_vslider_gradient =
{
	(unsigned long)UTOP_VSLIDER_GRADIENT,
	-1, 0,
	 0, 16,

	1, -1, {0},
	{{800,800,800},{500,500,500}},
};
struct xa_gradient utop_hslider_gradient =
{
	(unsigned long)UTOP_HSLIDER_GRADIENT,
	 0, -1,
	16,  0,

	0, -1, {0},
	{{800,800,800},{500,500,500}},
};

struct xa_gradient otop_title_gradient =
{
	(unsigned long)OTOP_TITLE_GRADIENT ,
	 0, -1,
	16,  0,

	0, -1, {0},
	{{700,700,900},{500,500,700}},
};
struct xa_gradient utop_title_gradient =
{
	(unsigned long)UTOP_TITLE_GRADIENT,
	0, -1,
	16, 0,

	0,-1,{0},
	{{600,600,600},{800,800,800}},
};

struct xa_gradient otop_info_gradient =
{
	(unsigned long)OTOP_INFO_GRADIENT ,
	0, -1,
	16, 0,

	0,-1,{0},
	{{800,800,850},{950,950,1000}},
};
struct xa_gradient utop_info_gradient =
{
	(unsigned long)UTOP_INFO_GRADIENT ,
	0, -1,
	16, 0,

	0, -1, {0},
	{{600,600,600},{800,800,800}},
};

struct xa_gradient otop_grey_gradient =
{
	(unsigned long)OTOP_GREY_GRADIENT,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{600,600,700},{900,900,1000}},
};
struct xa_gradient utop_grey_gradient =
{
	(unsigned long)UTOP_GREY_GRADIENT,
	-1, -1,
	0, 0,

	2, 0, {0},
	{{500,500,500},{800,800,800}},
};

/* ---------------------------------------------------------------------------------- */
/* ---------------- Alert window colour gradients --------------------------------------- */
/* ---------------------------------------------------------------------------------- */
struct xa_gradient alert_otop_title_gradient =
{
	(unsigned long)ALERT_OTOP_TITLE_GRADIENT ,
	-1, -1,
	0, 0,

	2, -1,{0},
	{{500,200,200},{1000,800,800}},
};
struct xa_gradient alert_utop_title_gradient =
{
	(unsigned long)ALERT_UTOP_TITLE_GRADIENT,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{500,500,500},{700,700,700}},
};

struct xa_gradient alert_utop_grey_gradient =
{
	(unsigned long)ALERT_UTOP_GREY_GRADIENT,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{300,300,300},{700,700,700}},
};

/* ---------------------------------------------------------------------------------- */
/* ---------------- Scroll List window colour gradients --------------------------------- */
/* ---------------------------------------------------------------------------------- */
struct xa_gradient slist_otop_vslide_gradient =
{
	(unsigned long)SLIST_OTOP_VSLIDE_GRADIENT,
	-1, 0,
	0, 16,

	1, -1, {0},
	{{300,300,300},{500,500,500}},
};
struct xa_gradient slist_otop_hslide_gradient =
{
	(unsigned long)SLIST_OTOP_HSLIDE_GRADIENT,
	0, -1,
	16, 0,

	0, -1, {0},
	{{300,300,300},{500,500,500}},
};
struct xa_gradient slist_otop_vslider_gradient =
{
	(unsigned long)SLIST_OTOP_VSLIDER_GRADIENT,
	-1, 0,
	0, 16,

	1, -1, {0},
	{{800,800,800},{500,500,500}},
};
struct xa_gradient slist_otop_hslider_gradient =
{
	(unsigned long)SLIST_OTOP_HSLIDER_GRADIENT,
	0, -1,
	16, 0,

	0, -1, {0},
	{{800,800,800},{500,500,500}},
};
struct xa_gradient slist_utop_vslide_gradient =
{
	(unsigned long)SLIST_UTOP_VSLIDE_GRADIENT,
	-1, 0,
	0, 16,

	1, -1, {0},
	{{300,300,300},{500,500,500}},
};
struct xa_gradient slist_utop_hslide_gradient =
{
	(unsigned long)SLIST_UTOP_HSLIDE_GRADIENT,
	0, -1,
	16, 0,

	0, -1, {0},
	{{300,300,300},{500,500,500}},
};
struct xa_gradient slist_utop_vslider_gradient =
{
	(unsigned long)SLIST_UTOP_VSLIDER_GRADIENT,
	-1, 0,
	0, 16,

	1, -1, {0},
	{{800,800,800},{500,500,500}},
};
struct xa_gradient slist_utop_hslider_gradient =
{
	(unsigned long)SLIST_UTOP_HSLIDER_GRADIENT,
	0, -1,
	16, 0,

	0, -1, {0},
	{{800,800,800},{500,500,500}},
};

struct xa_gradient slist_otop_title_gradient =
{
	(unsigned long)SLIST_OTOP_TITLE_GRADIENT ,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{00,00,400},{600,600,1000}},
};
struct xa_gradient slist_utop_title_gradient =
{
	(unsigned long)SLIST_UTOP_TITLE_GRADIENT,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{500,500,500},{700,700,700}},
/*	{{500,500,500},{900,900,900}}, */
};

struct xa_gradient slist_otop_info_gradient =
{
	(unsigned long)SLIST_OTOP_INFO_GRADIENT ,
	0, -1,
	16, 0,

	3, -1, {3, 0},
	{{200,200,200},{600,600,600},{900,900,900}},
};
struct xa_gradient slist_utop_info_gradient =
{
	(unsigned long)SLIST_UTOP_INFO_GRADIENT ,
	0, -1,
	16, 0,

	0, -1, {0},
	{{400,400,400},{700,700,700}},
};
struct xa_gradient slist_otop_grey_gradient =
{
	(unsigned long)SLIST_OTOP_GREY_GRADIENT,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{500,500,500},{900,900,900}},
};
struct xa_gradient slist_utop_grey_gradient =
{
	(unsigned long)SLIST_UTOP_GREY_GRADIENT,
	-1, -1,
	0, 0,

	2, -1, {0},
	{{300,300,300},{700,700,700}},
};

struct xa_gradient menu_gradient =
{
	(unsigned long)MENU_GRADIENT,
	 0, -1,
	16,  0,

	0, 0, {0},
	{{750,800,900},{700,750,950}},
};
struct xa_gradient sel_title_gradient =
{
	(unsigned long)SEL_TITLE_GRADIENT,
	0, -1,
	16, 0,
	0, 0, {0},
	{{750,800,900},{700,750,950}},
};
struct xa_gradient sel_popent_gradient =
{
	(unsigned long)SEL_POPENT_GRADIENT,
	0, -1,
	16, 0,
	0, 0, {0},
	{{950,950,1000},{800,800,1000}},
};

struct xa_gradient indbutt_gradient =
{
	(unsigned long)INDBUTT_GRADIENT,
	-1,   0,
	 0,  16,

	4, -1, { -35, 0, },
	{{700,700,700},{900,900,900},{700,700,700}},
};
struct xa_gradient sel_indbutt_gradient =
{
	(unsigned long)SEL_INDBUTT_GRADIENT,
	-1,   0,
	 0,  16,

	4, -1, { -35, 0, },
	{{700,700,700},{500,500,500},{700,700,700}},
};
struct xa_gradient actbutt_gradient =
{
	(unsigned long)ACTBUTT_GRADIENT,
	0, -1,
	16, 0,

	0, -1, { 0 },
	{{900,900,900},{700,700,700}},
};

struct xa_gradient popbkg_gradient =
{
	(unsigned long)POPBKG_GRADIENT,
	0, -1,
	16, 0,

	3, -1, {-40, 0},
	{{800,800,850}, {900,900,950}, {700,700,750}},
};

short stop = STOP;
