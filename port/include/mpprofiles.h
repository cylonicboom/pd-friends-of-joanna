#ifndef _IN_MPPROFILES_H
#define _IN_MPPROFILES_H

#include <PR/ultratypes.h>

/*
 * Profile operations for the fojOS profile manager (imgui_profiles.cpp),
 * implemented in src/game/mplayer/mplayer.c beside the wad code they depend
 * on. Every call takes a profile's fileguid and works whether or not the
 * profile is seated: a seated profile is edited live and saved through the
 * save queue (mpProfileMarkDirty), an unseated one is edited in its pak file
 * and its pd.ini section directly.
 */

struct fileguid;

#ifdef __cplusplus
extern "C" {
#endif

#define MPPROFILE_NAME_MAX 10 // the wad's name field is always 10 bytes

// the seat a profile is loaded in, or -1
s32 mpProfileSeatOf(const struct fileguid *guid);

// blank profile on the gamepak: the given name, default head and body, no
// stats. 0 on success, -1 pak error, -2 no free profile slot on the pak
s32 mpProfileCreateBlank(const char *name, struct fileguid *out);

s32 mpProfileRename(const struct fileguid *guid, const char *name);

// refused (-1) while the profile is seated
s32 mpProfileDelete(const struct fileguid *guid);

s32 mpProfileGetHeadBody(const struct fileguid *guid, s32 *head, s32 *body);
s32 mpProfileSetHeadBody(const struct fileguid *guid, s32 head, s32 body);

// Friends of Joanna: the operative (TeamAgentIndex), an index into the FoJo
// carousel's options (mainmenu.c g_FojoHeadOptions); -1 when never chosen
s32 mpProfileGetOperative(const struct fileguid *guid);
s32 mpProfileSetOperative(const struct fileguid *guid, s32 index);

#ifdef __cplusplus
}
#endif

#endif
