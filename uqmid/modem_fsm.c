
#include "osmocom/fsm.h"
#include "osmocom/utils.h"

#include "qmi-enums-dms.h"
#include "qmi-errors.h"
#include "talloc.h"

#include "qmi-struct.h"
#include "qmi-enums.h"
#include "qmi-message.h"
#include "qmi-enums-uim.h"

#include "uqmid.h"
#include "logging.h"
#include "utils.h"

#include "modem.h"
#include "services.h"
#include "modem_fsm.h"
#include "modem_tx.h"

#define S(x) (1 << (x))

/* FIXME: if a service "vanished", abort/re-sync with backoff */
/* Timeouts periods in seconds */
#define T_GET_VERSION_S 5
#define T_SYNC_S 3

/* Timeout number */

enum {
	N_RESEND = 1,
	N_FAILURE,
	N_DESTROY,
};

static const struct value_string modem_event_names[] = {
	{ MODEM_EV_REQ_START, "REQ_START" },
	{ MODEM_EV_REQ_DESTROY, "REQ_DESTROY" },
	{ MODEM_EV_RX_SYNC, "RX_SYNC" },
	{ MODEM_EV_RX_VERSION, "RX_VERSION" },
	{ MODEM_EV_RX_MODEL, "RX_MODEL" },
	{ MODEM_EV_RX_MANUFACTURER, "RX_MANUFACTURER" },
	{ MODEM_EV_RX_REVISION, "RX_REVISION" },

	{ MODEM_EV_RX_IMSI, "RX_IMSI" },
	{ MODEM_EV_RX_UIM_FAILED, "RX_UIM_FAILED" },
	{ MODEM_EV_RX_UIM_GET_SLOT_FAILED, "RX_UIM_GET_SLOT_FAILED" },
	{ MODEM_EV_RX_UIM_VALID_ICCID, "RX_UIM_VALID_ICCID" },
	{ MODEM_EV_RX_UIM_NO_UIM_FOUND, "RX_UIM_NO_UIM_FOUND" },
	{ MODEM_EV_RX_IMSI_DMS_FAILED, "RX_IMSI_DMS_FAILED" },

	{ MODEM_EV_RX_POWEROFF, "RX_POWEROFF" },
	{ MODEM_EV_RX_POWERON, "RX_POWERON" },
	{ MODEM_EV_RX_POWERSET, "RX_POWERSET" },

	{ MODEM_EV_RX_UNLOCKED_PIN, "RX_UNLOCKED_PIN" },
	{ MODEM_EV_RX_UIM_PUK_REQUIRED, "RX_UIM_PUK_REQUIRED" },
	{ MODEM_EV_RX_UIM_PIN_REQUIRED, "RX_UIM_PIN_REQUIRED" },
	{ MODEM_EV_RX_UIM_READY, "RX_UIM_READY" },

	{ MODEM_EV_RX_GET_PROFILE_LIST, "RX_GET_PROFILE_LIST" },
	{ MODEM_EV_RX_MODIFIED_PROFILE, "RX_MODIFIED_PROFILE" },
	{ MODEM_EV_RX_CONFIGURED, "RX_CONFIGURED" },

	{ MODEM_EV_RX_SEARCHING, "RX_SEARCHING" },
	{ MODEM_EV_RX_UNREGISTERED, "RX_UNREGISTERED" },
	{ MODEM_EV_RX_REGISTERED, "RX_REGISTERED" },

	{ MODEM_EV_RX_SUBSCRIBED, "RX_SUBSCRIBED" },
	{ MODEM_EV_RX_SUBSCRIBE_FAILED, "RX_SUBSCRIBE_FAILED" },

	{ MODEM_EV_RX_FAILED, "RX_FAILED" },
	{ MODEM_EV_RX_SUCCEED, "RX_SUCCEED" },
	{ 0, NULL }
};

#define NAS_SERVICE_POLL_TIMEOUT_S 5

void modem_fsm_start(struct modem *modem)
{
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_REQ_START, NULL);
}

static void modem_st_unconfigured(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case MODEM_EV_REQ_START:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_RESYNC, T_SYNC_S, N_FAILURE);
		break;
	}
}

static void ctl_sync_request_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;

	/* transaction aborted or timedout */
	if (!msg) {
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	ret = qmi_parse_ctl_sync_response(msg);
	if (ret != QMI_PROTOCOL_ERROR_NONE) {
		modem_log(modem, LOGL_ERROR, "Sync Request returned error %d", ret);
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_SYNC, NULL);
}

static void modem_st_resync_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *ctl = uqmi_service_find(modem->qmi, QMI_SERVICE_CTL);

	uqmi_service_send_simple(ctl, qmi_set_ctl_sync_request, ctl_sync_request_cb, modem);
}

static void modem_st_resync(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	osmo_fsm_inst_state_chg(fi, MODEM_ST_GET_VERSION, T_GET_VERSION_S, 0);
}

