/*
This code is in the public domain.
Created by Andreas Meyer controlord/at/controlord/dot/fr
Version 201712	(dec 2017)
*/

#include "duedue.h"
#include "usbhost.h"

void uhSetState(int i);			// only down!
void uhSetStateUp(int i);		// down or up 1
void uhSetStateXXX(int i);
const char *uhStatAsc();
void uhInit();
void uhInitx(char *s);
void uhInitPipe(int pipe, int token);
void uhInitPipeToken(int pipe, int token);
void uhInitAllocDpram();
void uhInitPeriph();
int uhInitResetUsb(int n, int inix);
int  uhInitEnum();
void uhEnumSaveDescr(u8 *d);
void uhEnumPrintDescr(u8 *d, int tlng);
int uhEnumConfig(u8 *d, int tlng);
void uhResetDevEndpointIn();
const char * uhErrorAsc(int ecode);
int uhSendPipe(int pipe, u8 *buf, int lng);
int uhReceivePipe(int pipe, u8 *buf, int lng);
extern void udd_interrupt();
void uhInterrupt();
void uhInterruptPipe(int pipe);
extern void (*gpf_isr)(void);

void mscInit();
int mscTstUnitReady();
int mscInquiry();
int mscReadCapacity();
int mscRequestSense();
int mscReceiveWrapper(int pipe, u32 stall);
int mscReadBlock(u32 lba, char *buf);
int mscWriteBlock(u32 lba, char *buf);
const char *scsi2asc(int code);
void uhDumpx(int pipe, const char *pre, int destr);

int uhStepFrom, uhStepTo;
#define VERBOSE0 			// severe internal errors, "can not happen"
#define VERBOSE1 if (uhstate.verbose)
#define VERBOSE2 if (uhstate.verbose>1)
#define VERBOSE3 if (uhstate.verbose>2)

// +++  usbhost, msc,  scsi ***********

// +++ usb host + enumeration

#define UHLOG if (0) uhramlog
void uhramlog(const char *fmt, ...);			// debug support

#define UHPIPE_CTRL	0
#define UHPIPE_IN	1
#define UHPIPE_OUT	2
#define UHPIPES		10			// just for arrays
//#define UOTGHS_DPRAM	0x20180000
#define UOTGHS_DPRAM	UOTGHS_RAM_ADDR

//struct _uhstate {
//	volatile int	state;		// off, enumeration, ready
//	u32	capacity;		// total number of blocks
//	u16	idVendor;		// enumeration
//	u16	idProduct;
//	u8	vendor_id[8+1];	
//	u8	product_id[16+1];
//	u8	product_rev[4+1];
//	u16	maxPower;
//};
struct _uhstate uhstate;
	
struct _uhx {					// internals
	struct _cnt {
		u32	sof;			// count sof events
		u32 	pipeint;
		u32	connect;
		u32	disconnect;
		u32	stall;
		u32	tout;
		u32	errs[10];	// -1...
		u32	mscErrors;	// signal msc rd/wr error
	} cnt;
	u8	*uhDmaStartaddress;
	char	*fifoaddr[UHPIPES];
	int	devpipein;		// device endpoint
	int	devpipeout;		// device endpoint
	int	devpipeoutLng;		// endpoint max package size
	u32	msctag;
	struct _it {
		volatile u32 pipe;
		volatile u32 mask;
		volatile u32 isr;
		volatile int done;	// 0=waiting for, >0: ok, <0: UHERROR_
	} it;				// uhx.it.
} uhx;

const char *
uhStatAsc()
{	switch(uhstate.state){
	case UHSTAT_NULL:	return "";
	case UHSTAT_NODEV:	return "NODEV";
	case UHSTAT_DEV:	return "DEV";
	case UHSTAT_ENUM:	return "ENUM";
	case UHSTAT_READY:	return "READY";
	default: 		return "??? stat unknown";
	}
}

#define UHERROR_NOTREADY -1
#define UHERROR_TIMEOUT -2
#define UHERROR_STALL -3		// not really an error
#define UHERROR_REPLY -4
#define UHERROR_INTERN -5
#define UHERROR_IO -6
#define UHTIMEOUT	1000		// ms
#define UHTIMEOUTSENSER	5000		// ms senserequest  ?????

const char *uhAscErrors[]= { "notready", "timeout", "stall", "reply",
       				"intern", "io"};
const char *
uhErrorAsc(int ecode)
{	switch(ecode){
	default:		return "";
	case UHERROR_NOTREADY:	return "ERROR: NOT READY";
	case UHERROR_TIMEOUT:	return "ERROR: TIMEOUT";
	case UHERROR_STALL:	return "ERROR: STALL";
	case UHERROR_REPLY:	return "ERROR: REPLY";
	case UHERROR_INTERN:	return "ERROR: INTERN";
	case UHERROR_IO:	return "ERROR: IO";
	}
}

// usb device setup request

typedef struct _usb_request {
	u8	urRequestType;
	u8	urRequest;
	u16	urValue;
	u16	urIndex;
	u16	urLeng;
} usb_request;
#define USBREQUESTLNG 8
#define urFill(t, rq, v, ix, lng) \
	memset(&ur, 0, sizeof(ur));\
	ur.urRequestType= t; \
	ur.urRequest= rq; \
	ur.urValue= v; \
	ur.urIndex= ix; \
	ur.urLeng= lng;

// urRequestType
#define  USB_REQ_DIR_OUT	(0<<7) //!< Host to device
#define  USB_REQ_DIR_IN		(1<<7) //!< Device to host

#define  USB_REQ_TYPE_STANDARD	(0<<5) //!< Standard request
//#define  USB_REQ_TYPE_CLASS	(1<<5) //!< Class-specific request
//#define  USB_REQ_TYPE_VENDOR	(2<<5) //!< Vendor-specific request

#define  USB_REQ_RECIP_DEVICE	(0<<0) //!< Recipient device
//#define  USB_REQ_RECIP_INTERFACE (1<<0) //!< Recipient interface
#define  USB_REQ_RECIP_ENDPOINT  (2<<0) //!< Recipient endpoint
//#define  USB_REQ_RECIP_OTHER	(3<<0) //!< Recipient other
//#define  USB_REQ_RECIP_MASK	(0x1F) //!< Mask

// urRequest
//efine USB_REQ_GET_STATUS		0
#define USB_REQ_CLEAR_FEATURE		1
//efine USB_REQ_SET_FEATURE		3
#define USB_REQ_SET_ADDRESS		5
#define USB_REQ_GET_DESCRIPTOR		6
//efine USB_REQ_SET_DESCRIPTOR		7
//efine USB_REQ_GET_CONFIGURATION	8
#define USB_REQ_SET_CONFIGURATION	9
//efine USB_REQ_GET_INTERFACE		10
//efine USB_REQ_SET_INTERFACE		11
//efine USB_REQ_SYNCH_FRAME		12

#define USB_EP_FEATURE_HALT		0	//  endpoint feature


#define USB_DT_DEVICE  1
#define USB_DT_CONFIGURATION  2
#define	USB_DT_INTERFACE  4
#define	USB_DT_ENDPOINT 5

//	USB device descriptor s

typedef struct _usb_devdesr{		// devioce descr
	u8	bLength;		//  0:
	u8	bDescriptorType;	//  1:
	u16	bcdUSB;			//  2:
	u8	bDeviceClass;		//  4:
	u8	bDeviceSubClass;	//  5:
	u8	bDeviceProtocol;	//  6:
	u8	bMaxPacketSize0;	//  7:
	u16	idVendor;		//  8:
	u16	idProduct;		//  A:
	u16	bcdDevice;		//  C:
	u8	iManufacturer;		//  E:
	u8	iProduct;		//  F:
	u8	iSerialNumber;		// 10:
	u8	bNumConfigurations;	// 11: total: 18 bytes
} usb_devdescr;

typedef struct {			// configuration decr
	u8	bLength;
	u8	bDescriptorType;
	u16	wTotalLength;
	u8	bNumInterfaces;
	u8	bConfigurationValue;
	u8	iConfiguration;
	u8	bmAttributes;		// 0x40= self powered
	u8	bMaxPower;		// *2mA		lng=9
	u8	stuff[46];		// 23= 1 interface(9) + 2 epdescr(7)
} usb_confdescr;
#define CONFDESCRLNG 9

