/** @author Levente Kurusa <levex@linux.com> **/

#if !defined(__cplusplus)
#include <stdbool.h> /* C-ben alapbol nincsenek is bool-ok. */
#endif
#include <stddef.h>
#include <stdint.h>
 
#include "include/string.h"
#include "include/display.h"
#include "display/textmode/dispi_textmode.h"
#include "include/x86/gdt.h"
#include "include/x86/idt.h"
#include "include/pit.h"
#include "include/pic.h"
#include "include/hal.h"
#include "include/tasking.h"
#include "include/levos.h"
#include "include/keyboard.h"
#include "include/device.h"
#include "include/rtc.h"
#include "include/x86/v86.h"

static DISPLAY* disp = 0;

MODULE("MAIN");

void _test();

int levex_id = 0;

extern void kernel_end;
extern void kernel_base;

static char* hostname = "levex-levos";
static char* username = "root";

void __login()
{
	uint8_t *buffer = (uint8_t*)malloc(64);
	int l = 0;
	char c = 0;
retry:
	memset(buffer, 0, 64);
	kprintf("\n%s login: ", hostname);
	c = 0;
	l = 0;
	while(1)
	{
		c = keyboard_get_key();
		if(!c) { schedule_noirq(); continue;}
		if(c == '\n')
		{
			if(strcmp(buffer, "root") == 0)
			{
				break;
			}
			goto retry;
		}
		buffer[l++] = c;
		buffer[l] = 0;
		disp->putc(c);
	}
pwd:
	memset(buffer, 0, 64);
	kprintf("\n%s@%s password: ", username, hostname);
	l = 0;
	c = 0;
	while(1)
	{
		c = keyboard_get_key();
		if(!c) { schedule_noirq(); continue;}
		if(c == '\n')
		{
			if(strcmp(buffer, "toor") == 0)
			{
				disp->putc('\n');
				_kill();
			} else goto retry;
		}
		buffer[l] = c;
		buffer[l + 1] = 0;
		l++;
	}
}

void __cursor_updater()
{
	uint16_t lastpos = 0;
	while(1)
	{
		uint16_t pos = disp->con.cy*80 + disp->con.cx;
		if(pos == lastpos) { schedule_noirq(); continue; }
		lastpos = pos;
		outportb(0x3D4, 0x0F);
		outportb(0x3D5, (uint8_t)pos&0xFF);
		outportb(0x3D4, 0x0E);
		outportb(0x3D5, (uint8_t)(pos>>8)&0xFF);
		schedule_noirq();
	}
	_kill();
}

void test_device_read(uint8_t* buffer, uint32_t offset, uint32_t len)
{
	len --;
	while(len--)
	{
		switch(len%5)
		{
			case 0:
				*buffer++ = 'X';
				break;
			case 1:
				*buffer++ = 'E';
				break;
			case 2:
				*buffer++ = 'V';
				break;
			case 3:
				*buffer++ = 'E';
				break;
			case 4:
				*buffer++ = 'L';
				break;
		}
	}
	*buffer = 0;
}

void create_test_device()
{
	device_t *testdev = 0;
	testdev = (device_t*)malloc(sizeof(device_t));
	testdev->name = "/dev/levex";
	testdev->unique_id = 0x1337;
	testdev->dev_type = DEVICE_CHAR;
	testdev->read = test_device_read;
	levex_id = device_add(testdev);
	_kill();
}

void __read()
{
	uint8_t* buffer = (uint8_t*)malloc(32);
	device_t *testdev = device_get(levex_id);
	testdev->read(buffer, 0, 32);
	kprintf("%s\n", buffer);
	_kill();
}

void __malloc_bug()
{
	malloc(0x1FFFFFFF);
	_kill();
}

void __ps()
{
	tasking_print_all();
	_kill();
}

void sig_test()
{
	asm volatile("movl $0, %eax\n"
				"idiv %eax");
}

