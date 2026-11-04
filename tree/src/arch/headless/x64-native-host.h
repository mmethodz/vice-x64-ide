/* x64-owned native transport. GPL-2.0-or-later, like the retained VICE host.
 * All command application and response copying happens on the VICE core thread.
 * The stdin/stdout pipes are private to the same-release supervisor, not a CLI.
 */
#ifndef X64_NATIVE_HOST_H
#define X64_NATIVE_HOST_H
void x64_native_prepare(int *argc, char **argv);
void x64_native_pump(void);
void x64_native_program_applied(void);
int x64_native_program_may_apply(void);
void x64_native_program_rejected(void);
void x64_native_stop_entered(void);
void x64_native_stop_left(void);
void x64_native_snapshot_applied(int operation, int result);
void x64_native_reset_applied(void);
#endif
