/*
 * ISRG Root X1: the root both api.open-meteo.com and
 * geocoding-api.open-meteo.com chain to (verified via their live
 * certificate chains, and cross-checked against the certificate's
 * well-known public SHA-256 fingerprint
 * 96:BC:EC:...:C6). DER, not PEM: CONFIG_MBEDTLS_PEM_PARSE_C is off by
 * default and pulling it in just for this isn't worth the extra flash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_WEATHER_ISRG_ROOT_X1_H_
#define APP_WEATHER_ISRG_ROOT_X1_H_

#define WEATHER_TLS_CA_CERT_TAG 100

static const unsigned char weather_ca_certificate[] = {
#include "isrg_root_x1.der.inc"
};

#endif /* APP_WEATHER_ISRG_ROOT_X1_H_ */
