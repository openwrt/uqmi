/*
 * Copyright (C) 2016  Bjørn Mork <bjorn@mork.no>
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <stdio.h>

#include "mbim.h"

static const uint8_t qmiuuid[16] = { 0xd1, 0xa3, 0x0b, 0xc2, 0xf9, 0x7a, 0x6e, 0x43,
				     0xbf, 0x65, 0xc7, 0xe2, 0x4f, 0xb0, 0xf0, 0xd3 };
#define LIBQMI_QMI_PROXY 1
#ifdef LIBQMI_QMI_PROXY
static const uint8_t proxyuuid[16] =  { 0x83, 0x8c, 0xf7, 0xfb, 0x8d, 0x0d, 0x4d, 0x7f,
					0x87, 0x1e, 0xd7, 0x1d, 0xbe, 0xfb, 0xb3, 0x9b };

#define MBIM_CMD_PROXY_CONTROL_CONFIGURATION	1

struct mbim_proxy_control {
	uint32_t dev_off;
	uint32_t dev_len;
	uint32_t timeout;
} __attribute__((packed));

int mbim_proxy_cmd(struct mbim_command_message *msg, const char *path)
{
	struct mbim_proxy_control *p = (void *)(msg + 1);
	int i, pathlen = strlen(path);
	char *c = (void *)(p + 1);;

	msg->header.type = cpu_to_le32(MBIM_MESSAGE_TYPE_COMMAND);
	msg->header.length = cpu_to_le32(sizeof(*msg) + 2 * pathlen + sizeof(struct mbim_proxy_control));
	msg->header.transaction_id = cpu_to_le32(1);
	msg->fragment_header.total = cpu_to_le32(1);
	msg->fragment_header.current = 0;
	memcpy(msg->service_id, proxyuuid, 16);
	msg->command_id = cpu_to_le32(MBIM_CMD_PROXY_CONTROL_CONFIGURATION);
	msg->command_type = cpu_to_le32(MBIM_MESSAGE_COMMAND_TYPE_SET);
	msg->buffer_length = cpu_to_le32(2 * pathlen + sizeof(struct mbim_proxy_control));

	p->timeout = cpu_to_le32(5); // FIXME: hard coded timeout
	p->dev_off = cpu_to_le32(sizeof(struct mbim_proxy_control));
	p->dev_len = cpu_to_le32(2 * pathlen);
	memset(c, 0, 2 * pathlen);
	for (i = 0; i < pathlen; i++)
		c[i * 2] = path[i];
	return sizeof(struct mbim_command_message) + sizeof(struct mbim_proxy_control) + 2 * pathlen;
}

bool is_mbim_proxy(struct mbim_command_message *msg)
{
	return msg->header.type == cpu_to_le32(MBIM_MESSAGE_TYPE_COMMAND_DONE) &&
		msg->command_id == cpu_to_le32(MBIM_CMD_PROXY_CONTROL_CONFIGURATION) &&
		!msg->command_type &&	/* actually 'status' here */
		!memcmp(msg->service_id, proxyuuid, 16);
}
#endif

bool is_mbim_qmi(struct mbim_command_message *msg)
{
	return msg->header.type == cpu_to_le32(MBIM_MESSAGE_TYPE_COMMAND_DONE) &&
		msg->command_id == cpu_to_le32(MBIM_CID_QMI_MSG) &&
		!msg->command_type &&	/* actually 'status' here */
		!memcmp(msg->service_id, qmiuuid, 16);
									    }

void mbim_qmi_cmd(struct mbim_command_message *msg, int len, uint16_t tid)
{
	msg->header.type = cpu_to_le32(MBIM_MESSAGE_TYPE_COMMAND);
	msg->header.length = cpu_to_le32(sizeof(*msg) + len);
	msg->header.transaction_id = cpu_to_le32(tid);
	msg->fragment_header.total = cpu_to_le32(1);
	msg->fragment_header.current = 0;
	memcpy(msg->service_id, qmiuuid, 16);
	msg->command_id = cpu_to_le32(MBIM_CID_QMI_MSG);
	msg->command_type = cpu_to_le32(MBIM_MESSAGE_COMMAND_TYPE_SET);
	msg->buffer_length = cpu_to_le32(len);
}
