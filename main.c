/*  GC to NES : Gamecube controller to NES adapter
    Copyright (C) 2012  Raphael Assenat <raph@raphnet.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <string.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#include "gamecube.h"
#include "boarddef.h"
#include "sync.h"

#define DEBUG_LOW()		PORTB &= ~(1<<5);
#define DEBUG_HIGH()	PORTB |= (1<<5);


Gamepad *gcpad;
unsigned char gc_report[GCN64_REPORT_SIZE];

static volatile unsigned char g_nes_polled = 0;

static volatile unsigned char nesbyte = 0xff;

#define NES_DATA_PORT 	PORTC
#define NES_DATA_BIT	0
#define NES_CLOCK_BIT	1
#define NES_CLOCK_PIN	PINC
#define NES_LATCH_PIN	PIND
#define NES_LATCH_BIT	2

#define NES_BIT_A		0
#define NES_BIT_B		1
#define NES_BIT_SELECT	2
#define NES_BIT_START	3
#define NES_BIT_UP		4
#define NES_BIT_DOWN	5
#define NES_BIT_LEFT	6
#define NES_BIT_RIGHT	7

ISR(INT0_vect)
{
	unsigned char bit, dat = nesbyte;

	DEBUG_HIGH();

	/**           __
	 * Latch ____|  |________________________________________
	 *       _________   _   _   _   _   _   _   _   ________
	 * Clk            |_| |_| |_| |_| |_| |_| |_| |_|
	 *
	 * Data      |       |   |   |   |   |   |   |
	 *           A       B   Sel St  U   D   L   R      
	 */
	
	if (dat & 0x80) {
		NES_DATA_PORT |= (1<<NES_DATA_BIT);
	} else {
		NES_DATA_PORT &= ~(1<<NES_DATA_BIT);
	}

	/* Wait until the latch pulse is over before continuing.
	 * This makes it possible to detect repeated latches
	 * by monitoring the pin later.
	 */
	while (NES_LATCH_PIN & (1<<NES_LATCH_BIT));

	TCNT0 = 0;
	TIFR = 1<<TOV0;
	bit = 0x40;
	for (bit=0x40; bit; bit>>=1) 
	{

		// wait clock falling edge
		while (NES_CLOCK_PIN & (1<<NES_CLOCK_BIT)) 
		{  	
			if (TIFR & (1<<TOV0)) {
				// clock wait timeout
				goto int0_done;
			}
		}

		if (dat & bit) {
			NES_DATA_PORT |= (1<<NES_DATA_BIT);
		} else {
			NES_DATA_PORT &= ~(1<<NES_DATA_BIT);
		}

		TCNT0 = 0;
		TIFR = 1<<TOV0;
		
		if (NES_LATCH_PIN & (1<<NES_LATCH_BIT)) {
			// If latch rises again, exit the interrupt. We
			// will re-enter this handler again very shortly because
			// this rising edge will set the INTF0 flag.
			goto int0_done;
		}
	}	
	
	/* One last clock cycle to go before we set the
	 * 'idle' level of the data line */
	while ((NES_CLOCK_PIN & (1<<NES_CLOCK_BIT))) 
	{  	
		if (TIFR & (1<<TOV0)) {
			// clock wait timeout
			goto int0_done;
		}
	
	}
	
	NES_DATA_PORT &= ~(1<<NES_DATA_BIT);

int0_done:

	/* Let the main loop know about this interrupt occuring. */
	g_nes_polled = 1;
	DEBUG_LOW();
}


void byteTo8Bytes(unsigned char val, unsigned char volatile *dst)
{
	unsigned char c = 0x80;

	do {
		*dst = val & c;
		dst++;
		c >>= 1;
	} while(c);
}

unsigned char scaleValue(unsigned char raw)
{
	return ((char)raw) * 24000L / 32767L;
}

void toNes(int pressed, int nes_btn_id)
{
	if (pressed)
		nesbyte &= ~(0x80 >> nes_btn_id);
	else
		nesbyte |= (0x80 >> nes_btn_id);

}

void axisToNes(unsigned char val, int nes_btn_low, int nes_btn_high, unsigned char thres)
{
	if (val < (0x80 - thres)) {
		toNes(1, nes_btn_low);
	} 

	if (val > (0x80 + thres)) {
		toNes(1, nes_btn_high);
	}
}

void axisToNes_mario(unsigned char val, int nes_btn_low, int nes_btn_high, int nes_run_button, unsigned char walk_thres, unsigned char run_thres)
{
	if (val < (0x80 - walk_thres)) {
		toNes(1, nes_btn_low);
		if (val < (0x80 - run_thres)) {
			toNes(1, nes_run_button);
		}
	} 

	if (val > (0x80 + walk_thres)) {
		toNes(1, nes_btn_high);
		if (val > (0x80 + run_thres)) {
			toNes(1, nes_run_button);
		}
	}
}

