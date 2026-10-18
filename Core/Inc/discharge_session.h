#ifndef DISCHARGE_SESSION_H
#define DISCHARGE_SESSION_H
#include "ff.h"
#include <stdbool.h>

typedef struct {
    bool logging;
    bool rate_error;
    FRESULT sd_error;
    char filename[16];
} DischargeStatus;
/* Application-level owner of Start/Stop and SD session file lifecycle.
 * Use these rather than starting LoadControl directly in the user interface. */
void Discharge_Init(void);
bool Discharge_Start(void);
void Discharge_Stop(void);
void Discharge_Process(void);
DischargeStatus Discharge_GetStatus(void);
#endif
