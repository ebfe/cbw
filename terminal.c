/* Terminal dependent routines (well, most of them ...)
 *
 * Author: Bob Baldwin, October 1986.
 */

/* 
 * Routines in terminal abstraction:
 *
 *	setup_term()
 *		Initialize terminal, clear the screen.
 *
 *	unset_term()
 *		Return terminal to state before setup_term().
 *
 *	char2sym(char)
 *		Return the symbol used to display the given char in the
 *		decryption window.
 *
 *	putsym(symbol)
 *		Displays the given symbol on the terminal.  Handles
 * 		entering and exiting graphics mode.
 *
 *	getcmd()
 *		Reads stdin for a keystroke and returns
 *		a command integer.
 *
 *	beep()
 *		Cause the terminal to beep or flash.
 */

/* The trick to device independence is to clearly separate
 * internal and external representations.  On the inbound
 * side, we separate KEYSTOKES which generate a sequence
 * of ascii characters (one per keystroke in the simple case),
 * from COMMAND-KEYS such as move-cursor-up.  On the outbound
 * side we separate SYMBOLS from the sequence of ascii characters,
 * called GRAPHICS, used to display the symbol.  Each symbol
 * has a single use by the code, though two symbols might
 * appear the same on the user's terminal.
 */

/* Symbols are represented by integers.  If the integer is
 * greater than 256, then it denotes one of the symbols defined
 * in terminal.h.
 *
 * Commands are represented by two byte integers.  The high byte
 * describes the command (see terminal.h), the low byte is the
 * argument to the command.  For example, the insert char command
 * sets the low byte to the character to be inserted.
 */

/* INTERNALS: symbols and graphics
 *
 * The terminal is assumed to be in one of three modes: normal, graphics,
 * or standout (inverse video).  This abstraction takes care of all the
 * switching between modes and tries to avoid sending redundant escape
 * sequences to the terminal.
 * 
 * Part of the terminal initialization is to build a table that describes
 * how to display each symbol.  Ascii characters (including control chars)
 * pass through unchanged, but symbols are looked up in graphtab.
 * Each entry in the graphics table specifies a terminal mode and a
 * string to print that will display that symbol in the given mode.
 * If necessary a mode switch sequence will be sent before printing the
 * string.
 *
 * The graphics corresponding to symbols can be set by the shell variable
 * 'GRAHPICS' using a format similar to the termcap format.  The two
 * character names of the symbols are defined in the table symnames[],
 * which can be indexed by the symbol codes (see terminal.h).
 * The mapping from symbols to graphics is first set to a default value,
 * and then updated by the values found in the shell variable (if any).
 *
 * The GRAPHICS string consists of a number of entries separated by colon
 * characters.  Each entry have the form xx=\Mccc, where xx is the name
 * of a symbol (see symnames[] below), \M indicates the mode for displaying
 * this symbol (it must be one of \N (for normal), \G (for graphics), or \S
 * (for standout or inverse video)), and ccc is a sequence of characters
 * to send.  To include a colon character in the ccc portion, preceed it
 * by a backslash (i.e., '\').  If the \M is ommitted, it defaults to normal.
 */

/* INTERNALS: keystrokes and commands
 *
 * The table keycmdtab is used to convert a sequence of ascii characters
 * into a command integer.  The table specifies the escape sequences that
 * might be generated by the different command keys.  The getcmd routine
 * will read characters from stdin until it has uniquely identified a
 * command, or decided that there is no match.  If no match is found,
 * the terminal is beeped and matching is restarted with the next character.
 * By default keystrokes turn into self-insert commands.
 *
 * In general the last character of the sequence is returned as the arg.
 * This helps windows assign different meanings to keystokes.  For example
 * The return key can be either an insert-return command or an execute-
 * command-line command.
 */


#include	<curses.h>
#include	<sgtty.h>
#include	<strings.h>
#include	"window.h"
#include	"terminal.h"
#include	"specs.h"


/* Routines from termcap library.
 */
