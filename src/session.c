/*
 * session.c -- remember cursor position and expanded nodes between runs
 *
 * State is written to "<db_file>_tnes_session" as plain ASCII lines:
 *
 *   cursor <flat node index>
 *   expanded <flat node index>
 *   ...
 *
 * Flat node indices are counted by tnes' own walk from node_root()
 * (first visible node = index 0), in both session_save() and
 * session_restore(), so save and restore always agree.
 *
 * The file is only rewritten when the state actually changed, so it is
 * safe to call session_save() on every commit/keystroke.
 *
 * Part of the tnes fork of Tines.
 * Tines is Copyright (C) 2016 Larry Kollar; hnb by Øyvind Kolås.
 * This file is distributed under the GNU General Public License v2 or later.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "tree.h"
#include "prefs.h"
#include "session.h"
#include "ui_cli.h"

/* session files belong to the same database as the rescue file:
   make them readable only by the owner */
#define SESSION_FILE_MODE 0600

static char *session_read_file (const char *path, long *len_out)
{
	FILE *file;
	char *buf;
	long len;

	*len_out = 0;
	file = fopen (path, "r");
	if (file == NULL)
		return NULL;

	fseek (file, 0, SEEK_END);
	len = ftell (file);
	fseek (file, 0, SEEK_SET);
	if (len < 0) {
		fclose (file);
		return NULL;
	}

	buf = malloc (len + 1);
	if (buf == NULL) {
		fclose (file);
		return NULL;
	}

	if (len > 0 && fread (buf, 1, len, file) != (size_t) len) {
		free (buf);
		fclose (file);
		return NULL;
	}
	buf[len] = 0;
	fclose (file);
	*len_out = len;
	return buf;
}

/* build the session file content for the current tree;
   returns malloc'ed buffer (caller frees) or NULL

   Node indices are counted by our own walk from node_root(), NOT with
   node_no(): node_no() also counts the invisible tree root created by
   tree_new(), while node_root() starts at the first visible node.
   Numbering both save and restore with the same walk keeps them
   consistent no matter what the root node semantics are. */
static char *session_build_content (Node *pos, long *len_out)
{
	char *buf;
	size_t cap;
	size_t used = 0;
	int written;
	Node *node;
	Node *root;
	int idx;
	int cur_no = 0;

	*len_out = 0;
	if (pos == NULL)
		return NULL;

	root = node_root (pos);

	/* worst-case size: header + longest possible db path + one
	   "expanded NNNNNNNNNN\n" line per node + cursor line + NUL */
	cap = 64;
	cap += strlen (prefs.db_file) + 8;
	for (node = root; node != NULL; node = node_recurse (node))
		cap += 32;

	buf = malloc (cap);
	if (buf == NULL)
		return NULL;

	/* append or fail: never write a silently truncated session file */
#define SESSION_APPEND(...) \
	do { \
		written = snprintf (buf + used, cap - used, __VA_ARGS__); \
		if (written < 0 || (size_t) written >= cap - used) { \
			free (buf); \
			return NULL; \
		} \
		used += (size_t) written; \
	} while (0)

	SESSION_APPEND ("# tnes session state\n");
	SESSION_APPEND ("db %s\n", prefs.db_file);

	for (idx = 0, node = root; node != NULL;
		 idx++, node = node_recurse (node)) {
		if (node == pos)
			cur_no = idx;
		if (node_getflag (node, F_expanded) && node_right (node))
			SESSION_APPEND ("expanded %i\n", idx);
	}

	SESSION_APPEND ("cursor %i\n", cur_no);

#undef SESSION_APPEND

	*len_out = (long) used;
	return buf;
}

/* save the session file next to the current database;
   no-op when the content is unchanged */