typedef struct {			// interface descr.
	u8	bLength;
	u8	bDescriptorType;
	u8	bInterfaceNumber;
	u8	bAlternateSetting;
	u8	bNumEndpoints;
	u8	bInterfaceClass;	// need MSC_CLASS
	u8	bInterfaceSubClass;
	u8	bInterfaceProtocol;
	u8	iInterface;
} usb_ifacedescr;

#define MSC_CLASS 0x08
#define  MSC_SUBCLASS_TRANSPARENT   0x06
#define  MSC_PROTOCOL_BULK          0x50

typedef struct {			// endpoint descr
	u8	bLength;
	u8	bDescriptorType;
	u8	bEndpointAddress;
	u8	bmAttributes;
	u8	wMaxPacketSize[2];
	u8	bInterval;
} usb_epdescr;
#define  USB_EP_DIR_IN        0x80
#define  USB_EP_DIR_OUT       0x00

#define USB_EP_TYPE_BULK 2

void
uhSetState(int i)			// only down!
{	if (i< uhstate.state)
		uhstate.state= i;	
	uhStatServer();
}

void
uhSetStateUp(int i)			// down or up 1
{	if (i< uhstate.state)
		uhstate.state= i;	
	if (i== uhstate.state+1)
		uhstate.state= i;	
	uhStatServer();
}

void
uhSetStateXXX(int i)			// set uncondiotioned (debug only)
{	uhstate.state= i;	
	uhStatServer();
}

int
uhError(int e)
{	int i;
	i= -1-e;				// -1->0 -2->1 ...
	if ((i>=0) && (i<10)) uhx.cnt.errs[i]++;
	UHLOG("error %d", e);
//	uhSetState(sowhat);
//	set last error ?;
	return e;
}

void
uhPrintState()
{	const char *speed;
	switch ((UOTGHS->UOTGHS_SR & UOTGHS_SR_SPEED_Msk)>>UOTGHS_SR_SPEED_Pos){
		case 0: speed= "full"; break;
		case 1: speed= "high"; break;
		case 2: speed= "low" ; break;
		default: speed="???" ; break;
	}
	printf("%s %s speed",
		uhStatAsc(),  speed);
	if (uhstate.verbose)
		printf(" v=%d", uhstate.verbose);
	if (uhstate.state>= UHSTAT_READY)
		printf(" vendor= %04X %s product= %04X %s curr= %dmA"
			" mem= %dGB",
		uhstate.idVendor, uhstate.vendor_id, uhstate.idProduct,
	       	uhstate.product_id,
	       	uhstate.maxPower,
		(uhstate.capacity/2000+300)/1000);
	printf("\n");
}


//  uhInit():
//  uhInitPeriph()	init usb host				-> UHSTAT_NODEV
//  wait for dev to connect					-> UHSTAT_DEV
//  uhInitEnum()	enumeration phase			-> UHSTAT_ENUM
//  mscInit()		int msc/sci interface, get device infos	-> UHSTAT_READY
//
// ATmel:
//	step1 0: reset
//	step2 11: 20ms
//	step3 32: reset
//	step4 43: 100ms
//	step5 143: get device descr
//	step6 143: 20ms
//	step7 163: reset
//	step8 174: 100ms
//	step9 274: set address 
//	step10 274: 20ms
//	step11 294: new adrress, get dev descriptor

void
uhInit()
{ uhInitx((char *)""); }

void
uhInitx(char *s)
{
	uhStepFrom= strtol(s, &s, 10);
	if (*s==',') s++;
	uhStepTo= strtol(s, &s, 10);
	if (uhStepTo<=0) uhStepTo= 9999;
	uhInitAllocDpram();
	if (uhstate.state== UHSTAT_NULL){
		uhSetStateUp(UHSTAT_NODEV);		// stat _NULL -> _NODEV
		uhInitPeriph();
		u64 t0;
		for (t0=millis(); millis()< (t0+500);){
			uhSwitch();
			if (uhstate.state== UHSTAT_DEV)
				break;
		}
		VERBOSE2 printf("%s dev after %d ms\n",
			uhstate.state== UHSTAT_DEV? "": "no", (int)(millis()-t0));
	}
	if (uhStepTo<3)					// uhin ,2 -> stop
		goto end;
	if (uhstate.state== UHSTAT_DEV)
		uhInitEnum();
	if (uhStepTo<100)				// uhin ,99 -> stop
		goto end;
	if (uhstate.state== UHSTAT_ENUM)
		mscInit();
    end:
	VERBOSE2 printf("uhInit status= %d %s\n", uhstate.state,  uhStatAsc());
}

// seems to be ok (?)
//	same70: PLL=600  HCLK=300  MCK=150MHz   
//	sam3x:  PLL=168  HCLK= 84  MCK= 84
//  UPLLCK = 480 MHz
#define  USBDIV (10-1)			// UPLLCK / 10 --> 48 MHz
void
usbclkInit(int on)
{	volatile Pmc *pmc= PMC;
	PMC->CKGR_UCKR= CKGR_UCKR_UPLLCOUNT(0x3Fu) | CKGR_UCKR_UPLLEN;
	while ( !(PMC->PMC_SR & PMC_SR_LOCKU) );
	pmc->PMC_USB= PMC_USB_USBDIV(USBDIV)|PMC_USB_USBS; // UPLLCK /10
//	pmc->PMC_SCER= PMC_SCER_USBCLK;       		// enable usb clock
	pmc->PMC_SCER= PMC_SCER_UOTGCLK;       		// enable usb clock NYI ???

}

void
uhInitPeriph()
{	volatile Uotghs *usb= UOTGHS;
	u64 t0;
	VERBOSE2 printf("uhInitPeriph\n");
#define UOTGVBOF (1<<10)
	PIOB->PIO_PER= UOTGVBOF;
	PIOB->PIO_OER= UOTGVBOF;
	PIOB->PIO_SODR= UOTGVBOF;		// ext-power-> usb-power

	usb->UOTGHS_CTRL =  			// host mode, unfreeze
			UOTGHS_CTRL_USBE|		// enable usb
			UOTGHS_CTRL_VBUSHWC;
//NYI	perClockEnable(ID_UOTGHS, 1);
	usbclkInit(1);
//NYI	NVIC_SetPriority((IRQn_Type) ID_UOTGHS, 5);
//NYI	NVIC_EnableIRQ((IRQn_Type) ID_UOTGHS);
	gpf_isr=  uhInterrupt;
	usb->UOTGHS_CTRL =  			// host mode, unfreeze
			UOTGHS_CTRL_USBE|		// enable usb
			UOTGHS_CTRL_VBUSHWC;
	usb->UOTGHS_HSTCTRL= 0;
	for (t0=millis()+1000; t0>millis();){
		uhSwitch();
		if (usb->UOTGHS_SR& UOTGHS_SR_CLKUSABLE)
			goto uhp1;
	}
	VERBOSE0 printf("uhInitPerph failed (clk usable)\n");
	return;
   uhp1:
	usb->UOTGHS_HSTICR= 0x7F;			// clear all it
	usb->UOTGHS_SFR = UOTGHS_SFR_VBUSRQS;		// enable vbus (must)
	usb->UOTGHS_HSTIER=
		UOTGHS_HSTIER_DCONNIES|
		UOTGHS_HSTIER_DDISCIES|
		0; //  UOTGHS_HSTIER_HSOFIES;	// remove SOF ???
}

#define XX 1  // normal delay timing

// atmel reset proc step3:

