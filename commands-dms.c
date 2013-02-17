#include "qmi-message.h"

static const char *get_pin_status(QmiDmsUimPinStatus status)
{
	static const char *pin_status[] = {
		[QMI_DMS_UIM_PIN_STATUS_NOT_INITIALIZED] = "not_initialized",
		[QMI_DMS_UIM_PIN_STATUS_ENABLED_NOT_VERIFIED] = "not_verified",
		[QMI_DMS_UIM_PIN_STATUS_ENABLED_VERIFIED] = "verified",
		[QMI_DMS_UIM_PIN_STATUS_DISABLED] = "disabled",
		[QMI_DMS_UIM_PIN_STATUS_BLOCKED] = "blocked",
		[QMI_DMS_UIM_PIN_STATUS_PERMANENTLY_BLOCKED] = "permanently_blocked",
		[QMI_DMS_UIM_PIN_STATUS_UNBLOCKED] = "unblocked",
		[QMI_DMS_UIM_PIN_STATUS_CHANGED] = "changed",
	};
	const char *res = "Unknown";

	if (status < ARRAY_SIZE(pin_status) && pin_status[status])
		res = pin_status[status];

	return res;
}

static void cmd_dms_get_pin_status_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	struct qmi_dms_uim_get_pin_status_response res;

	qmi_parse_dms_uim_get_pin_status_response(msg, &res);
	if (res.set.pin1_status) {
		printf("pin1_status=%s\n"
			   "pin1_verify_tries=%d\n"
			   "pin1_unblock_tries=%d\n",
			   get_pin_status(res.data.pin1_status.current_status),
			   res.data.pin1_status.verify_retries_left,
			   res.data.pin1_status.unblock_retries_left);
	}
	if (res.set.pin2_status) {
		printf("pin2_status=%s\n"
			   "pin2_verify_tries=%d\n"
			   "pin2_unblock_tries=%d\n",
			   get_pin_status(res.data.pin2_status.current_status),
			   res.data.pin2_status.verify_retries_left,
			   res.data.pin2_status.unblock_retries_left);
	}
}

static enum qmi_cmd_result
cmd_dms_get_pin_status_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	qmi_set_dms_uim_get_pin_status_request(msg);
	return QMI_CMD_REQUEST;
}

static void cmd_dms_verify_pin1_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	if (!msg) {
		printf("failed\n");
		return;
	}

	printf("ok\n");
}

static enum qmi_cmd_result
cmd_dms_verify_pin1_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	struct qmi_dms_uim_verify_pin_request data = {
		QMI_INIT_SEQUENCE(info,
			.pin_id = QMI_DMS_UIM_PIN_ID_PIN,
			.pin = arg
		)
	};
	qmi_set_dms_uim_verify_pin_request(msg, &data);
	return QMI_CMD_REQUEST;
}

#define cmd_dms_verify_pin2_cb cmd_dms_verify_pin1_cb
static enum qmi_cmd_result
cmd_dms_verify_pin2_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	struct qmi_dms_uim_verify_pin_request data = {
		QMI_INIT_SEQUENCE(info,
			.pin_id = QMI_DMS_UIM_PIN_ID_PIN2,
			.pin = arg
		)
	};
	qmi_set_dms_uim_verify_pin_request(msg, &data);
	return QMI_CMD_REQUEST;
}
