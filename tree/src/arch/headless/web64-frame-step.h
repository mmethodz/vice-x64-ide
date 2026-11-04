#ifndef WEB64_FRAME_STEP_H
#define WEB64_FRAME_STEP_H

#if defined(VICE_WEB) && defined(__EMSCRIPTEN__)
/* Called by the VIC-II at its real start-of-frame transition. */
void vice_web_frame_step_vic_start(void);
/* Only the main CPU calls this, with its registers already exported. */
extern int vice_web_frame_step_state;
void vice_web_frame_step_cpu_boundary(int pc);
void vice_web_frame_step_cancel(void);
#endif

#endif
