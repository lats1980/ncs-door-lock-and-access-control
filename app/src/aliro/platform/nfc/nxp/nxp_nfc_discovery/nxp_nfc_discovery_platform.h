#ifndef NXP_NFC_DISCOVERY_PLATFORM_H
#define NXP_NFC_DISCOVERY_PLATFORM_H

#include <nxp_nfc.h>

#include <ph_Status.h>
#include <phDriver.h>
#include <phNfcLib.h>
#include <phacDiscLoop.h>

extern phhalHw_Pn5190_DataParams_t *hal;

extern phStatus_t nxp_nfc_configure_lpcd(void);
extern void nxp_nfc_irq_handler(void);

#endif /* NXP_NFC_DISCOVERY_PLATFORM_H */
