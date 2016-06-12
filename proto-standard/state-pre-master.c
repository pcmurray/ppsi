/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>

int pp_pre_master(struct pp_instance *ppi, void *pkt, int plen)
{
	return _pp_master(ppi, pkt, plen, 1);
}
