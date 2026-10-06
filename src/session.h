#ifndef TINES_SESSION_H
#define TINES_SESSION_H

#include "tree.h"

/* Save the cursor position and expanded-node list for this database.
   Writes "<db_file>_tnes_session" next to the database. */
void session_save (Node *pos);

/* Restore the cursor position and expanded-node list for this database.
   Returns the (possibly changed) position. */
Node *session_restore (Node *pos);

/* init hook: registers the session_save / session_restore commands */
void init_session ();

#endif /* TINES_SESSION_H */
