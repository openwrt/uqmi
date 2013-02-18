#include "qmi-message.h"

static void cmd_wms_list_messages_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	struct qmi_wms_list_messages_response res;
	int i, len = 0;

	qmi_parse_wms_list_messages_response(msg, &res);
	blobmsg_alloc_string_buffer(&status, "messages", 1);
	for (i = 0; i < res.data.message_list_n; i++) {
		len += sprintf(blobmsg_realloc_string_buffer(&status, len + 12) + len,
		               " %d" + (len ? 0 : 1),
					   res.data.message_list[i].memory_index);
	}
	blobmsg_add_string_buffer(&status);
}

static enum qmi_cmd_result
cmd_wms_list_messages_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	static struct qmi_wms_list_messages_request mreq = {
		QMI_INIT(storage_type, QMI_WMS_STORAGE_TYPE_UIM),
		QMI_INIT(message_tag, QMI_WMS_MESSAGE_TAG_TYPE_MT_NOT_READ),
	};

	qmi_set_wms_list_messages_request(msg, &mreq);

	return QMI_CMD_REQUEST;
}

static void decode_7bit(char *name, const unsigned char *data, int data_len)
{
	bool multipart = false;
	char *dest;
	int part = 0, n_parts = 0;
	int len, pos_offset = 0;
	int i;

	if (data[0] == 5 && data[1] == 0 && data[2] == 3) {
		multipart = true;
		n_parts = data[4];
		part = data[5];
	} else if (data[0] == 6 && data[1] == 8 && data[2] == 4) {
		multipart = true;
		n_parts = data[5];
		part = data[6];
	}

	if (multipart) {
		len = data[0] + 1;
		data += len;
		data_len -= len;
		pos_offset = 6;
	}

	dest = blobmsg_alloc_string_buffer(&status, name, data_len * 8 / 7 + 2);
	for (i = 0; i < data_len; i++) {
		int pos = (i + pos_offset) % 7;

		if (pos == 0) {
			*(dest++) = data[i] & 0x7f;
		} else {
			if (i)
				*(dest++) = (data[i - 1] >> (7 + 1 - pos)) |
				            ((data[i] << pos) & 0x7f);

			if (pos == 6)
				*(dest++) = (data[i] >> 1) & 0x7f;
		}
	}
	*dest = 0;
	blobmsg_add_string_buffer(&status);

	if (multipart) {
		blobmsg_add_u32(&status, "part", part + 1);
		blobmsg_add_u32(&status, "parts", n_parts);
	}
}

static char *add_semioctet(char *str, char val)
{
	*str = '0' + (val & 0xf);
	if (*str <= '9')
		str++;

	*str = '0' + ((val >> 4) & 0xf);
	if (*str <= '9')
		str++;

	return str;
}

static unsigned char *
decode_semioctet_number(char *str, char *name, unsigned char *data, int len)
{
	str = blobmsg_alloc_string_buffer(&status, name, len * 2 + 2);
	if (*data == 0x91) {
		len--;
		*(str++) = '+';
	}
	data++;
	while (len) {
		str = add_semioctet(str, *data);
		data++;
		len--;
	}

	*str = 0;
	blobmsg_add_string_buffer(&status);

	return data;
}

static void cmd_wms_get_message_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	struct qmi_wms_raw_read_response res;
	unsigned char *data, *end;
	char *str;
	int i, cur_len;
	bool sent;

	qmi_parse_wms_raw_read_response(msg, &res);
	data = (unsigned char *) res.data.raw_message_data.raw_data;
	end = data + res.data.raw_message_data.raw_data_n;

	do {
		cur_len = *(data++);
		if (data + cur_len >= end)
			break;

		if (cur_len)
			data = decode_semioctet_number(str, "smsc", data, cur_len);

		if (data + 3 >= end)
			break;

		sent = (*data & 0x3) == 1;
		data++;
		if (sent)
			data++;

		cur_len = *(data++);
		if (data + cur_len >= end)
			break;

		if (cur_len)
			data = decode_semioctet_number(str, sent ? "receiver" : "sender", data, (cur_len + 1) / 2);

		if (data + 3 >= end)
			break;

		/* Protocol ID */
		if (*(data++) != 0)
			break;

		/* Data Encoding */
		if (*(data++) != 0)
			break;

		if (sent) {
			/* Message validity */
			data++;
		} else {
			if (data + 6 >= end)
				break;

			str = blobmsg_alloc_string_buffer(&status, "timestamp", 32);

			/* year */
			*(str++) = '2';
			*(str++) = '0';
			str = add_semioctet(str, data[0]);
			/* month */
			*(str++) = '-';
			str = add_semioctet(str, data[1]);
			/* day */
			*(str++) = '-';
			str = add_semioctet(str, data[2]);

			/* hour */
			*(str++) = ' ';
			str = add_semioctet(str, data[3]);
			/* minute */
			*(str++) = ':';
			str = add_semioctet(str, data[4]);
			/* second */
			*(str++) = ':';
			str = add_semioctet(str, data[5]);
			*str = 0;

			blobmsg_add_string_buffer(&status);

			data += 7;
		}

		cur_len = *(data++);
		decode_7bit("text", data, end - data);
	} while (0);
}

static enum qmi_cmd_result
cmd_wms_get_message_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	static struct qmi_wms_raw_read_request mreq = {
		QMI_INIT_SEQUENCE(message_memory_storage_id,
			.storage_type = QMI_WMS_STORAGE_TYPE_UIM,
		),
		QMI_INIT(message_mode, QMI_WMS_MESSAGE_MODE_GSM_WCDMA),
	};
	char *err;
	int id;

	id = strtoul(arg, &err, 10);
	if (err && *err) {
		blobmsg_add_string(&status, "error", "Invalid message ID");
		return QMI_CMD_EXIT;
	}

	mreq.data.message_memory_storage_id.memory_index = id;
	qmi_set_wms_raw_read_request(msg, &mreq);

	return QMI_CMD_REQUEST;
}


static void cmd_wms_get_raw_message_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	struct qmi_wms_raw_read_response res;
	unsigned char *data;
	int i, len = 0;
	char *str;

	qmi_parse_wms_raw_read_response(msg, &res);
	data = (unsigned char *) res.data.raw_message_data.raw_data;
	str = blobmsg_alloc_string_buffer(&status, "data", res.data.raw_message_data.raw_data_n * 3);
	for (i = 0; i < res.data.raw_message_data.raw_data_n; i++) {
		str += sprintf(str, " %02x" + (i ? 0 : 1), data[i]);
	}
	blobmsg_add_string_buffer(&status);
}

#define cmd_wms_get_raw_message_prepare cmd_wms_get_message_prepare