static void uqmid_modem_version_cb(struct qmi_service *ctl_service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	struct qmi_service *service;
	uint8_t service_id;
	uint16_t major, minor;

	struct qmi_ctl_get_version_info_response res = {};
	qmi_parse_ctl_get_version_info_response(msg, &res);

	for (int i = 0; i < res.data.service_list_n; i++) {
		service_id = res.data.service_list[i].service;
		major = res.data.service_list[i].major_version;
		minor = res.data.service_list[i].minor_version;

		service = uqmi_service_find_or_init(modem->qmi, service_id);
		if (!service)
			continue;

		service->major = major;
		service->minor = minor;
	}

	/* FIXME: create a list of required services and validate them */

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_VERSION, NULL);
}

static void modem_st_get_version_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_request *req = talloc_zero(modem->qmi->ctrl, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	int ret = qmi_set_ctl_get_version_info_request(msg);
	if (ret) {
		LOG_ERROR("Failed to encode get version info");
		return;
	}

	req->msg = msg;
	req->cb = uqmid_modem_version_cb;
	req->cb_data = modem;
	uqmi_service_send_msg(modem->qmi->ctrl, req);
}

static void modem_st_get_version(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case MODEM_EV_RX_VERSION:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_GET_MODEL, 0, 0);
		break;
	}
}

static void get_manuf_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;

	struct qmi_dms_get_manufacturer_response res = {};
	ret = qmi_parse_dms_get_manufacturer_response(msg, &res);

	if (ret) {
		/* FIXME: No manufacturer. Ignoring */
		modem_log(modem, LOGL_ERROR, "Failed to get a manufacturer.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MANUFACTURER, NULL);
		return;
	}

	if (!res.data.manufacturer) {
		/* FIXME: No manufacturer */
		modem_log(modem, LOGL_ERROR, "Got empty manufacturer.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MANUFACTURER, NULL);
		return;
	}

	if (modem->manuf)
		talloc_free(modem->manuf);

	modem->manuf = talloc_strdup(modem, res.data.manufacturer);
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MANUFACTURER, NULL);
}

static void get_model_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;

	struct qmi_dms_get_model_response res = {};
	ret = qmi_parse_dms_get_model_response(msg, &res);

	if (ret) {
		/* FIXME: No model. Ignoring */
		modem_log(modem, LOGL_ERROR, "Failed to get a model.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MODEL, NULL);
		return;
	}

	if (!res.data.model) {
		/* FIXME: No model */
		modem_log(modem, LOGL_ERROR, "Got empty model.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MODEL, NULL);
		return;
	}

	if (modem->model)
		talloc_free(modem->model);

	modem->model = talloc_strdup(modem, res.data.model);
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MODEL, NULL);
}

static void get_revision_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;

	struct qmi_dms_get_revision_response res = {};
	ret = qmi_parse_dms_get_revision_response(msg, &res);

	if (ret) {
		/* FIXME: No revision. Ignoring */
		modem_log(modem, LOGL_ERROR, "Failed to get a revision.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_REVISION, NULL);
		return;
	}

	if (!res.data.revision) {
		/* FIXME: No revision */
		modem_log(modem, LOGL_ERROR, "Got empty revision.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_REVISION, NULL);
		return;
	}

	if (modem->rev)
		talloc_free(modem->rev);

	modem->rev = talloc_strdup(modem, res.data.revision);
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_REVISION, NULL);
}

static void modem_st_get_model_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *service = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	if (!service) {
		modem_log(modem, LOGL_ERROR, "DMS service unavailable");
		/* FIXME: fail to perm failure */
	}

	uqmi_service_send_simple(service, qmi_set_dms_get_model_request, get_model_cb, modem);
}

static void modem_st_get_model(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;
	struct qmi_service *service = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	if (!service) {
		modem_log(modem, LOGL_ERROR, "DMS service unavailable");
		/* FIXME: fail to perm failure */
	}

	/* FIXME: enable timeout and check if enough information has been gathered */

	switch (event) {
	case MODEM_EV_RX_MODEL:
		uqmi_service_send_simple(service, qmi_set_dms_get_manufacturer_request, get_manuf_cb, modem);
		break;
	case MODEM_EV_RX_MANUFACTURER:
		uqmi_service_send_simple(service, qmi_set_dms_get_revision_request, get_revision_cb, modem);
		break;
	case MODEM_EV_RX_REVISION:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_GET_IMSI, 0, 0);
		break;
	}
}

static void uim_get_slot_status_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0, i = 0;
	char *iccid_str;

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get slot status. Returned %d/%s. Trying next card status",
			  req->ret, qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_GET_SLOT_FAILED, NULL);
		return;
	}

	struct qmi_uim_get_slot_status_response res = {};
	ret = qmi_parse_uim_get_slot_status_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get slot status.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_FAILED, NULL);
		return;
	}

	// res->data.physical_slot_status_n
	// struct {
	// 	uint32_t physical_card_status;
	// 	uint32_t physical_slot_status;
	// 	uint8_t logical_slot;
	// 	unsigned int iccid_n;
	// 	uint8_t *iccid;
	// } *physical_slot_status;

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
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_VALID_ICCID, NULL);
	}

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_NO_UIM_FOUND, NULL);
}