#define MAPPING_DEFAULT	0
#define MAPPING_AUTORUN	1

static int cur_mapping = MAPPING_DEFAULT;

void doMapping()
{
	static unsigned char turbo = 0xff;

	switch(cur_mapping) {
		case MAPPING_DEFAULT:
			toNes(GC_GET_A(gc_report), 			NES_BIT_A);	
			toNes(GC_GET_B(gc_report), 			NES_BIT_B);	
			toNes(GC_GET_Z(gc_report), 			NES_BIT_SELECT);
			toNes(GC_GET_START(gc_report), 		NES_BIT_START);
			toNes(GC_GET_DPAD_UP(gc_report), 	NES_BIT_UP);
			toNes(GC_GET_DPAD_DOWN(gc_report), 	NES_BIT_DOWN);
			toNes(GC_GET_DPAD_LEFT(gc_report), 	NES_BIT_LEFT);
			toNes(GC_GET_DPAD_RIGHT(gc_report), NES_BIT_RIGHT);

			axisToNes(gc_report[0], NES_BIT_LEFT, NES_BIT_RIGHT, 32);
			axisToNes(gc_report[1], NES_BIT_UP, NES_BIT_DOWN, 32);
			break;

		case MAPPING_AUTORUN:
			toNes(GC_GET_A(gc_report), 			NES_BIT_A);	
			toNes(GC_GET_B(gc_report), 			NES_BIT_B);	
			toNes(GC_GET_Z(gc_report), 			NES_BIT_SELECT);
			toNes(GC_GET_START(gc_report), 		NES_BIT_START);
			toNes(GC_GET_DPAD_UP(gc_report), 	NES_BIT_UP);
			toNes(GC_GET_DPAD_DOWN(gc_report), 	NES_BIT_DOWN);
			toNes(GC_GET_DPAD_LEFT(gc_report), 	NES_BIT_LEFT);
			toNes(GC_GET_DPAD_RIGHT(gc_report), NES_BIT_RIGHT);

			axisToNes_mario(gc_report[0], NES_BIT_LEFT, NES_BIT_RIGHT, NES_BIT_B, 32, 64);

			// This is not useful in mario, but as it does not appear to cause
			// any problems, I do it anyway since it might be good for other games.
			// (e.g. 2D view from above, with B button to run)
			axisToNes_mario(gc_report[1], NES_BIT_UP, NES_BIT_DOWN, NES_BIT_B, 32, 64);

			break;
	}

	if (GC_GET_L(gc_report)) {
		if (!(nesbyte & (0x80>>NES_BIT_B)))
			nesbyte ^= turbo & (0x80>>NES_BIT_B);
		if (!(nesbyte & (0x80>>NES_BIT_A)))
			nesbyte ^= turbo & (0x80>>NES_BIT_A);
	}

	turbo ^= 0xff;
}

int main(void)
{
	
	gcpad = gamecubeGetGamepad();

	/* PORTD
	 * 2: NES Latch interrupt
	 */
	DDRD = 0;
	PORTD = 0xff;

	DDRB = 0;
	PORTB = 0xff;
	DDRB = 1<<5;
	DEBUG_LOW();

	/* PORTC
	 * 0: Data (output) 
	 * 1: Clock (input)
	 */
	DDRC=1;
	PORTC=0xff;

	// configure external interrupt 0 to trigger on rising edge
	MCUCR |= (1<<ISC01) | (1<<ISC00);
	GICR |= (1<<INT0);
	GICR &= ~(1<<INT1);


	TCCR0 = (1<<CS01); // /8, overflows at 170us intervals

	gcpad->init();

	_delay_ms(500);

	/* Read from Gamecube controller */
	gcpad->update();
	gcpad->buildReport(gc_report);


	if (GC_GET_A(gc_report)) {
		cur_mapping = MAPPING_AUTORUN;
	}


	sync_init();

	sei();

	while(1)
	{
		if (g_nes_polled) {
			//DEBUG_HIGH();
			g_nes_polled = 0;
			sync_master_polled_us();
			DEBUG_LOW();
		}

		if (sync_may_poll()) {	

//			DEBUG_HIGH();
			gcpad->update();
//			DEBUG_LOW();


			if (gcpad->changed()) {
				// Read the gamepad	
				gcpad->buildReport(gc_report);				
	
				// prepare the controller data byte
				doMapping();

			}
		}
	}
}


