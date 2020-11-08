/* Modified by KeresztG <keresztg@podolin.piar.hu> for NASM 	1998 August */
#include <curses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#define C_COMMENTS 1 /* comment out to disable check for C style comments */

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE -1
#endif

int debug_level=0;
#define used_stderr stderr
int tasm_syntax=0;
int use_bss=0;

int text_def=0, text_in=0;
int data_def=0, data_in=0;
int bss_def=0, bss_in=0;

void debug(int level, const char *fmt, ...)
{
  va_list args;
  if (level>debug_level) return;
  va_start(args, fmt);
  vfprintf(used_stderr, fmt, args);
  va_end(args);
}

/* function to replace dots in strings with underscores. this is because 
   as allows dots but tasm doesn't */
void undot (char *victim) {
	do {
		if (*victim == '.') {
			*victim = '_';
			debug (2,"{X.}");
		}
	} while (*(victim++) != '\0');
}

/* stupid functions to test whether a string is numeric, hex, or other */
/* this may not work if the system is using something other than ascii */

int ischarnum (char chr) {
	if (chr >= '0' && chr <= '9') {
		return TRUE;
	}
	if (chr == '-') return TRUE;
	return FALSE;
}

int ischarhexy (char chr) {
	if (ischarnum (chr) || (chr >= 'A' && chr <= 'F') || (chr >= 'a' && chr <= 'f')) {
		return TRUE;
	}
	return FALSE;
}

int isstrnum (char *str) {
	while (*str != '\0') {
		if (!ischarnum (*str)) return FALSE;
		++str;
	}
	return TRUE;
}

/* is str a hex number in the format 0xdddd... */
int isstrhex (char *str) {
	if (*str != '0') return FALSE;
	++str;
	if (*str != 'x') return FALSE;
	++str;
	while (*str != '\0') {
		if (!ischarhexy (*str)) return FALSE;
		++str;
	}
	return TRUE;
}



#define MAX_SYMBOL_LEN 80

/*void */

/* SEGREG:IMM(BASE,INDEX,SCALE)
   SEGREG:[IMM + BASE + INDEX*SCALE]
exception:
   IMM(,1) is [IMM] */

char read_symbol (char *symbol, FILE *in, int limit, char stopon, char stopon2, int ignore_white_space) {
	do {
		*symbol = fgetc (in);
	} while (*symbol == ' ' || *symbol == 9);


	while (*symbol != '\n' && (ignore_white_space || (*symbol != '\0' && *symbol != ' ' && *symbol != 9)) && *symbol != EOF && limit != 1) {
		if (*symbol == stopon || *symbol == stopon2) break;
			/* that last line is there because we need to consider
			   all the special things that a comma can mean.
			   It can separate operands or it can be used in
			   memory references... */
		symbol++;
		limit --;
		*symbol = fgetc (in);
	}
	/* 9 is tab */

	if (limit == 1) {
		puts ("symbol too long");
	}
	limit = *symbol;
	*symbol = '\0';
	if (limit == '\0' || limit == 9) limit = ' ';
	return (char) limit;
}


void translate_number (char *out, char *in) {
	int hexify = 0;
	*out = '\0';

	while (*in != '\0' && *in != '\n') {	/* \n is paranoia, I guess */
		if (*in == 'x') {
			hexify = TRUE;
		} else {
			*out = *in;
			++out;
		}
		++in;
	}
	if (hexify) {
		*out = 'h';
		++out;
	}
	*out = '\0';
}


/* the following procedure will either transfer immediately to a different cat
   or it will concatenate the translation of an immediate value in *in to *out
   ie $4 or 0x29 or _label, which would be translated to 4, 029h, and _label */
/* it wil also handle a reg - ie %ebx would be ebx, %st would be st, %bob would
   be bob. there is no justification for this, I can only say that this 
   function is a little misnamed */
/* it returns true if it parsed something that calls for an instruction suffix
   to be removed. this includes:
   registers, but not %st. NOT!!! NOT NOT NOT NOT NOT!!! N-O-T!!! it turned
   out that it would always return true then, so we used the return value
   for something useful */
/* will return whether it encountered a register, a constant, or an unknown,
   ie a label */
