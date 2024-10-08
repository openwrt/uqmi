/*
 * uqmi -- tiny QMI support implementation
 *
 * Copyright (C) 2014-2015 Felix Fietkau <nbd@openwrt.org>
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

#ifndef __UQMI_H
#define __UQMI_H

#include <stdbool.h>

#include <libubox/uloop.h>
#include <libubox/ustream.h>

#include "qmi-message.h"

#ifdef DEBUG_PACKET
void dump_packet(const char *prefix, void *ptr, int len);
#else
static inline void dump_packet(const char *prefix, void *ptr, int len)
{
}
#endif

#define __qmi_services \
    __qmi_service(QMI_SERVICE_WDS), \
    __qmi_service(QMI_SERVICE_DMS), \
    __qmi_service(QMI_SERVICE_NAS), \
    __qmi_service(QMI_SERVICE_QOS), \
    __qmi_service(QMI_SERVICE_WMS), \
    __qmi_service(QMI_SERVICE_PDS), \
    __qmi_service(QMI_SERVICE_AUTH), \
    __qmi_service(QMI_SERVICE_AT), \
    __qmi_service(QMI_SERVICE_VOICE), \
    __qmi_service(QMI_SERVICE_CAT2), \
    __qmi_service(QMI_SERVICE_UIM), \
    __qmi_service(QMI_SERVICE_PBM), \
    __qmi_service(QMI_SERVICE_LOC), \
    __qmi_service(QMI_SERVICE_SAR), \
    __qmi_service(QMI_SERVICE_RMTFS), \
    __qmi_service(QMI_SERVICE_CAT), \
    __qmi_service(QMI_SERVICE_RMS), \
    __qmi_service(QMI_SERVICE_OMA), \
    __qmi_service(QMI_SERVICE_WDA)

#define __qmi_service(_n) __##_n
enum {
	__qmi_services,
	__QMI_SERVICE_LAST
};
#undef __qmi_service

struct qmi_dev;
struct qmi_request;
struct qmi_msg;

typedef void (*request_cb)(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg);

struct qmi_dev {
	struct ustream_fd sf;

	struct list_head req;

	struct {
		bool connected;
		uint8_t client_id;
		uint16_t tid;
	} service_data[__QMI_SERVICE_LAST];

	uint32_t service_connected;
	uint32_t service_keep_cid;
	uint32_t service_release_cid;

	uint8_t ctl_tid;
	void *buf;

	bool is_mbim;
};

struct qmi_request {
	struct list_head list;

	request_cb cb;

	bool *complete;
	bool pending;
	bool no_error_cb;
	uint8_t service;
	uint16_t tid;
	int ret;
};

extern bool cancel_all_requests;
int qmi_device_open(struct qmi_dev *qmi, const char *path);
void qmi_device_close(struct qmi_dev *qmi);

int qmi_request_start(struct qmi_dev *qmi, struct qmi_request *req, request_cb cb);
void qmi_request_cancel(struct qmi_dev *qmi, struct qmi_request *req);
int qmi_request_wait(struct qmi_dev *qmi, struct qmi_request *req);

static inline bool qmi_request_pending(struct qmi_request *req)
{
	return req->pending;
}

int qmi_service_connect(struct qmi_dev *qmi, QmiService svc, int client_id);
int qmi_service_get_client_id(struct qmi_dev *qmi, QmiService svc);
int qmi_service_release_client_id(struct qmi_dev *qmi, QmiService svc);
QmiService qmi_service_get_by_name(const char *str);

static inline uint8_t *uqmi_hexstring_parse(uint8_t *output,
					    const uint8_t *hexstr,
					    size_t hexstr_size)
{
	uint8_t *out = output;
	size_t i;

	for (i = 0; i < hexstr_size; i += 2) {
		if (hexstr[i] >= '0' && hexstr[i] <= '9')
			*out = (hexstr[i] - '0') << 4;
		else if (hexstr[i] >= 'a' && hexstr[i] <= 'f')
			*out = (hexstr[i] - 'a' + 10) << 4;
		else if (hexstr[i] >= 'A' && hexstr[i] <= 'F')
			*out = (hexstr[i] - 'A' + 10) << 4;
		else
			return NULL;

		if (i + 1 >= hexstr_size)
			return NULL;

		if (hexstr[i + 1] >= '0' && hexstr[i + 1] <= '9')
			*out |= hexstr[i + 1] - '0';
		else if (hexstr[i + 1] >= 'a' && hexstr[i + 1] <= 'f')
			*out |= hexstr[i + 1] - 'a' + 10;
		else if (hexstr[i + 1] >= 'A' && hexstr[i + 1] <= 'F')
			*out |= hexstr[i + 1] - 'A' + 10;
		else
			return NULL;

		out++;
	}

	return output;
}

static inline uint8_t *uqmi_hexstring_create(uint8_t *output,
					      const uint8_t *data,
					      size_t data_size)
{
	uint8_t *out = output;
	size_t i;

	for (i = 0; i < data_size; i++) {
		*out++ = "0123456789abcdef"[data[i] >> 4];
		*out++ = "0123456789abcdef"[data[i] & 0xf];
	}

	return output;
}

#endif