int
uhInitResetUsb(int n, int inix)					// reset usb
{	volatile Uotghs *usb= UOTGHS;
	u64 t0; int ret=0;
	u32 hstier= usb->UOTGHS_HSTIMR;
	uhInitPeriph();
	UHLOG("reset usb");
	usb->UOTGHS_HSTIDR= 0xFFFFFFFF;
	usb->UOTGHS_HSTCTRL= 0;
	usb->UOTGHS_HSTCTRL= UOTGHS_HSTCTRL_RESET;	// reset usb
	for (t0=millis(); millis()<t0+ 5000;){	// 500 !!!
		uhSwitch();
		if (usb->UOTGHS_HSTISR& UOTGHS_HSTISR_RSTI)
			goto uhp2;
	}
	VERBOSE1 printf("uhInitReset failed (reset %d)\n", n);
	ret= UHERROR_NOTREADY;
    uhp2:
	VERBOSE2 printf("reset in %d ms\n", (int)(millis()-t0));
	uhWaitMs(30*XX);
	usb->UOTGHS_HSTCTRL= 0;
	usb->UOTGHS_HSTICR= UOTGHS_HSTICR_RSTIC;
	usb->UOTGHS_HSTCTRL= UOTGHS_HSTCTRL_SOFE;		// sof enable, needed!
	uhWaitMs(20*XX);
	if (inix==0)
		goto ier;
	uhWaitMs(100*XX);
	usb->UOTGHS_HSTCTRL= UOTGHS_HSTCTRL_SOFE;		// sof enable, needed!
	uhWaitMs(3*XX);
	uhInitPipe(UHPIPE_CTRL, -1);
	uhWaitMs(10*XX);
    ier:
	usb->UOTGHS_HSTICR= 0x7F;			// clear all ints
	usb->UOTGHS_HSTIER= hstier;			// restore hst int mask
	return ret;
}


int
uhInitEnum()						// uhin
{	volatile Uotghs *usb= UOTGHS;
	int i, redocnt=0, lng=100;
	usb_request ur;
	usb_devdescr udev;
	usb_confdescr uconf;
	u8 confno;
	VERBOSE2 printf("uhInitEnum\n");
	switch (uhStepFrom){
	case 5:	goto step5;				// uhin 5
	case 6:	goto step6;
	case 7:	goto step7;
	case 13: goto step13;
	}
	uhWaitMs(50*XX);				// essentiel needs this
	if ((i=uhInitResetUsb(1, 0))<0)			// reset usb
		return i;
	if ((i=uhInitResetUsb(2, 1))<0)			// reset usb
		return i;

	usb->UOTGHS_HSTADDR1= (0<<16)|(0<<8)|(0<<0);
	if (uhStepTo<= 5) return 0;			// uhin ,5
    step5:
	VERBOSE2 printf("Step5 get dev descr\n");	// step5 get devdescr
	VERBOSE3 uhDumpx(-1, "", 1);
	UHLOG("step5 get dev descr");
	urFill( USB_REQ_RECIP_DEVICE|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_IN,
		USB_REQ_GET_DESCRIPTOR,	
		USB_DT_DEVICE<<8,
		0,
		8);
	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0) // step5
		return i;
	if (uhStepTo<= 6) return 0;			// uhin ,6
    step6:
	if ((i=uhReceivePipe(UHPIPE_CTRL, (u8 *)&udev, sizeof(udev)))<0)
		return i;
	if (uhStepTo<= 7) return 0;			// uhin ,7

	uhWaitMs(20*XX);
    step7:
	if ((i=uhInitResetUsb(3, 1))<0)			// reset usb step7
		return i;

	VERBOSE2 printf("Step9 set addr\n");		// step9 set address
	UHLOG("step9 set addr");
	urFill( USB_REQ_RECIP_DEVICE|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_OUT,
		USB_REQ_SET_ADDRESS,		// set address
		1,				// new address
		0,
		0);

	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0)
		return i;
	if ((i=uhReceivePipe(UHPIPE_CTRL, 0, 0))<0)
		return i;
	uhWaitMs(20*XX);

	VERBOSE2 printf("Step11 get dev descr\n");  // step11 new ad, get dev descr
	UHLOG("step11 get dev descr");
	usb->UOTGHS_HSTADDR1= (1<<16)|(1<<8)|(1<<0);
	urFill( USB_REQ_RECIP_DEVICE|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_IN,
		USB_REQ_GET_DESCRIPTOR,			// get descriptor
		USB_DT_DEVICE<<8,
		0,
		sizeof(usb_devdescr));
	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0)
		return i;
	if ((i=uhReceivePipe(UHPIPE_CTRL, (u8 *)&udev, sizeof(udev)))<0)
		return i;
	uhEnumSaveDescr((u8 *) &udev);
	VERBOSE2 uhEnumPrintDescr((u8 *) &udev, 0);

	VERBOSE2 printf("Step12 get conf descr\n");	// step12 get config descr
	UHLOG("step12 get conf descr");
	urFill( USB_REQ_RECIP_DEVICE|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_IN,
		USB_REQ_GET_DESCRIPTOR,			// get descriptor
		USB_DT_CONFIGURATION<<8,
		0,
		CONFDESCRLNG);
	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0)
		return i;
	if ((i=uhReceivePipe(UHPIPE_CTRL, (u8 *)&uconf, CONFDESCRLNG))<0)
		return i;
	uhEnumSaveDescr((u8 *) &uconf);
	VERBOSE2 uhEnumPrintDescr((u8 *) &uconf, 0);
	confno= 1; 
	lng= uconf.wTotalLength;

	if (uhStepTo<= 13) return 0;			// uhin ,5
    step13:
	VERBOSE2 printf("Step13 get interf descr\n");	// step13 get interf. descr
	UHLOG("step13 get interf descr");
	if (lng> sizeof(uconf)) lng= sizeof(uconf);
	urFill( USB_REQ_RECIP_DEVICE|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_IN,
		USB_REQ_GET_DESCRIPTOR,			// get descriptor
		USB_DT_CONFIGURATION<<8|(confno-1),
		0,
		lng);
	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0)
		return i;
	memset(&uconf, 0, sizeof(uconf));
	if ((i=uhReceivePipe(UHPIPE_CTRL, (u8 *)&uconf, lng))<0){
		if (++redocnt<3) goto step13;		// for the sakeOf Lexar
		return i;
	}
	VERBOSE2 uhEnumPrintDescr((u8 *) &uconf, i);
	i=uhEnumConfig((u8 *) &uconf, i);
//	verify interface & endpoint descr, set confno
//	and reinit pipes 1,2
	uhInitPipe(UHPIPE_IN, -1);
	uhInitPipe(UHPIPE_OUT, -1);
	if (i>=0){
		VERBOSE2 printf("conf interface in pipe=%d endp=%d out pipe=%d"
			       " endp=%d\n",
		       	UHPIPE_IN, uhx.devpipein, UHPIPE_OUT, uhx.devpipeout);
	} else VERBOSE2 printf("conf interface failed\n");

	VERBOSE2 printf("Step14\n");			// step14 enable dev config
	UHLOG("step14");

	urFill( USB_REQ_RECIP_DEVICE|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_OUT,
		USB_REQ_SET_CONFIGURATION,		//  set config
		confno,
		0,
		0);
	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0)
		return i;
	if ((i=uhReceivePipe(UHPIPE_CTRL, 0, 0))<0)
		return i;

	uhSetStateUp(UHSTAT_ENUM);
	return 0;
}

void
uhResetDevEndpointIn()			// reset device endpoint in
{	int i;
	int ep=	uhx.devpipein;			// device endpoint IN
	if (ep==0) ep= UHPIPE_IN;
	ep|= 0X80;
	usb_request ur;
	urFill( USB_REQ_RECIP_ENDPOINT|USB_REQ_TYPE_STANDARD|USB_REQ_DIR_OUT,
		USB_REQ_CLEAR_FEATURE,	
		USB_EP_FEATURE_HALT,
		ep,
		0);
	if ((i=uhSendPipe(UHPIPE_CTRL, (u8 *)&ur, USBREQUESTLNG))<0)
		return;
	if ((i=uhReceivePipe(UHPIPE_CTRL, 0, 0))<0)
		return;
	return;
}

void			// save descriptor device, config, -> uhstat
uhEnumSaveDescr(u8 *d)
{	
	usb_devdescr *udev;
	usb_confdescr *uco;
	udev= (usb_devdescr *) d;
	uco= (usb_confdescr *) d;
	switch(d[1]){
	case USB_DT_DEVICE:
		uhstate.idVendor= udev->idVendor;
		uhstate.idProduct= udev->idProduct;
		break;
	case USB_DT_CONFIGURATION:
		uhstate.maxPower= uco->bMaxPower*2;
		break;
	}
}