static void uim_get_card_status_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0, i = 0, found = 0;

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get card status %d/%s.", req->ret, qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_FAILED, NULL);
		return;
	}

	struct qmi_uim_get_card_status_response res = {};
	ret = qmi_parse_uim_get_card_status_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get card status. Decoder failed.");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_FAILED, NULL);
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
			osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_VALID_ICCID, NULL);
			return;
		}
	}

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_NO_UIM_FOUND, NULL);
}

static void dms_get_imsi_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;

	struct qmi_dms_uim_get_imsi_response res = {};
	ret = qmi_parse_dms_uim_get_imsi_response(msg, &res);

	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get imsi via dms. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_IMSI_DMS_FAILED, NULL);
		return;
	}

	if (!res.data.imsi || strlen(res.data.imsi)) {
		modem_log(modem, LOGL_INFO, "Received empty IMSI");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_IMSI_DMS_FAILED, (void *)1);
		return;
	}

	modem->imsi = talloc_strdup(modem, res.data.imsi);
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_IMSI, NULL);
}

static void modem_st_get_imsi_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *uim = uqmi_service_find(modem->qmi, QMI_SERVICE_UIM);

	/* FIXME: register for indication of removed uims
	 * - register_physical_slot_status_events
	 * - Physical Slot Status?
	 */
	if (uim) {
		modem_log(modem, LOGL_INFO, "Trying to query UIM for slot status.");
		uqmi_service_send_simple(uim, qmi_set_uim_get_slot_status_request, uim_get_slot_status_cb, modem);
		return;
	}

	/* Retrieve it via DMS */
	struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);
	if (dms) {
		modem_log(modem, LOGL_INFO, "Trying to query UIM for IMSI.");
		uqmi_service_send_simple(dms, qmi_set_dms_uim_get_imsi_request, dms_get_imsi_cb, modem);
		return;
	}
}

static void modem_st_get_imsi(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;
	struct qmi_service *uim = uqmi_service_find(modem->qmi, QMI_SERVICE_UIM);
	struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	/* TODO: figure out when DMS: Get IMSI is available, same for Get UIM State.
	 *       Are those legacy calls?
	 */

	switch (event) {
	case MODEM_EV_RX_UIM_VALID_ICCID:
		/* FIXME: ICCID must be enough for now.
		 *        It seems only Get Record or Get Transparent file for UIM would be the only
		 *        available solution to retrieve the IMSI. Ignoring IMSI for now.
		 */
		modem_log(modem, LOGL_INFO, "Found valid & usable USIM.");
		osmo_fsm_inst_state_chg(fi, MODEM_ST_POWEROFF, 0, 0);
		break;
	case MODEM_EV_RX_UIM_GET_SLOT_FAILED:
		if (uim)
			uqmi_service_send_simple(uim, qmi_set_uim_get_card_status_request, uim_get_card_status_cb,
						 modem);
		else
			osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UIM_FAILED, NULL);
		break;
	case MODEM_EV_RX_UIM_NO_UIM_FOUND:
		modem_log(modem, LOGL_INFO, "No valid UIM found. Waiting for timeout to retry.");
		break;
	case MODEM_EV_RX_UIM_FAILED:
		if (dms)
			uqmi_service_send_simple(dms, qmi_set_dms_uim_get_imsi_request, dms_get_imsi_cb, modem);
		break;
	case MODEM_EV_RX_IMSI:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_POWEROFF, 0, 0);
		break;
	case MODEM_EV_RX_IMSI_DMS_FAILED:
		modem_log(modem, LOGL_INFO, "DMS failed to retrieve the IMSI. Err %p", data);
		break;
	}
}

static void dms_get_operating_mode_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	int ret = 0;
	struct qmi_dms_get_operating_mode_response res = {};

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get operating mode. Status %d/%s.", req->ret,
			  qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	ret = qmi_parse_dms_get_operating_mode_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get operating mode. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	if (!res.set.mode) {
		modem_log(modem, LOGL_INFO, "Failed to get operating mode. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
	}

	modem_log(modem, LOGL_INFO, "Current in operating mode %d", res.data.mode);
	switch (res.data.mode) {
	case QMI_DMS_OPERATING_MODE_ONLINE:
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_POWERON, (void *)(long)res.data.mode);
		break;
	case QMI_DMS_OPERATING_MODE_LOW_POWER:
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_POWEROFF, (void *)(long)res.data.mode);
		break;
	default:
		modem_log(modem, LOGL_ERROR, "Unhandled power mode! (%d) Trying to power off", res.data.mode);
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_POWERON, (void *)(long)res.data.mode);
	}
}

static void dms_set_operating_mode_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to set operating mode. Status %d/%s.", req->ret,
			  qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	/* TODO: we should request the state again to validate the modem is following our request. */
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_POWERSET, 0);
}