#define REGISTER 1 /* these are pretty much arbitrary, they are used to figure */
#define CONSTANT 2 /* out when a (D)WORD/BYTE PTR is needed. I wonder what */
#define UNKNOWN 4  /* to do about QWORD? Maybe it's not really necessary to */
#define MEMORY 8   /* worry about it... */
#define FPUSTACK 16
#define OFFSET 32 /* this is only needed because tasm 
                     compiles push dword ptr (offset label) to 
                     push dword ptr [label] (b*tch!!!)
                     NASM really is better, I guess... */
int transcat_immediate (char *out, char *in) {
	if (in[0] == '\0') return 0;
	if (in[0] == '%') {
		debug (2,"{reg/st(0)}");
		strcat (out, &in[1]);
		if (in[1]=='s' && in[2]=='t' && !tasm_syntax) strcat(out,"0");
		return REGISTER;
	}
	if (in[0] == '$') {
		debug (2,"{arg! imm or offset?}");
		if (isstrnum (&in[1]) || isstrhex (&in[1])) {
			debug (2,"{imm!}");
			while (*out) ++out; /* to perform concatenation */
			translate_number (out, &in[1]);
			return CONSTANT;
		} else {
			debug (2,"{offset?}");
			undot (&in[1]);
			if (tasm_syntax) strcat (out, "(offset ");
			else strcat (out, "DWORD ");
			strcat (out, &in[1]);
			if (tasm_syntax) strcat (out, ")");
			return OFFSET;
		}
	} else {
		debug (2,"{imm?}");
		if (isstrnum (in) || isstrhex (in)) {
			debug (2,"{imm!}");
			while (*out) ++out; /* "" */ 
			translate_number (out, in);
			return CONSTANT;
		} else {
			debug (2,"{Eh?}");
			undot (in);
			if (*in == '*') {
				debug (2,"{'*' delimited absolute call/jump}"); 
				/* strcat (out, &in[1]); */
				return transcat_immediate (out, &in[1]);
			} else {
				strcat (out, in);
			}
			return UNKNOWN;  /* btw, it's probably a label */
		}
	}
}


/* 1st symbol is opcode (or label or preprocessor directive)
   then read symbols into array
   check for comma 
   ...
   ...
   ...
   (check for coma) */
   

#define MAX_SYMBOLS 10

/* SEGREG:IMM(BASE,INDEX,SCALE)
   SEGREG:[IMM + BASE + INDEX*SCALE]
exception:
   IMM(,1) is [IMM] */

/* reasons for stripping instruction suffix */
/* bits in a bitfield, so they are powers of two */
#define MEMREF 1
#define OTHER 2

/* the word divine is used here as a verb, as in the process of divination */
/* returns true if there are more arguments to be had on this line */
/* argtype will basically store what transcat_immediate returns, or MEMORY,
   or FPUSTACK */
