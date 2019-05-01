#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#define DRIVER_AUTHOR "Colin Gilgenbach <colin@gilgenbach.net>"
#define DRIVER_DESC   "An efficient driver for controlling a 3x4 matrix keypad"

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h> //ndelay, udelay, mdelay
#include <linux/sched.h>
#include <linux/input.h>

#define C1 10
#define C2 9
#define C3 11
#define R1 5 
#define R2 6
#define R3 13
#define R4 19

static long keymap[12] = { KEY_KP1, KEY_KP4, KEY_KP7, KEY_KPASTERISK,
                           KEY_KP2, KEY_KP5, KEY_KP8, KEY_KP0,
                           KEY_KP3, KEY_KP6, KEY_KP9, KEY_KPENTER }; //KPENTER instead of pound

#define INIT_ROW(num, pin) \
    if (!gpio_is_valid(pin)) { \
		printk(KERN_ERR "Keypad: Invalid GPIO " #pin " for ROW" #num); \
                input_free_device(keypad_dev); \
		return -1; \
	} \
	if (gpio_request(pin, "ROW" #num)) { \
	    printk(KERN_ERR "Keypad: Unable to request GPIO " #pin " for ROW" #num); \
            input_free_device(keypad_dev); \
	    return -1; \
	} \
	gpio_direction_input(pin); \
	gpio_set_debounce(pin, 150); \
	/*gpio_export(pin, false);*/ \
	irq_ ##num = gpio_to_irq(pin); \
	rslt = request_irq(irq_ ##num, \
                           (irq_handler_t) row_irq_handler, \
                           IRQF_TRIGGER_RISING, \
                           "3x4 Matrix Keypad", \
                           &keypad_dev); \
	if (rslt) { \
		printk(KERN_ERR "Keypad: Could not request interrupt for pin ROW" #num); \
		input_free_device(keypad_dev); \
 		return rslt; \
	}

#define EXIT_ROW(num, pin) \
	free_irq(irq_ ##num, &keypad_dev); \
	gpio_free(pin);

#define INIT_COL(num, pin) \
    if (!gpio_is_valid(pin)) { \
		printk(KERN_ERR "Keypad: Invalid GPIO " #pin " for COL" #num); \
	        input_free_device(keypad_dev); \
		return -1; \
	} \
	if (gpio_request(pin, "COL" #num)) { \
	    printk(KERN_ERR "Keypad: Unable to request GPIO " #pin " for COL" #num); \
	    input_free_device(keypad_dev); \
	    return -1; \
	} \
	gpio_direction_output(pin, 1); \
	/*gpio_export(pin, false);*/

#define EXIT_COL(num, pin) \
	gpio_free(pin);

#define INIT_ROWS() \
	INIT_ROW(1, R1); \
	INIT_ROW(2, R2); \
	INIT_ROW(3, R3); \
	INIT_ROW(4, R4);
#define INIT_COLS() \
	INIT_COL(1, C1); \
	INIT_COL(2, C2); \
	INIT_COL(3, C3);
#define EXIT_ROWS() \
	EXIT_ROW(1, R1); \
	EXIT_ROW(2, R2); \
	EXIT_ROW(3, R3); \
	EXIT_ROW(4, R4);
#define EXIT_COLS() \
	EXIT_COL(1, C1); \
	EXIT_COL(2, C2); \
	EXIT_COL(3, C3);

#define REPORT_ROW(num, col) \
	value = gpio_get_value(R ## num); \
	pressed = pressed || value; \
	input_report_key(keypad_dev, keymap[num-1 + 4*col], value);

#define REPORT_ROWS(col) \
	REPORT_ROW(1, col); \
	REPORT_ROW(2, col); \
	REPORT_ROW(3, col); \
	REPORT_ROW(4, col); \

static unsigned int irq_1;
static unsigned int irq_2;
static unsigned int irq_3;
static unsigned int irq_4;
static int should_die = 0;
static bool working = false;
static struct workqueue_struct *keypad_wq;
static void work_routine(struct work_struct *work);
DECLARE_WORK(work, work_routine);

static struct input_dev *keypad_dev;

static irq_handler_t row_irq_handler(unsigned int irq, void *devi_id, struct pt_regs *regs) {
	if (!working) {
		//printk(KERN_INFO "Keypad: Row interrupt");
		working = true;
		queue_work(keypad_wq, &work);
	}
	return (irq_handler_t) IRQ_HANDLED;
}

static void work_routine(struct work_struct *_work) {
	bool pressed = false;
	int value;
	gpio_set_value(C2, 0);
	gpio_set_value(C3, 0);
	udelay(50);
	REPORT_ROWS(0);

	gpio_set_value(C1, 0);
	gpio_set_value(C2, 1);
	udelay(50);
	REPORT_ROWS(1);

	gpio_set_value(C2, 0);
	gpio_set_value(C3, 1);	
	udelay(50);
	REPORT_ROWS(2);

	input_sync(keypad_dev);

	gpio_set_value(C1, 1);
	gpio_set_value(C2, 1);

	if (should_die || !pressed) {
		working = false;
	} else {
		mdelay(5);
		queue_work(keypad_wq, &work);
	}
}

static int __init keypad_init(void) {
	int rslt;
	int i = 0;
	keypad_wq = create_singlethread_workqueue("keypadWQ");
	keypad_dev = input_allocate_device();
	if (!keypad_dev) {
		printk(KERN_ERR "Keypad: Could not allocate device");
		return -2;
	}
	keypad_dev->name = "3x4 Matrix Keypad";

	INIT_COLS()
	INIT_ROWS()

	set_bit(EV_KEY, keypad_dev->evbit); //key events only
	for (i = 0; i < 12; i++) { //set keys we may return
	    set_bit(keymap[i], keypad_dev->keybit);
	}
	/*keypad_dev->keycode = keymap; //map from scancodes to keycodes
	keypad_dev->keycodemax = 12;
	keypad_dev->keycodesize = sizeof(keymap[0]);*/
	rslt = input_register_device(keypad_dev);
	if (rslt) {
		printk(KERN_ERR "Keypad: Failed to register device");
		input_free_device(keypad_dev);
		EXIT_ROWS()
		EXIT_COLS()
		return rslt;
	}
	printk(KERN_INFO "Keypad initalized");
	return 0;
}

static void __exit keypad_exit(void) {
	input_unregister_device(keypad_dev);
	input_free_device(keypad_dev);
	should_die = 1;
	cancel_work_sync(&work);
	flush_workqueue(keypad_wq);   //wait for all remaining tasks to finish
	destroy_workqueue(keypad_wq);
	EXIT_ROWS()
	EXIT_COLS()
	printk(KERN_INFO "Keypad exited");
}

module_init(keypad_init);
module_exit(keypad_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
