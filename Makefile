att2intel: att2intel.o translate.o
	gcc translate.o att2intel.o -o att2intel -lm

att2intel.o: att2intel.c
	gcc -O1 -Wall -c att2intel.c 

translat.o: translat.c
	gcc -O1 -Wall -c translate.c

