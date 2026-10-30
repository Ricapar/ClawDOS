/* What CLAWDOS.EXE's screen offers tools.cpp. */
#ifndef CXT_UI_H
#define CXT_UI_H

void ui_tool_header(const char *title, const char *arg);   /* "* Title(arg)" */
void ui_tool_detail(const char *text);                     /* dim line under it */
void ui_tool_note(const char *text, int error);            /* "  L summary" */
int  ui_ask(const char *question);                         /* 'y', 'n' or 27 (Esc) */
void ui_tool_busy(const char *verb);                       /* spinner verb while it runs */
void ui_tool_done(void);
void ui_redraw(void);                                      /* repaint the footer */
int  ui_send(const char *verb, const char *payload);       /* to the bridge; 0 = ok */
void ui_local_address(char *buf);                          /* our IP, "a.b.c.d" */

#endif
