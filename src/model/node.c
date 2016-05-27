/*
 * node.c
 *
 *
 * Author:
 *  Richard Hult <rhult@hem.passagen.se>
 *  Ricardo Markiewicz <rmarkie@fi.uba.ar>
 *  Andres de Barbara <adebarbara@fi.uba.ar>
 *  Marc Lorber <lorber.marc@wanadoo.fr>
 *
 * Web page: https://ahoi.io/project/oregano
 *
 * Copyright (C) 1999-2001  Richard Hult
 * Copyright (C) 2003,2006  Ricardo Markiewicz
 * Copyright (C) 2009-2012  Marc Lorber
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <math.h>

#include "node.h"
#include "part.h"

#include "debug.h"

static void node_class_init (NodeClass *klass);
static void node_init (Node *node);

enum { NODE_DOT_ADDED, NODE_DOT_REMOVED, VOLTAGE_CHANGED, LAST_SIGNAL };

G_DEFINE_TYPE (Node, node, G_TYPE_OBJECT)

static guint node_signals[LAST_SIGNAL] = {0};

static void node_dispose (GObject *object) { G_OBJECT_CLASS (node_parent_class)->dispose (object); }

static void node_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);

	// Remove the pins and wires encountered by the node.
	if (NODE (object)->pins) {
		g_slist_free (NODE (object)->pins);
	}

	if (NODE (object)->wires) {
		g_slist_free (NODE (object)->wires);
	}
	G_OBJECT_CLASS (node_parent_class)->finalize (object);
}

static void node_class_init (NodeClass *klass)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *)klass;
	object_class->dispose = node_dispose;
	object_class->finalize = node_finalize;
	node_parent_class = g_type_class_peek_parent (klass);

	node_signals[NODE_DOT_ADDED] =
	    g_signal_new ("node_dot_added", G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_FIRST, 0,
	                  NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

	node_signals[NODE_DOT_REMOVED] =
	    g_signal_new ("node_dot_removed", G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_FIRST, 0,
	                  NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

	node_signals[VOLTAGE_CHANGED] =
	    g_signal_new ("voltage_changed", G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_FIRST, 0,
	                  NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void node_init (Node *node)
{
	node->number = 0;
	node->pin_count = 0;
	node->wire_count = 0;
	node->pins = NULL;
	node->wires = NULL;
	node->visited = FALSE;
}

Node *node_new (Coords pos, gint number)
{
	Node *node;

	node = NODE (g_object_new (TYPE_NODE, NULL));

	node->key = pos;
	node->number = number;

	return node;
}

#define SEP(p1, p2) ((fabs (p1.x - p2.x) < 1e-3) && (fabs (p1.y - p2.y) < 1e-3))
#define ON_THE_WIRE(p1, start, end)                                                                \
	(fabs ((end.y - start.y) * (p1.x - start.x) - (end.x - start.x) * (p1.y - start.y)) < 1.e-5)
gboolean node_needs_dot (Node *node)
{
	Wire *wire1, *wire2;
	Coords start_pos1, length1, end_pos1;
	Coords start_pos2, length2, end_pos2;

	oregano_echo ("node: %p --- pins: %i --- wires: %i", node, node->pin_count, node->wire_count);

	// always display a black dot if a part hits a wire
	if (node->pin_count > 0 && node->wire_count > 0) {
		oregano_echo ("  TRUE (pins>0 && wires>0)");
		return TRUE;
	} else if (node->pin_count > 1 || node->wire_count > 2) {
		oregano_echo ("  TRUE (pins>1 || wires>2)");
		return TRUE;
	} else if (node->wire_count == 2) {
		// Check that we don't have two wire endpoints.
		wire1 = node->wires->data;
		wire2 = node->wires->next->data;

		wire_get_pos_and_length (wire1, &start_pos1, &length1);
		wire_get_pos_and_length (wire2, &start_pos2, &length2);

		end_pos1.x = start_pos1.x + length1.x;
		end_pos1.y = start_pos1.y + length1.y;
		end_pos2.x = start_pos2.x + length2.x;
		end_pos2.y = start_pos2.y + length2.y;

		if (!(SEP (start_pos1, start_pos2) || SEP (start_pos1, end_pos2) ||
		      SEP (end_pos1, end_pos2) || SEP (end_pos1, start_pos2))) {
			return TRUE;
		}
		return FALSE;
	}
	oregano_echo (" FALSE (else)");
	return FALSE;
}

gboolean node_add_pin (Node *node, Pin *pin)
{
	gboolean dot;

	g_return_val_if_fail (node != NULL, FALSE);
	g_return_val_if_fail (IS_NODE (node), FALSE);
	g_return_val_if_fail (pin != NULL, FALSE);

	if (g_slist_find (node->pins, pin)) {
		oregano_echo ("node_add_pin: pin already there.\n");
		return FALSE;
	}

	dot = node_needs_dot (node);

	node->pins = g_slist_prepend (node->pins, pin);
	node->pin_count++;
	if (!dot && node_needs_dot (node))
		g_signal_emit_by_name (G_OBJECT (node), "node_dot_added", &node->key);

	return TRUE;
}

gboolean node_remove_pin (Node *node, Pin *pin)
{
	gboolean dot;

	g_return_val_if_fail (node != NULL, FALSE);
	g_return_val_if_fail (IS_NODE (node), FALSE);
	g_return_val_if_fail (pin != NULL, FALSE);

	if (node->pin_count == 0)
		return FALSE;

	dot = node_needs_dot (node);

	node->pins = g_slist_remove (node->pins, pin);
	node->pin_count--;

	if (dot && !node_needs_dot (node))
		g_signal_emit_by_name (G_OBJECT (node), "node_dot_removed", &node->key);

	return TRUE;
}

gboolean node_add_wire (Node *node, Wire *wire)
{
	gboolean dot;

	g_return_val_if_fail (node != NULL, FALSE);
	g_return_val_if_fail (IS_NODE (node), FALSE);
	g_return_val_if_fail (wire != NULL, FALSE);
	g_return_val_if_fail (IS_WIRE (wire), FALSE);

	if (g_slist_find (node->wires, wire)) {
		oregano_echo ("node_add_wire: wire already there.\n");
		return FALSE;
	}

	dot = node_needs_dot (node);

	node->wires = g_slist_prepend (node->wires, wire);
	node->wire_count++;

	if (!dot && node_needs_dot (node))
		g_signal_emit_by_name (G_OBJECT (node), "node_dot_added", &node->key);

	return TRUE;
}

gboolean node_remove_wire (Node *node, Wire *wire)
{
	gboolean dot;

	g_return_val_if_fail (node != NULL, FALSE);
	g_return_val_if_fail (IS_NODE (node), FALSE);
	g_return_val_if_fail (wire != NULL, FALSE);
	g_return_val_if_fail (IS_WIRE (wire), FALSE);

	if (node->wire_count == 0)
		return FALSE;

	if (!g_slist_find (node->wires, wire)) {
		oregano_echo ("node_remove_wire: not there.\n");
		return FALSE;
	}

	dot = node_needs_dot (node);

	node->wires = g_slist_remove (node->wires, wire);
	node->wire_count--;

	if (dot && (!node_needs_dot (node)))
		g_signal_emit_by_name (G_OBJECT (node), "node_dot_removed", &node->key);

	return TRUE;
}

gboolean node_is_empty (Node *node)
{
	g_return_val_if_fail (node != NULL, FALSE);
	g_return_val_if_fail (IS_NODE (node), FALSE);

	if ((node->wire_count == 0) && (node->pin_count == 0))
		return TRUE;

	return FALSE;
}

gboolean node_is_visited (Node *node)
{
	g_return_val_if_fail (node != NULL, FALSE);
	g_return_val_if_fail (IS_NODE (node), FALSE);

	return node->visited;
}

void node_set_visited (Node *node, gboolean is_visited)
{
	g_return_if_fail (node != NULL);
	g_return_if_fail (IS_NODE (node));

	node->visited = is_visited;
}

#define HASH_EPSILON 1e-3

/**
 * Hash function to be used with a GHashTable (and others)
 */
guint node_hash (gconstpointer key)
{
	Coords *sp = (Coords *)key;
	register int x, y;
	const int shift = sizeof(int) * 8 / 2;

	// Hash on any other node?

	x = (int)rint (sp->x) % 1 << shift;
	y = (int)rint (sp->y) % 1 << shift;

	return (y << shift) | x;
}

/**
 * Comparsion function to be used with a GHashTable (and others)
 */
gboolean node_equal (gconstpointer a, gconstpointer b)
{
	const Coords *ca = a;
	const Coords *cb = b;

	if (fabs (ca->y - cb->y) > HASH_EPSILON)
		return 0;

	if (fabs (ca->x - cb->x) > HASH_EPSILON)
		return 0;

	return 1;
}
