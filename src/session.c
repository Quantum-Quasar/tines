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
#include <ctype.h>
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

/* parse a decimal index after a "<keyword> " prefix; returns -1 if the
   rest of the line is not a plain number (e.g. "expanded banana" must
   not be mistaken for index 0) */
static long session_parse_index (const char *line)
{
	char *end;
	long v;

	v = strtol (line, &end, 10);
	if (end == line)		/* no digits at all */
		return -1;
	while (isspace ((unsigned char) *end))
		end++;
	if (*end != 0)			/* trailing garbage */
		return -1;
	if (v < 0 || v > 100000000)
		return -1;
	return v;
}

/* save the session file next to the current database;
   no-op when the content is unchanged */
void session_save (Node *pos)
{
	char path[PREFS_FN_LEN + 16];
	char tmppath[PREFS_FN_LEN + 21];
	char *new_content;
	char *old_content;
	long new_len = 0;
	long old_len = 0;
	int plen;
	FILE *file;

	if (pos == NULL)
		return;
	if (prefs.db_file[0] == (char) 255)	/* magic value of tutorial */
		return;

	plen = snprintf (path, sizeof (path), "%s_tnes_session", prefs.db_file);
	if (plen < 0 || (size_t) plen >= sizeof (path))
		return;				/* path would be truncated */

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

	/* write to a temporary file and rename, so a crash mid-write can
	   never leave a truncated or half-written session file behind */
	plen = snprintf (tmppath, sizeof (tmppath), "%s.tmp", path);
	if (plen < 0 || (size_t) plen >= sizeof (tmppath)) {
		free (new_content);
		return;
	}

	file = fopen (tmppath, "w");
	if (file == NULL) {
		free (new_content);
		return;
	}

	if (new_len > 0)
		fwrite (new_content, 1, (size_t) new_len, file);
	fclose (file);
	free (new_content);

	chmod (tmppath, SESSION_FILE_MODE);
	if (rename (tmppath, path) != 0)
		remove (tmppath);	/* keep the old session file instead */
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

	{
		int plen = snprintf (path, sizeof (path), "%s_tnes_session",
							 prefs.db_file);
		if (plen < 0 || (size_t) plen >= sizeof (path))
			return pos;		/* path would be truncated */
	}
	file = fopen (path, "r");
	if (file == NULL)
		return pos;

	root = node_root (pos);

	while (fgets (line, sizeof (line), file) != NULL) {
		if (strncmp (line, "cursor ", 7) == 0) {
			long v = session_parse_index (line + 7);

			if (v >= 0)
				cursor_no = (int) v;
		} else if (strncmp (line, "expanded ", 9) == 0) {
			long v = session_parse_index (line + 9);
			int i;

			if (v < 0)
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

	/* The "db" line in the file is informational only and is not
	   checked here: the sidecar name is derived from the database name,
	   so a mismatch can only happen if the user copied both files on
	   purpose (e.g. restoring a backup), and then the state is wanted. */

	/* move the cursor to the remembered node (0-based flat index).
	   Counted from the root of the tree: the caller may already have
	   moved pos (in-file savepos hint, etc.), so a relative walk would
	   drift. Index 0 (the first visible node) is restored too, so the
	   session file consistently overrides any in-file hint. A stale
	   index (file changed since the last run) falls back to wherever
	   the caller is, instead of walking off the tree. */
	if (cursor_no >= 0) {
		Node *target;
		int i;

		for (i = 0, target = root; i < cursor_no && target != NULL; i++)
			target = node_recurse (target);

		if (target != NULL)
			pos = target;
	}

	if (cursor_no >= 0 || any_expanded)
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