static void modem_st_poweroff_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	/* FIXME: abort when DMS doesn't exist */
	if (dms)
		uqmi_service_send_simple(dms, qmi_set_dms_get_operating_mode_request, dms_get_operating_mode_cb, modem);
}
static void modem_st_poweroff(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;
	struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	switch (event) {
	case MODEM_EV_RX_POWERON:
		tx_dms_set_operating_mode(modem, dms, QMI_DMS_OPERATING_MODE_LOW_POWER, dms_set_operating_mode_cb);
		break;
	case MODEM_EV_RX_POWERSET:
		uqmi_service_send_simple(dms, qmi_set_dms_get_operating_mode_request, dms_get_operating_mode_cb, modem);
		break;
	case MODEM_EV_RX_POWEROFF:
		if (modem->config.configured && !modem->state.error)
			osmo_fsm_inst_state_chg(fi, MODEM_ST_UNLOCK_PIN, 0, 0);
		else /* stop timer and wait for the user to continue */
			osmo_timer_del(&fi->timer);
		break;
	case MODEM_EV_REQ_CONFIGURED: /* when the modem reach this state, but isn't yet configured, wait for the config */
		osmo_fsm_inst_state_chg(fi, MODEM_ST_UNLOCK_PIN, 0, 0);
		break;
	}
}

static void modem_st_unlock_pin_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	osmo_fsm_inst_state_chg(fi, MODEM_ST_CONFIGURE_MODEM, 0, 0);
}
static void modem_st_unlock_pin(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void wds_modify_profile_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	struct qmi_wds_modify_profile_response res = {};
	long err = 1;
	int ret;
	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to modify profile list. Status %d/%s.", req->ret,
			  qmi_get_error_str(req->ret));
		/* FIXME: add extended profile error */
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MODIFIED_PROFILE, (void *)err);
		return;
	}

	ret = qmi_parse_wds_modify_profile_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to modify profile list. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MODIFIED_PROFILE, (void *)err);
		return;
	}

	err = 0;
	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_MODIFIED_PROFILE, (void *)err);
}

static void wds_get_profile_list_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	struct qmi_wds_get_profile_list_response res = {};
	long err = 1;
	int ret;
	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get profile list. Status %d/%s.", req->ret,
			  qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	ret = qmi_parse_wds_get_profile_list_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get operating mode. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	for (int i = 0; i < res.data.profile_list_n; i++) {
		uint8_t type = res.data.profile_list[i].profile_type;
		uint8_t idx = res.data.profile_list[i].profile_index;
		/* const char *name = res.data.profile_list[i].profile_name; */
		if (type != QMI_WDS_PROFILE_TYPE_3GPP)
			continue;

		err = 0;
		/* we're taking the first one and ignoring the rest for now */
		modem_log(modem, LOGL_INFO, "Found profile id %d", idx);
		modem->qmi->wds.profile_id = idx;
		modem->qmi->wds.valid = 1;
	}

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_GET_PROFILE_LIST, (void *)err);
}

static void modem_st_configure_modem_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *wds = uqmi_service_find(modem->qmi, QMI_SERVICE_WDS);

	switch (modem->config.pdp_type) {
	case QMI_WDS_PDP_TYPE_IPV4:
		modem->qmi->wds.ip_family = QMI_WDS_IP_FAMILY_IPV4;
		break;
	/* FIXME: add support for IPV4/IPV6 dual stack */
	case QMI_WDS_PDP_TYPE_IPV4_OR_IPV6:
	case QMI_WDS_PDP_TYPE_IPV6:
		modem->qmi->wds.ip_family = QMI_WDS_IP_FAMILY_IPV6;
		break;
	default:
		modem->qmi->wds.ip_family = QMI_WDS_IP_FAMILY_UNSPECIFIED;
		break;
	}

	tx_wds_get_profile_list(modem, wds, wds_get_profile_list_cb);
}

static void modem_st_configure_modem(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;
	struct qmi_service *wds = uqmi_service_find(modem->qmi, QMI_SERVICE_WDS);

	/* get Profile list, if no Profile,
	 * create one ()
	 * modify profile () */

	switch (event) {
	case MODEM_EV_RX_GET_PROFILE_LIST:
		/* err */
		if (data) {
			/* failed to get profile list/generate a new profile */
		} else {
			tx_wds_modify_profile(modem, wds, wds_modify_profile_cb, modem->qmi->wds.profile_id,
					      modem->config.apn, modem->config.pdp_type);
		}
		break;
	case MODEM_EV_RX_MODIFIED_PROFILE:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_POWERON, 0, 0);
		break;
	}
	/* TODO: check if this is the default profile! */
}

