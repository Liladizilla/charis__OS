/* wizard.h - First-run setup flow */
#ifndef KERNEL_WIZARD_H
#define KERNEL_WIZARD_H

#include <kernel/types.h>

/* True when this machine has never completed setup, i.e. no setup_complete
 * flag in the config store. */
bool wizard_is_first_boot(void);

/* Run the setup flow. Returns true if it ran and the user finished it. Only
 * ever true when a linear framebuffer is present, since the flow is graphical;
 * a text-only build simply skips it. */
bool wizard_run(void);

#endif