extern	char *getenv(), *tgetstr(), *tgoto();


/* Screen control strings from termcap entry.
 */
char	*term_is;		/* Terminal initializaion string. */
char	*erase_eol;		/* Erase to end of line. */
char	*erase_eos;		/* Erase to end of screen. */
char	*erase_scr;		/* Erase whole screen. */
char	*cm;			/* Cursor motion. */
char	*start_kp;		/* Start graphics mode. */
char	*end_kp;		/* End graphics mode. */
char	*start_alt;		/* Start graphics mode. */
char	*end_alt;		/* End graphics mode. */
char	*start_so;		/* Start standout mode. */
char	*end_so;		/* End standout mode. */

/* Keymap strings from termcap file.
 */
char	*term_f1;		/* The f1 key. */
char	*term_f2;		/* The f2 key. */
char	*term_f3;		/* The f3 key. */
char	*term_f4;		/* The f4 key. */
char	*term_up;		/* Up arrow. */
char	*term_down;		/* Down arrow. */
char	*term_left;		/* Left arrow. */
char	*term_right;		/* Right arrow. */


/* Symbol names for the shell variable 'GRAPHICS'
 * The values must be and'ed with SYMBOLM to make
 * suitable indices for graphtab[].
 */
labelv	symnames[NSYMC + 1] = {
	{"tb", STAB},		/* Tab */
	{"na", SNOTASCII},	/* Not ascii */
	{"lf", SLINEFEED},	/* Linefeed */
	{"cr", SCARETURN},	/* Carriage return */
	{"ff", SFORMFEED},	/* Formfeed */
	{"cc", SCONTCODE},	/* Other control characters */
	{"uk", SUNKNOWN},	/* Plaintext unknown */
	{"ul", SUNDERLINE},	/* Pseudo underline char */
	{"hb", SHORZBAR},	/* Horizontal bar */
	{"vb", SVERTBAR},	/* Vertical bar */
	{"ll", SLLCORNER},	/* Lower left corner */
	{NULL, 0},		/* End flag. */
};

/* Table of graphics characters initialized for ordinary CRT.
 */
symgraph graphtab[NSYMC];


/* Symbol names for the shell variable KEYMAPVAR
 * A command's index in this table should be one less
 * than the command code.
 */
labelv	cmdnames[] = {
	{"up", CGO_UP},
	{"do", CGO_DOWN},
	{"le", CGO_LEFT},
	{"ri", CGO_RIGHT},
	{"re", CREFRESH},
	{"un", CUNDO},
	{"cl", CCLRLINE},
	{"ws", CWRDSRCH},
	{"df", CDELF},
	{"db", CDELB},
	{"pr", CPREVBLOCK},
	{"ne", CNEXTBLOCK},
	{"ac", CACCEPT},
	{"ex", CEXECUTE},
	{"--", CINSERT},		/* Should not be in keymap var. */
	{"ta", CTRYALL},
	{"jc", CJUMPCMD},
	{NULL, 0},
};

/* Table of keystroke commands.
 * Self-insert commands are the default.
 * There can be several keystrokes that generate the same command.
 * To insert control chars, they must be quoted, see QUOTEC.
 * The table is terminated by an entry with c_seq == NULL.
 */
#define	QUOTEC	(CNTRL & 'Q')
keycmd	keycmdtab[100];
keycmd	*keycmdp;		/* Pointer to next free entry. */



/* Saved process control characters.
 */
struct	tchars	saved_tchars;


/* Buffer for termcap entry. */
#define TBUFSIZ 1024
char	buf[TBUFSIZ];
char	free_buf[1000], *fr;		/* Must be global, see tgetstr() */


/* Current terminal mode. */
int	termmode = -1;


/* Set up the terminal. This package now makes calls to both curses
 * and termcap subroutine packages, although the old code is used
 * for screen refresh.
 */
