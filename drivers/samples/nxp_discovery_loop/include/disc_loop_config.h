
#ifndef DISC_LOOP_CONFIG_H
#define DISC_LOOP_CONFIG_H

#include <nxp_nfc.h>

#include <ph_Status.h>
#include <phDriver.h>
#include <phNfcLib.h>
#include <phacDiscLoop.h>

extern phhalHw_Pn5190_DataParams_t *hal;

extern phStatus_t disc_loop_configure_lpcd(void);
extern void disc_loop_irq_handler(void);

#ifdef CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG
phStatus_t disc_loop_apply_profile(phacDiscLoop_Sw_DataParams_t *disc_loop,
				   phacDiscLoop_Profile_t profile);
#endif /* CONFIG_NCS_NXP_DISCOVERY_LOOP_DISC_CONFIG */

#endif /* DISC_LOOP_CONFIG_H */
