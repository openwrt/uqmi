#define __uqmi_wds_commands \
	__uqmi_command(wds_set_auth, wds-set-auth, required, QMI_SERVICE_WDS), \
	__uqmi_command(wds_set_username, wds-set-username, required, QMI_SERVICE_WDS), \
	__uqmi_command(wds_set_password, wds-set-password, required, QMI_SERVICE_WDS)

