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
#include <avr/io.h>
#include "atmega168compat.h"

/* Forces the old behaviour which means a stable time distance 
 * between N64 poll and our Gamecube * poll. Sometimes useful
 * when debugging with an oscilloscope. */
//#define OLD_MODE

/* 
 * - Introduction
 *
 * The most simple approach for such an adapter is to wait
 * until the N64 finishes polling us, and then, since we
 * has one or several frames of free time, poll our gamcube
 * adapter. The next time we are polled by the N64, we serve
 * this previously polled data.
 *
 * Albeit simple, this approach has a serious drawback. It answers
 * the N64 polls with OLD data. How old? This depends on the game.
 * If the game polls the controller every frame, the data will
 * be approximately 15ms old. 
 *
 * 15ms might not be a lot, and in many games our brain can 
 * anticipate and we can get used to it. But in games with 
 * lower poll rates such as Mario Kart (approx. every 35ms), the
 * delay becomes serious.
 *
 * But be it 15ms or 35ms, any delay is highly undesirable for games 
 * which requires a very quick reponse from the player. And otherwise,
 * let's not forget that small delays can accumulate. We already
 * have so many unwanted delays with LCD TVs, etc... But at least, 
 * since we can do something about the delay this adapter can cause,
 * let's do it.
 *
 *
 * - Strategy
 * 
 * Instead of polling right away after being polled by the N64,
 * we wait and poll at the last minute before the N64 polls us.
 * This way, we are able to reply with a controller status as fresh
 * as possible.
 *
 * The time we wait is based on the duration between polls and
 * is updated after each poll, unless the previous poll is too
 * close in time. This is meant to prevent problems with games which
 * quick series of commands.
 *
 */

/* The time required to poll a gamecube controller is 300uS. 
 * The extra 150uS is a safety margin against jitter.
 * */
#define TIME_TO_POLL				333	// 450uS
#define MARGIN						666

#define MIN_IDLE					1700 // 5ms

#define DEFAULT_THRESHOLD			2333	// approx 7ms at /64 prescaler

#define STATE_WAIT_THRES			0
#define STATE_THRESHOLD_REACHED		1

static unsigned int poll_threshold;
static unsigned char state;

#ifdef AT168_COMPATIBLE
#define TIFR TIFR1
#endif

void sync_init(void)
{
	TCCR1A = 0;
	TCCR1B = (1<<CS11) | (1<<CS10);
	TCNT1 = 0;

	/* /64 divisor. Overflows every 262ms */
	state = STATE_WAIT_THRES;
	poll_threshold = DEFAULT_THRESHOLD;

}

void sync_master_polled_us(void)
{
	unsigned int elapsed;
	int ignore_elapsed = 0;

	if (TIFR & (1<<TOV1)) {
		TIFR |= 1<<TOV1; // clear overflow

		/* The N64 is probably not polling. Revert to default
		 * threshold instead of calculating an invalid one. */
		poll_threshold = DEFAULT_THRESHOLD;
		ignore_elapsed = 1;
	}

	if (!ignore_elapsed) {
		elapsed = TCNT1;
#ifdef OLD_MODE
		if (elapsed > MIN_IDLE)
			poll_threshold = 2; //MARGIN;
#else
		if (elapsed > MIN_IDLE)
		{
			if (elapsed > TIME_TO_POLL + MIN_IDLE + MARGIN) {
				// Program the next GC poll at the last moment before the
				// expected N64 poll.
				poll_threshold = elapsed - TIME_TO_POLL - MARGIN;
			} else {
				poll_threshold = DEFAULT_THRESHOLD;
			}
		
			if (poll_threshold < MIN_IDLE) {
				poll_threshold = DEFAULT_THRESHOLD;
			}
		}
#endif

	}

	/* Reset counter */
	TCNT1 = 0;
	TIFR |= (1<<TOV1); // clear overflow
	state = STATE_WAIT_THRES;
}

char sync_may_poll(void)
{
	if (state == STATE_WAIT_THRES)
	{
		if (TCNT1 >= poll_threshold) {
			state = STATE_THRESHOLD_REACHED;
			return 1;
		}
	}

		
	return 0;
}