void			// print descriptor device, config, interface, endpoint
uhEnumPrintDescr(u8 *d, int tlng)
{	
	usb_devdescr *udev;
	usb_confdescr *uco;
	usb_ifacedescr *id;
	usb_epdescr *ep;
	int i, lng;
	char mclass[10], subcl[10], prot[10], eptype[0];
  more:
	lng= d[0];
	udev= (usb_devdescr *) d;
	uco= (usb_confdescr *) d;
	id=  (usb_ifacedescr *) d;
	ep=  (usb_epdescr *) d;
	switch(d[1]){
	case USB_DT_DEVICE:
		printf("dev descr devClass=%X devSubClass=%X devProt=%X"
			" idVendor=%04X idProduct=%04X\n",
			udev->bDeviceClass, udev->bDeviceSubClass,
		       	udev->bDeviceProtocol, udev->idVendor, udev->idProduct);
		break;
	case USB_DT_CONFIGURATION:
		printf("config descr tlng=%d interf=%d curr=%dmA\n",
		       uco->wTotalLength, uco->bNumInterfaces, uco->bMaxPower*2);
		break;
	case USB_DT_INTERFACE:
		switch (id->bInterfaceClass){
		case MSC_CLASS: strcpy(mclass,"msc"); break;
		default: strcpy(mclass,"");
		}
		switch (id->bInterfaceSubClass){
		case MSC_SUBCLASS_TRANSPARENT: strcpy(subcl,"transp"); break;
		default: strcpy(subcl,"");
		}
		switch (id->bInterfaceProtocol){
		case MSC_PROTOCOL_BULK: strcpy(prot,"bulk"); break;
		default: strcpy(prot,"");
		}
		printf("interface desccr no=%d endps=%d class=%X(%s)"
			       " subclass=%X(%s) prot=%d(%s)\n",
			id->bInterfaceNumber, id->bNumEndpoints,
		       	id->bInterfaceClass, mclass, id->bInterfaceSubClass,
		       	subcl, id->bInterfaceProtocol, prot);
		break;

	case USB_DT_ENDPOINT:
		switch (ep->bmAttributes){
		case USB_EP_TYPE_BULK: strcpy(eptype,"bulk"); break;
		default: strcpy(eptype,"");
		}
		i= (ep->wMaxPacketSize[1]<<8)|ep->wMaxPacketSize[0];
		printf("endpoint desccr attr=%d(%s) addr=%X"
			       " packagesize=%d\n",
			ep->bmAttributes, eptype, ep->bEndpointAddress, i);
		break;
	default:
		printf("descr UNKNOWN typ=%d lng=%d\n", d[1], lng);
		break;
	}
	if ((lng>0) && ((tlng-= lng)>0)){
		d+= lng;
		goto more;
	}
}

int
uhEnumConfig(u8 *d, int tlng)
{	int got=0, i;
	usb_ifacedescr *id;
	usb_epdescr *ep;
	for (;tlng>0; tlng-= d[0], d+= d[0]){
		switch(d[1]){
		case USB_DT_INTERFACE:
			id= (usb_ifacedescr *) d;
			if ((id->bInterfaceClass   == MSC_CLASS)
			&& (id->bInterfaceSubClass == MSC_SUBCLASS_TRANSPARENT)
			&& (id->bInterfaceProtocol == MSC_PROTOCOL_BULK) )
				got=0x4;
			break;
		case USB_DT_ENDPOINT:
			ep= (usb_epdescr *) d;
			if (got==0)
			       	break;
			if (ep->bmAttributes != USB_EP_TYPE_BULK)
				break;
			i= (ep->wMaxPacketSize[1]<<8)|ep->wMaxPacketSize[0];
			if (ep->bEndpointAddress & USB_EP_DIR_IN) {
				uhx.devpipein = ep->bEndpointAddress&
				       			~USB_EP_DIR_IN;
				got|= 0x2;
			} else {
				uhx.devpipeout= ep->bEndpointAddress;
				uhx.devpipeoutLng= i;
				got|= 0x1;
			}
			break;
		}
	}
	return (got==0x7? 0: -1);
}


void
uhInitPipe(int pipe, int token)
{	volatile Uotghs *usb= UOTGHS;
	u32 type, size, devep;
	switch (pipe){
	case UHPIPE_CTRL:					// default?  cc 
	default:	type=	UOTGHS_HSTPIPCFG_PTYPE_CTRL;
			if (token<0)
				token=	UOTGHS_HSTPIPCFG_PTOKEN_SETUP;
			size=	UOTGHS_HSTPIPCFG_PSIZE_64_BYTE;
			devep= UHPIPE_CTRL;
			break;
	case UHPIPE_IN:	if (token<0)
				token=	UOTGHS_HSTPIPCFG_PTOKEN_IN;
			if ((devep= uhx.devpipein)==0)
				devep= UHPIPE_IN;
			goto ptypeblk;
	case UHPIPE_OUT: if (token<0)
				token=	UOTGHS_HSTPIPCFG_PTOKEN_OUT;
			if ((devep= uhx.devpipeout)==0)
				devep= UHPIPE_OUT;
	ptypeblk:	type=	UOTGHS_HSTPIPCFG_PTYPE_BLK;
			size=	UOTGHS_HSTPIPCFG_PSIZE_512_BYTE;
			break;
	}

	usb->UOTGHS_HSTPIP|= UOTGHS_HSTPIP_PEN0<<pipe;		// enable
	usb->UOTGHS_HSTPIP|= UOTGHS_HSTPIP_PRST0<<pipe;		// reset 
	usb->UOTGHS_HSTIDR= (UOTGHS_HSTIDR_PEP_0<<pipe);		// disable ints
	usb->UOTGHS_HSTPIPCFG[pipe]=				// config
	       		UOTGHS_HSTPIPCFG_PEPNUM(devep)|
			type|
			token|
			size|
			UOTGHS_HSTPIPCFG_PBK_1_BANK|
			UOTGHS_HSTPIPCFG_ALLOC;

	usb->UOTGHS_HSTPIP&= ~(UOTGHS_HSTPIP_PRST0<<pipe);	// stop reset
	usb->UOTGHS_HSTPIPIER[pipe]=				// enable ints
			UOTGHS_HSTPIPIER_PFREEZES|
			UOTGHS_HSTPIPIER_RXSTALLDES|
			UOTGHS_HSTPIPIER_PERRES;
	usb->UOTGHS_HSTPIPICR[pipe]= 0xF7;			// ack all ints
	usb->UOTGHS_HSTIER= (UOTGHS_HSTIER_PEP_0<<pipe);		// enable ints
}

void
uhInitPipeToken(int pipe, int token)
{
	uhInitPipe(pipe, token);
}

void
uhInitAllocDpram()
{
	char *dp= (char*)UOTGHS_DPRAM;
	uhx.fifoaddr[UHPIPE_CTRL]= dp;
	dp+= 64;				// PSIZE_64 see above
	uhx.fifoaddr[UHPIPE_IN]= dp;
	dp+= 512;				// PSIZE_512 see above
	uhx.fifoaddr[UHPIPE_OUT]= dp;
}


