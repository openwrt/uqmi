#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include "uqmi.h"
#include "commands.h"

static void no_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
}

static void cmd_version_cb(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg)
{
	struct qmi_ctl_get_version_info_response res;
	int i;

	if (!msg) {
		printf("Request version failed: %d\n", req->ret);
		return;
	}

	qmi_parse_ctl_get_version_info_response(msg, &res);

	printf("Found %d: services:\n", res.data.service_list_n);
	for (i = 0; i < res.data.service_list_n; i++) {
		printf("Service %d, version: %d.%d\n",
			res.data.service_list[i].service,
			res.data.service_list[i].major_version,
			res.data.service_list[i].minor_version);
	}
}

static enum qmi_cmd_result
cmd_version_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	qmi_set_ctl_get_version_info_request(msg);
	return QMI_CMD_REQUEST;
}

#define cmd_get_client_id_cb no_cb
static enum qmi_cmd_result
cmd_get_client_id_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	QmiService svc = qmi_service_get_by_name(arg);

	if (svc < 0) {
		fprintf(stderr, "Invalid service name '%s'\n", arg);
		return QMI_CMD_EXIT;
	}

	if (qmi_service_connect(qmi, svc, -1)) {
		fprintf(stderr, "Failed to connect to service\n");
		return QMI_CMD_EXIT;
	}

	printf("%d\n", qmi_service_get_client_id(qmi, svc));
	return QMI_CMD_DONE;
}

#define cmd_set_client_id_cb no_cb
static enum qmi_cmd_result
cmd_set_client_id_prepare(struct qmi_dev *qmi, struct qmi_request *req, struct qmi_msg *msg, char *arg)
{
	QmiService svc;
	int id;
	char *s;

	s = strchr(arg, ',');
	if (!s) {
		fprintf(stderr, "Invalid argument\n");
		return QMI_CMD_EXIT;
	}
	*s = 0;
	s++;

	id = strtoul(s, &s, 0);
	if (s && *s) {
		fprintf(stderr, "Invalid argument\n");
		return QMI_CMD_EXIT;
	}

	svc = qmi_service_get_by_name(arg);
	if (svc < 0) {
		fprintf(stderr, "Invalid service name '%s'\n", arg);
		return QMI_CMD_EXIT;
	}

	if (qmi_service_connect(qmi, svc, id)) {
		fprintf(stderr, "Failed to connect to service\n");
		return QMI_CMD_EXIT;
	}

	return QMI_CMD_DONE;
}

#include "commands-wds.c"
#include "commands-dms.c"

#define __uqmi_command(_name, _optname, _arg, _type) \
	[__UQMI_COMMAND_##_name] = { \
		.name = #_optname, \
		.type = _type, \
		.prepare = cmd_##_name##_prepare, \
		.cb = cmd_##_name##_cb, \
	}

const struct uqmi_cmd_handler uqmi_cmd_handler[__UQMI_COMMAND_LAST] = {
	__uqmi_commands
};
#undef __uqmi_command

static struct uqmi_cmd *cmds;
static int n_cmds;

void uqmi_add_command(char *arg, int cmd)
{
	int idx = n_cmds++;

	cmds = realloc(cmds, n_cmds * sizeof(*cmds));
	cmds[idx].handler = &uqmi_cmd_handler[cmd];
	cmds[idx].arg = optarg;
}

static bool __uqmi_run_commands(struct qmi_dev *qmi, bool option)
{
	static char buf[2048];
	static struct qmi_request req;
	int i;

	for (i = 0; i < n_cmds; i++) {
		enum qmi_cmd_result res;
		bool cmd_option = cmds[i].handler->type == CMD_TYPE_OPTION;

		if (cmd_option != option)
			continue;

		if (cmds[i].handler->type > QMI_SERVICE_CTL &&
		    qmi_service_connect(qmi, cmds[i].handler->type, -1)) {
			fprintf(stderr, "Error in command '%s': failed to connect to service\n",
			        cmds[i].handler->name);
			return false;
		}
		res = cmds[i].handler->prepare(qmi, &req, (void *) buf, cmds[i].arg);
		switch(res) {
		case QMI_CMD_REQUEST:
			qmi_request_start(qmi, &req, (void *) buf, cmds[i].handler->cb);
			qmi_request_wait(qmi, &req);
			break;
		case QMI_CMD_EXIT:
			return false;
		case QMI_CMD_DONE:
		default:
			continue;
		}
	}
	return true;
}

void uqmi_run_commands(struct qmi_dev *qmi)
{
	if (__uqmi_run_commands(qmi, true))
		__uqmi_run_commands(qmi, false);
	free(cmds);
	cmds = NULL;
	n_cmds = 0;
}
