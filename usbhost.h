
//  usb-host driver, class msc / scsi, to handle a USB key



#define UHSTAT_NULL	0		// not initialized
#define UHSTAT_NODEV	1		// dev not present
#define UHSTAT_DEV	2		// dev present
#define UHSTAT_ENUM	3		// enum done
#define UHSTAT_READY	4		// disk info valid

#define UHERROR_NOTREADY -1
#define UHERROR_TIMEOUT -2
#define UHERROR_STALL -3		// not really an error
#define UHERROR_REPLY -4
#define UHERROR_INTERN -5
#define UHERROR_IO -6

void uhInit();				// initialise usb host interface
       					// 	until state = UHSTAT_READY
int mscReadBlock(u32 lba, char *buf);	// read block
int mscWriteBlock(u32 lba, char *buf);

struct _uhstate {
	volatile int	state;		// off, enumeration, ready
	u32	capacity;		// total number of blocks

	u16	idVendor;		// enumeration
	u16	idProduct;
	u8	vendor_id[8+1];	
	u8	product_id[16+1];
	u8	product_rev[4+1];
	u16	maxPower;
	int	verbose;
};

extern struct _uhstate uhstate;

void uhPrintState();			// print state
void uhDump();				// print internal state


// +++ external functions for integration

void uhWaitMs(int n);
void uhSwitch();
void uhStatServer();

// millis();				// return system millisecond tick
// printf();