int divine_arg (char *intelarg, FILE *in, char *command, char delim, char suffix, int *argtype, int *stripped) {
	char symbols[MAX_SYMBOLS][MAX_SYMBOL_LEN];
	char delim_array[MAX_SYMBOLS];
	int n;
	char *tmp = "\0\0";
	int cur_symbol = 0;
	int moreoperands = FALSE;
	int stripit = 0; /* whether to strip instruction suffixes */
	char tmp1;

	*intelarg = '\0';


	do {
		delim_array[cur_symbol] = read_symbol (symbols[cur_symbol], in, MAX_SYMBOL_LEN, ',', '(', TRUE);
		debug (2,"%s%c", symbols[cur_symbol], delim_array[cur_symbol]);

		if (delim_array[cur_symbol] == '(') {
			debug (2,"{ fpu op or memory ref }");
			if (!strcmp ("%st", symbols[cur_symbol])) {
				debug (2,"{FPU!}");
				strcat (intelarg, " st");
				if (tasm_syntax) strcat(intelarg, "(");
				do {
					*tmp = fgetc (in);
					if (tasm_syntax) strcat (intelarg, tmp);
					else if (*tmp != ')') strcat (intelarg, tmp);
				} while (*tmp != ')');
				*argtype = FPUSTACK;
			} else {
				debug (2,"{Ref! :");
				stripit |= MEMREF;
				*argtype = MEMORY;

				/* look for segreg */
				n = 0;
				while (symbols[cur_symbol][n] != '\0' && symbols[cur_symbol][n] != ':') ++n;
				if (symbols[cur_symbol][n] == ':') { /* segreg! */
					debug(2, "S");
					symbols[cur_symbol][n] = '\0'; /* split string into segreg and imm */
					transcat_immediate (intelarg, symbols[cur_symbol]);
					strcat (intelarg, ":[");
					++n;
				} else {
					n = 0; /* no segreg, so imm will start
					          at [0] of string, if at all */
					strcat (intelarg, "[");
				}
				if (symbols[cur_symbol][n] != '\0') { /* displacement! */
					debug (2,"d");
					transcat_immediate (intelarg, &symbols[cur_symbol][n]);
				}
				
				/* next we shall read BASE */
				/* ++cur_symbol;
				if (cur_symbol == MAX_SYMBOLS) {
					fprintf (used_stderr, "FATAL: ran out of symbol space while trying to do something!");
					exit (255);
				} */
				
				delim_array[cur_symbol] = read_symbol (symbols[cur_symbol], in, MAX_SYMBOL_LEN, ',', ')', TRUE);
				
				if (symbols[cur_symbol][0] != '\0') { /* BASE! */
					debug(2, "b");
					if (intelarg[strlen (intelarg) - 1] != '[') strcat (intelarg, "+");
					transcat_immediate (intelarg, symbols[cur_symbol]);
				}
				
				++cur_symbol;
				if (cur_symbol == MAX_SYMBOLS) {
					fprintf (used_stderr, "FATAL: ran out of symbol space while trying to do something!");
					exit (255);
				}

				/* try to read INDEX */
				delim_array[cur_symbol] = read_symbol (symbols[cur_symbol], in, MAX_SYMBOL_LEN, ',', ')', TRUE);

				if (delim_array[cur_symbol - 1] == ')') {
					debug (2,"}");
					strcat (intelarg, "]");
				} else {
					if (symbols[cur_symbol] != '\0' && !!strcmp(symbols[cur_symbol], "1")) { /* INDEX */
						debug(2, "i");
						if (intelarg[strlen (intelarg) - 1] != '[') strcat (intelarg, "+");
						transcat_immediate (intelarg, symbols[cur_symbol]);
					}

					if (delim_array[cur_symbol] == ')') {
						debug (2,"}");
						strcat (intelarg, "]");
					} else {
						/* try to read SCALE */
						debug (2,"s");
						delim_array[cur_symbol] = read_symbol (symbols[cur_symbol], in, MAX_SYMBOL_LEN, ',', ')', TRUE);
						if (symbols[cur_symbol][0] == '\0') debug (2,"I've broken it!!!");
						strcat (intelarg, "*");
						transcat_immediate (intelarg, symbols[cur_symbol]);
						strcat (intelarg, "]");
					}
				}
			}
		}
		
		if (delim_array[cur_symbol] == ',') {
			debug (2,"{Next Operand}");
			moreoperands = TRUE;
			++cur_symbol;	
			break;
		}

		++cur_symbol;

	} while (delim_array[cur_symbol - 1] != '\n' && delim_array[cur_symbol - 1] != EOF && cur_symbol < MAX_SYMBOLS);

	if (cur_symbol == MAX_SYMBOLS) {
		puts ("Too many arguments in line!");
	}

	/* check for immediate numerical constant or immediate label offset */
	/* an offset is apparently preceded by a '$', ie the offset of */
	/* Label: is $Label */
	/* Note that if cur_symbol != 1 then the arguments have already been
	   dealt with */

	if (cur_symbol == 1) { /* this means only 1 or 0 symbol(s) were found, I hope */
		debug (2,"{1 s.}");
		stripit |= OTHER;
		*argtype = transcat_immediate (intelarg, symbols[0]);
		if (!strcmp ("%st", symbols[0])) {
			stripit &= MEMREF;
			*argtype = FPUSTACK;
		}
	}

	if (!strcmp ("call", command)) stripit = 0;
	tmp1  = command[strlen (command) - 1];
	/* if (tmp1 != 'b' && tmp1 != 'w' && tmp1 != 'l' && !((tmp1 == 't' || tmp1 == 's') && command[0] == 'f') ) stripit = 0; */
	if (command[0] == 'j' || \
	  (tmp1 != 'l' && \
	 !(command[0] == 'f' && (tmp1 == 't' || tmp1 == 's')) && \
	 !(command[0] != 'f' && (tmp1 == 'b' || tmp1 == 'w')) && \
	 !(command[0] == 'f' && command[1] == 'i' && tmp1 == 'w'))) stripit = 0;
	if (stripit && !*stripped) {
		command[strlen (command) - 1] = '\0';
		*stripped = TRUE;
	}

	return moreoperands;
}

