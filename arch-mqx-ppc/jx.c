/* jx.c - initial linking test for ppsi port to mqx/ppc
 */
#include "mqx_ppc_port.h"
#include "ppsi/assert.h"
#include "ppsi/ppsi.h"
// included by ppsi.h:
//  "arch/arch.h"
//  "ppsi/constants.h"
//      "arch/constants.h
//  "ppsi/diag-macros.h"
//  "ppsi/ieee1588_types.h"
//      "ppsi/pp-instance.h"
//  "ppsi/jiffies.h"
//  "ppsi/lib.h"


struct foo_ext_hooks
	{
	int x;
	int y;
	int z;
	};

struct foo_ext_hooks WEAK foo_hooks =
	{
	1,
	2
	};

//int jx (char *p, char *q)
int main_task (char *p, char *q)
	{
	printf ("foo_hooks x=%d, y=%d, z=%d\n",
		foo_hooks.x, foo_hooks.y, foo_hooks.z);
    
    assert((1 != 0), "%s\n", "Hi There, Folks");

	return (strcmp (p, q));
	}

