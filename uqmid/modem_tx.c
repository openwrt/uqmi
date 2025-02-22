/*
 * uqmi -- tiny QMI support implementation
 *
 * Copyright (C) 2023 Alexander Couzens <lynxis@fe80.eu>
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

/* Used by uqmid to contain the modem state */

#include <stdint.h>
#include <talloc.h>

#include "qmi-struct.h"
#include "qmi-enums.h"
#include "qmi-message.h"
#include "qmi-enums-uim.h"

#include "logging.h"
#include "utils.h"

#include "modem.h"
#include "services.h"
#include "modem_fsm.h"

#include "modem_tx.h"

/* TODO: add qmap */
/* TODO: check when we have to use the endpoint number (usb) */
int
tx_wda_set_data_format(struct modem *modem, struct qmi_service *wda, request_cb cb)
{
	struct qmi_request *req = talloc_zero(wda, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_wda_set_data_format_request set_data_format_req = { 0 };
	qmi_set(&set_data_format_req, qos_format, false);
	qmi_set(&set_data_format_req, link_layer_protocol, QMI_WDA_LINK_LAYER_PROTOCOL_RAW_IP);
	qmi_set(&set_data_format_req, downlink_data_aggregation_protocol, QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED);
	qmi_set(&set_data_format_req, uplink_data_aggregation_protocol, QMI_WDA_DATA_AGGREGATION_PROTOCOL_DISABLED);

	int ret = qmi_set_wda_set_data_format_request(msg, &set_data_format_req);
	if (ret) {
		modem_log(modem, LOGL_ERROR, "Failed to encode set_data_format_req");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(wda, req);
}


int tx_dms_set_operating_mode(struct modem *modem, struct qmi_service *dms, uint8_t operating_mode, request_cb cb)
{
	struct qmi_request *req = talloc_zero(dms, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_dms_set_operating_mode_request set_op_mode_req = { QMI_INIT(mode, operating_mode) };

	int ret = qmi_set_dms_set_operating_mode_request(msg, &set_op_mode_req);
	if (ret) {
		LOG_ERROR("Failed to encode get version info");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(dms, req);
}

int tx_nas_subscribe_nas_events(struct modem *modem, struct qmi_service *nas, bool action, request_cb cb)
{
	struct qmi_request *req = talloc_zero(nas, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_nas_register_indications_request register_req = {};
	qmi_set(&register_req, serving_system_events, action);
	qmi_set(&register_req, subscription_info, action);
	qmi_set(&register_req, system_info, action);
	qmi_set(&register_req, signal_info, action);
	register_req.set.network_reject_information = 1;
	register_req.data.network_reject_information.enable_network_reject_indications = action;

	int ret = qmi_set_nas_register_indications_request(msg, &register_req);
	if (ret) {
		LOG_ERROR("Failed to encode get version info");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(nas, req);
}

int tx_wds_get_profile_list(struct modem *modem, struct qmi_service *wds, request_cb cb)
{
	struct qmi_request *req = talloc_zero(wds, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_wds_get_profile_list_request profile_req = { QMI_INIT(profile_type, QMI_WDS_PROFILE_TYPE_3GPP) };

	int ret = qmi_set_wds_get_profile_list_request(msg, &profile_req);
	if (ret) {
		LOG_ERROR("Failed to encode get profile list");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(wds, req);
}

int tx_wds_modify_profile(struct modem *modem, struct qmi_service *wds, request_cb cb, uint8_t profile, const char *apn,
			  uint8_t pdp_type)
{
	struct qmi_request *req = talloc_zero(wds, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_wds_modify_profile_request profile_req = {};

	profile_req.set.profile_identifier = 1;
	profile_req.data.profile_identifier.profile_type = QMI_WDS_PROFILE_TYPE_3GPP;
	profile_req.data.profile_identifier.profile_index = profile;
	qmi_set(&profile_req, pdp_type, pdp_type);

	if (apn)
		profile_req.data.apn_name = (char *) apn;

	int ret = qmi_set_wds_modify_profile_request(msg, &profile_req);
	if (ret) {
		LOG_ERROR("Failed to encode get profile list");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(wds, req);
}

int tx_wds_start_network(struct modem *modem, struct qmi_service *wds, request_cb cb, uint8_t profile_idx,
			 uint8_t ip_family)
{
	struct qmi_request *req = talloc_zero(wds, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_wds_start_network_request start_req = {};
	qmi_set(&start_req, profile_index_3gpp, profile_idx);
	qmi_set(&start_req, ip_family_preference, ip_family);

	int ret = qmi_set_wds_start_network_request(msg, &start_req);
	if (ret) {
		LOG_ERROR("Failed to encode start network request");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(wds, req);
}

int tx_wds_get_current_settings(struct modem *modem, struct qmi_service *wds, request_cb cb)
{
	struct qmi_request *req = talloc_zero(wds, struct qmi_request);
	struct qmi_msg *msg = talloc_zero_size(req, 1024);

	struct qmi_wds_get_current_settings_request get_settings_req = {};
	qmi_set(&get_settings_req, requested_settings,
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_PDP_TYPE |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_DNS_ADDRESS |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_GRANTED_QOS |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_IP_ADDRESS |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_GATEWAY_INFO |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_MTU |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_DOMAIN_NAME_LIST |
		QMI_WDS_GET_CURRENT_SETTINGS_REQUESTED_SETTINGS_IP_FAMILY);

	int ret = qmi_set_wds_get_current_settings_request(msg, &get_settings_req);
	if (ret) {
		LOG_ERROR("Failed to encode start network request");
		return 1;
	}

	req->msg = msg;
	req->cb = cb;
	req->cb_data = modem;
	return uqmi_service_send_msg(wds, req);
}
