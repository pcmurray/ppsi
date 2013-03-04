/*
 * Alessandro Rubini for CERN, 2011 -- public domain
 */

/*
 * This is the main loop for posix stuff.
 */
#include <stdlib.h>
#include <errno.h>
#include <sys/select.h>
#include <linux/if_ether.h>

#include <ppsi/ppsi.h>
#include <ppsi/diag.h>
#include "posix.h"

void posix_main_loop(struct pp_instance *ppi)
{
	int delay_ms;

	if (OPTS(ppi)->ethernet_mode)
		NP(ppi)->proto_ofst = 14;

	/*
	 * The main loop here is based on select. While we are not
	 * doing anything else but the protocol, this allows extra stuff
	 * to fit.
	 */
	ppi->is_new_state = 1;
	delay_ms = pp_state_machine(ppi, NULL, 0);
	while (1) {
		int i;
		unsigned char packet[1500];
		void *payload = packet + 16; /* aligned */

	again:

		i = posix_net_check_pkt(ppi, delay_ms);

		if (i < 0)
			continue;

		if (i == 0) {
			delay_ms = pp_state_machine(ppi, NULL, 0);
			continue;
		}

		/*
		 * We got a packet. If it's not ours, continue consuming
		 * the pending timeout
		 */
		i = ppi->n_ops->recv(ppi, payload, sizeof(packet) - 16,
				      &ppi->last_rcv_time);

		ppi->last_rcv_time.seconds += DSPRO(ppi)->currentUtcOffset;

		if (i < PP_MINIMUM_LENGTH) {
			PP_PRINTF("Error or short packet: %d < %d\n", i,
				PP_MINIMUM_LENGTH
			);
			delay_ms = -1;
			goto again;
		}

		delay_ms = pp_state_machine(ppi, payload, i);
	}
}