static void modem_st_poweron_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	/* FIXME: abort when DMS doesn't exist */
	if (dms)
		uqmi_service_send_simple(dms, qmi_set_dms_get_operating_mode_request, dms_get_operating_mode_cb, modem);
}
static void modem_st_poweron(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;
	struct qmi_service *dms = uqmi_service_find(modem->qmi, QMI_SERVICE_DMS);

	switch (event) {
	case MODEM_EV_RX_POWEROFF:
		tx_dms_set_operating_mode(modem, dms, QMI_DMS_OPERATING_MODE_ONLINE, dms_set_operating_mode_cb);
		break;
	case MODEM_EV_RX_POWERSET:
	case MODEM_EV_RX_POWERON:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_NETSEARCH, NAS_SERVICE_POLL_TIMEOUT_S, 0);
		break;
	}
}

static void subscribe_result_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Service %d: Subscribe check failed with %d/%s", service->service, req->ret,
			  qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_SUBSCRIBE_FAILED,
				       (void *)(long)le32_to_cpu(msg->svc.message));
		return;
	}

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_SUBSCRIBED, (void *)(long)le32_to_cpu(msg->svc.message));
}

static void get_serving_system_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	struct qmi_nas_get_serving_system_response res = {};

	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to get system info. Status %d/%s", req->ret,
			  qmi_get_error_str(req->ret));
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)(long)1);
		return;
	}

	int ret = qmi_parse_nas_get_serving_system_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_ERROR, "Failed to decode serving system response");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)(long)1);
		return;
	}

	if (!res.set.serving_system) {
		modem_log(modem, LOGL_ERROR, "No serving system in get serving system");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)(long)1);
		return;
	}

	modem_log(modem, LOGL_INFO, "Network registration state %d", res.data.serving_system.registration_state);

	switch (res.data.serving_system.registration_state) {
	case QMI_NAS_REGISTRATION_STATE_REGISTERED:
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_REGISTERED, NULL);
		return;
	case QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED:
	case QMI_NAS_REGISTRATION_STATE_UNKNOWN:
	case QMI_NAS_REGISTRATION_STATE_REGISTRATION_DENIED:
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_UNREGISTERED, NULL);
		return;
	case QMI_NAS_REGISTRATION_STATE_NOT_REGISTERED_SEARCHING:
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_SEARCHING, NULL);
		return;
	}
}

static void nas_force_search_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	if (req->ret) {
		modem_log(modem, LOGL_INFO, "Failed to force a network search. Status %d/%s", req->ret,
			  qmi_get_error_str(req->ret));
		/* FIXME: charge timer */
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}
}

static void modem_st_netsearch_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *nas = uqmi_service_find(modem->qmi, QMI_SERVICE_NAS);
	tx_nas_subscribe_nas_events(modem, nas, 1, subscribe_result_cb);
}

static void modem_st_netsearch(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	/* TODO: register to internal indications */
	struct modem *modem = fi->priv;
	struct qmi_service *nas = uqmi_service_find(modem->qmi, QMI_SERVICE_NAS);

	switch (event) {
	case MODEM_EV_RX_REGISTERED:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_REGISTERED, 0, 0);
		break;
	case MODEM_EV_RX_FAILED:
		osmo_timer_schedule(&fi->timer, 5, 0);
		break;
	case MODEM_EV_RX_SUBSCRIBED:
	case MODEM_EV_RX_SUBSCRIBE_FAILED:
		uqmi_service_send_simple(nas, qmi_set_nas_get_serving_system_request, get_serving_system_cb, modem);
		break;
	case MODEM_EV_RX_UNREGISTERED:
		modem_log(modem, LOGL_INFO, "Start network search.");
		uqmi_service_send_simple(nas, qmi_set_nas_force_network_search_request, nas_force_search_cb, modem);
		break;
	}
}

static void modem_st_registered_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	osmo_fsm_inst_state_chg(fi, MODEM_ST_START_IFACE, 5, 0);
}
static void modem_st_registered(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void wds_start_network_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	struct qmi_wds_start_network_response res = {};
	long err = 1;
	int ret;

	ret = qmi_parse_wds_start_network_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get operating mode. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	/* to get error causes */
	err = ret = req->ret;
	if (ret && ret == QMI_PROTOCOL_ERROR_NO_EFFECT) {
		modem_log(modem, LOGL_INFO, "Start network return NO_EFFECT. Trying to continue");
	} else if (ret) {
		modem_log(modem, LOGL_INFO, "Failed start network. QMI Status %d/%s.", req->ret,
			  qmi_get_error_str(req->ret));
		if (res.set.call_end_reason)
			modem_log(modem, LOGL_INFO, "Call End Reason %x", res.data.call_end_reason);

		if (res.set.verbose_call_end_reason) {
			modem_log(modem, LOGL_INFO, "Verbose End Reason type %x/reason %x",
				  res.data.verbose_call_end_reason.type, res.data.verbose_call_end_reason.reason);
		}

		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)(long)req->ret);
		return;
	}

	if (res.set.packet_data_handle) {
		modem->qmi->wds.packet_handle = res.data.packet_data_handle;
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_SUCCEED, (void *)err);
	} else {
		modem_log(modem, LOGL_INFO, "No packet data handle. Suspicious...");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)err);
	}
}

