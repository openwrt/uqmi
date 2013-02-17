#include "qmi-message.h"

static struct qmi_nas_set_system_selection_preference_request sel_req;

#define cmd_nas_set_network_modes_cb no_cb
static enum qmi_cmd_result
cmd_nas_set_network_modes_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	static const struct {
		const char *name;
		QmiNasRatModePreference val;
	} modes[] = {
		{ "cdma", QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1X | QMI_NAS_RAT_MODE_PREFERENCE_CDMA_1XEVDO },
		{ "td-scdma", QMI_NAS_RAT_MODE_PREFERENCE_TD_SCDMA },
		{ "gsm", QMI_NAS_RAT_MODE_PREFERENCE_GSM },
		{ "umts", QMI_NAS_RAT_MODE_PREFERENCE_UMTS },
		{ "lte", QMI_NAS_RAT_MODE_PREFERENCE_LTE },
	};
	QmiNasRatModePreference val = 0;
	char *word;
	int i;

	for (word = strtok(arg, ",");
	     word;
	     word = strtok(NULL, ",")) {
		bool found = false;

		for (i = 0; i < ARRAY_SIZE(modes); i++) {
			if (strcmp(word, modes[i].name) != 0 &&
				strcmp(word, "all") != 0)
				continue;

			val |= modes[i].val;
			found = true;
		}

		if (!found) {
			fprintf(stderr, "Invalid network mode '%s'\n", word);
			return QMI_CMD_EXIT;
		}
	}

	qmi_set(&sel_req, mode_preference, val);
	qmi_set_nas_set_system_selection_preference_request(msg, &sel_req);
	return QMI_CMD_REQUEST;
}

#define cmd_nas_initiate_network_register_cb no_cb
static enum qmi_cmd_result
cmd_nas_initiate_network_register_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	static struct qmi_nas_initiate_network_register_request register_req;
	qmi_set_nas_initiate_network_register_request(msg, &register_req);
	return QMI_CMD_REQUEST;
}

static void
cmd_nas_get_signal_info_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	struct qmi_nas_get_signal_info_response res;

	qmi_parse_nas_get_signal_info_response(msg, &res);

	if (res.set.cdma_signal_strength)
		printf("cdma_rssi=%d\ncdma_ecio=%d\n",
			res.data.cdma_signal_strength.rssi,
			res.data.cdma_signal_strength.ecio);

	if (res.set.hdr_signal_strength)
		printf("hdr_rssi=%d\nhdr_ecio=%d\nhdr_io=%d\n",
			res.data.hdr_signal_strength.rssi,
			res.data.hdr_signal_strength.ecio,
			res.data.hdr_signal_strength.io);

	if (res.set.gsm_signal_strength)
		printf("gsm_rssi=%d\n", res.data.gsm_signal_strength);

	if (res.set.wcdma_signal_strength)
		printf("wcdma_rssi=%d\nwcdma_ecio=%d\n",
			res.data.wcdma_signal_strength.rssi,
			res.data.wcdma_signal_strength.ecio);

	if (res.set.lte_signal_strength)
		printf("lte_rssi=%d\nlte_rsrq=%d\nlte_rsrp=%d\nlte_snr=%d\n",
			res.data.lte_signal_strength.rssi,
			res.data.lte_signal_strength.rsrq,
			res.data.lte_signal_strength.rsrp,
			res.data.lte_signal_strength.snr);

}

static enum qmi_cmd_result
cmd_nas_get_signal_info_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	qmi_set_nas_get_signal_info_request(msg);
	return QMI_CMD_REQUEST;
}
