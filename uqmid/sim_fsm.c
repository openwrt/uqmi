/*
 * uqmi -- tiny QMI support implementation
 *
 * Copyright (C) 2024 Alexander Couzens <lynxis@fe80.eu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

/* SIM/UIM card FSM
 *
 * tracks the state of the sim and notifies the parent on hard events.
 * e.g. removal of the sim
 */

#include "logging.h"
#include "modem.h"
#include "modem_tx.h"
#include "osmocom/fsm.h"
#include "osmocom/utils.h"
#include "sim_fsm.h"
#include "modem_fsm.h"
#include "qmi-enums.h"
#include "qmi-message.h"
#include "qmi-enums-uim.h"
#include "services.h"
#include "utils.h"

#include <talloc.h>

#define S(x) (1 << (x))

#define SIM_DEFAULT_TIMEOUT_S 5

// move the sim configuration
// child fsm
// what happens when the sim goes down?

#define MAX_PATH 3
struct uim_file {
	uint16_t file_id;
	uint8_t path_n; /* count of elements in path */
	uint16_t path[MAX_PATH]; /* a path is a 16 bit integer */
};

enum uim_file_names {
	UIM_FILE_IMSI,
	UIM_FILE_ICCID,
};

/* 3GPP TS 31.102 */
static const struct uim_file uim_files[] = {
	[UIM_FILE_IMSI] = { 0x6f07, 2, { 0x3f00, 0x7fff } },
	[UIM_FILE_ICCID] = { 0x2fe2, 1, { 0x3f00 } },
};

static int _tx_uim_read_transparent_file(struct modem *modem, struct qmi_service *uim, request_cb cb,
					 enum uim_file_names file_name)
{
	uint8_t path[MAX_PATH * 2];
	const struct uim_file *file = &uim_files[file_name];

	for (int i = 0, j = 0; i < file->path_n; i++, j += 2) {
		path[j] = file->path[i] & 0xff;
		path[j + 1] = file->path[i] >> 8;
	}

	return tx_uim_read_transparent_file(modem, uim, cb, file->file_id, &path[0], file->path_n * 2);
}

static void sim_st_idle(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case SIM_EV_REQ_START:
		osmo_fsm_inst_state_chg(fi, SIM_ST_GET_INFO, SIM_DEFAULT_TIMEOUT_S, 0);
		break;
	default:
		break;
	}
}

static void sim_st_wait_uim_present(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void uim_get_slot_status_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0, i = 0;
	char *iccid_str;

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get slot status. Returned %d/%s. Trying next card status",
			  req->ret, qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_GET_SLOT_FAILED, NULL);
		return;
	}

	struct qmi_uim_get_slot_status_response res = {};
	ret = qmi_parse_uim_get_slot_status_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get slot status.");
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_GET_SLOT_FAILED, NULL);
		return;
	}

	for (i = 0; i < res.data.physical_slot_status_n; ++i) {
		uint32_t physical_card_status = res.data.physical_slot_status[i].physical_card_status;
		uint32_t physical_slot_status = res.data.physical_slot_status[i].physical_slot_status;
		/* ignoring uint8_t logical_slot */
		unsigned int iccid_n = res.data.physical_slot_status[i].iccid_n;
		uint8_t *iccid_bcd = res.data.physical_slot_status[i].iccid;

		if (physical_card_status != QMI_UIM_PHYSICAL_CARD_STATE_PRESENT)
			continue;

		if (physical_slot_status != QMI_UIM_SLOT_STATE_ACTIVE)
			continue;

		if (iccid_n == 0) {
			modem_log(modem, LOGL_INFO, "slot %d: Empty ICCID. UIM not yet ready? Or an EUICC?", i);
			continue;
		}

		/* FIXME: check for uneven number .. */
		int iccid_str_len = iccid_n * 2;
		iccid_str = talloc_zero_size(modem, iccid_n * 2);
		ret = osmo_bcd2str(iccid_str, iccid_str_len, iccid_bcd, 0, iccid_n, 1);

		if (ret) {
			modem_log(modem, LOGL_INFO, "Couldn't decode ICCID of slot %d", i);
			talloc_free(iccid_str);
			continue;
		}

		if (modem->iccid) {
			modem_log(modem, LOGL_INFO, "Modem already contains an ICCID. Replacing old entry %s with %s",
				  modem->iccid, iccid_str);
			talloc_free(modem->iccid);
		}
		modem->iccid = iccid_str;
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_VALID_ICCID, NULL);
	}

	osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_NO_UIM_FOUND, NULL);
}

