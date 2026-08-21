#ifndef NXP_NFC_PLATFORM_H
#define NXP_NFC_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

extern int nxp_nfc_lib_init(void);
extern int nxp_nfc_init(void);
extern int nxp_nfc_discovery_start(void);

#ifdef __cplusplus
}
#endif

#endif /* NXP_NFC_PLATFORM_H */