/* firstchar is the first character of the instruction. This is needed to
   distinguish FPU instructions from other instructions since FPU instrucion
   suffixes mean different things than the suffixes on normal instructions */
void print_ptr (FILE *out, char suffix, char *command) {
	if (command[0] == 'f' && command[1] != 'i') {
		switch (suffix) {
			case 't':
				if (!tasm_syntax) fprintf (out, "TWORD ");
				else fprintf (out, "TBYTE PTR ");
				break;
			case 'l':
				fprintf (out, "QWORD ");
                if (tasm_syntax) fprintf(out, "PTR ");
				break;
			case 's':
				fprintf (out, "DWORD ");
				if (tasm_syntax) fprintf(out, "PTR ");
				break;
			default:
			debug (2,"Shut up, Mr. Gumby!!!");
		}
	} else {
		switch (suffix) {
			case 'l':
				fprintf (out, "DWORD ");
				if (tasm_syntax) fprintf(out, "PTR ");
				break;
			case 'w':
				fprintf (out, "WORD ");
				if (tasm_syntax) fprintf(out, "PTR ");
				break;
			case 'b':
				fprintf (out, "BYTE ");
				if (tasm_syntax) fprintf(out, "PTR ");
				break;
			default:
				/* a jmp table, ie switch/case will use this, I think */
				/* I have no idea if gcc would ever generate a
				   call with a memory reference (ie "call [ebx]"), it would be
				   kind of demented, I think... */
				if (command[0] == 'j' || !strcmp (command, "call")) {
					fprintf (out, "DWORD ");
					if (tasm_syntax) fprintf(out, "PTR ");
					/* Depending on what the hell you're
					   doing, this could end up being
					   *very* wrong! */
				} else {
					debug (2,"I've got my head stuck in the cupboard again!!!");
				}
		}
	}
}

void do_ptr (FILE *out, char *command, char suffix, int argtype) {
	switch (argtype) {
		case MEMORY:
			print_ptr (out, suffix, command);
			break;
		case REGISTER:
			break;
		case OFFSET:
			/*if (!!strcmp (command, "push")) {
				print_ptr (out, suffix, command);
			}*/
			break;
		case CONSTANT:
			if (!strcmp (command, "push")) {
				print_ptr (out, suffix, command);
			}
			break;
		case UNKNOWN:
			if (!strcmp (command, "call") || *command == 'j') {
				break;
			}
			print_ptr (out, suffix, command);
			break;
		case FPUSTACK:
			break;
		default:
			debug (2,"*CRACK* *CRASH* *Open the door* and come in!!!");
	}
}