static void uim_get_card_status_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0, i = 0, found = 0;

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get card status %d/%s.", req->ret, qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_FAILED, NULL);
		return;
	}

	struct qmi_uim_get_card_status_response res = {};
	ret = qmi_parse_uim_get_card_status_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get card status. Decoder failed.");
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_FAILED, NULL);
		return;
	}

	for (i = 0; i < res.data.card_status.cards_n; ++i) {
		if (res.data.card_status.cards[i].card_state != QMI_UIM_CARD_STATE_PRESENT)
			continue;

		for (int j = 0; j < res.data.card_status.cards[i].applications_n; ++j) {
			/* TODO: We should prioritize the USIM application. If not found, fallback to SIM -> ISIM -> CSIM */
			if (res.data.card_status.cards[i].applications[j].type == QMI_UIM_CARD_APPLICATION_TYPE_UNKNOWN)
				continue;

			modem_log(modem, LOGL_INFO, "Found valid card application type %x",
				  res.data.card_status.cards[i].applications[j].type);
			modem->sim.use_upin = res.data.card_status.cards[i].applications[i].upin_replaces_pin1;

			/* check if pin1 or upin is the correct method */
			if (modem->sim.use_upin) {
				modem->sim.state = uim_pin_to_uqmi_state(res.data.card_status.cards[i].upin_state);
				modem->sim.pin_retries = res.data.card_status.cards[i].upin_retries;
				modem->sim.puk_retries = res.data.card_status.cards[i].upuk_retries;
			} else {
				modem->sim.state =
					uim_pin_to_uqmi_state(res.data.card_status.cards[i].applications[i].pin1_state);
				modem->sim.pin_retries = res.data.card_status.cards[i].applications[i].pin1_retries;
				modem->sim.puk_retries = res.data.card_status.cards[i].applications[i].puk1_retries;
			}

			found = 1;
			break; /* handle first application only for now */
		}

		if (found) {
			osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_CARD_STATUS_VALID, NULL);
			return;
		}
	}

	modem_log(modem, LOGL_INFO, "Failed to find a valid UIM in get_status. Failing UIM.");
	osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_FAILED, NULL);
}

static void uim_read_imsi_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to read imsi. Qmi Ret %d/%s.", req->ret,
			  qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_GET_IMSI_FAILED, NULL);
		return;
	}

	struct qmi_uim_read_record_response res = {};
	ret = qmi_parse_uim_read_record_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to read imsi. Decoder failed. %d", ret);
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_GET_IMSI_FAILED, NULL);
		return;
	}

	/* TODO: card result on 5g without imsi? */
	// if (res.set.card_result) {
	//     modem_log(modem, LOGL_INFO, "Failed to read imsi. Card Result.");
	//     return;
	// }

	if (!res.data.read_result_n) {
		modem_log(modem, LOGL_INFO, "Failed to read imsi. No Read Data");
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_FAILED, NULL);
		return;
	}

	// where to get a BCD decoder?
	ret = uqmi_sim_decode_imsi(res.data.read_result, res.data.read_result_n, modem->imsi, sizeof(modem->imsi));
	if (ret < 0)
		osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_GET_IMSI_FAILED, NULL);

	osmo_fsm_inst_dispatch(modem->sim.fi, SIM_EV_RX_UIM_GET_IMSI_SUCCESS, NULL);
}

static void sim_st_get_info_on_enter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;

	struct qmi_service *uim = uqmi_service_find(modem->qmi, QMI_SERVICE_UIM);
	// struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	// should we use uim and have it available?
	if (uim && modem->sim.use_uim) {
		modem_log(modem, LOGL_INFO, "Trying to query UIM for slot status.");
		uqmi_service_send_simple(uim, qmi_set_uim_get_slot_status_request, uim_get_slot_status_cb, modem);
		return;
	} else {
		// dms
	}
}

static void sim_st_get_info(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;

	struct qmi_service *uim = uqmi_service_find(modem->qmi, QMI_SERVICE_UIM);

	switch (event) {
	case SIM_EV_RX_UIM_GET_SLOT_FAILED:
	case SIM_EV_RX_UIM_VALID_ICCID:
	case SIM_EV_RX_UIM_NO_UIM_FOUND:
		/* Get Slot Status succeeded? */
		if (uim && modem->sim.use_uim)
			uqmi_service_send_simple(uim, qmi_set_uim_get_card_status_request, uim_get_card_status_cb,
						 modem);
		break;
	case SIM_EV_RX_UIM_CARD_STATUS_VALID:
		if (uim && modem->sim.use_uim) {
			_tx_uim_read_transparent_file(modem, uim, uim_read_imsi_cb, UIM_FILE_IMSI);
		}
		break;
	case SIM_EV_RX_UIM_GET_IMSI_FAILED:
	case SIM_EV_RX_UIM_GET_IMSI_SUCCESS:
		switch (modem->sim.state) {
		case UQMI_SIM_PIN_REQUIRED:
			osmo_fsm_inst_state_chg(fi, SIM_ST_CHV_PIN, SIM_DEFAULT_TIMEOUT_S, 0);
			break;
		case UQMI_SIM_PUK_REQUIRED:
			osmo_fsm_inst_state_chg(fi, SIM_ST_CHV_PUK, SIM_DEFAULT_TIMEOUT_S, 0);
			break;
		case UQMI_SIM_READY:
			osmo_fsm_inst_state_chg(fi, SIM_ST_READY, 0, 0);
			break;
		case UQMI_SIM_UNKNOWN:
			/* FIXME: what to do when unknown? */
			break;
		case UQMI_SIM_BLOCKED:
			osmo_fsm_inst_state_chg(fi, SIM_ST_FAILED, SIM_DEFAULT_TIMEOUT_S, 0);
			break;
		default:
			/* FIXME: default case */
			break;
		}
		break;
	}
}

