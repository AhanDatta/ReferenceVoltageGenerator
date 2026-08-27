#ifndef CONFIG_H
#define CONFIG_H

/*
 * Compile-time limits shared by every module.
 */

/* ---- Voltage limits -------------------------------------------------- */

/* Hardware full scale of the DAC70501 reference: the voltage a code of
 * 0xFFFF produces. This is a property of the board, NOT a user preference,
 * so it is fixed here and is the ceiling every per-channel maximum is
 * clamped against. Changing it would silently rescale every DAC code. */
#define VOLTAGE_FULL_SCALE 5.0

/* Lowest voltage a channel can be set to. */
#define VOLTAGE_MIN 0.0

/* Voltage entries snap to this resolution (100 uV). This is the display
 * and rounding grid, not the size of a nudge. */
#define VOLTAGE_STEP 0.0001

/* How much the up/down arrows beside each channel move the value. Keep it
 * a whole multiple of VOLTAGE_STEP so stepping never drifts off the grid. */
#define VOLTAGE_ARROW_STEP 0.1

/* Per-channel user maximum a freshly opened GUI starts with. */
#define VOLTAGE_USER_MAX_DEFAULT VOLTAGE_FULL_SCALE

/* ---- DAC layout ------------------------------------------------------ */

#define NUM_BANKS     3
#define DACS_PER_BANK 4
#define TOTAL_DACS    (NUM_BANKS * DACS_PER_BANK)

/* ---- Serial port list ------------------------------------------------ */

/* The port dropdown offers COM1 .. COM<MAX_COM_PORT>. Raise this if a
 * machine enumerates ports above the current ceiling. */
#define MAX_COM_PORT 16

/* Port pre-selected when the window opens. */
#ifdef _WIN32
#  define DEFAULT_PORT_NAME "COM3"
#else
#  define DEFAULT_PORT_NAME "/dev/ttyACM0"
#endif

#define DEFAULT_BAUD_NAME "115200"

#endif /* CONFIG_H */
