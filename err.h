#ifndef _ERR_
#define _ERR_
/* Nie jestem autorem tej biblioteki, nagłówek i implementacja pochodzą ze strony
 * moodle.mimuw.edu.pl, z sekcji PW */

/* print system call error message and terminate */
extern void syserr(int bl, const char *fmt, ...);

/* print error message and terminate */
extern void fatal(const char *fmt, ...);

#endif