void session_save (Node *pos)
{
	char path[PREFS_FN_LEN + 16];
	char *new_content;
	char *old_content;
	long new_len = 0;
	long old_len = 0;
	FILE *file;

	if (pos == NULL)
		return;
	if (prefs.db_file[0] == (char) 255)	/* magic value of tutorial */
		return;

	snprintf (path, sizeof (path), "%s_tnes_session", prefs.db_file);

	new_content = session_build_content (pos, &new_len);
	if (new_content == NULL)
		return;

	old_content = session_read_file (path, &old_len);
	if (old_content != NULL && old_len == new_len &&
		memcmp (old_content, new_content, new_len) == 0) {
		free (old_content);
		free (new_content);
		return;					/* nothing changed */
	}
	free (old_content);

	file = fopen (path, "w");
	if (file == NULL) {
		free (new_content);
		return;
	}

	if (new_len > 0)
		fwrite (new_content, 1, (size_t) new_len, file);
	fclose (file);
	free (new_content);

	chmod (path, SESSION_FILE_MODE);
}

/* restore cursor and expansion state written by session_save().
   Returns the new position (may be the same node). */
Node *session_restore (Node *pos)
{
	char path[PREFS_FN_LEN + 16];
	char line[64];
	FILE *file;
	Node *root;
	Node *node;
	int cursor_no = -1;
	int any_expanded = 0;

	if (pos == NULL)
		return pos;
	if (prefs.db_file[0] == (char) 255)	/* magic value of tutorial */
		return pos;

	snprintf (path, sizeof (path), "%s_tnes_session", prefs.db_file);
	file = fopen (path, "r");
	if (file == NULL)
		return pos;

	root = node_root (pos);

	while (fgets (line, sizeof (line), file) != NULL) {
		if (strncmp (line, "cursor ", 7) == 0) {
			long v = strtol (line + 7, NULL, 10);

			if (v >= 0 && v <= 100000000)
				cursor_no = (int) v;
		} else if (strncmp (line, "expanded ", 9) == 0) {
			long v = strtol (line + 9, NULL, 10);
			int i;

			if (v < 0 || v > 100000000)
				continue;		/* corrupt line: ignore it */

			/* flat walk from the root to find the numbered node (same
		   walk order as session_save) */
			for (i = 0, node = root; i < v && node != NULL; i++)
				node = node_recurse (node);
			if (node != NULL && node_right (node)) {
				node_setflag (node, F_expanded, 1);
				any_expanded++;
			}
		}
	}
	fclose (file);

	/* move the cursor to the remembered node (0-based flat index).
	   Counted from the root of the tree: the caller may already have
	   moved pos (in-file savepos hint, etc.), so a relative walk would
	   drift. A stale index (file changed since the last run) falls back
	   to wherever the caller is, instead of walking off the tree. */
	if (cursor_no > 0) {
		Node *target;
		int i;

		for (i = 0, target = root; i < cursor_no && target != NULL; i++)
			target = node_recurse (target);

		if (target != NULL)
			pos = target;
	}

	if (cursor_no > 0 || any_expanded)
		cli_outfunf ("tnes: session restored (cursor %i, %i expanded)",
					 cursor_no, any_expanded);

#ifdef SESSION_DEBUG
	fprintf (stderr, "tnes session: cursor_no=%i expanded_flags_set=%i\n",
			 cursor_no, any_expanded);
	{
		Node *d;
		int i;
		for (i = 0, d = root; d != NULL; i++, d = node_recurse (d))
			fprintf (stderr, "  node %i: '%s' flags=%x\n", i,
					 fixnullstring (node_get (d, TEXT)),
					 (unsigned) node_getflags (d));
	}
#endif

	return pos;
}

/* wrappers so the functions can also be called as cli commands */
static void *session_save_cmd (int argc, char **argv, void *data)
{
	session_save ((Node *) data);
	return data;
}

static void *session_restore_cmd (int argc, char **argv, void *data)
{
	return session_restore ((Node *) data);
}

/*
!init_session();
*/
void init_session ()
{
	cli_add_command ("session_save", session_save_cmd, "");
	cli_add_help ("session_save",
				  "Save the cursor position and expanded nodes to the tnes session file.");

	cli_add_command ("session_restore", session_restore_cmd, "");
	cli_add_help ("session_restore",
				  "Restore the cursor position and expanded nodes from the tnes session file.");
}