setup_term()
{
	printf("\n\nInitializing terminal ...");
	fflush(stdout);

	get_termstrs();
	get_genv();
	get_kenv();
	savetty();
	crmode();
	noecho();
	noflow();
	Puts(term_is);
	Puts(start_kp);
	enter_mode(SMNORMAL);

	printf(" done.\n");

	clrscreen();
}


/* Get keystroke characters, build keymap.
 * The earlier entries have priority, so fill them in from
 * the shell var, then add defaults from a string then termcap.
 */
get_kenv()
{
	char	*kenv;
	char	tcapstr[1000];

	keycmdp = keycmdtab;
	kenv = getenv(KEYMAPVAR);
	if (kenv != NULL)
	  	read_keymap(kenv);
	kenv_termcap(tcapstr);
	read_keymap(tcapstr);
	read_keymap(DKEYMAP);
}


/* Build a keymap string in the given buffer from the info
 * in the termcap file.
 * The string format is like: "up=\Eu:do=\033d".
 */
kenv_termcap(str)
char	*str;
{
	*str = NULL;

	if (term_up != NULL)  {
		strcat(str, cmdnames[CGO_UP - 1].label);
		strcat(str, "=");
		strcat(str, term_up);
		strcat(str, ":");
	}
	if (term_down != NULL)  {
		strcat(str, cmdnames[CGO_DOWN - 1].label);
		strcat(str, "=");
		strcat(str, term_down);
		strcat(str, ":");
	}
	if (term_left != NULL)  {
		strcat(str, cmdnames[CGO_LEFT - 1].label);
		strcat(str, "=");
		strcat(str, term_left);
		strcat(str, ":");
	}
	if (term_right != NULL)  {
		strcat(str, cmdnames[CGO_RIGHT - 1].label);
		strcat(str, "=");
		strcat(str, term_right);
		strcat(str, ":");
	}
	if (term_f1 != NULL)  {
		strcat(str, cmdnames[CPREVBLOCK - 1].label);
		strcat(str, "=");
		strcat(str, term_f1);
		strcat(str, ":");
	}
	if (term_f2 != NULL)  {
		strcat(str, cmdnames[CNEXTBLOCK - 1].label);
		strcat(str, "=");
		strcat(str, term_f2);
		strcat(str, ":");
	}
	if (term_f3 != NULL)  {
		strcat(str, cmdnames[CACCEPT - 1].label);
		strcat(str, "=");
		strcat(str, term_f3);
		strcat(str, ":");
	}
	if (term_f4 != NULL)  {
		strcat(str, cmdnames[CJUMPCMD - 1].label);
		strcat(str, "=");
		strcat(str, term_f4);
		strcat(str, ":");
	}
}



/* Add key bindings from the given string to the keycmdtab.
 */
read_keymap(var)
char	*var;
{
	int	cmd_code;

	while (*var != NULL)  {
		if (! read_varlabel(&var, cmdnames, &cmd_code))  {
			if (*var == NULL)
			  	break;
			disperr("Can't parse label in read_keymap.");
			exit(1);
		}

		keycmdp->c_code = cmd_code;
		if (! read_varval(&var, &(keycmdp->c_seq)))  {
			disperr("keymap value has bad format.");
			exit(1);
		}
		keycmdp++;
	}
}


/* Get graphics characters.
 * Set to defaults, then read changes from shell var (if any).
 */
get_genv()
{
	char	*genv;

	read_graphics(DGRAPHICS);
	genv = getenv(GRAPHICSVAR);
	if (genv == NULL)
	  	return;
	read_graphics(genv);
}


/* Read graphics map from the given string.
 */
read_graphics(var)
char	*var;
{
	int	sym_idx;

	while (*var != NULL)  {
		if (! read_varlabel(&var, symnames, &sym_idx))  {
			if (*var == NULL)
			  	break;
			disperr("Can't parse label in GRAPHICSMAP.");
			exit(1);
		}

		if ((var[0] != '\\') || (index(GVARMODES, var[1]) == 0))  {
			disperr("GRAPHICSMAP value has bad mode.");
			exit(1);
 		}
		sym_idx &= SYMBOLM;
		graphtab[sym_idx].s_mode = var[1] & CHARM;
		var++, var++;

		if (! read_varval(&var, &(graphtab[sym_idx].s_seq)))  {
			disperr("GRAPHICSMAP val has bad format.");
			exit(1);
		}
	}
}


