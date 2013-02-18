#define __uqmi_wms_commands \
	__uqmi_command(wms_list_messages, list-messages, no, QMI_SERVICE_WMS), \
	__uqmi_command(wms_get_message, get-message, required, QMI_SERVICE_WMS), \
	__uqmi_command(wms_get_raw_message, get-raw-message, required, QMI_SERVICE_WMS) \

#define wms_helptext \
		"  --list-messages:                  List SMS messages\n" \
		"  --get-message <id>:               Get SMS message at index <id>\n" \
		"  --get-raw-message <id>:           Get SMS raw message contents at index <id>\n" \

