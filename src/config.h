
#define PACKAGE "tnes"
#define VERSION "1.12.0"
/* NOTE: this header shadows the configure-generated ../config.h (quoted
   includes search the source dir first), so it is the one actually used
   by the build. Keep VERSION in sync with AC_INIT in configure.ac.
   SHAREDIR is a fallback only: the autotools build does not install the
   shared data files, and missing files fall back to generated defaults. */
#define SHAREDIR "/usr/local/share/tines/"
#define RCFILEIN "tinesrc"
#define RCFILEOUT ".tinesrc"
#define DATFILEIN "init.hnb"
#define DATFILEOUT ".tines"

#ifdef WIN32
#define snprintf(a,b,args...) sprintf(a,args)
#endif
