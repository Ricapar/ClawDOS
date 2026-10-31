/* ClawDOS sound: PLAY strings (play.h) on the PC speaker. */
#ifndef CLAWDOS_SOUND_H
#define CLAWDOS_SOUND_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

int tool_play(ToolCtx *t);           /* the play tool: argument "music" */

#ifdef __cplusplus
}
#endif

#endif