/* Advance to the next label in strp, look the label up in the
 * labeltab, and set *valp to the value in the labeltab.
 * Return with *strp pointing after '=' that follows the label.
 * Return TRUE is parses ok, else return FALSE.
 * If string empty, return false and set *strp to point to a NULL.
 */
int read_varlabel(strp, labeltab, valp)
char	**strp;
labelv	*labeltab;
int	*valp;
{
	char	*str;
	labelv	*lp;

	for (str = *strp ; *str && index(VARSEP, *str) != 0 ; str++ );

	for (lp = labeltab ; lp->label != NULL ; lp++)  {
		if (substrp(str, lp->label))  {
			*valp = lp->value;
			str = index(str, '=');
			if (str == NULL)
			  	return(FALSE);
			str++;
			*strp = str;
			return(TRUE);
		}
	}
	*strp = str;
	return(FALSE);
}


/* Read a string value from a shell var string.
 * Return with *strp pointing after the string,
 * fill in valp with a pointer to a copy of the value string
 * on the heap.
 * Return TRUE if things go ok.
 */
int read_varval(strp, valp)
char	**strp;
char	**valp;
{
	char	buf[100], *bp;
	char	*var;

	var = *strp;
	bp = buf;
	while ((*var != NULL) && (index(VARTERM, *var) == NULL))  {
		*bp = read_slashed(&var);
	       	bp++;
        }
	*bp = 0;
	*valp = savestr(buf);
	*strp = var;
	return (TRUE);
}


/* Read the given (slashified) string and return a character.
 * Advance *strp over chars read.
 * Handle \\, \001, \n, \t, \r.
 * The symbol \E maps to escape (\033).
 * An unexpected char after the slash is just returned.
 */
int read_slashed(strp)
char	**strp;
{
	char	*str;
	char	c;

	str = *strp;
	if (*str != '\\')  {
		*strp = (*strp) + 1;
	  	return (*str);
	}
	str++;
	c = *str;
	switch (c)  {
	  default:
		if (index(DIGITS, c) == NULL)
		  	break;
		c -= '0';
		if (index(DIGITS, str[1]) == NULL)
		  	break;
		str++;
		c = (c * 8) + (*str - '0');
		if (index(DIGITS, str[1]) == NULL)
		  	break;
		str++;
		c = (c * 8) + (*str - '0');
		break;

	  case 'n':
		c = '\n';
		break;

	  case 't':
		c = '\t';
		break;

	  case 'E':
		c = '\033';
		break;

	  case 'f':
		c = '\f';
		break;

	  case 'r':
		c = '\r';
		break;
	}
	*strp = str + 1;
  	return (c);
}


/* Turn off flow control characters.
 */
noflow()
{
	struct	tchars	new_tchars;
	
	/* Turn off C-Q, C-S flow control. */
	if (ioctl(0, TIOCGETC, &saved_tchars) < 0)  {
		perror("noflow iocl get");
		exit(1);
	}
	new_tchars = saved_tchars;
	new_tchars.t_stopc = -1;
	new_tchars.t_startc = -1;
	if (ioctl(0, TIOCSETC, &new_tchars) < 0)  {
		perror("noflow iocl set");
		exit(1);
	}
}


/* Restore the flow control characters.
 */
restore_flow()
{
	if (ioctl(0, TIOCSETC, &saved_tchars) < 0)  {
		perror("restore_flow iocl set");
		exit(1);
	}
}


/* Read in the termcap strings.
 */