static void modem_st_start_iface_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *wds = uqmi_service_find(modem->qmi, QMI_SERVICE_WDS);

	tx_wds_start_network(modem, wds, wds_start_network_cb, modem->qmi->wds.profile_id, modem->qmi->wds.ip_family);
}
static void modem_st_start_iface(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct modem *modem = fi->priv;
	long reason;

	switch (event) {
	case MODEM_EV_RX_FAILED:
		reason = (long)data;
		if (reason == QMI_PROTOCOL_ERROR_CALL_FAILED) {
			fi->T = N_RESEND;
		} else {
			uqmid_modem_set_error(modem, "Start Iface/Network failed!");
			osmo_fsm_inst_state_chg(fi, MODEM_ST_POWEROFF, 0, 0);
		}
		break;
	case MODEM_EV_RX_SUCCEED:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_LIVE, 0, 0);
		break;
	}
}

static void ipv4_to_sockaddr(const uint32_t wds_ip, struct sockaddr_in *addr)
{
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(wds_ip);
}

static void ipv6_to_sockaddr(const uint16_t *wds_ip6, struct sockaddr_in6 *addr)
{
	memset(addr, 0, sizeof(*addr));
	addr->sin6_family = AF_INET6;
	memcpy(&addr->sin6_addr, wds_ip6, sizeof(struct in6_addr));
}

static void wds_get_current_settings_cb(struct qmi_service *service, struct qmi_request *req, struct qmi_msg *msg)
{
	struct modem *modem = req->cb_data;
	struct qmi_wds_get_current_settings_response res = {};
	int ret;

	ret = qmi_parse_wds_get_current_settings_response(msg, &res);
	if (ret) {
		modem_log(modem, LOGL_INFO, "Failed to get current settings. Failed to parse message");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, NULL);
		return;
	}

	if (!res.set.ip_family) {
		modem_log(modem, LOGL_ERROR, "Modem didn't include ip family");
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)1);
	}

	memset(&modem->brearer.dns1, 0, sizeof(modem->brearer.dns1));
	memset(&modem->brearer.dns2, 0, sizeof(modem->brearer.dns2));

	switch (res.data.ip_family) {
	case QMI_WDS_IP_FAMILY_IPV4:
		memset(&modem->brearer.v4_addr, 0, sizeof(modem->brearer.v4_addr));
		memset(&modem->brearer.v4_netmask, 0, sizeof(modem->brearer.v4_netmask));
		memset(&modem->brearer.v4_gateway, 0, sizeof(modem->brearer.v4_gateway));

		if (!res.set.ipv4_address) {
			modem_log(modem, LOGL_ERROR, "Modem didn't include IPv4 Address.");
			osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)2);
		}
		ipv4_to_sockaddr(res.data.ipv4_address, &modem->brearer.v4_addr);

		/* Still unsure why there is a this subnet mask. maybe natt'ed by the modem? */
		if (res.set.ipv4_gateway_subnet_mask) {
			ipv4_to_sockaddr(res.data.ipv4_gateway_subnet_mask,
					 (struct sockaddr_in *)&modem->brearer.v4_netmask);
		}

		if (res.set.ipv4_gateway_address) {
			ipv4_to_sockaddr(res.data.ipv4_gateway_address,
					 (struct sockaddr_in *)&modem->brearer.v4_gateway);
		}

		if (res.set.primary_ipv4_dns_address) {
			ipv4_to_sockaddr(res.data.primary_ipv4_dns_address, (struct sockaddr_in *)&modem->brearer.dns1);
		}

		if (res.set.secondary_ipv4_dns_address) {
			ipv4_to_sockaddr(res.data.secondary_ipv4_dns_address,
					 (struct sockaddr_in *)&modem->brearer.dns2);
		}
		break;
	case QMI_WDS_IP_FAMILY_IPV6:
		if (!res.set.ipv6_address) {
			modem_log(modem, LOGL_ERROR, "Modem didn't include IPv6 Address.");
			osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)3);
		}

		if (res.set.ipv6_primary_dns_address) {
			ipv6_to_sockaddr(res.data.ipv6_primary_dns_address,
					 (struct sockaddr_in6 *)&modem->brearer.dns1);
		}

		if (res.set.ipv6_secondary_dns_address) {
			ipv6_to_sockaddr(res.data.ipv6_secondary_dns_address,
					 (struct sockaddr_in6 *)&modem->brearer.dns2);
		}

		break;
	default:
		modem_log(modem, LOGL_ERROR, "Modem reported an unknown ip_family %d.", res.data.ip_family);
		osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_FAILED, (void *)4);
		break;
	}

	osmo_fsm_inst_dispatch(modem->fi, MODEM_EV_RX_SUCCEED, NULL);
}

static void modem_st_live_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;
	struct qmi_service *wds = uqmi_service_find(modem->qmi, QMI_SERVICE_WDS);

	tx_wds_get_current_settings(modem, wds, wds_get_current_settings_cb);
	/* TODO: register to indications */
}