int parse_line (FILE *in, FILE *out) {
	char symbol1[MAX_SYMBOL_LEN];
	int stripped=0;
	char delimiter;
	char tmp;
	int tmpoctal;
	int tmpalign;
	int alignout;
	int comtm; /* comment searching temp integer */
	int n;

	char ptrsize[3];
	/* two characters representing the dword/word/byte ptr ripped directly
	   from the AT&T instruction. with something like movsbw they'll be
	   {'b','w'} with a movb they'll be {'b','b'} 
	   there are really only two significant characters there. the 3rd
	   is always a '\0' so the array is a valid asciiz string which will
	   allow it to be used with strcat*/

	char args[3][MAX_SYMBOL_LEN]; /* 2 or 3 parameters */
	/* strings containing the Intel syntax operands, derived from the
	   AT&T source, in AT&T order */

	int argtypes[3] = {0, 0, 0};
	/* integers indicating the type of argument, ie MEMORY, FPUSTACK
	   in the same order as args[][] */

	/* Init */
	args[0][0] = '\0';
	args[1][0] = '\0';
	args[2][0] = '\0';

	/* no special characters to check for, thus "0, 0" */
	/* read preproc directive, instruction, label, or comment */
	delimiter = read_symbol (symbol1, in, MAX_SYMBOL_LEN, 0, 0, FALSE);
        debug (2,"%s%c", symbol1, delimiter);

        /* check for comments first */
	/* do this later, since gcc doesn't generate comments */

	/* C style comments checked for by request */
#ifdef C_COMMENTS
	tmp = '\0';
	for (comtm = 0; comtm < MAX_SYMBOL_LEN; ++comtm) if (symbol1 [comtm] == '/' && symbol1 [comtm + 1] == '*') {
		do {
			while (tmp != '*') {
				tmp = fgetc (in);
				if (tmp == EOF) {
					debug (2, "\nNeverending comment encountered!"); exit (1);
				}
			}
			tmp = fgetc (in);
			if (tmp == EOF) {
				debug (2,"The comment never ends!"); exit (1);
			}
		} while (tmp != '/');
		return -1;
	}
#endif
					

        /* check for... hm... let's see... label */
        if (symbol1 [strlen (symbol1) - 1] == ':') {
		undot (symbol1);
				fprintf (out, "%s\t; basic label\n", symbol1);
		return -1;
        }

	/* if (symbol1[0] == '/') {
		fprintf (out, " ; %s\n", symbol1);
		return -1;
	} */

	/* check for preprocessor directives here */
	if (symbol1[0] == '.' || symbol1[0] == '/') {
		debug (2,"{preproc}");
		if (!strcmp (".ascii", symbol1)) {
			fprintf (out, "db ");
			debug (2,"{.ascii, copy the rest as db}\n");
			do {
				tmp = fgetc (in);
				sim_fgetc:;
				if (tmp == '\\') {
					tmp = fgetc (in);
					if (tmp == 'n'){
						fputc ('"', out);
						fprintf (out, ", 0Dh, 0Ah, ");
						fputc ('"', out);
					} else if (tmp == '\\') {
						fputc ('\\', out);
					} else if (tmp >= '0' && tmp <= '9') {
						tmpoctal = 0;
						while (tmp >= '0' && tmp <= '9') {
							tmpoctal *= 8;
							if (tmp == '8') {
								tmpoctal += 10;
							} else if (tmp == '9') {
								tmpoctal += 11;
							} else {
								tmpoctal += (tmp - '0');
							}
							tmp = fgetc (in);
						}
						fputc ('"', out);
						if (tmpoctal == 10) { /* new line */
							fprintf (out, ", 0Dh, 0Ah, ");
						} else {
							fprintf (out, ", %d, ", tmpoctal);
						}
						fputc ('"', out);
						goto sim_fgetc; /* Aaargh! I'm sorry!!! :) */
					} else {
						fprintf (out, "\\%c", tmp);
						debug (2,"I don't get it!");
					}
				} else {
					fputc (tmp, out);
				}
			} while (tmp != '\n' && tmp != EOF);
			return -1;
		}
		if (!strcmp (".long", symbol1) || !strcmp (".int", symbol1) || !strcmp(".byte", symbol1) || !strcmp(".word", symbol1)) {
			debug (2,"{list of numbers}");
			if (!strcmp (".byte", symbol1)) fprintf (out, "db ");
			if (!strcmp (".word", symbol1)) fprintf (out, "dw ");
			if (!strcmp (".long", symbol1) || !strcmp (".int", symbol1)) fprintf (out, "dd ");
			do {
				tmp = divine_arg (args[0], in, symbol1, delimiter, '\0', argtypes, &stripped);
				fprintf (out, " %s", args[0]);
				if (tmp) fprintf (out, ",");
			} while (tmp);
			fprintf (out, "\n");
			return -1;
		}
		if (!strcmp (".comm", symbol1)) {
			debug (2,"{.comm to dup(?)}");
			tmp = divine_arg (args[0], in, symbol1, delimiter, '\0', argtypes, &stripped);
			if (!tmp) debug (2,"{BROKEN .COMM!!!}");
			tmp = divine_arg (args[1], in, symbol1, delimiter, '\0', argtypes, &stripped);
			if (tmp) debug (2,"{MILDLY DEFECTIVE .COMM!!!}");
			if (!tasm_syntax)
			{
				if (!bss_in && use_bss)
				{
					fprintf (out, "segment _BSS ");
					if (!bss_def++) fprintf (out, "para public use32 align=4 class=BSS");
					fprintf(out,"\n");
					text_in=0; data_in=0; bss_in++;
				}
				fprintf (out, "global %s\n%s  resb %s\n", args[0], args[0], args[1]);
			}
			else fprintf (out, "public %s\n%s  db %s dup(?)\n", args[0], args[0], args[1]);
			return -1;
		}
		if (!strcmp (".lcomm", symbol1)) {
			debug (2,"{.lcomm to dup(?)}");
			tmp = divine_arg (args[0], in, symbol1, delimiter, '\0', argtypes, &stripped);
			if (!tmp) debug (2,"{BROKEN .LCOMM!!!}");
			tmp = divine_arg (args[1], in, symbol1, delimiter, '\0', argtypes, &stripped);
			if (tmp) debug (2,"{MILDLY DEFECTIVE .LCOMM!!!}");
			if (!tasm_syntax)
			{
				if (!bss_in && use_bss)
				{
					fprintf (out, "segment _BSS ");
					if (!bss_def++) fprintf (out, "para public use32 align=4 class=BSS");
					fprintf(out,"\n");
					text_in=0; data_in=0; bss_in++;
				}
				fprintf (out, "%s  resb %s\n", args[0], args[1]);
			}
			else fprintf (out, "%s  db %s dup(?)\n", args[0], args[1]);
			return -1;
		}
		if (!strcmp (".globl", symbol1)) {
			debug (2,"{.globl}");
			tmp = divine_arg (args[0], in, symbol1, delimiter, '\0', argtypes, &stripped);
			if (!tmp) debug (2,"{dead!}");
			if (!tasm_syntax) fprintf (out, "global %s\n", args[0]);
			else fprintf (out, "public %s\n", args[0]);
			return -1;
		}
		if (!strcmp (".align", symbol1)) {
			debug (2,"{align}");
			tmp = divine_arg (args[0], in, symbol1, delimiter, '\0', argtypes, &stripped);
			if (tmp) while ((tmp = fgetc (in)) != '\n');
			sscanf (args[0], "%d", &tmpalign);
			alignout = (int) pow ((double) tmpalign, 2);
			fprintf (out, "\nalign %d\n", alignout);
			/*fprintf (out, "\nalign %s\n", args[0]);*/
			return -1;
		}

		if (!tasm_syntax)
		{
			if (!strcmp (".text", symbol1)) {
				debug (2,"{section code start}");
				fprintf (out, "segment _TEXT ");
				if (!text_def++) fprintf (out, "para public use32 class=CODE");
				fprintf(out,"\n");
				text_in++; data_in=0; bss_in=0;
				return -1;
			}
			if (!strcmp (".data", symbol1)) {
				debug (2,"{section data start}");
				fprintf (out, "segment _DATA ");
				if (!data_def++) fprintf (out, "para public use32 align=4 class=DATA");
				fprintf(out,"\n");
				text_in=0; data_in++; bss_in=0;
				return -1;
			}
			if (!strcmp (".bss", symbol1)) {
				debug (2,"{section bss start}");
				fprintf (out, "segment _BSS ");
				if (!bss_def++) fprintf (out, "para public use32 align=4 class=BSS");
				fprintf(out,"\n");
				text_in=0; data_in=0; bss_in++;
				return -1;
			}
		}
			/* This is not the way it's going to work. everything
			   will be in the Code32 segment and parse_file will put
			   that in. */
			/* Why? I think it's better to have separate segments.
			   We may need them... (KeresztG) */
			/* Geee. I got it why you wanted to put it in parse_file...
			   I can only say: stupid TASM syntax! (KeresztG) */
		debug (2,"{??? trash comments}\n");
		fprintf (out, " ; %s%c", symbol1, delimiter);
		tmp = delimiter;
		while (tmp != '\n' && tmp != EOF) {
			tmp = fgetc (in);
			fputc (tmp, out);
		}
		if (tmp == EOF) return FALSE;
		return TRUE;
	}
	/* ... */


	/* change retarded instructions that are named differently in at&t */
	if (!strcmp ("cbtw", symbol1)) {
		strcpy (symbol1, "cbw");
	} else if (!strcmp ("cwtl", symbol1)) {
		strcpy (symbol1, "cwde");
	} else if (!strcmp ("cwtd", symbol1)) {
		strcpy (symbol1, "cwd");
	} else if (!strcmp ("cltd", symbol1)) {
		strcpy (symbol1, "cdq");
	} else if (!strcmp ("movsl", symbol1)) {
		strcpy (symbol1, "movsd");
	}
	/* other retarded instructions such as movsbw and ljmp are not
	     included here because they take parameters and are dealt with
	     later */


	/* obviously there won't be any arguments if it's an EOL, so quit */
	if (delimiter == '\n') {
		fprintf (out, "%s\n", symbol1);
                debug(2, ";EOL");
		return -1;
	}


	/* set the memory reference size character array information bearing
	   support structure to contain the appropriate bytes as they relate
	   to the memory reference data type size in the current instruction */
	/* and stuff */
	if (!strcmp ("lcall", symbol1)) {
		strcpy (symbol1, "call far ");
	} else if (!strcmp ("ljmp", symbol1)) {
		strcpy (symbol1, "jmp far ");
	} else if (!strcmp ("movsbw", symbol1) || !strcmp ("movsbl", symbol1) \
		|| !strcmp ("movswl", symbol1) || !strcmp ("movzbw", symbol1) \
		|| !strcmp ("movzbl", symbol1) || !strcmp ("movzwl", symbol1))
	{
		strcpy (ptrsize, &symbol1[4]);
		strcpy (&symbol1[4], "x");
	} else {
		ptrsize[0] = ptrsize[1] = ptrsize[2] = symbol1[strlen (symbol1) - 1];
	}

	/* parse arguments. divine_arg will also take the liberty of stripping
	   memory reference size indicator characters, such as the 'b' in
	   movb. I think this is a valid place to do it because the suffix
	   isn't part of the instruction, as far as I'm concerned. That
	   information goes with arguments. */
	n = 0;
	while (divine_arg (args[n], in, symbol1, delimiter, ptrsize[n], &argtypes[n], &stripped)) {
		++n;
		if (n == 3) {
			fprintf (used_stderr, "FATAL: n == 3 at line %d",__LINE__);
			exit (100);
		}
	}


	/* print arguments. Really check for exceptions first. (see above) */

	fprintf (out, "%s ", symbol1);

	/* Here we do some check whether we need square brackets around the
	   operand. This is done by checking the symbol1 and the argtypes[n],
	   so it _may_ weak in some cases!!! (KeresztG)*/
	for (n = 2; n >= 0; --n) {
		if (args[n][0] != '\0') {
			fputc (9, out); /* tab */
			if (!tasm_syntax)
			{
				if (!strcmp(symbol1,"lea"))	/* LEA does not need do_ptr (KeresztG) */
				{
					if (argtypes[n]==UNKNOWN) fprintf (out, "[%s]", args[n]);
					else fprintf (out, "%s", args[n]);
				}
				else
				{
					do_ptr (out, symbol1, ptrsize[n], argtypes[n]);
					/* Now we checks that ops which don't need square brackets.
					   (More logical... Thanks Hmaon!   (KeresztG) */
					if (argtypes[n]==UNKNOWN)
					{
						if (!strncmp(symbol1,"call",4)||
							symbol1[0]=='j')
						{ 
					             fprintf (out, "%s", args[n]);
						     debug (2, "{op based check... bad juju}");
					        }
						else fprintf (out, "[%s]", args[n]);
					}
					else fprintf (out, "%s", args[n]);
				}
			}
			else
			{
				do_ptr (out, symbol1, ptrsize[n], argtypes[n]);
				fprintf (out, "%s", args[n]);
			}
			if (n > 0) fputc (',', out);
		}
	}
	fprintf (out, "\n");

#if 0
	if (args[1][0] == '\0') {
		fputc (9, out); /* tab */
		do_ptr (out, symbol1, ptrsize[0], argtypes[0]);
		fprintf (out, "%s\n", args[0]);
	} else {
		fputc (9, out); /* tab */
		do_ptr (out, symbol1, ptrsize[1], argtypes[1]);
		fprintf (out, "%s,", args[1]);
		do_ptr (out, symbol1, ptrsize[0], argtypes[0]);
		fprintf (out, "%s\n", args[0]);
	}
#endif

	if (delimiter == EOF) return 0;
	debug (2,"{nothing more?}\n");
	return -1;
}


void parse_file (FILE *in, FILE *out) {
	if (tasm_syntax)
	{
		fprintf (out, ".386p\n");
		fprintf (out, "Code32 segment para public use32\n");
		fprintf (out, "assume cs:Code32, ds:Code32\n");
	}
	while (parse_line (in, out)) {
		asm ("nop;");
	};
	if (tasm_syntax)
	{
		fprintf (out, "Code32 ends\n");
		fprintf (out, "end\n");
	}
	else
	{
		if (data_def || bss_def)
		{
			fprintf (out, "group DGROUP ");
			if (data_def) fprintf (out, "_DATA ");
			if (bss_def) fprintf (out, "_BSS ");
			fprintf(out, "\n");
		}
	}
}