int
uhSendPipe(int pipe, u8 *buf, int lng)
{	volatile Uotghs *usb= UOTGHS;
	u64 t0;
	int ret= 0;
	uhInitPipeToken(pipe, UOTGHS_HSTPIPCFG_PTOKEN_SETUP);
	uhx.it.pipe= pipe;
	uhx.it.mask=  UOTGHS_HSTPIPISR_TXSTPI;
	uhx.it.done= 0;
	VERBOSE2 printf("uhSendPipe pipe=%d %08X:\n", pipe, uhx.fifoaddr[pipe]);
	VERBOSE2 dumphex((char *)buf, (u32)buf, lng);
	VERBOSE3 uhDumpx(0, "->fifo", 0);
	memcpy(uhx.fifoaddr[pipe], buf, lng);		// fill fifo
	usb->UOTGHS_HSTPIPICR[pipe]= 0xF7;		// ack all ints -> FIFOCON=1
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_TXSTPES;	// en txstp  int
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_FIFOCONC; // FIFOCOn=0, start 
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_PFREEZEC;	// unfreeze pipe

	for (t0=millis()+UHTIMEOUT; ;){		// wait TXOUTI
		if (uhx.it.done)
			break;
		uhSwitch();
		if (millis()>=t0){
			VERBOSE1 printf("ERROR uhSendPipe timeout\n");
			ret= uhError(UHERROR_TIMEOUT);
			break;
		}
	}
	if ((uhstate.verbose>1) && (ret==0))
		printf("uhSendPipe ok intisr=%08X isr=%08X dma=%08X lng= %d(%x)\n",
			uhx.it.isr, usb->UOTGHS_HSTPIPISR[pipe],
	 		0, lng, lng);
	if ((uhstate.verbose>0) && (ret!=0)){
		printf("uhSendPipe ERROR intisr=%08X lng= %d(%x)\n",
		uhx.it.isr, 0, lng, lng);
		uhDumpx(pipe,0,1);
	}
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_TXSTPEC;	// dis txstp int
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_PFREEZES;	// freeze pipe
	if (uhstate.state< UHSTAT_DEV) ret= UHERROR_NOTREADY;
	return ret;
}

//
// setup pipe, receive data, copie fifo->buf

int
uhReceivePipe(int pipe, u8 *buf, int lng)
{	volatile Uotghs *usb= UOTGHS;
	u32 dlng, isr;
	u64 t0;
	int ret= 0;
	uhInitPipeToken(pipe, UOTGHS_HSTPIPCFG_PTOKEN_IN);
	uhx.it.pipe= pipe;
	uhx.it.mask=  UOTGHS_HSTPIPISR_RXINI;
	uhx.it.done=  0;
	VERBOSE2 printf("uhReceivePipe pipe=%d\n", pipe);
//	VERBOSE2 uhDumpx(0, "<-rcvP", 0);
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_RXINES;	// en tr empty int
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_FIFOCONC;	// FIFOCOn=0, start 
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_PFREEZEC;	// unfreeze pipe

	for (t0=millis()+UHTIMEOUT; ;){
		if ((uhx.it.done) &&
			(usb->UOTGHS_HSTPIPIMR[pipe]& UOTGHS_HSTPIPIMR_PFREEZE))
			break;
		uhSwitch();
		if (millis()>=t0){
			VERBOSE1 printf("ERROR uhReceivePipe timeout\n");
			ret= uhError(UHERROR_TIMEOUT);
			break;
		}
	}
	isr= usb->UOTGHS_HSTPIPISR[pipe];
	dlng= isr>> UOTGHS_HSTPIPISR_PBYCT_Pos;
	memset(buf, 0, lng);
	memcpy(buf, uhx.fifoaddr[pipe], dlng<lng? dlng: lng);
	if ((uhstate.verbose>0) && ((uhstate.verbose>1)||(ret!=0))){
		printf("uhReceivePipe %s lng=%d(%X) ",
			ret==0? "ok": "error", dlng, dlng);
		uhDumpx(0,0,1);
	}
	VERBOSE2 dumphex((char *)buf, (u32)buf, dlng);
	usb->UOTGHS_HSTPIPICR[pipe]= 0xF7;			// ack all ints 
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_RXINEC;	// en tr empty int
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_PFREEZES;	// freeze pipe
	if (uhstate.state< UHSTAT_DEV) return UHERROR_NOTREADY;
	if (ret==0) ret= dlng;
	return ret;
}

void
uhDmaSetupIn(int pipe, u8 *buf, int lng)
{	volatile Uotghs *usb= UOTGHS;
	volatile UotghsHstdma *hs= & usb->UOTGHS_HSTDMA[pipe-1];
	u32 ctrl;
	uhx.uhDmaStartaddress= buf;
	hs->UOTGHS_HSTDMANXTDSC=0;
	hs->UOTGHS_HSTDMAADDRESS= (u32)buf;
	ctrl=	UOTGHS_HSTDMACONTROL_CHANN_ENB;
	hs->UOTGHS_HSTDMACONTROL= (lng<<16)| ctrl;
}

int
uhDmaSetupOut(int pipe, u8 *buf, int lng)
{	volatile Uotghs *usb= UOTGHS;
	volatile UotghsHstdma *hs= & usb->UOTGHS_HSTDMA[pipe-1];
	u32 ctrl;
	int i;
	uhx.uhDmaStartaddress= buf;
	hs->UOTGHS_HSTDMANXTDSC=0;
	hs->UOTGHS_HSTDMAADDRESS= (u32)buf;
	ctrl=	UOTGHS_HSTDMACONTROL_CHANN_ENB |
		UOTGHS_HSTDMACONTROL_END_B_EN;		// -> pipe
	hs->UOTGHS_HSTDMACONTROL= (lng<<16)| ctrl;
	for (i=0; i<1000; i++)
		if (hs->UOTGHS_HSTDMAADDRESS>= ((u32)uhx.uhDmaStartaddress+lng))
			break;
	if (i>=1000){
		VERBOSE1 printf("ERROR uhDmaSetupOut send data to fifo\n");
		return -1;
	}
	return 0;
}

u32
uhDmaEnd(int pipe)			// return length of last dma transfer
{	volatile Uotghs *usb= UOTGHS;
	volatile UotghsHstdma *hs= & usb->UOTGHS_HSTDMA[pipe-1];
	return hs->UOTGHS_HSTDMAADDRESS- (u32)uhx.uhDmaStartaddress;
}

//	setup pipe, setup dma, copy data->fifo, send data
int
uhSend(int pipe, u8 *buf, int lng)
{	volatile Uotghs *usb= UOTGHS;
	u32 dlng;
	u64 t0;
	int ret= 0;
	if (uhstate.verbose>1) {
		printf("uhSend pipe=%d: ", pipe);
		if (strncmp((char *)buf, "USBC", 4)==0)
			printf("%s", scsi2asc(buf[15])); 
		printf("\n");
		dumphex((char *)buf, (u32)buf, lng);
	}
    more:
	uhx.it.pipe= pipe;
	uhx.it.mask=  UOTGHS_HSTPIPISR_TXOUTI;
	uhx.it.done= 0;
	dlng= uhx.devpipeoutLng;
	if ((dlng==0)||(dlng>lng))
		dlng= lng;
	if (uhDmaSetupOut(pipe, buf, dlng)<0){			// fill fifo
			ret= uhError(UHERROR_INTERN);
			goto ex;
	}
	usb->UOTGHS_HSTPIPICR[pipe]= 0xF7;		// ack all ints -> FIFOCON=1
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_TXOUTES;	// en tr empty int
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_PFREEZEC;	// unfreeze pipe
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_FIFOCONC; // FIFOCOn=0, start 

	for (t0=millis()+UHTIMEOUT; uhx.it.done==0;){
		uhSwitch();
		if (millis()>=t0){
			VERBOSE1 printf("ERROR uhSend timeout\n");
			ret= uhError(UHERROR_TIMEOUT);
			break;
		}
	}
	dlng= uhDmaEnd(pipe);
	if ((dlng<=0) && (ret==0)) ret= uhError(UHERROR_INTERN);
	if ((uhstate.verbose>0) && ((uhstate.verbose>1)||(ret!=0)))
		printf("uhSend %s intisr=%08X isr=%08X dma=%08X lng= %d(%x)\n",
		ret==0? "ok": "error",
		uhx.it.isr, usb->UOTGHS_HSTPIPISR[pipe], 0, dlng, dlng);
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_PFREEZES;	// freeze pipe
	if (uhstate.state< UHSTAT_ENUM) ret= UHERROR_NOTREADY;
	if ((ret==0) && (dlng< lng)) {				// more
		buf+= dlng;
		lng-= dlng;
		goto more;
	}
    ex:
	return ret;
}


//	setup dma, pipe, receive data

