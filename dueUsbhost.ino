/*
This code is in the public domain.
Created by Andreas Meyer controlord/at/controlord/dot/fr
Version 201712	(dec 2017)
*/

#define USEFATFS			// use fat filesystem
#include "duedue.h"
#include "usbhost.h"
#ifdef USEFATFS	
#include "ff.h"
#include "diskio.h"
#endif // USEFATFS	

void cmdServer();
void uhServer();
int uhgogo;

void
setup()
{
	pinMode(13, OUTPUT);
	Serial.begin(115600);
	printf("Pigs on the catwalk %s %s\n", __DATE__, __TIME__);
	printf("\n");
	uhstate.verbose= 1;		// set verbose= 1
	uhgogo= 1;			// auto start server
}


void
loop()
{	int i= millis()%1000;
	if ((i<20)||
	   ((i<500) && (uhstate.state==UHSTAT_READY)))
		digitalWrite(13, HIGH);			// turn the LED on 
	else	digitalWrite(13, LOW);			// turn the LED off
	cmdServer();
	if (uhgogo)
		uhServer();
}


char otgbufrd[512];
char otgbufwr[512];
void otgfill(char *buf, int pat);
int otgBlRead(int slot, int block, int pat);
int otgBlWrite(int slot,int block, int pat);
int otgBlTest(int slot, int n);
extern char otgbufrd[512];
extern char otgbufwr[512];

#ifdef USEFATFS
FATFS fatfs;
void otgFsRead(char *fname);
void otgFsWrite(char *s);
void otgFsLs(char *s);
#endif // USEFATFS

//	cmdServer()
//	read a command from Rs232
//	execute the command

extern void (*gpf_isr)(void);
#define CMDLINEFULL 80
char cmdline[CMDLINEFULL+2];
int cmdlinepp;
void
cmdServer()
{	char *s= cmdline;
	int i, j;
	if (cmdlinepp==0) memset(cmdline, 0, sizeof(cmdline));
	if (Serial.available()>0){
		i= Serial.read();
		if ((i>=' ')&& (cmdlinepp < CMDLINEFULL))
			cmdline[cmdlinepp++]= i;
		if ((i=='\n') && (cmdlinepp>0))
			cmdlinepp= CMDLINEFULL;
	}
	if (cmdlinepp< CMDLINEFULL)
		return;					// and loop
	printf("> %s\n", cmdline);
	cmdlinepp= 0;

	if (strcmp(s, "go")==0){				// go
		printf("start uh server\n");
		uhgogo= 1;
		return;
	}
	if (strncmp(s, "v=", 2)==0){				// v= <verbose>
		i= strtol(s+2, 0, 10);
		uhstate.verbose= i;
		printf("verbose=%d\n", i);
		return;
	}
	if (strncmp(s, "blrd ", 5)==0){				// blrd <lba>
		i= strtol(s+5, &s, 10);
		j= strtol(s, 0, 16);
		otgBlRead(0, i, j<=0? -1: j);
		return;
	}
	if (strncmp(s, "blwr ", 5)==0){				// blwr <lba> <pat>
		i= strtol(s+5, &s, 10);
		j= strtol(s, 0, 16);
		otgBlWrite(0, i, j);
		return;
	}
	if (strncmp(s, "bltst", 5)==0){				// bltst <cnt>
		i= strtol(s+5, &s, 10);
		otgBlTest(0, i);
		return;
	}
	if (strcmp(s, "u")==0){					// u
		uhDump();
		return;
	}
#ifdef USEFATFS
	if (strcmp(s, "ls")==0){				// ls
		otgFsLs(s+3);
		return;
	}
	if (strncmp(s, "rd ", 3)==0){				// rd <filename>
		otgFsRead(s+3);
		return;
	}
	if (strncmp(s, "wr ", 3)==0){			// wr <filename> <n> <pat>
		otgFsWrite(s+3);
		return;
	}
#endif // USEFATFS
	printf("ERROR unknown cmd <%s>\n", cmdline);
}


#define VERBOSE1 if (uhstate.verbose)
#define VERBOSE2 if (uhstate.verbose>1)

void
uhServer()
{	static u64 t0init;			// no more than 1 per 2s
	static int ostat= UHSTAT_NULL;
	static int fs=0;
	int st;
	if (ostat== UHSTAT_NULL)
		uhInit();			// init driver
	st= uhstate.state;
	if (st<UHSTAT_DEV)
		t0init= 0;
	if (st!=ostat)				// stat changed
		VERBOSE1 uhPrintState();
	if ((st>=UHSTAT_DEV) && (st<UHSTAT_READY) && ((millis()-t0init)>2000)){
		uhInit();			// silent
		fs= 0;
	t0init= millis();
	}
	if ((st>= UHSTAT_READY)&& (fs==0)){
#ifdef USEFATFS
		memset (&fatfs, 0, sizeof(fatfs));
		f_mount(0, &fatfs);
#endif // USEFATFS
		fs= 1;
	}
	ostat= st;
}


