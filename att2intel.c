/* Modified by KeresztG <keresztg@podolin.piar.hu> for NASM 	1998 August */

const char *message=R"(
Att2Intl v0.2.1
(c) Greg Velichansky (Hmaon / Xylem)
Distributed under the GNU General Public License (see the file 'copying')
See http://www.wam.umd.edu/~gvelicha/a2i/
Read the file 'notes.'
)";

#include <curses.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
// #include <process.h>
#include <unistd.h>
#include <ctype.h>
#include "translate.h"

#define TMPFILENAME "a2itmpfl.asm"
#define used_stderr stderr

int doing_again_extern=0;

/* Following things comes for eliminating duplicated externs (KeresztG)*/
#define GROW_MEM_WITH 1000
int extNo=0,maxextNo=2000;
char **exts;

int doWeHave(char *inp)
{
	int i;
	for (i=0;i<extNo;i++)
	{
		if (!strcmp(inp,exts[i])) return(1);
	}
	return(0);
}
/* End of deduplicator things */

char *extension (char *filename) {
	if (filename == NULL) return NULL;

	while (*filename != '.' && filename != '\0') {
		++filename;
	}

	if (*filename == '.') {
		++filename;
	}

	return filename;
}

void copybasename (char *dest, char *filename) {
	char *extptr;

	extptr = extension (filename);
	while (filename != extptr) {
		*dest = *filename;
		++dest;
		++filename;
	}
	*dest = '\0';
}

/* This gets the next short jump error of NASM in the LST file (KeresztG) */
int getNextShortJumpError(FILE *f)
{
	char fl[200];
	int i;
	char *cc1,*cc2;

	while (!feof(f))
	{
		fgets(fl,199,f);
		if (strstr(fl,"short jump")==NULL) continue;
		if ((cc1=strchr(fl,':'))==NULL) continue;
		if ((cc2=strchr(cc1,':'))==NULL) continue;
		cc2[0]=0;
		i=atoi(++cc1);
		return(i);
	}
	return(-1);
}
/* End of error getting */

int do_externs (FILE *out, FILE *tmp) {
	char string[80];
	char command[80];
	char lline[200],dline[200];
	int n,errorn=-1;
	int doagain = 0;
	int tmpchar;
	int isJumpToDo=0;

	FILE *lst;

	fprintf (out, "; This file was automatically generated using att2intl\n");
	fprintf (out, "; Please see documentation, assuming there is any, if\n");
	fprintf (out, "; att2intl does something wrong\n");
	fprintf (out, "; Greg Velichansky\n");
	fprintf (out, "; (Hmaon / Xylem)\n");
	fprintf (out, "; With support from Cam Horn / Xylem and KeresztG \n;\n");
	fprintf (out, "; I MAKE NO WARRANTIES OF ANY KIND REGARDING THIS PRODUCT\n");
	fprintf (out, "; IN FACT I CAN GUARANTEE THAT IN SOME CASES IT WILL *NOT*\n");
	fprintf (out, "; WORK CORRECTLY!\n\n");
	/* fclose (tmp); */

	*command = '\0';
	if (tasm_syntax) strcat (command, "tasm /ml ");
	else strcat (command, "nasm -s -fobj ");
	strcat (command, TMPFILENAME);
	strcat (command, " > a2itmpfl.lst");
	debug (1,"\ndo_externs will run:   %s\n", command);
	fflush (stdout);
	system (command);

	lst = fopen ("a2itmpfl.lst", "rt");

	debug(1, "scanning summary for externs\n");

	exts=malloc(maxextNo*sizeof(char *));
	while (!feof (lst)) {
		string[0]=0;
		fscanf (lst, "%s", string);
		if (!strncmp (string, "symbol", 6)) {
			fscanf (lst, "%s", string); //Silly scanf cannot handle `%s' well :(
			if (!tasm_syntax)
			{
				if (string[0]!='`' || string[strlen(string)-1]!=39) continue;
				strcpy(string,string+1);
				string[strlen(string)-1]=0;
			}
			if (!doWeHave(string))
			{
			   if (!tasm_syntax) fprintf (out, "extern %s\n", string);
			   else fprintf (out, "extrn %s:near\n", string);
			   if (extNo+1>=maxextNo && maxextNo!=-1)
			   {
				maxextNo+=GROW_MEM_WITH;
				exts=realloc(exts,maxextNo*sizeof(char *));
				if (exts==NULL)
				{
				   fprintf(used_stderr,"WARNING: Not enough memory!\n");
				   maxextNo=-1;
				}
			   }
			   if (maxextNo!=-1) if ((exts[extNo++]=strdup(string))==NULL)
			   {
				fprintf(used_stderr,"WARNING: Not enough memory!\n");
				extNo--;
			   }
			}
		}
		if (!strcmp (string, "Too")) {
			doagain = 1;
		}
		if (!strcmp (string, "short")) {
			fscanf (lst, "%s", string);
			if (!strcmp (string, "jump"))
			{
				isJumpToDo = 1;
				doagain=1;
			}
		}
	}
	for (n=0;n<extNo;n++) free(exts[n]);
	free(exts);
	fseek (lst,0,SEEK_SET);
	debug(1, "Done scanning.\n");
	tmp = fopen (TMPFILENAME, "rt");
	/* Skip the first 11 lines we've already put in */
	n=0;
	if (doing_again_extern)
	{
		for (n = 1; n <= 11; ++n) {
			do {
				*string = fgetc (tmp);
			} while (*string != '\n');
		}
		n--;
	}
	if (isJumpToDo) errorn=getNextShortJumpError(lst);
	while (!feof(tmp)) {
		lline[0]=0;
		fgets(lline,199,tmp);
		n++;
		if (isJumpToDo && n==errorn)
		{
			dline[0]=0;
			errorn=getNextShortJumpError(lst);
			if (lline[0]=='j' && lline[1]!='m')
			{
				sscanf(lline,"%s",dline);
				tmpchar=strlen(dline);
				strcat(dline," near ");
				strcat(dline,lline+tmpchar);
				strcpy(lline,dline);
			}
		}
		fputs (lline, out);
	}
	fclose (tmp);
	fclose (lst);
	doing_again_extern++;
	return doagain;
}