get_termstrs()
{
	char	*term;
	int	res;

	fr=free_buf;
	term = getenv("TERM");
	if (term == NULL)  {
		disperr("The shell variable TERM is not defined.");
		exit(1);
	}
	res = tgetent(buf,term);
	switch (res)  {
	  case -1:
		disperr("Can't open termcap file.");
		exit(1);

	  case 0:
		disperr("No termcap entry for your terminal.");
		exit(1);

	  default:
		break;
	}	       

	term_is = tgetstr("is", &fr);
	if (term_is == NULL)
	  	term_is = "";
	erase_eol = tgetstr("ce", &fr);
	erase_eos = tgetstr("cd", &fr);
	erase_scr = tgetstr("cl", &fr);
	start_so = tgetstr("so", &fr);
	end_so = tgetstr("se", &fr);
	start_alt = tgetstr("as", &fr);
	end_alt = tgetstr("ae", &fr);
	if (start_alt == 0  ||  end_alt == 0) {
		start_alt = "\033F";	       	/* VT100 default. */
		end_alt = "\033G";
	}
	cm = tgetstr("cm", &fr);		/* for cursor positioning */
	start_kp = tgetstr("ks", &fr);
	end_kp = tgetstr("ke", &fr);

	if ((term_is == NULL) || (erase_eol == NULL) ||
	    (erase_eos == NULL) || (erase_scr == NULL) ||
	    (cm == NULL) ||
	    (end_kp == NULL) || (start_kp == NULL) ||
	    (end_alt == NULL) || (start_alt == NULL) ||
	    (end_so == NULL) || (start_so == NULL) )  {
		disperr("A required termcap capability is missing.");
		disperr("\t one of: ce, cd, cl, so, se, cm, ks, ke.");
		exit(1);
	}

	/* Now get entries for keymap, NULL means no such entry.
	 */
	term_f1 = tgetstr("k1", &fr);
	term_f2 = tgetstr("k2", &fr);
	term_f3 = tgetstr("k3", &fr);
	term_f4 = tgetstr("k4", &fr);
	term_up = tgetstr("ku", &fr);
	term_down = tgetstr("kd", &fr);
	term_left = tgetstr("kl", &fr);
	term_right = tgetstr("kr", &fr);
}


/* Restore the terminal to its original mode.
 */
unset_term()
{
	enter_mode(SMNORMAL);
	Puts(end_kp);		/* Can't tell if this is original. */
	fflush(stdout);
	nocrmode();
	echo();
	restore_flow();
	resetty();
}


/* Return the symbol used to display the given char in the
 * decryption window.
 */
int  char2sym(pchar)
int	pchar;
{
 	int	gchar;

	if (printable(pchar))		{gchar = pchar;}
	else if (pchar == -1) 		{gchar = SUNKNOWN;}
	else if (notascii(pchar))	{gchar = SNOTASCII;}
	else if (pchar == '\n')		{gchar = SLINEFEED;}
	else if (pchar == '\r')		{gchar = SCARETURN;}
	else if (pchar == '\f')		{gchar = SFORMFEED;}
	else if (pchar == '\t')		{gchar = STAB;}
	else				{gchar = SCONTCODE;}
	return (gchar);
}


/* Displays the given symbol on the terminal.  Handles
 * entering and exiting graphics or standout mode.
 */
putsym(symbol)
int	symbol;
{
	int		symcode;
	symgraph	*gp;

	if (! graphic(symbol))  {
		enter_mode(SMNORMAL);
		putchar(symbol & CHARM);
		return;
	}
	symcode = symbol & SYMBOLM;
	if (symcode >= NSYMC)  {
		disperr("Bad symbol code in putsym.");
		return;
	}
	gp = &graphtab[symcode];
	enter_mode(gp->s_mode);
	Puts(gp->s_seq);
}


/* Enter a particular mode.  If necessary send escape sequence
 * to the terminal.  Handle terminating the previous mode.
 */