//   pattern	-2	no compare, no dump
//		-1	no compare, dump
//		0..	compare, dump if error
//   return	<0	error
//   		0	rd ok, compare error
//   		>0	ok, compare ok
//   		
void
otgfill(char *buf, int pat)
{	int i;
	for (i=0; i<512; i++, buf++)
		*buf= pat++;
}

int
otgBlRead(int slot, int block, int pat)
{	int i;
	VERBOSE1 if (pat!=-2) printf("read %d %0x\n", block, pat==-1? 0: pat);
	otgfill(otgbufwr, pat);
	if ((i= mscReadBlock(block, otgbufrd))< 0){
		printf("read failed %d\n", i);
		return i;
	}
	if (memcmp(otgbufrd, otgbufwr, 512)==0){
		return 1;
	}
	if (pat>=0)
		printf("read %d %0x compare failed\n", block, pat);
	if (pat!=-2)
		dumphex(otgbufrd, 0, 512);
	if (pat>=0){
		printf("pattern:\n", block, pat);
		dumphex(otgbufwr, 0, 512);
	}
	return 0;
}

int
otgBlWrite(int slot,int block, int pat)
{	int i;
	VERBOSE1 printf("write %d %0x\n", block, pat);
	otgfill(otgbufwr, pat);
	if ((i= mscWriteBlock( block, otgbufwr))!= 0){
		printf("write failed %d\n", i);
		return 0;
	}
	return 1;
}

const int otgdt[]= { 13,0x55, 177,0x33, 1333,0x12,  117,0x00, 888,0x88, 300, 0x33,
		     73,0xCC, 155,0xAF, 1404,0xE2, 1488,0x99, 999,0xFF, 257, 0xA5,
		0 };
int
otgBlTest(int slot, int n)
{	const int *p;
	if (n<1) n=1;
	for (;n>0; n--){
		for (p= otgdt; *p>0; p+=2 )
			if (otgBlWrite(slot, *p, p[1]+n)==0)
				return 0;
		for (p= otgdt; *p>0; p+=2 )
			if (otgBlRead(slot, *p, p[1]+n)==0)
				return 0;
	}
	printf("Test finished ok\n");
	return 1;
}

#ifdef USEFATFS

void
otgFsLs(char *s)
{	DIR dir; int i; char at;
	FILINFO fin;
	char lfn[_MAX_LFN+1];
#if _USE_LFN
	fin.lfname= lfn;
	fin.lfsize= sizeof(lfn);
#endif
	if ((i=f_opendir(&dir, "/"))!=0){
		printf("ERROR f_opendir %d\n", i);
		return;
	}
	for (i=0; i<100; i++){
		memset(lfn, 0, sizeof(lfn));
		if (f_readdir(&dir, &fin)!=0)
			break;
		if (fin.fname[0]==0) break;		// problem f_readdir() !
		if (lfn[0]!=0)
			printf("< %-20s", lfn);
		else
			printf("< %-20s", fin.fname);
		at=' ';
		if (fin.fattrib&AM_DIR) at='/';
		printf(" %c %d\n", at, fin.fsize);
	}
	
}

void
otgFsRead(char *fname)
{	int i;
	FIL fd;
	char buf[80];
	if ((i=f_open(&fd, fname, FA_READ))!=0){
		printf("ERROR f_open <%s> %d\n", fname, i);
		return;
	}
	while (f_gets( buf, sizeof(buf), &fd)!=0){
		printf("< %s", buf);
	}
	printf("\nEOF\n", buf);
	f_close(&fd);
}

void
otgFsWrite(char *s)
{	int i, lmax;
	FIL fd;
	char *fname=s;
	char buf[80];
	while (*s>' ') s++;
	if (*s!=0) *s++= 0;
	lmax= strtol(s, &s, 10);
	while (*s==' ') s++;
	if (lmax<=0) lmax=10;
	if ((i=f_open(&fd, fname, FA_WRITE|FA_CREATE_ALWAYS))!=0){
		printf("ERROR f_open <%s> %d\n", fname, i);
		return;
	}
	for (i=1; i<= lmax; i++){
		sprintf(buf,"line %d ", i);
		f_puts(buf, &fd);
		f_puts(s, &fd);
		f_puts("\n", &fd);
	}
	f_close(&fd);
	printf("write %s %d lines done\n", fname, lmax);
}

