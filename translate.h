/* Modified by KeresztG <keresztg@podolin.piar.hu> for NASM 	1998 August */
#ifndef __TRANSLAT_H__
#define __TRANSLAT_H__

void parse_file (FILE *in, FILE *out);
void debug(int level, const char *fmt, ...);
extern int debug_level;
extern FILE *used_stderr;
extern int tasm_syntax;
extern int use_bss;

#endif /*__TRANSLAT_H__*/

