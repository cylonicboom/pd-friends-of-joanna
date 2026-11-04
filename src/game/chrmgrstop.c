#include <ultra64.h>
#include "constants.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "bss.h"
#include "data.h"
#include "types.h"

void chrmgrStop(void)
{
	s32 i;

	for (i = 0; i < chrsGetNumSlots(); i++) {
		if (chrslotGet(i)->chrnum >= 0
				&& chrslotGet(i)->prop
				&& chrslotGet(i)->prop->type == PROPTYPE_CHR) {
			chrRemove(chrslotGet(i)->prop, true);
			propDelist(chrslotGet(i)->prop);
			propDisable(chrslotGet(i)->prop);
			propFree(chrslotGet(i)->prop);
		}
	}
}