void usage()
{
	puts("usage: att2intl [options] <attfile.s>");
	puts("	-r		redirect stderr to stdio");
	puts("	-d level	sets debug_level to level");
	puts("	-t		use TASM syntax instead of the default NASM syntax");
	puts("	-b		use BSS when found an .lcomm or .comm directive");
}

int main (int argc, char *argv[]) {
	char *inext;
	char *outname;
	FILE *in, *out, *tmp;
	int tmpchar,c;

	fprintf (used_stderr, message);
	if (argc<2)
	{
		usage();
		exit(1);
	}
	opterr=0;
	while ((c=getopt(argc,argv,"d:rtb")) != -1)
	{
		switch(c)
		{
			case 'd':
				debug_level=atoi(optarg);
				break;
			case 'r':
				// used_stderr=stdout;
                fprintf(used_stderr,"%c is not implemented. It is useless anyway\n",c);
				break;
			case 't':
				tasm_syntax=1;
				break;
			case 'b':
				use_bss=1;
				break;
			case '?':
				usage();
				exit(1);
			default:
				fprintf(used_stderr,"Unknown option: %c\n",c);
				usage();
				exit(1);
		}
	}
	argv+=optind;
	inext = extension (argv[0]);

	if (strcmp (inext, "s") != 0) {
		fprintf(used_stderr, "FATAL: input is not .s");
		exit(-1);
	}

	in = fopen (argv[0], "rt");

	if (in == NULL) {
		fprintf(used_stderr,"FATAL: Error on opening %s; ", argv[0]);
		perror ("fopen");
		exit(-1);
	}

	/* 4 is a generous value so ".asm" can always be appended */
	outname = (char *) malloc (strlen (argv[0]) + 4);
	*outname = '\0';
	copybasename (outname, argv[0]);
	strcat (outname, "asm");
	
	out = fopen (outname, "wt");
	tmp = fopen (TMPFILENAME, "wt");

/*
	copybasename (outname, argv[0]);
	strcat (outname, "lst");
*/

	debug(1, "%s => %s ... \n", argv[0], outname);

	parse_file (in, tmp);
	/* fflush(tmp); */
	fclose(tmp);

	while (do_externs (out, tmp)) {
		/* fclose (tmp); */
		fclose (out);
		debug(1, "Calling do_externs() again. Slow. :(\n");
		tmp = fopen (TMPFILENAME, "wb");
		out = fopen (outname, "rb");
		while ((tmpchar = fgetc (out)) != EOF) { /* slow copy :( */
			fputc (tmpchar, tmp);
		}
		fclose (tmp);
		fclose (out);
		out = fopen (outname, "wt");
	}

	free (outname);

	fclose (out);
	fclose (in);

	debug(1, "Cleaning up\n");

	if (debug_level==0)
	{
		remove ("a2itmpfl.asm");
		remove ("a2itmpfl.lst");
		remove ("a2itmpfl.obj");
	}
	fprintf(used_stderr, "All done!\n");

	return 0;
}

