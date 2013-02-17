#define __uqmi_wds_commands \
	__uqmi_command(wds_set_auth, set-auth, required, QMI_SERVICE_WDS), \
	__uqmi_command(wds_set_username, set-username, required, QMI_SERVICE_WDS), \
	__uqmi_command(wds_set_password, set-password, required, QMI_SERVICE_WDS)

#define wds_helptext \
		"  --set-auth pap|chap|both|none:    Set network authentication type\n" \
		"  --set-username:                   Set network username\n" \
		"  --set-password:                   Set network password\n" \

