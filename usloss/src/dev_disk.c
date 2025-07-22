
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "project.h"
#include "globals.h"
#include "usloss.h"
#include "dev_disk.h"
#include "devices.h"

//#define LOGGING_DISK_IO

typedef struct
{
	int fd;					// Open fd for disk file.
	int tracks;				// # tracks in the disk.
	int currentTrack;		// head position
	int status;				// Disk's status
	device_request request; // Current request
} DiskInfo;

static DiskInfo disks[DISK_UNITS];

/*
 *  Initialize all disk handling code.
 */
dynamic_fun void disk_init(void)
{
	struct stat inode;
	int i;
	char name[256];

	for (i = 0; i < DISK_UNITS; i++)
	{
		sprintf(name, "disk%d", i);
		disks[i].fd = open(name, O_RDWR, 0);
		if (disks[i].fd != -1)
		{
			/*  Figure out how may tracks it has - check for errors */
			usloss_sys_assert(fstat(disks[i].fd, &inode) == 0,
							  "Error in fstat() on disk file");
			if (inode.st_size % (DISK_TRACK_SIZE * DISK_SECTOR_SIZE) != 0)
			{
				console("Disk %s has an incomplete last track\n", name);
				close(disks[i].fd);
				disks[i].fd = -1;
			}
			disks[i].tracks = inode.st_size /
							  (DISK_TRACK_SIZE * DISK_SECTOR_SIZE);
			disks[i].currentTrack = 0;
			disks[i].status = DEV_READY;
		}
	}
}

/*
 *  Returns the current device status of the disk.  Resets the status to
 *  DEV_READY if the last I/O operation resulted in an error.
 */
dynamic_fun int disk_get_status(int unit, int *statusPtr)
{
	if ((unit < 0) || (unit >= DISK_UNITS) || (disks[unit].fd == -1))
	{
		return DEV_INVALID;
	}
	*statusPtr = disks[unit].status;
	if (*statusPtr == DEV_ERROR)
	{
		disks[unit].status = DEV_READY;
	}
	return DEV_OK;
}

/*
 *  Handles requests to the disk device (via the outp() instruction).
 */
dynamic_fun int disk_request(int unit, void *arg)
{
	int rc;
	int delay;
	device_request *request = (device_request *)arg;

	if ((unit < 0) || (unit >= DISK_UNITS) || (disks[unit].fd == -1))
	{
		rc = DEV_INVALID;
		goto done;
	}
	/*  Check if a request is already pending - if so, do nothing, else
	indicate a pending request */
	if (disks[unit].status == DEV_BUSY)
	{
		rc = DEV_BUSY;
		goto done;
	}
	disks[unit].status = DEV_BUSY;

	/*  Store the new request data, calculate
	the delay to fulfill the request, and schedule the interrupt */
	memcpy(&disks[unit].request, request, sizeof(*request));
	/*
	 * A disk access should take 30ms (3 ticks), tops.
	 */
	if (request->opr == DISK_SEEK)
		delay = 1 + (abs((disks[unit].currentTrack) -
						 ((int)request->reg1)) %
					 10);
	else
		delay = 1;
	if (delay > 3)
		delay = 3;
	schedule_int(DISK_INT, (void *)unit, delay);
	rc = DEV_OK;
done:
	return rc;
}

/*
 *  This routine performs the actual I/O actions. It is called just before
 *  the interrupt signalling I/O completion is sent. Note that the virtual
 *  timer is off while the Unix kernel calls are made, making the I/O
 *  operations appear to occur instantaneously.  Impossible requests cause
 *  the device status to be set to DEV_ERROR. The number of sectors per
 *  track (DISK_TRACK_SIZE) is known at compile time,
 *  while the number of tracks on the disk (disk_tracks) is determined
 *  at startup time.
 */
dynamic_fun int disk_action(void *arg)
{
	static int opCount;
	int status = DEV_READY;
	long seek_loc;
	int err_return;
	int unit = (int)arg;
	device_request *request;

	usloss_sys_assert((unit >= 0) && (unit < DISK_UNITS),
					  "invalid disk unit in disk_action");
	request = &disks[unit].request;

	switch (request->opr)
	{
	case DISK_SEEK:
#ifdef LOGGING_DISK_IO
		printf("%d DISK_SEEK:  Unit:%d, Track: %d, Sector: %d\n", 
				opCount++, 
				unit, 
				(int)request->reg1);
#endif				
		if ((((int)request->reg1) >= disks[unit].tracks) ||
			(((int)request->reg1) < 0))
			status = DEV_ERROR;
		else
			disks[unit].currentTrack = (int)request->reg1;
		break;
	case DISK_READ:
	case DISK_WRITE:
		if (((int)request->reg1) >= DISK_TRACK_SIZE)
			status = DEV_ERROR;
		else
		{
			seek_loc = ((disks[unit].currentTrack * DISK_TRACK_SIZE) +
						((int)request->reg1)) *
					   DISK_SECTOR_SIZE;
			err_return = lseek(disks[unit].fd, seek_loc, 0);
			usloss_sys_assert(err_return != -1, "error seeking in disk file");
			if (request->opr == DISK_WRITE)
			{
#ifdef LOGGING_DISK_IO
				printf("%d DISK_WRITE: Unit:%d, Track: %d, Sector: %d\n", 
						opCount++, 
						unit, 
						disks[unit].currentTrack,
                        (int)request->reg1);
#endif
				err_return = write(disks[unit].fd, request->reg2,
								   DISK_SECTOR_SIZE);
				usloss_sys_assert(err_return != -1,
								  "error writing to disk file");
			}
			else
			{
#ifdef LOGGING_DISK_IO
				printf("%d DISK_READ:  Unit:%d, Track: %d, Sector: %d\n", 
						opCount++, 
						unit, 
						disks[unit].currentTrack,
                        (int)request->reg1);
#endif
				err_return = read(disks[unit].fd, (void *)request->reg2,
								  DISK_SECTOR_SIZE);
				usloss_sys_assert(err_return == DISK_SECTOR_SIZE,
								  "error reading from disk file");
			}
		}
		break;
	case DISK_TRACKS:
		*((int *)request->reg1) = disks[unit].tracks;
		break;
	default:
		usloss_usr_assert(0, "Illegal disk request operation");
		break;
	}
	disks[unit].status = status;
	return unit;
}