static void sim_st_chv_pin_on_enter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	// FIXME: try to unlock if enough tries are there
}

static void sim_st_chv_pin(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void sim_st_chv_puk_on_enter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	// FIXME: try to unlock if enough tries are there
}

static void sim_st_chv_puk(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void sim_st_ready_on_enter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_REQ_SIM_READY, NULL);
}

static void sim_st_ready(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void sim_st_failed(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void sim_st_destroy_on_enter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	// TODO: deregister indications?, wait x seconds, then terminate
	// in theory we don't have to because the modem should close the registration when closing
	// the uim service
}

static void sim_st_destroy(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case SIM_EV_RX_FAILED:
		break;
	case SIM_EV_RX_SUCCEED:
		break;
	}
	// terminate here when the derg
}

static int sim_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	// struct modem *modem = fi->priv;
	// struct qmi_service *service;

	switch (fi->state) {
	default:
		switch (fi->T) {
		default:
			break;
		}
		break;
	}

	return 0;
}

static void sim_fsm_allstate_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case SIM_EV_REQ_DESTROY:
		osmo_fsm_inst_state_chg(fi, SIM_ST_DESTROY, 0, 0);
		break;
	}
}

static const struct value_string sim_event_names[] = {
	{ SIM_EV_REQ_START,			"REQ_START" },
	{ SIM_EV_REQ_CONFIGURED,		"REQ_CONFIGURED" },
	{ SIM_EV_REQ_DESTROY,		"REQ_DESTROY" },
	{ SIM_EV_RX_UIM_FAILED,		"RX_UIM_FAILED" },
	{ SIM_EV_RX_UIM_GET_SLOT_FAILED,	"RX_UIM_GET_SLOT_FAILED" },
	{ SIM_EV_RX_UIM_NO_UIM_FOUND,	"RX_UIM_NO_UIM_FOUND" },
	{ SIM_EV_RX_UIM_VALID_ICCID,	"RX_UIM_VALID_ICCID" },
	{ SIM_EV_RX_UIM_CARD_STATUS_VALID,	"RX_UIM_CARD_STATUS_VALID" },
	{ SIM_EV_RX_UIM_GET_IMSI_SUCCESS,	"RX_UIM_GET_IMSI_SUCCESS" },
	{ SIM_EV_RX_UIM_GET_IMSI_FAILED,	"RX_UIM_GET_IMSI_FAILED" },
	{ SIM_EV_RX_UIM_PIN_REQUIRED,	"RX_UIM_PIN_REQUIRED" },
	{ SIM_EV_RX_UIM_PUK_REQUIRED,	"RX_UIM_PUK_REQUIRED" },
	{ SIM_EV_RX_UIM_READY,		"RX_UIM_READY" },
	{ SIM_EV_RX_FAILED,			"RX_FAILED" },
	{ SIM_EV_RX_SUCCEED,		"RX_SUCCEED" },
	{ 0, NULL },
};

