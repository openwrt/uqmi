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

#ifndef __UQMI_MESSAGE_H
#define __UQMI_MESSAGE_H

#include <libubox/utils.h>
#include <stdbool.h>

#include "qmi-struct.h"
#include "qmi-enums.h"

#include "qmi-enums-private.h"
#include "qmi-message-ctl.h"

#include "qmi-enums-dms.h"
#include "qmi-flags64-dms.h"
#include "qmi-message-dms.h"

#include "qmi-enums-nas.h"
#include "qmi-flags64-nas.h"
#include "qmi-message-nas.h"

#include "qmi-enums-pds.h"
#include "qmi-message-pds.h"

#include "qmi-enums-wds.h"
#include "qmi-message-wds.h"

#include "qmi-enums-wms.h"
#include "qmi-message-wms.h"

#include "qmi-enums-wda.h"
#include "qmi-message-wda.h"

#include "qmi-enums-uim.h"
#include "qmi-message-uim.h"

#define qmi_set(_data, _field, _val) \
	do { \
		(_data)->set._field = 1; \
		(_data)->data._field = _val; \
	} while (0)

#define qmi_set_ptr(_data, _field, _val) \
	do { \
		(_data)->data._field = _val; \
	} while (0)

#define qmi_set_static_array(_data, _field, _val) \
	do { \
		(_data)->data._field##_n = ARRAY_SIZE(_val); \
		(_data)->data._field = _val; \
	} while (0);

#define qmi_set_array(_data, _field, _val, _n) \
	do { \
		(_data)->data.n_##_field = _n; \
		(_data)->data._field = _val; \
	} while (0);

#define QMI_INIT(_field, _val) \
	.set._field = 1, \
	.data._field = (_val)

#define QMI_INIT_SEQUENCE(_field, ...) \
	.set._field = 1, \
	.data._field = { __VA_ARGS__ }

#define QMI_INIT_PTR(_field, _val) \
	.data._field = (_val)

#define QMI_INIT_STATIC_ARRAY(_field, _val) \
	.data._field##_n = ARRAY_SIZE(_val), \
	.data._field = (_val)

#define QMI_INIT_ARRAY(_field, _val, _num_elems) \
	.data._field##_n = (_num_elems), \
	.data._field = (_val)


enum {
	QMI_ERROR_NO_DATA = -1,
	QMI_ERROR_INVALID_DATA = -2,
	QMI_ERROR_CANCELLED = -3,
};

#define QMI_BUFFER_LEN 2048

void __qmi_alloc_reset(void);
void *__qmi_alloc_static(unsigned int len);
char *__qmi_copy_string(void *data, unsigned int len);
uint8_t *__qmi_get_buf(unsigned int *ofs);

static inline int tlv_data_len(struct tlv *tlv)
{
	return le16_to_cpu(tlv->len);
}

struct tlv *tlv_get_next(void **buf, unsigned int *buflen);
void tlv_new(struct qmi_msg *qm, uint8_t type, uint16_t len, void *data);

void qmi_init_request_message(struct qmi_msg *qm, QmiService service);
int qmi_complete_request_message(struct qmi_msg *qm);
int qmi_check_message_status(void *buf, unsigned int len);
void *qmi_msg_get_tlv_buf(struct qmi_msg *qm, int *len);

#endif