#endif // USEFATFS

// +++ usbhost() driver integration

void
uhStatServer()				// display stat on LED
{
#if 0
	if (uhstate.state>= UHSTAT_READY)
		digitalWrite(13, HIGH);		// turn led on
	else	digitalWrite(13, LOW);		// turn led off
	       	PIOA->PIO_CODR= LEDBIT2;
	else	PIOA->PIO_SODR= LEDBIT2;
#endif
}

void
uhWaitMs(int n)
{
	uhStatServer();
	delay(n);
	uhStatServer();
}

void
uhSwitch()
{
	uhStatServer();
//	switch to other task
	uhStatServer();
}

#ifdef USEFATFS
#
// +++ fatfs integration

struct billstime {
	u32	sec:5,		// seconds/2 0-29
		min: 6,		// minutes 0-59
		hour: 5,	// hour 0-23 (gmt)
		day:5,		// dd 1-31
		month: 4,	// MM 1-12
		year: 7;	// yyyy-1980
};

DWORD
get_fattime()
{	union {
		DWORD d;
		struct billstime b;
	} u;
	u.d= 0;
	u.b.year= 2017-1980;
	u.b.month= 7;
	u.b.day= 24;
	u.b.hour= 16;
	u.b.min= 44;
	return u.d;
}

#if _USE_LFN
WCHAR
ff_convert(WCHAR x, UINT mode)
{	return x;
}

WCHAR
ff_wtoupper(WCHAR x)
{	return x;
}
#endif // _USE_LFN

// +++ fatfs -> usbhost interface

//	ff -> usbhost interface

DSTATUS
disk_initialize (BYTE lun)
{
	if (uhstate.state== UHSTAT_READY)
		return RES_OK;
	return RES_ERROR;
}

DSTATUS
disk_status (BYTE lun)
{
	if (uhstate.state== UHSTAT_READY)
		return RES_OK;
	return RES_ERROR;
}

DRESULT
disk_read (BYTE lun, BYTE *buf, DWORD lba, BYTE cnt)
{	int i;
	for (;cnt>0; lba++, buf+=512, cnt--){
		VERBOSE2 printf("disk_read lba=%d\n", lba);
		if ((i=mscReadBlock(lba, (char *)buf))<0)
			return RES_ERROR;
	}
	return RES_OK;
}

DRESULT
disk_write (BYTE lun, const BYTE *buf, DWORD lba, BYTE cnt)
{	int i;
	for (;cnt>0; lba++, buf+=512, cnt--){
		VERBOSE2 printf("disk_write lba=%d\n", lba);
		if ((i=mscWriteBlock(lba, (char *)buf))<0)
			return RES_ERROR;
	}
	return RES_OK;
}

DRESULT
disk_ioctl (BYTE lun, BYTE cmd, void* p)
{
	VERBOSE1 printf("disk_ioctl %d\n", cmd);
	switch (cmd) {
	case GET_SECTOR_SIZE:
	case GET_BLOCK_SIZE:
			* ((DWORD *)p)= 512;
			return RES_OK;
	case GET_SECTOR_COUNT:
			* ((DWORD *)p)= uhstate.capacity;
			return RES_OK;
	case CTRL_SYNC:
			return RES_OK;
	}
	VERBOSE1 printf("ERROR disk_ioctl %d\n", cmd);
	return RES_ERROR;
}

#endif // USEFATFS


// +++ suppport and debug functions

void
dumphex(const char *p, u32 addr, int cnt)
{	int i; uchar c; u32 *pp;
	const char tohex[]="0123456789ABCDEF";
	for (;cnt >0; cnt-=16, p+=16, addr+=16) {
		printf("%08X:", addr);
		for (i=0; i<16; i++){
			c= p[i];
			if ((i%4)==0)
				printf(" ");
			if (i < cnt)
				printf("%c%c", tohex[(c>>4)&0xF], tohex[c&0xF]);
			else	printf("  ");
		}
		printf(" /");
		for (i=0; i<16; i++){
			c= p[i];
			if ((i < cnt)&& (c>=' ') && (c<0x7F))
				printf("%c", c);
			else	printf(" ");
		}
		printf("/");
#if 1
		if ((((u32)p)&0x3)==0){		// 32bit alligned
			pp= (u32*) p; 
			for (i=0; i<16; i+=4, pp++)
				if (i<cnt)
					printf(" %08X", *pp);
		}
#endif
		printf("\n");
	}
}