static const struct osmo_fsm_state sim_states[] = {
	[SIM_ST_IDLE] = {
		.in_event_mask = S(SIM_EV_REQ_START),
		.out_state_mask = S(SIM_ST_GET_INFO) | S(SIM_ST_DESTROY),
		.name = "IDLE",
		.action = sim_st_idle,
	},
	[SIM_ST_WAIT_UIM_PRESENT] = {
		.in_event_mask = 0,
		.out_state_mask = S(SIM_ST_DESTROY) | S(SIM_ST_GET_INFO),
		.name = "WAIT UIM PRESENT",
		.action = sim_st_wait_uim_present,
	},
	[SIM_ST_GET_INFO] = {
		.in_event_mask = S(SIM_EV_RX_UIM_GET_SLOT_FAILED) |
				 S(SIM_EV_RX_UIM_VALID_ICCID) |
				 S(SIM_EV_RX_UIM_NO_UIM_FOUND) |
				 S(SIM_EV_RX_UIM_CARD_STATUS_VALID) |
				 S(SIM_EV_RX_UIM_GET_IMSI_SUCCESS) |
				 S(SIM_EV_RX_UIM_GET_IMSI_FAILED),
		.out_state_mask = S(SIM_ST_READY) |
				  S(SIM_ST_CHV_PIN) |
				  S(SIM_ST_CHV_PUK) |
				  S(SIM_ST_DESTROY),
		.name = "GET_INFO",
		.onenter = sim_st_get_info_on_enter,
		.action = sim_st_get_info,
	},
	[SIM_ST_READY] = {
		.in_event_mask = 0,
		.out_state_mask = S(SIM_ST_DESTROY) | S(SIM_ST_GET_INFO),
		.name = "READY",
		.onenter = sim_st_ready_on_enter,
		.action = sim_st_ready,
	},
	[SIM_ST_CHV_PIN] = {
		.in_event_mask = 0,
		.out_state_mask = S(SIM_ST_DESTROY) | S(SIM_ST_CHV_PUK) | S(SIM_ST_GET_INFO),
		.name = "CHV_PIN",
		.onenter = sim_st_chv_pin_on_enter,
		.action = sim_st_chv_pin,
	},
	[SIM_ST_CHV_PUK] = {
		.in_event_mask = 0,
		.out_state_mask = S(SIM_ST_DESTROY) | S(SIM_ST_CHV_PIN) | S(SIM_ST_GET_INFO),
		.name = "CHV_PIN",
		.onenter = sim_st_chv_puk_on_enter,
		.action = sim_st_chv_puk,
	},
	[SIM_ST_FAILED] = {
		.in_event_mask = 0,
		.out_state_mask = S(SIM_ST_DESTROY) | S(SIM_ST_GET_INFO),
		.name = "FAILED",
		.action = sim_st_failed,
	},
	[SIM_ST_DESTROY] = {
		.in_event_mask = 0,
		.out_state_mask = 0,
		.name = "DESTROY",
		.action = sim_st_destroy,
		.onenter = sim_st_destroy_on_enter,
	},
};

static struct osmo_fsm sim_fsm = {
	.name = "SIM",
	.states = sim_states,
	.num_states = ARRAY_SIZE(sim_states),
	.allstate_event_mask = S(SIM_EV_REQ_DESTROY),
	.allstate_action = sim_fsm_allstate_action,
	.timer_cb = sim_fsm_timer_cb,
	.event_names = sim_event_names,
	.pre_term = NULL,
};

struct osmo_fsm_inst *sim_fsm_alloc(struct modem *modem)
{
	struct osmo_fsm_inst *fi = osmo_fsm_inst_alloc_child(&sim_fsm, modem->fi, MODEM_EV_REQ_SIM_TERM);
	if (fi)
		fi->priv = modem;

	return fi;
}

static __attribute__((constructor)) void on_dso_load_ctx(void)
{
	OSMO_ASSERT(osmo_fsm_register(&sim_fsm) == 0);
}

// just to be removed later

// static void
// dms_get_imsi_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
// {
//     struct modem *modem = req->cb_data;
//     int ret = 0;

//     struct qmi_dms_uim_get_imsi_response res = {};
//     ret = qmi_parse_dms_uim_get_imsi_response(msg, &res);

//     if (ret) {
//         modem_log(modem, LOGL_INFO, "Failed to get imsi via dms. Failed to parse message");
//         osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_IMSI_DMS_FAILED, NULL);
//         return;
//     }

//     if (!res.data.imsi || strlen(res.data.imsi)) {
//         modem_log(modem, LOGL_INFO, "Received empty IMSI");
//         osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_IMSI_DMS_FAILED, (void *) 1);
//         return;
//     }

//     strncpy(modem->imsi, res.data.imsi, 15);
//     osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_IMSI, NULL);
// }

// static void tx_get_imsi_req(struct osmo_fsm_inst *fi, uint32_t old_state)
// {
//     struct modem *modem = fi->priv;
//     struct qmi_service *uim = uqmi_service_find(modem->qmi, QMI_SERVICE_UIM);

//     /* FIXME: register for indication of removed uims
//      * - register_physical_slot_status_events
//      * - Physical Slot Status?
//      */
//     if (uim) {
//         modem_log(modem, LOGL_INFO, "Trying to query UIM for slot status.");
//         uqmi_service_send_simple(uim, qmi_set_uim_get_slot_status_request, uim_get_slot_status_cb, modem);
//         return;
//     }

//     /* Retrieve it via DMS */
//     struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);
//     if (dms) {
//         modem_log(modem, LOGL_INFO, "Trying to query UIM for IMSI.");
//         uqmi_service_send_simple(dms, qmi_set_dms_uim_get_imsi_request, dms_get_imsi_cb, modem);
//         return;
//     }
// }
