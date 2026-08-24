#ifndef LWGPS_OPTS_HDR_H
#define LWGPS_OPTS_HDR_H

/**
 * \brief           LwGPS configuration for X-Track 2.5
 */

/* Use double for latitude/longitude/altitude precision */
#ifndef LWGPS_CFG_DOUBLE
#define LWGPS_CFG_DOUBLE                  1
#endif

/* Statement parsers */
#ifndef LWGPS_CFG_STATEMENT_GPGGA
#define LWGPS_CFG_STATEMENT_GPGGA         1
#endif

#ifndef LWGPS_CFG_STATEMENT_GPGSA
#define LWGPS_CFG_STATEMENT_GPGSA         1
#endif

#ifndef LWGPS_CFG_STATEMENT_GPRMC
#define LWGPS_CFG_STATEMENT_GPRMC         1
#endif

#ifndef LWGPS_CFG_STATEMENT_GPGSV
#define LWGPS_CFG_STATEMENT_GPGSV         1
#endif

#ifndef LWGPS_CFG_STATEMENT_GPGSV_SAT_DET
#define LWGPS_CFG_STATEMENT_GPGSV_SAT_DET 1
#endif

#ifndef LWGPS_CFG_SATS_IN_VIEW_SIZE
#define LWGPS_CFG_SATS_IN_VIEW_SIZE       32
#endif

#ifndef LWGPS_CFG_STATEMENT_GPVTG
#define LWGPS_CFG_STATEMENT_GPVTG         1
#endif

#ifndef LWGPS_CFG_CRC
#define LWGPS_CFG_CRC                     1
#endif

#endif /* LWGPS_OPTS_HDR_H */
