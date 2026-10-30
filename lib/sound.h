/* ClawDOS sound: PLAY strings (play.h) on the PC speaker. */
#ifndef CXT_SOUND_H
#define CXT_SOUND_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

int tool_play(ToolCtx *t);           /* the play tool: argument "music" */

#ifdef __cplusplus
}
#endif

#endif