int
uhReceive(int pipe, u8 *buf, int lng, int tout)
{	volatile Uotghs *usb= UOTGHS;
	u32 dlng;
	u64 t0;
	int ret= 0;
	u8 *cbuf= buf;
	int   clng= lng;
	memset(buf, 0x33, lng);
    more:
	uhx.it.pipe= pipe;
	uhx.it.mask=  UOTGHS_HSTPIPISR_RXINI;
	uhx.it.done=  0;
	VERBOSE2 printf("uhReceive pipe=%d\n", pipe);
//	usb->UOTGHS_HSTPIPICR[pipe]= 0xF7;		// ack all ints -> FIFOCON=1
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_RXINES;	// en tr empty int
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_FIFOCONC; // FIFOCOn=0, start 
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_PFREEZEC;	// unfreeze pipe
	uhDmaSetupIn(pipe, cbuf, clng);

	for (t0=millis()+ tout; uhx.it.done==0;){
		uhSwitch();
		if (millis()>=t0){
			VERBOSE1 printf("ERROR uhReceive timeout\n");
			ret= uhError(UHERROR_TIMEOUT);
			break;
		}
	}
	if (uhx.it.done<0)
		ret= uhError(uhx.it.done);			// STALL
	dlng= uhDmaEnd(pipe);
	if ((uhstate.verbose>1) ||
		       	((uhstate.verbose>1)&&(ret!=0)&&(ret!=UHERROR_STALL))){
		printf("uhReceive %s lng=%d(%X) intisr= ",
	     		ret==0?"ok":"error", dlng,dlng, uhx.it.isr);
		uhDumpx(0,0,1);
	}
	usb->UOTGHS_HSTPIPIDR[pipe]= UOTGHS_HSTPIPIDR_RXINEC;	// en tr empty int
	usb->UOTGHS_HSTPIPIER[pipe]= UOTGHS_HSTPIPIER_PFREEZES;	// freeze pipe
	if (uhstate.state< UHSTAT_ENUM) ret= UHERROR_NOTREADY;
	if ((ret==0) && (dlng< clng)) {				// more
		cbuf+= dlng;
		clng-= dlng;
		goto more;
	}
	VERBOSE2 dumphex((char *)buf, (u32)buf, lng);
	return ret;
}

void
uhConnect(int present, u32 isr)
{	static  u64 otic; static int ocnt; int v;
	if ((millis()-otic)>1000) ocnt=0;		// avoid >5 msg/s
	otic= millis();
	ocnt++;
	if (present){
		uhx.cnt.connect++;
		uhSetStateUp(UHSTAT_DEV);
		if (ocnt<5){
			UHLOG("dev con %08x", isr);
			VERBOSE2 printf("dev connected\n");
		}
	} else {
		v= uhstate.verbose;
		memset(&uhstate, 0, sizeof(uhstate));
		uhstate.verbose= v;
		uhx.cnt.disconnect++;
		uhSetStateUp(UHSTAT_NODEV);
		UOTGHS->UOTGHS_HSTCTRL= 0;	// host to idle state
						// for the sakeOf Toshiba
		if (ocnt< 5){
			UHLOG("dev discon %08x", isr);
			VERBOSE2 printf("dev disconnected\n");
		}
	}
}

void
uhInterrupt()
{	volatile Uotghs *usb= UOTGHS;
	u32 pipe, isr, imr;
	uhStatServer();
	isr= UOTGHS->UOTGHS_HSTISR;
	imr = UOTGHS->UOTGHS_HSTIMR;
	pipe= ctz(((isr & imr) >> 8) | (1 << 10));
	if (isr & UOTGHS_HSTISR_HSOFI){				// sof
		usb->UOTGHS_HSTICR= UOTGHS_HSTICR_HSOFIC;
		uhx.cnt.sof++;
		return;
	}
	VERBOSE2 printf(".");
	if (imr & isr & UOTGHS_HSTISR_DDISCI){			// disconnect
		usb->UOTGHS_HSTICR= UOTGHS_HSTICR_DDISCIC |
				   UOTGHS_HSTICR_DCONNIC;

		uhConnect(0, isr);
		return;
	}
	if (imr & isr & UOTGHS_HSTISR_DCONNI){			// connect
		usb->UOTGHS_HSTICR= UOTGHS_HSTICR_DCONNIC|
				   UOTGHS_HSTICR_DDISCIC;
		uhConnect(1, isr);
		return;
	}
	if (pipe <10){
		uhInterruptPipe(pipe);
		uhStatServer();
		return;
	}
	printf("ERROR unexpected usb host int isr=%08X imr=%08X\n", isr, imr);
	usb->UOTGHS_HSTICR= 0x7F;
}
		
void
uhInterruptPipe(int pipe)
{	volatile Uotghs *usb= UOTGHS;
	u32 isr, imr, err, ack;
	isr= usb->UOTGHS_HSTPIPISR[pipe];
	imr= usb->UOTGHS_HSTPIPIMR[pipe];
	err= usb->UOTGHS_HSTPIPERR[pipe];
	uhx.cnt.pipeint++;
	if ((uhx.it.pipe==pipe) && ((uhx.it.mask&isr)!=0)&& (uhx.it.done==0)){
		uhx.it.done= 1;
	endpipe:
		uhx.it.isr= isr;
		UHLOG("p%d: int %X isr=%08X", pipe, isr&imr, isr);
		ack= isr&uhx.it.mask;
		usb->UOTGHS_HSTPIPICR[pipe]= ack;		// ack int
		return;
	}
	if (isr&UOTGHS_HSTPIPISR_RXSTALLDI){
		VERBOSE2 uhDumpx(pipe,"ERROR usb pipe STALL", 0);	// STALL
		uhx.cnt.stall++;
		if ((uhx.it.pipe==pipe) && (uhx.it.done==0)){
			uhx.it.done= UHERROR_STALL;
			goto endpipe;
		}
	} else	VERBOSE1 uhDumpx(pipe,"ERROR unexpected usb pipe int", 0);
	if (err!=0)
		usb->UOTGHS_HSTPIPERR[pipe]=0;
	UHLOG("p%d: int %X isr=%08X", pipe, isr&imr, isr);
	usb->UOTGHS_HSTPIPICR[pipe]= 0xF7;			// ack all
}

#if 0 // just an example, to handle host & dev drivers
void UOTGHS_Handler()
{
	if (UOTGHS->UOTGHS_CTRL& UOTGHS_CTRL_UIMOD)
		udd_interrupt();
	else	uhInterrupt();
}
#endif

const char *uhpiperr[]={ "0 datagl","1 datapid","2 pid","3 timeout","4 crc16", 0 };
const char *uhpipimr[]={ "0 rxin", "1 txout", "2 txstp", "3 perr", "4 naked",
			"5 overf", "6 rxstalld", "7 shortpck", 
			"12 nbusybk", "14 ficocon", "16 pdishdma", "17 pfreeze",
			"18 rstdt", 0};
const char *uhpipisr[]= { "0 rxin", "1 txout", "2 txstp", "3 perr", "4 naked",
			"5 overf", "6 rxstalld", "7 shortpack",
			"16 rwall", "18 cfgok", "12,2 nbusybk", "14,2 currbk",
		        "20,12 pbyct",	0 };


char *
uhDumpBits(u32 reg, char *buf, const char **txt)
{	const char *t;
	char *s= buf;
	u32 msk, r2;;
	int i, cnt;
	for (cnt=0; *txt!=0; txt++) {
		t= *txt;
		i= strtol(t, (char **)&t, 10);
		if (*t==','){
			msk= strtol(t+1, (char **)&t, 10);
		} else	msk=1;
		msk= (1<<msk)-1;			// 1->1, 2->3, 3->7
		if (*t==' ')t++;
		if ((r2= ((reg>>i) & msk))!=0){
			if (cnt++>0)
				*s++=',';
			if (msk==1)
				s+= sprintf(s, "%s", t);
			else	s+= sprintf(s, "%s=%d", t, r2);
		}
	}
	*s= 0;
	return buf;
}

void
uhDump()
{	uhDumpx(-1,"",1);
}

