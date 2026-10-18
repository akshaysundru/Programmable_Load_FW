#ifndef SD_SPI_H
#define SD_SPI_H
/* Main-loop hook called at most once per tick between SPI bytes. It must never
 * access SPI, FatFs, display, or the discharge logger (no recursive I/O). */
void SD_SPI_SetServiceHook(void (*hook)(void));
#endif
