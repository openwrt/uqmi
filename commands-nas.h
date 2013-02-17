#define __uqmi_nas_commands \
	__uqmi_command(nas_set_network_modes, set-network-modes, required, QMI_SERVICE_NAS) \

#define nas_helptext \
		"  --set-network-modes <modes>:      Set preferred network mode (Syntax: <mode1>[,<mode2>,...])\n" \
		"                                    Available modes: all, lte, umts, gsm, cdma, td-scdma\n"