enter_mode(mode)
int	mode;
{
	if (termmode == mode)
	  	return;

	switch (termmode)  {
	  case SMNORMAL:
		break;

	  case SMGRAPHIC:
		Puts(end_alt);
		break;

	  case SMSTANDOUT:
		Puts(end_so);
		break;

	  default:
		Puts(end_so);
		Puts(end_alt);
		break;
	}

	termmode = mode;

	switch (termmode)  {
	  case SMNORMAL:
		break;

	  case SMGRAPHIC:
		Puts(start_alt);
		break;

	  case SMSTANDOUT:
		Puts(start_so);
		break;

	  default:
		disperr("Bad terminal mode.");
		break;
	}
}


/* Return values from srch_ktab().
 */
#define SK_SUBSTR	-1
#define SK_NOMATCH	-2


/* Search the key command table for the given keystroke.
 * If no match, return SK_NOMATCH (which is < 0).
 * If the keystroke is the prefix for one or more commands,
 * return SK_SUBSTR (which is < 0).
 * If an exact match is found, return the index ( >= 0) of
 * the entry that matched.
 */
int srch_ktab(ktab, stroke)
keycmd	ktab[];
char	*stroke;
{
	int	i;
	int	nsubstr = 0;		/* Number of close entries */

	for (i = 0 ; ktab[i].c_seq != NULL ; i++)  {
		if (strcmp(ktab[i].c_seq, stroke) == 0)
		  	return(i);
		if (substrp(ktab[i].c_seq, stroke))
		  	nsubstr++;
	}
	if (nsubstr > 0)
	  	return (SK_SUBSTR);
	return (SK_NOMATCH);
}


/* Return TRUE if the model string starts with the given string.
 * Return false if strlen(given) > strlen(model).
 */
int substrp(model, given)
char	*model;
char	*given;
{
	for ( ; (*model != 0) && (*given != 0) ; model++, given++)  {
		if (*model != *given)
		  	return (FALSE);
	}
	if (*given == 0)
	  	return (TRUE);
	return (FALSE);
}


/* Read a keystroke from stdin and return the command for it.
 *
 * Single character keystrokes not found in the table generate
 * self-insert commands.
 * Control characters other than \n, and \t must be quoted in order
 * to generate self-insert commands.  To quote a char, preceed it
 * by the QUOTEC character.
 * Multi character keystrokes should end in an exact match.  If not,
 * throw away the char that caused no matches in the keycmdtab,
 * beep the terminal, and start over.
 */
int getcmd()
{
	char	keystroke[10];		/* Chars in keystroke. */
	int	nchars;			/* Length of keystroke[]. */
	int	c;
	int	index;			/* Cmd index in keycmdtab. */
	int	code;			/* Cmd code. */

  start_over:
	nchars = 0;
	keystroke[0] = 0;

	while (TRUE)  {
		c = getchar();
		keystroke[nchars++] = c;
		keystroke[nchars] = 0;
		index = srch_ktab(keycmdtab, keystroke);
		switch (index)  {
		  case SK_SUBSTR:
			continue;

		  case SK_NOMATCH:
			if (nchars != 1)  {
				beep();
				goto start_over;
			}
			code = CINSERT;
			if (c == QUOTEC)  {
				c = getchar();
				break;
			}
			else if (printable(c))  {
				break;
			}
			else if ((c != LINEFEED) && (c != TAB))  {
				beep();
				goto start_over;
			}
			break;
			
		  default:
			if (index < 0)  {
				disperr("Bad keycmdtab index.");
			}
			code = keycmdtab[index].c_code;
			break;
		}
	return ((code << CMDSHIFT) | (c & CHARM));
	}
}


/* Cause the terminal to beep.
 */
beep()
{
	Puts("\007");			/* C-G */
}


/* Save a copy of the given string on the heap.
 * Return pointer to copy.
 */
char *savestr(s)
register char	*s;
{
	char	 *p;
	register char	*t;

	if (s == NULL)
		return( NULL );
	t = p = (char*) calloc(strlen(s) + 1, 1);
	if (t == NULL)
		return(NULL);
	while (*t++ = *s++);
	return(p); 
}
