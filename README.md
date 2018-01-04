# dueUsbhost
Arduino due usb host driver class msc for usb key

Version 1712 December 2017
Also handles usb sd-card readers.
Does not handle other USB devices, nor hubs.
When starting the USB host driver, the USB device driver is no longer available.
There is no automatism to switch from usb host to usb dev driver and vice versa.
usb host on Arduino due native USB connector.
Needs an otg cable: USB micro B (board) to USB A (key).
Or an USB key with micro B connector.
Uses UOTGVBOF=1 to supply current to the connector.
May need additional power supply 5v on the connector
Fatfs routines are not reentrant, no multitasking support, see ffconf.h
Fatfs gat_fattime to set file access time, not realized, no rtc
Last file write time is always: 24.7.2107 16:44 (gmt)

Sd-card holder: Do not replace the sd-card, without extracting the holder.
The driver will notice the change, but not before the next read, or write.

Uses the serial port over the programming port as user interface.
115200 baud, Newline (LF,'\n')= end of line, end of command
Led "L" flashes  20ms/s, when program running,
	flashes 500ms/s, when disk is ready.

The usbdriver was based on Atmel Studio "USB Host MSC FatFS Example SAMV71-XULTRA"
completely rewritten.
fatfs: from Atmel Studio "USB Host MSC FatFS Example SAMV71-XULTRA"

commands:
go			You must execute this command.
			Start server for disk on task 2 in background.
			connect, disconnect, init
			A new disk shows an info line "READY..."
			And a second line about the file system in partition 0.
blrd <lba>		read and dump a block (lba= logical block number, 0..)
blwr <lba> <pattern>	write a block
bltst <cnt>		disk test.
			write / read & compare a sequence of blocks <cnt> times
			may destroy the file system on the disk!
u			dump internal driver state
v=<n>			set verbose level to 0,1,2, default is 1

file system commands:
ls			show root dir
rd <filename>		read and dump the file
wr <filename> <lines> <pattern>
			create file, write <lines> lines, with pattern

dueUsbhost.ino:
- main program
- usbdriver integration
	uhWaitMs()		wait some milliseconds
	uhSwitch()		switch to another task (if exist)
	uhStatServer()		display state on the led (led on= ready)
	millis()		get a millisecond tick
	printf()
- fat integration
	get_fattime()		shall return real time for file write time
				at the moment always: 24.7.2107 16:44 (gmt)
	ff_convert		long file name support
	ff_wtupper		long file name support
- fat to usbdriver interface
	disk_initialize()
	disk_status()
	disk_read()
	disk_write()
	disk_ioctl()
- debug support
	dumphex()

usb host/msc driver functions: see usbhost.h
void uhInit();
int mscReadBlock(u32 lba, char *buf);		return: 0= OK
						<0: UHERROR_NOTREADY ...
int mscWriteBlock(u32 lba, char *buf);
void uhPrintState();				print state
void uhDump();					print internal state

Fat function: see ff.h

Interrupts:
uhInit() writes
	gpf_isr=  uhInterrupt;
to enable uhInerrupt() as usb interrupt handler.
( gpf_isr? what is this? where is this in the Arduino doc? )
Thereby switching of any usb device driver.
In the driver you find an example for a UOTGHS_Handler(),
to handle both: host, and dev

usbhost driver was tested on the following USB keys, and sd-card readers
   		Gb
sandisk blade	 4
philips		16
listo		16
sandisk 3g ddrv	16
toshiba		32
Lexar		32
Emtec		32
pny		32
sony		16
essential 	16
sandisk reader+sd
essentiel reader+sd
I had to addapt the driver nearly for each other USB key !
Some USB drives send out some error messages during enumeration phase.
But when state comes to READY, ignore these errors.

Comments are welcome. Please send to
Andreas Meyer, mail controlord/at/controlrd/dot/fr
