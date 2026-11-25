/**
 * @file can_ids.h
 * @brief The firmware's CAN ID catalogue: every ID it sends or listens
 * for, grouped by bus.
 *
 * This is the one place an ID is written down. Services take IDs from
 * here, never as literals, so a change to the bus definition is one edit,
 * and two messages cannot quietly share an ID.
 *
 * All IDs here are standard 11-bit unless named *_EXT_*. Message layouts
 * live with the code that encodes or decodes them (see the comment on each
 * ID).
 *
 * When the DBC (#4) lands, this file should match it exactly (or be
 * generated from it).
 */
#ifndef CAN_IDS_H
#define CAN_IDS_H

/* --- main vehicle bus (CAN1) ----------------------------------------------
 * No IDs yet: the MC, BMS and IMD messages arrive with #20, #21 and #36. */

/* --- sensor bus (CAN2) ----------------------------------------------------
 * Sensors are read by a separate board on this bus, which also sends their
 * data. This board only sends its debug heartbeat here; IDs it needs to
 * listen for go here too. */

/** Monoboard heartbeat, sent by the CAN router in Debug builds to prove
 *  its TX path on a bench. Layout: see can_heartbeat.h. PLACEHOLDER until
 *  the sensor-bus DBC (#4) assigns it. */
#define CAN_ID_SENS_HEARTBEAT 0x7F0U

#endif /* CAN_IDS_H */