void
uhDumpx(int pipe, const char *pre, int destr)				// debug only
{	volatile Uotghs *usb= UOTGHS;
	volatile Pmc *pmc = PMC;
	u32 sta, isr, imr, ad, err, cfg, pepnum;
	const char *ptoken;
	char imrbits[50];
	char isrbits[50];
	char errbits[50];
	int i, n;
	if (pre==0) pre="";
	ad= usb->UOTGHS_HSTADDR1;
	if (pipe<0) {
	uhPrintState();
	printf("%s usb clk ckgr_uckr=%08X pmc_sr=%08X usb=%08X scsr=%08X"
		" ctrl=%08X stat=%08X\n",
		  pre, pmc->CKGR_UCKR, pmc->PMC_SR, pmc->PMC_USB, pmc->PMC_SCSR,
		  usb->UOTGHS_CTRL, usb->UOTGHS_SR);
	printf("%s host ctrl=%08X isr=%08X imr=%08X pip=%08X\n",
		pre, usb->UOTGHS_HSTCTRL, usb->UOTGHS_HSTISR, usb->UOTGHS_HSTIMR,
			usb->UOTGHS_HSTPIP);
	printf("%s ints=%d sof=%d cnct=%d disc=%d stall=%d tout=%d", pre,
		uhx.cnt.pipeint, uhx.cnt.sof,
	       	uhx.cnt.connect, uhx.cnt.disconnect, uhx.cnt.stall, uhx.cnt.tout);
	for (i=0; i<6; i++) printf(" %s:%d", uhAscErrors[i], uhx.cnt.errs[i]);
	printf("\n");
	}
	for (n=0; n<=2; n++){
		if ((pipe>=0) && (pipe!=n))
			continue;
		imr= usb->UOTGHS_HSTPIPIMR[n];
		isr= usb->UOTGHS_HSTPIPISR[n];
		err= usb->UOTGHS_HSTPIPERR[n];
		uhDumpBits(imr, imrbits, uhpipimr);
		uhDumpBits(isr, isrbits, uhpipisr);
		uhDumpBits(err, errbits, uhpiperr);
		cfg= usb->UOTGHS_HSTPIPCFG[n];
		pepnum=  (cfg& UOTGHS_HSTPIPCFG_PEPNUM_Msk)>> UOTGHS_HSTPIPCFG_PEPNUM_Pos;
		switch (cfg& UOTGHS_HSTPIPCFG_PTOKEN_Msk){
			case UOTGHS_HSTPIPCFG_PTOKEN_IN:  ptoken="in "; break;
			case UOTGHS_HSTPIPCFG_PTOKEN_OUT: ptoken="out"; break;
			default:		   ptoken="???"; break;
		}
		printf("%s pipe %d->%d %s ad=%d cfg=%05X imr=%06X isr=%08X err=%X",
			pre, n, pepnum, ptoken, (ad>>(8*n))&0x7F, cfg, imr, isr, err);
		if (n>0){
			printf(" dma addr=%08X ctrl=%04X",
				usb->UOTGHS_HSTDMA[n-1].UOTGHS_HSTDMAADDRESS,
				usb->UOTGHS_HSTDMA[n-1].UOTGHS_HSTDMACONTROL&0xFFFF);
			if (destr){
				sta= usb->UOTGHS_HSTDMA[n-1].UOTGHS_HSTDMASTATUS;
				printf(" cnt=%04X stat=%04X", sta>>16, sta& 0xFFFF);
			}
		}
		printf(" imr=%s isr=%s err=%s", imrbits, isrbits, errbits);
		printf("\n");
	}
	if (pipe<0)
	printf("%s dev pipe in=%d out=%d packagesz=%d\n", pre,
		uhx.devpipein, uhx.devpipeout, uhx.devpipeoutLng);
}

void
uhramlog(const char *fmt, ...)			// debug support
{
}

// +++ msc + scsi

// +++ msc wrapper
//  doc: usb mass storage class bulk-only transport: 5: command/data/status
//  little endian 
typedef struct _cbwrapper {		// msc command block wrapper
	char	cbwsig[4];		//  0: signature
	u32	cbwtag;			//  4: tag
	u32	cbwlng;			//  8: data transfer length
	u8	cbwflags;		//  C: flags
	u8	cbwlun;			//  D: dev logical unit number
	u8	cbwcblng;		//  E: length cbwcb
	u8	cbwcb[16];		//  F: cbwcb
} cbwrap;
#define CBWRAPLNG 31
#define CBWFLAGIN 0x80
#define CBWFLAGOUT 0x00

typedef struct _cswrapper {		// msc command status wrapper
	char	cswsig[4];		//  4: signature
	u32	cswtag;			//  8: tag
	u32	cswlng;			//  C: data residue
	u8	cswstatus;		//  D: status
} cswrap;
#define CSWRAPLNG 13

// +++ scsi commands
// big endian 
// doc: Usb Mass Storage Calls UFI Command spec
typedef struct _scsicmd {
	u8	a[12];
} scsicmd;
#define SCSICMDLNG  12

#define SCSI_TESTUNITREADY	0x00
#define SCSI_REQUESTSENSE	0x03
#define SCSI_INQUIRY		0x12
//efine SCSI_MODESENSE		0x1A	
#define SCSI_READCAPACITY	0x25
#define SCSI_READBLOCK		0x28
#define SCSI_WRITEBLOCK		0x2A

typedef struct _scsiInquiry {		// should be in msc scsi, needed
	u8	type;			//  0:per. data type
#define SCSI_TYPE_CONNECTED	0
	u8	flags1;
#define SCSI_FLAG1_REMOVABLE	0x80
	u8	version;
	u8	flags3;
	u8	addlng;			//  add. lng
	u8	flags5;
	u8	flags6;
	u8	flags7;
	u8	vendor_id[8];		//  8: vendor
	u8	product_id[16];		// 10: product
	u8	product_rev[4];		// 20: revision
} scsiInquiry;
#define SCSI_INQUIRYLNG		0x24

typedef struct _scsiCapacity{
	u32	lastlba;		// 0: last lba 
	u32	lastlbalng;		// 0: last lba lng
} scsiCapacity;
#define SCSI_CAPACITYLNG	0x8

typedef struct _scsiSense {
	u8	ecode;
	u8	res1;
	u8	sensekey;
	u8	info[4];
	u8	addlng;
	u8	res2[4];
	u8	asc;			// add sense code
	u8	ascq;			// add sense code quqlif
	u8	res3[3];
} scsiSense;
#define SCSI_SENSELNG  17

int mscReceive(u8 *cbwcb, int cbwcblng, u8 *buf, int datalng, int tout);
int mscSend(u8 *cbwcb, int cbwcblng, u8 *buf, int datalng);
int mscSendWrapper(int pipe, u8 *cbwcb, int flags, int cbwcblng, int datalng);
int mscReceiveWrapper(int pipe, u32 stall);

const char
*scsi2asc(int code)
{	switch(code){
	case  SCSI_TESTUNITREADY:	return "test unit ready";
	case  SCSI_REQUESTSENSE:	return "request sense";
	case  SCSI_INQUIRY:		return "inquiry";
//	case  SCSI_MODESENSE:		return "mode sense";
	case  SCSI_READCAPACITY:	return "read capacity";
	case  SCSI_READBLOCK:		return "read block";
	case  SCSI_WRITEBLOCK:		return "write block";
	}
	return "";
}


//       getcapa
//        stall-> ResetDevEndpointIn, read stat, again
void
mscInit()
{	int ret, i1, i2=-1, i3=-1;

	if (uhstate.state< UHSTAT_ENUM) return; //  UHERROR_NOTREADY;
	VERBOSE2 printf("uhInit mscInit\n");
	VERBOSE3 uhDumpx(-1, "mscinit", 1);
	i1=ret= mscInquiry();
	if (ret>=0)
		i2=ret=mscRequestSense();
	if (ret>=0)
		i3=ret= mscReadCapacity();
	if (ret>=0) uhSetStateUp(UHSTAT_READY);
	if (uhstate.state!= UHSTAT_READY)
		VERBOSE1 printf("ERROR  ");
	VERBOSE2 printf("stat=%d %s inquiry= %d reqSense=%d getcapa=%d\n",
		       	uhstate.state, uhStatAsc(), i1, i2, i3);
}

int
mscTstUnitReady()
{	scsicmd sc;
	int i;
	if (uhstate.state< UHSTAT_ENUM) return UHERROR_NOTREADY;
	memset(&sc, 0, sizeof(sc));
	sc.a[0]= SCSI_TESTUNITREADY;
	i= mscReceive((u8 *)&sc, SCSICMDLNG, 0, 0, UHTIMEOUT);
	return i;
}

