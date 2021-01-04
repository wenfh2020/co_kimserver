#ifndef __SET_PROC_TITLE_H__
#define __SET_PROC_TITLE_H__

#ifdef __cplusplus
extern "C" {
#endif

void spt_init(int argc, char *argv[]);
void set_proc_title(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
