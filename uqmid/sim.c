/*
 * uqmi -- tiny QMI support implementation
 *
 * Copyright (C) 2024 Alexander Couzens <lynxis@fe80.eu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "sim.h"
#include "qmi-enums-uim.h"

enum uqmi_sim_state uim_card_application_state_to_uqmi_state(int app_state)
{
	switch (app_state) {
	case QMI_UIM_CARD_APPLICATION_STATE_UNKNOWN:
		return UQMI_SIM_UNKNOWN;
	case QMI_UIM_CARD_APPLICATION_STATE_PIN1_OR_UPIN_PIN_REQUIRED:
		return UQMI_SIM_PIN_REQUIRED;
	case QMI_UIM_CARD_APPLICATION_STATE_PUK1_OR_UPIN_PUK_REQUIRED:
		return UQMI_SIM_PUK_REQUIRED;
	case QMI_UIM_CARD_APPLICATION_STATE_READY:
		return UQMI_SIM_READY;
	default:
		return UQMI_SIM_UNKNOWN;
	}
}

enum uqmi_sim_state uim_pin_to_uqmi_state(int upin_state)
{
	switch (upin_state) {
	case QMI_UIM_PIN_STATE_NOT_INITIALIZED:
		return UQMI_SIM_UNKNOWN;
	case QMI_UIM_PIN_STATE_DISABLED:
		return UQMI_SIM_READY;
	case QMI_UIM_PIN_STATE_ENABLED_VERIFIED:
		return UQMI_SIM_READY;
	case QMI_UIM_PIN_STATE_ENABLED_NOT_VERIFIED:
		return UQMI_SIM_PIN_REQUIRED;
	case QMI_UIM_PIN_STATE_BLOCKED:
		return UQMI_SIM_PUK_REQUIRED;
	case QMI_UIM_PIN_STATE_PERMANENTLY_BLOCKED:
		return UQMI_SIM_BLOCKED;
	default:
		return UQMI_SIM_UNKNOWN;
	}
}
