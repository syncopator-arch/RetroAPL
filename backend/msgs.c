/*
	msgs.c
*/

#define MSGS_C 1

#include "parse.h"
#include "externs.h"

void
message(n)
short n;
{
short p;
char *s;

	p = 0;
	switch (n) {
		case ERR_WSFULL:
			s = "WS Full";
			p=1;
			break;
		case ERR_SYSTEM:
			s = "System Error";
			break;
		case ERR_SYNTAX:
			s = "Syntax Error";
			p=1;
			break;
		case ERR_VALUE:
			s = "Value Error";
			p = 1;
			break;
		case ERR_DOMAIN:
			s = "Domain Error";
			p=1;
			break;
		case ERR_INDEX:
			s = "Index Error";
			p = 1;
			break;
		case ERR_RANK:
			s = "Rank Error";
			p = 1;
			break;
		case ERR_COMMAND:
			s = "Incorrect Command";
			p = 1;
			break;
		case ERR_INTERRUPT:
			s = "Interrupt";
			p = 1;
			break;
		case ERR_STOVF:
			s = "Symbol Table Overflow";
			p = 1;
			break;
		case ERR_SI:
			s = "SI Damage";
			p = 1;
			break;
		case ERR_LENGTH:
			s = "Length Error";
			p = 1;
			break;
		case ERR_KILL:
			s = "";
			break;
		case ERR_FTOVF:
			s = "File Table Full";
			p = 1;
			break;
		case ERR_FILE:
			s = "File Tie Error";
			p = 1;
			break;
		case ERR_FNOTF:
			s = "File Not Found";
			p = 1;
			break;
		case ERR_WSNOTF:
			s = "Ws Not Found";
			p = 1;
			break;
		case ERR_SAVE:
			s = "Can't write save file";
			p = 1;
			break;
		case ERR_WSD:
			s = "Ws Damaged";
			p = 1;
			break;
		case ERR_WSLOAD:
			s = "Can't Load Ws";
			p = 1;
			break;
		case ERR_FOPEN:
			s = "Can't open file";
			p = 1;
			break;
		case ERR_NOMEM:
			s = "Out of Memory";
			p = 1;
			break;
		case ERR_NOFLOATV:
			s = "Out of FLOATV's";
			p = 1;
			break;
		case ERR_BADFN:
			s = "Function Damaged";
			p = 1;
			break;
		case MSG_WAS:
			s = "Was ";
			break;
		case MSG_IS:
			s = "Is ";
			break;
		case MSG_ON:
			s = "ON";
			break;
		case MSG_OFF:
			s = "OFF";
			break;
		case MSG_SAVED:
			s = "Saved ";
			break;
		case MSG_HEX:
			s = "HEX";
			break;
		case MSG_DEC:
			s = "DEC";
			break;
		case ERR_NUMOVF:
			s = "Numeric Overflow";
			p = 1;
			break;
		case ERR_IMGLOAD:
			s = "Can't Load Image";
			p = 1;
			break;
		case ERR_SEQNOTOPEN:
			s = "Sequencer Not Open";
			p = 1;
			break;
		case ERR_SEQOPEN:
			s = "Sequencer Already Open";
			p = 1;
			break;
		case ERR_SEQLOST:
			s = "MIDI Port Lost";
			p = 1;
			break;
		default:
			sprintf((char *)LINEA,"Error %d",n);
			s = (char *)LINEA;
			p = 1;
			break;
	}
	if (p) 
	{
		/* output with no newline yet (⍞←X just before the error):
		   start the caret on a line of its own */
		if (CURSX) NL();
		PRINT("     ^");
		NL();
	}
	PRINT(s);
	if (p) NL();
}

