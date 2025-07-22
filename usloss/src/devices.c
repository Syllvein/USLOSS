
#include <stdio.h>
#include "project.h"
#include "globals.h"
#include "usloss.h"
#include "dev_alarm.h"
#include "dev_clock.h"
#include "dev_disk.h"
#include "dev_term.h"

static struct {
    int		device;
    void	*arg;
} dev_event_queue[256];		// Indexed by unsigned char - must be
				// 256 (major changes if not) */

static unsigned char dev_event_ptr;	/*  Index into queue of pending ints */

void (*int_vec[NUM_INTS])(int dev, void *arg);	/*  Interrupt vector table */
     
/*
 *  Initialize USLOSS interrupt processing routines.
 */
dynamic_fun void devices_init(void)
{
    int count;

    /*  Initialize the device event queue */
    for (count = 0; count < 256; count++)
	dev_event_queue[count].device = LOW_PRI_DEV;
    dev_event_ptr = 0;
    /*  Initialize the device status and interrupt vector tables */
    for (count = 0; count < NUM_INTS; count++)
    {
	int_vec[count] = NULL;
    }
}

/*
 *  Schedule an interrupt for a given number of clock ticks (must be < 255)
 *  in the future.  When two interrupts are scheduled for the same tick,
 *  the interrupt with lower priority is assigned to a later tick.
 */
dynamic_fun void schedule_int(int device, void *arg, int future_time)
{
    unsigned char index;	/* index MUST be char for wrap */
    int 	low_pri_dev;
    void 	*low_pri_arg;

    index = ((unsigned char) future_time) + dev_event_ptr;
    do
    {
	/*  Loop until we find a lower priority device than current */
	while(dev_event_queue[index].device <= device)
	    index++;
	/*  Swap the lower with the higher and continue */
	low_pri_dev = dev_event_queue[index].device;
	low_pri_arg = dev_event_queue[index].arg;
	dev_event_queue[index].device = device;
	dev_event_queue[index].arg = arg;
	device = low_pri_dev;
	arg = low_pri_arg;
    }
    while(low_pri_dev != LOW_PRI_DEV);
}

/*
 *  Gets the next event from the queue at interrupt time and performs
 *  all processing needed for this interrupt - calling the device
 *  action routine and the user interrupt handler.
 */
dynamic_fun void dispatch_int(void)
{
    static unsigned int tick = 0;
    int event_device;
    int unit_num = -1;
    void *arg;

    /*  Update and check the 'tick' variable to see if this is a clock
	interrupt */
    tick = ~tick;
    if (tick)
    {
	clock_action();
	if (int_vec[CLOCK_INT] == NULL) {
	    rpt_sim_trap("USLOSS_IntVec[USLOSS_CLOCK_INT] is NULL!\n");
	}
	(*int_vec[CLOCK_INT])(CLOCK_DEV, 0);
	return;
    }

    /*  This is not a clock interrupt - get the next event (from a device) */
    dev_event_ptr++;
    event_device = dev_event_queue[dev_event_ptr].device;
    arg = dev_event_queue[dev_event_ptr].arg;

    /*  Reset the queue entry to the terminal device and perform the
	action for this device */
    dev_event_queue[dev_event_ptr].device = LOW_PRI_DEV;
    switch(event_device)
    {
      case ALARM_DEV:
	unit_num = alarm_action(arg);
	break;
      case DISK_DEV:
	unit_num = disk_action(arg);
	break;
      case TERM_DEV:
	unit_num = term_action(arg);
	break;
      default:
        {
	    char msg[60];

	    sprintf(msg, "illegal device number %d in event queue, index %u",
		event_device, dev_event_ptr);
	    usloss_usr_assert(0, msg);
	}
    }

    /*  If the unit returned from the device action routine is -1, do
	nothing, otherwise call the user interrupt handler */
    if (unit_num != -1)
    {
	waiting = 0;		/*  Even on terminal input?? */
	if (int_vec[event_device] == NULL) {
	    rpt_sim_trap("USLOSS_IntVec contains NULL handle for interrupt.\n");
	}
	(*int_vec[event_device])(event_device, (void *) unit_num);
    }
}

/*
 *  Perform the inp() operation, which returns the status of a device.  We
 *  call on a per-device basis because the device may clear its status when
 *  the USLOSS_DeviceInput() is performed.
 */
int device_input(unsigned int dev, int unit, int *statusPtr)
{
    int result = DEV_INVALID;
    check_kernel_mode("USLOSS device_input");
    switch(dev)
    {
      case CLOCK_DEV:
	result = clock_get_status(unit, statusPtr);
	break;
      case ALARM_DEV:
	result =  alarm_get_status(unit, statusPtr);
	break;
      case DISK_DEV:
	result =  disk_get_status(unit, statusPtr);
	break;
      case TERM_DEV:
	result =  term_get_status(unit, statusPtr);
	break;
    }
    usloss_sys_assert((result == DEV_OK) || (result == DEV_INVALID),
	"bogus result in USLOSS device_input");
    return result;
}

/*
 *  Perform the USLOSS_DeviceOutput() operation, which is translated into 
 * a request to a device.
 */
int device_output(unsigned int dev, int unit, void *arg)
{
    int		result = DEV_ERROR;

    check_kernel_mode("USLOSS device_output");
    switch(dev)
    {
      case CLOCK_DEV:
	result = clock_request(unit, arg);
	break;
      case ALARM_DEV:
	result = alarm_request(unit, arg);
	break;
      case DISK_DEV:
	result = disk_request(unit, arg);
	break;
      case TERM_DEV:
	result = term_request(unit, arg);
	break;
    }
    usloss_sys_assert((result == DEV_OK) || (result == DEV_INVALID)
	|| (result == DEV_BUSY),
	"bogus result in USLOSS device_output");
    return result;
}