static void modem_st_live(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case MODEM_EV_RX_FAILED:
		break;
	case MODEM_EV_RX_SUCCEED:
		break;
	}
}

static void modem_st_failed(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static void modem_st_destroy_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	struct modem *modem = fi->priv;

	/* FIXME: try to close the network, poweroff the modem ? */
	qmi_device_close(modem->qmi, 5000);
}
static void modem_st_destroy(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
}

static int modem_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	struct modem *modem = fi->priv;
	struct qmi_service *service;

	switch (fi->state) {
	case MODEM_ST_NETSEARCH:
		service = uqmi_service_find(modem->qmi, QMI_SERVICE_NAS);
		if (!service) {
			modem_log(modem, LOGL_ERROR, "NAS service doesn't exist");
			return 1;
		}
		uqmi_service_send_simple(service, qmi_set_nas_get_serving_system_request, get_serving_system_cb, modem);
		osmo_timer_schedule(&fi->timer, NAS_SERVICE_POLL_TIMEOUT_S, 0);
		break;
	case MODEM_ST_START_IFACE:
		switch (fi->T) {
		case N_RESEND:
			/* resend the packet */
			service = uqmi_service_find(modem->qmi, QMI_SERVICE_WDS);
			if (!service) {
				modem_log(modem, LOGL_ERROR, "WDS service doesn't exist");
				return 1;
			}
			tx_wds_start_network(modem, service, wds_start_network_cb, modem->qmi->wds.profile_id,
					     modem->qmi->wds.ip_family);
			osmo_timer_schedule(&fi->timer, 5, 0);
			break;
		default:
			/* we timedout ! No answer? */
			modem_log(modem, LOGL_ERROR, "WDS: Couldn't start the interface!");
			/* TODO: enter failure state */
			break;
		}
		break;
	default:
		switch (fi->T) {
		case N_FAILURE:
			modem_log(modem, LOGL_ERROR, "State timedout. Entering failure state");
			osmo_fsm_inst_state_chg(fi, MODEM_ST_FAILED, 0, 0);
			break;
		}

		break;
	}

	return 0;
}

static void modem_fsm_allstate_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	switch (event) {
	case MODEM_EV_REQ_DESTROY:
		osmo_fsm_inst_state_chg(fi, MODEM_ST_DESTROY, 0, 0);
		break;
	}
}