#if defined(__cplusplus)
extern "C" /* ha C++ compiler, akkor ez a függvény legyen C99es linkage-ű. */
#endif
void kernel_main()
{
	int eax;
	asm("movl %%eax, %0": "=m"(eax));
	/* We create a textmode display driver, register it, then set it as current */
	display_setcurrent(display_register(textmode_init()));
	disp = display_getcurrent();
	mprint("Welcome to LevOS 4.0, very unstable!\n");
	mprint("Kernel base is 0x%x,  end is 0x%x\n", &kernel_base, &kernel_end);
	if(eax == 0x1337) { mprint("Assembly link established.\n"); }
	else panic("Couldn't establish assembly link!");
	/* Setup memory manager */
	mm_init(&kernel_end);
	/* So far good, we have a display running,
	 * but we are *very* early, set up arch-specific
	 * tables, timers and memory to continue to tasking.
	 */
	gdt_init();
	/* Now, we have the GDT setup, let's load the IDT as well */
	idt_init();
	exceptions_init();
	/* Next step, setup PIT. */
	hal_init();
	pic_init();
	pit_init();
	/* Enabling interrupts early... */
	asm volatile("sti");
	mprint("Interrupts were enabled, beware.\n");
	/* Setup paging. */
	paging_init();
	/* Good job ladies and gentleman, we are now alive.
	 * Safe and sound, on the way to tasking!
	 * Let's roll.
	 */
	tasking_init();
	/* Tasking should have started late_init(),
	 * if not, we are screwed as no peripherials are in
	 * an operating state. Tell the user that we ran away
	 * in terror.
	 */
	panic("Reached end of main(), but tasking was not started.");
	for(;;);
}

/* This function is ought to setup peripherials and such,
 * while also starting somekind of /bin/sh
 */
void late_init()
{
	/* From now, we are preemptible. Setup peripherials */
	int pid = 0;	
	pid = START("kbd_init", keyboard_init);
	pid = START("cursor_update", __cursor_updater);
	pid = START("devicemm", device_init);
	pid = START("testdev", create_test_device);
	pid = START("rtc_init", rtc_init);

	/* We now wait till all the late_inits have finished */
	while(is_pid_running(pid))schedule_noirq();
	/* Once they are done, pass control to the kernel terminal,
	 * so that it will (eventually) start a /init or /bin/sh
	 */
	START_AND_WAIT("login", __login);
	START("_test", _test);
	/* We cannot die as we are the idle thread.
	 * schedule away so that we don't starve others
	 */
	while(1) schedule_noirq();
	panic("Reached end of late_init()\n");
}
static char c = 0;
static char* buffer = 0;
static uint16_t loc = 0;
void _test()
{
	buffer = (char*)malloc(256);
	char* prompt = "(kernel) $ ";
	uint8_t prompt_size = strlen(prompt);
	kprintf("Welcome to LevOS 4.0\nThis is a very basic terminal.\nDon't do anything stupid.\n");
prompt:
	kprintf(prompt);
	while(1) {
		if(!keyboard_enabled()){ schedule_noirq(); continue; }
		c = keyboard_get_key();
		if(!c) continue;
		if(c == '\r')
		{
			if(disp->con.cx <= prompt_size) continue;
			disp->con.cx --;
			disp->putc(' ');
			disp->con.cx --;
			buffer[loc--] = 0;
			continue;
		}
		if(c == '\n')
		{
			disp->putc(c);
			buffer[loc] = 0;
			loc = 0;
			if(strcmp(buffer, "help") == 0)
			{
				kprintf("LevOS4.0\nThis is the kernel terminal.\nDon't do anything stupid.\n");
				kprintf("Commands available: help; reboot; read; malloc; ps\nclear; reset; time\n");
			}
			if(strcmp(buffer, "reboot") == 0)
			{
				outportb(0x64, 0xFE);
			}
			if(strcmp(buffer, "read") == 0)
			{
				START_AND_WAIT("read", __read);
			}
			if(strcmp(buffer, "malloc") == 0)
			{
				START_AND_WAIT("malloc", __malloc_bug);
			}
			if(strcmp(buffer, "ps") == 0)
			{
				START_AND_WAIT("ps", __ps);
			}
			if(strcmp(buffer, "time") == 0)
			{
				START_AND_WAIT("time", rtc_print_time_as_proc);
			}
			if(strcmp(buffer, "clear") == 0 || strcmp(buffer, "reset") == 0)
			{
				disp->clear();
			}
			if(strcmp(buffer, "v") == 0)
			{
				START_AND_WAIT("sig_test", sig_test);
			}
			if(strcmp(buffer, "kill") == 0)
			{
				kill(5);
				while(is_pid_running(5)) {schedule_noirq(); continue;}
			}
			goto prompt;
		}
		buffer[loc++] = c;
		buffer[loc] = 0;
		disp->putc(c);
	}
}