int
mscInquiry()
{	scsicmd sc;
	scsiInquiry inq;
	int i, rcnt=2;
    redo:
	if (uhstate.state< UHSTAT_ENUM) return UHERROR_NOTREADY;
	memset(&sc, 0, sizeof(sc));
	sc.a[0]= SCSI_INQUIRY;
	sc.a[4]= SCSI_INQUIRYLNG; 
	i= mscReceive((u8 *)&sc, SCSICMDLNG, (u8*)&inq, SCSI_INQUIRYLNG, UHTIMEOUT);
	if ((i== UHERROR_STALL)&&(--rcnt>0)){
		uhWaitMs(100);
		goto redo;
	}
	if (i<0)
		memset(&inq, 0, sizeof(inq));
	memcpy(uhstate.vendor_id, inq.vendor_id, sizeof(inq.vendor_id));
	memcpy(uhstate.product_id, inq.product_id, sizeof(inq.product_id));
	memcpy(uhstate.product_rev, inq.product_rev, sizeof(inq.product_rev));
	return i;
}

int
mscReadCapacity()
{	scsicmd sc;
	scsiCapacity cap;
	int i, capa, rcnt= 3;
    redo:
	if (uhstate.state< UHSTAT_ENUM) return UHERROR_NOTREADY;
	memset(&sc, 0, sizeof(sc));
	sc.a[0]= SCSI_READCAPACITY;
	uhstate.capacity= 0;
	i= mscReceive((u8 *)&sc, SCSICMDLNG, (u8*)&cap, SCSI_CAPACITYLNG,UHTIMEOUT);
	if ((i== UHERROR_STALL)&&(--rcnt>0)){
		uhWaitMs(100);
		goto redo;
	}
	if (i<0) return i;
	capa=  __builtin_bswap32(cap.lastlba);
	uhstate.capacity= capa;
	VERBOSE2 printf("capacity=%d blocks %d MB\n", capa,  capa/2000);
	return 0;
}


int
mscRequestSense()
{	scsicmd sc;
	scsiSense ss;
	int i, rcnt= 3;
    redo:
	if (uhstate.state< UHSTAT_ENUM) return UHERROR_NOTREADY;
	memset(&sc, 0, sizeof(sc));
	sc.a[0]= SCSI_REQUESTSENSE;
	sc.a[4]= SCSI_SENSELNG;
	uhstate.capacity= 0;
	i= mscReceive((u8 *)&sc, SCSICMDLNG, (u8*)&ss, SCSI_SENSELNG,
		       	UHTIMEOUTSENSER);
	if ((i== UHERROR_STALL)&&(--rcnt>0)){
		uhWaitMs(100);
		goto redo;
	}
	if (i<0)
		return i;
	VERBOSE2 printf("request sense: error=%02X key=%02x asc=%02x ascq=%02x\n",
			ss.ecode, ss.sensekey, ss.asc, ss.ascq);
	return 0;
}

void
little2big32(u8 *to, u32 from)		// conver little<->big endian
{	u8* fr= (u8*)&from;
	to[0]= fr[3];			
	to[1]= fr[2];
	to[2]= fr[1];
	to[3]= fr[0];
}

int
mscReadBlock(u32 lba, char *buf)
{	scsicmd sc;
	int i;
#if 0
	if (!ISALIGN32(buf)){
		VERBOSE1 printf("ERROR mscReadBlock alignment \n");
		return UHERROR_IO;
	 }
#endif
	if (uhstate.state< UHSTAT_READY) return UHERROR_NOTREADY;
	if (lba>= uhstate.capacity) return UHERROR_IO;
	memset(&sc, 0, sizeof(sc));
	sc.a[0]= SCSI_READBLOCK;
	little2big32(&sc.a[2], lba);
	sc.a[8]= 1;				//big endian
	if ((i= mscReceive((u8 *) &sc, SCSICMDLNG, (u8*) buf, 512, UHTIMEOUT))<0){
		uhx.cnt.mscErrors++;
		uhSetState(UHSTAT_DEV);		// set down to reinit
		return i;
	}
	VERBOSE2 printf("mscReadBlock %d ok\n", lba);
	return 0;
}


int
mscWriteBlock(u32 lba, char *buf)
{	scsicmd sc;
	int i;
#if 0
	if (!ISALIGN32(buf)){
		VERBOSE1 printf("ERROR mscWriteBlock alignment \n");
		return UHERROR_IO;
	}
#endif
	if (uhstate.state< UHSTAT_READY) return UHERROR_NOTREADY;
	if (lba>= uhstate.capacity) return UHERROR_IO;
	memset(&sc, 0, sizeof(sc));
	sc.a[0]= SCSI_WRITEBLOCK;
	little2big32(&sc.a[2], lba);
	sc.a[8]= 1;				//big endian
	if ((i= mscSend((u8*) &sc, SCSICMDLNG, (u8*) buf, 512))<0){
		uhx.cnt.mscErrors++;
		uhSetState(UHSTAT_DEV);		// set down to reinit
		return i;
	}
	VERBOSE2 printf("mscWriteBlock %d ok\n", lba);
	return 0;
}

//	send msc wrapper
//	receive data
//	receive wrapper
int
mscReceive(u8 *cbwcb, int cbwcblng, u8 *buf, int datalng, int tout)
{	int i, i2;
	if ((i=mscSendWrapper(UHPIPE_OUT, cbwcb, CBWFLAGIN, cbwcblng, datalng))<0)
		return i;
	if (datalng>0){
		UHLOG("uhReceive:");
		i=uhReceive(UHPIPE_IN, buf, datalng, tout);
		UHLOG("mscReceive end");
		if (i==UHERROR_STALL)
			uhResetDevEndpointIn();
	}
	i2= mscReceiveWrapper(UHPIPE_IN, i);
	if (i<0) return i; else return i2;
}

//	send msc wrapper
//	send data
//	receive wrapper
int
mscSend(u8 *cbwcb, int cbwcblng, u8 *buf, int datalng)
{	int i;
	if ((i=mscSendWrapper(UHPIPE_OUT, cbwcb, CBWFLAGOUT, cbwcblng, datalng))<0)
		return i;
	UHLOG("uhSend:");
	if ((i=uhSend(UHPIPE_OUT, buf, datalng))<0)
		return i;
	UHLOG("mscReceive end");
	return mscReceiveWrapper(UHPIPE_IN, 0);
}

//	send msc wrapper
int
mscSendWrapper(int pipe, u8 *cbwcb, int flags, int cbwcblng, int datalng)
{	cbwrap w;
	UHLOG("Sendwrap:");
	memset(&w, 0, sizeof(w));
	memcpy(w.cbwsig, "USBC", 4);
	w.cbwtag= ++uhx.msctag;
	w.cbwlng=  datalng;
	w.cbwflags=  flags;
	w.cbwlun=  0;
	w.cbwcblng=  cbwcblng;
	memcpy(w.cbwcb, cbwcb, cbwcblng);
	return uhSend(pipe, (u8*) &w, CBWRAPLNG);
}


//	receive msc wrapper
//	verify wrapper
int
mscReceiveWrapper(int pipe, u32 stall)
{	int i;
	cswrap csw;
	UHLOG("Recwrap:");
	if ((i=uhReceive(pipe, (u8 *)&csw, CSWRAPLNG, UHTIMEOUT))<0)
		return i;
	if (memcmp(csw.cswsig,"USBS", 4)!=0){
		VERBOSE1 printf("ERROR mscReceiveWrap bad reply frame: sig\n");
		return uhError(UHERROR_REPLY);
	}
	if (csw.cswtag!= uhx.msctag){
		VERBOSE1 printf("ERROR mscReceiveWrap bad reply frame: tag %d %d\n",
				uhx.msctag, csw.cswtag);
		return uhError(UHERROR_REPLY);
	}
	if (csw.cswstatus!=0){
		if ((uhstate.verbose>1) ||
			       	((uhstate.verbose>0) && (stall!= UHERROR_STALL)))
		printf("ERROR mscReceiveWrap bad reply state=%02X\n",
			  				csw.cswstatus);
		return uhError(UHERROR_REPLY);
	}
	return 0;
}

