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
//gpio_request(num, name);
//gpio_direction_input(num);
//gpio_sysfs_set_active_low(num);
//gpio_set_debounce(num, ms);
//gpio_get_value(num);
//gpio_export(num, bool direction_may_change);
//gpio_direction_output(num, 0/1);
//gpio_set_value(num, val);
//gpio_free(num);

#define C1 5
#define C2 6
#define C3 7
#define R1 5
#define R2 5
#define R3 6
#define R4 8

#define INIT_ROW(num, pin) \
	gpio_request(pin, "ROW" #num); \
	gpio_direction_input(pin); \
	gpio_set_debounce(pin, 50); \
	/*gpio_export(pin, false);*/ \
	irq_ ## name = gpio_to_irq(pin); \
	rslt = request_irq(irq_ ## num, \
		            (irq_handler_t) row_irq_handler, \
		            IRQF_TRIGGER_RISING, \
		            name "_irq_handler", \
		            NULL); \
	if (rslt) { \
		printk(KERN_ERR "Keypad: Could not request interrupt for pin ROW" #num); \
		return rslt; \
	}

#define EXIT_ROW(num, pin) \
	free_irq(irq_ ## num, row_irq_handler); \
	gpio_free(pin);

#define INIT_COL(num, pin) \
	gpio_request(pin, "COL" #num); \
	gpio_direction_output(pin, 1); \
	/*gpio_export(pin, false);*/

#define EXIT_COL(num, pin) \
	gpio_free(pin);

#define INIT_ROWS() \
	EXIT_ROW(1, R1); \
	EXIT_ROW(2, R2); \
	EXIT_ROW(3, R3); \
	EXIT_ROW(4, R4);
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

#define REPORT_ROW(num, key) \
	value = gpio_get_value(R ## num); \
	pressed = pressed || value; \
	input_report_key(keypad_dev, key, value);

#define REPORT_ROWS(key1, key2, key3, key4) \
	REPORT_ROW(1, key1); \
	REPORT_ROW(2, key2); \
	REPORT_ROW(3, key3); \
	REPORT_ROW(4, key4); \

static unsigned int irq_1;
static unsigned int irq_2;
static unsigned int irq_3;
static unsigned int irq_4;
static int die = 0;
static bool working = false;
static struct workqueue_struct *keypad_wq;
static void work_routine(struct work_struct *work);
DECLARE_DELAYED_WORK(work, work_routine);

static struct input_dev *keypad_dev;

static irq_handler_t row_irq_handler(unsigned int irq, void *devi_id, struct pt_regs *regs) {
	printk(KERN_INFO "Keypad: Row interrupt");
	if (!working) {
		working = true;
		queue_delayed_work(keypad_wq, &work, 100); //delay in jiffies
	}
	return (irq_handler_t) IRQ_HANDLED;
}

static void work_routine(struct work_struct *_work) {
	bool pressed = false;
	int value;
	printk(KERN_INFO "Keypad: Work routine");
	gpio_set_value(C2, 0);
	gpio_set_value(C3, 0);
	udelay(100);
	REPORT_ROWS(KEY_NUMERIC_1, KEY_NUMERIC_4, KEY_NUMERIC_7, KEY_NUMERIC_POUND);
	
	gpio_set_value(C1, 0);
	gpio_set_value(C2, 1);
	udelay(100);
	REPORT_ROWS(KEY_NUMERIC_2, KEY_NUMERIC_5, KEY_NUMERIC_8, KEY_NUMERIC_0);
	
	gpio_set_value(C2, 0);
	gpio_set_value(C3, 1);	
	udelay(100);
	REPORT_ROWS(KEY_NUMERIC_3, KEY_NUMERIC_6, KEY_NUMERIC_9, KEY_NUMERIC_STAR);
	
	input_sync(keypad_dev);

	gpio_set_value(C1, 1);
	gpio_set_value(C2, 1);

	if (die || !pressed)
		working = false;
	else
		queue_delayed_work(keypad_wq, &work, 100); //dunno if good idea
}

static int __init keypad_init(void) {
	int rslt;
	if (!gpio_is_valid(R1)) {
		printk(KERN_ERR "Keypad: Invalid GPIO Number");
		return -1;
	}
	INIT_COLS()
	INIT_ROWS()

	keypad_wq = create_singlethread_workqueue("WQsched.c");

	keypad_dev = input_allocate_device();
	if (!keypad_dev) {
		printk(KERN_ERR "Keypad: Could not allocate device");
		EXIT_ROWS()
		EXIT_COLS()
		return -2;
	}
	set_bit(EV_KEY, keypad_dev->evbit); //key events only
	set_bit(BTN_0, keypad_dev->keybit); //only 0s 
	rslt = input_register_device(keypad_dev);
	if (rslt) {
		printk(KERN_ERR "Keypad: Failed to register device");
		input_free_device(keypad_dev);
		EXIT_ROWS()
		EXIT_COLS()
		return rslt;
	}
	//task = kthread_run(keypad_thread, NULL, "keypad"); //start thread
	printk(KERN_INFO "Keypad initalized");
	return 0;
}

static void __exit keypad_exit(void) {
	input_unregister_device(keypad_dev);
	die = 1;
	cancel_delayed_work(&work);
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