static const struct osmo_fsm_state modem_states[] = {
	[MODEM_ST_IDLE] = {
		.in_event_mask = S(MODEM_EV_REQ_START),
		.out_state_mask = S(MODEM_ST_RESYNC) | S(MODEM_ST_DESTROY),
		.name = "UNCONFIGURED",
		.action = modem_st_unconfigured,
	},
	[MODEM_ST_RESYNC] = {
		.in_event_mask = S(MODEM_EV_RX_SYNC),
		.out_state_mask = S(MODEM_ST_GET_VERSION)
				  | S(MODEM_ST_FAILED)
				  | S(MODEM_ST_DESTROY),
		.name = "RESYNC",
		.action = modem_st_resync,
		.onenter = modem_st_resync_onenter,
	},
	[MODEM_ST_GET_VERSION] = {
		.in_event_mask = S(MODEM_EV_RX_VERSION),
		.out_state_mask = S(MODEM_ST_GET_MODEL) | S(MODEM_ST_DESTROY),
		.name = "GET_VERSION",
		.action = modem_st_get_version,
		.onenter = modem_st_get_version_onenter,
	},
	[MODEM_ST_GET_MODEL] = {
		.in_event_mask = S(MODEM_EV_RX_MODEL) | S(MODEM_EV_RX_MANUFACTURER) | S(MODEM_EV_RX_REVISION),
		.out_state_mask = S(MODEM_ST_GET_IMSI) | S(MODEM_ST_DESTROY),
		.name = "GET_MODEL",
		.action = modem_st_get_model,
		.onenter = modem_st_get_model_onenter,
	},
	[MODEM_ST_GET_IMSI] = {
		.in_event_mask = S(MODEM_EV_RX_IMSI)
				 | S(MODEM_EV_RX_UIM_FAILED)
				 | S(MODEM_EV_RX_UIM_GET_SLOT_FAILED)
				 | S(MODEM_EV_RX_UIM_VALID_ICCID)
				 | S(MODEM_EV_RX_UIM_NO_UIM_FOUND)
				 | S(MODEM_EV_RX_IMSI_DMS_FAILED),
		.out_state_mask = S(MODEM_ST_POWEROFF) | S(MODEM_ST_DESTROY),
		.name = "GET_IMSI",
		.action = modem_st_get_imsi,
		.onenter = modem_st_get_imsi_onenter,
	},
	[MODEM_ST_POWEROFF] = {
		.in_event_mask = S(MODEM_EV_RX_POWEROFF)
				 | S(MODEM_EV_RX_POWERON)
				 | S(MODEM_EV_RX_POWERSET)
				 | S(MODEM_EV_REQ_CONFIGURED),
		.out_state_mask = S(MODEM_ST_UNLOCK_PIN) | S(MODEM_ST_DESTROY),
		.name = "POWEROFF",
		.action = modem_st_poweroff,
		.onenter = modem_st_poweroff_onenter,
	},
	[MODEM_ST_UNLOCK_PIN] = {
		.in_event_mask = S(MODEM_EV_RX_UNLOCKED_PIN),
		.out_state_mask = S(MODEM_ST_CONFIGURE_MODEM) | S(MODEM_ST_DESTROY),
		.name = "UNLOCK_PIN",
		.action = modem_st_unlock_pin,
		.onenter = modem_st_unlock_pin_onenter,
	},
	[MODEM_ST_CONFIGURE_MODEM] = {
		.in_event_mask = S(MODEM_EV_RX_CONFIGURED)
				 | S(MODEM_EV_RX_GET_PROFILE_LIST)
				 | S(MODEM_EV_RX_MODIFIED_PROFILE),
		.out_state_mask = S(MODEM_ST_POWERON) | S(MODEM_ST_DESTROY),
		.name = "CONFIGURE_MODEM",
		.action = modem_st_configure_modem,
		.onenter = modem_st_configure_modem_onenter,
	},
	[MODEM_ST_POWERON] = {
		.in_event_mask = S(MODEM_EV_RX_POWEROFF)
				 | S(MODEM_EV_RX_POWERON)
				 | S(MODEM_EV_RX_POWERSET),
		.out_state_mask = S(MODEM_ST_NETSEARCH) | S(MODEM_ST_DESTROY),
		.name = "POWERON",
		.action = modem_st_poweron,
		.onenter = modem_st_poweron_onenter,
	},
	[MODEM_ST_NETSEARCH] = {
		.in_event_mask = S(MODEM_EV_RX_REGISTERED)
				 | S(MODEM_EV_RX_UNREGISTERED)
				 | S(MODEM_EV_RX_SEARCHING)
				 | S(MODEM_EV_RX_FAILED)
				 | S(MODEM_EV_RX_SUBSCRIBED)
				 | S(MODEM_EV_RX_SUBSCRIBE_FAILED),
		.out_state_mask = S(MODEM_ST_REGISTERED) | S(MODEM_ST_DESTROY),
		.name = "NETSEARCH",
		.action = modem_st_netsearch,
		.onenter = modem_st_netsearch_onenter,
	},
	[MODEM_ST_REGISTERED] = {
		.in_event_mask = 0,
		.out_state_mask = S(MODEM_ST_START_IFACE) | S(MODEM_ST_DESTROY),
		.name = "REGISTERED",
		.action = modem_st_registered,
		.onenter = modem_st_registered_onenter,
	},
	[MODEM_ST_START_IFACE] = {
		.in_event_mask = S(MODEM_EV_RX_SUCCEED)
				 | S(MODEM_EV_RX_FAILED),
		.out_state_mask = S(MODEM_ST_LIVE) | S(MODEM_ST_DESTROY) | S(MODEM_ST_POWEROFF),
		.name = "START_IFACE",
		.action = modem_st_start_iface,
		.onenter = modem_st_start_iface_onenter,
	},
	[MODEM_ST_LIVE] = {
		.in_event_mask = S(MODEM_EV_RX_SUCCEED) | S(MODEM_EV_RX_FAILED),
		.out_state_mask = S(MODEM_ST_DESTROY),
		.name = "LIVE",
		.action = modem_st_live,
		.onenter = modem_st_live_onenter,
	},
	[MODEM_ST_FAILED] = {
		.in_event_mask = 0,
		.out_state_mask = S(MODEM_ST_DESTROY),
		.name = "FAILED",
		.action = modem_st_failed,
		// .onenter = modem_st_live_onenter,s
	},
	[MODEM_ST_DESTROY] = {
		.in_event_mask = 0,
		.out_state_mask = 0,
		.name = "DESTROY",
		.action = modem_st_destroy,
		.onenter = modem_st_destroy_onenter,
	},
};

static struct osmo_fsm modem_fsm = {
	.name = "MODEM",
	.states = modem_states,
	.num_states = ARRAY_SIZE(modem_states),
	.allstate_event_mask = S(MODEM_EV_REQ_DESTROY),
	.allstate_action = modem_fsm_allstate_action,
	//    .cleanup = modem_fsm_cleanup,
	.timer_cb = modem_fsm_timer_cb,
	.event_names = modem_event_names,
	.pre_term = NULL,
};

struct osmo_fsm_inst *modem_fsm_alloc(struct modem *modem)
{
	return osmo_fsm_inst_alloc(&modem_fsm, modem, modem, LOGL_DEBUG, modem->name);
}

static __attribute__((constructor)) void on_dso_load_ctx(void)
{
	OSMO_ASSERT(osmo_fsm_register(&modem_fsm) == 0);
}
