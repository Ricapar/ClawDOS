/* Claude-XT file tools: dir, read, write, delete, mkdir, rename. */
#ifndef CXT_DOSFS_H
#define CXT_DOSFS_H

#include "toolapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_PATH_MAX 64

int tool_dir(ToolCtx *t);
int tool_read(ToolCtx *t);
int tool_write(ToolCtx *t);
int tool_delete(ToolCtx *t);
int tool_mkdir(ToolCtx *t);
int tool_rename(ToolCtx *t);

long fs_size(const char *path);      /* file size, or -1 if it doesn't exist */

#ifdef __cplusplus
}
#endif

#endif
