/* SPDX-License-Identifier: MIT
 * aster_imports.h - one-stop include for ASTER apps.
 *
 * Pulls in the whole ABI 1.0 surface (all aster_* functions are declared as
 * wasm imports of module "env" by the individual headers) plus the tiny
 * logging helper. Apps should only need: #include <aster_imports.h>
 */
#ifndef ASTER_IMPORTS_H
#define ASTER_IMPORTS_H

#include <aster/aster.h>

#include "aster_libc.h"
#include "aster_printf.h"

#endif /* ASTER_IMPORTS_H */
