#include "temperature.h"

/* Deliberately unconfigured: the probe values and channel mapping have not
 * been confirmed. Start is blocked until this table is completed correctly.
 * TEMP1..6 = PA6, PA7, PB0, PB1, PB11, PB12 respectively.
 * Four EXTERNAL and at least one INTERNAL channel are required.
 * Mark an actually unwired spare TEMP_UNUSED. Never mark a required probe unused.
 * See SESSION_README.md for a NON-PRODUCTION example and measurement checks. */
const TemperatureChannel temperature_channels[TEMPERATURE_COUNT] = {
    {.role = TEMP_UNASSIGNED}, /* TEMP1 */
    {.role = TEMP_UNASSIGNED}, /* TEMP2 */
    {.role = TEMP_UNASSIGNED}, /* TEMP3 */
    {.role = TEMP_UNASSIGNED}, /* TEMP4 */
    {.role = TEMP_UNASSIGNED}, /* TEMP5 */
    {.role = TEMP_UNASSIGNED}  /* TEMP6 */
};
