#define __uqmi_dms_commands \
	__uqmi_command(dms_get_pin_status, dms-get-pin-status, no, QMI_SERVICE_DMS), \
	__uqmi_command(dms_verify_pin1, dms-verify-pin1, required, QMI_SERVICE_DMS), \
	__uqmi_command(dms_verify_pin2, dms-verify-pin2, required, QMI_SERVICE_DMS) \

