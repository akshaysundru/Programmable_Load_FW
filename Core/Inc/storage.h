#ifndef STORAGE_H
#define STORAGE_H
#include "ff.h"

/* Main-loop only; do not access SPI/display concurrently from interrupts/DMA. */
FRESULT Storage_Mount(void);
FRESULT Storage_Unmount(void); /* Close all files first. */
/* Creates SDT000.TXT through SDT999.TXT without overwriting existing files.
 * Writes text, closes, reopens and verifies it. Mount first.
 */
FRESULT Storage_TestWriteRead(void);
#endif
