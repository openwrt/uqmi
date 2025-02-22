#!/bin/sh /etc/rc.common

START=35
STOP=85
USE_PROCD=1

start_service() {
	procd_open_instance
	procd_set_param command /sbin/uqmid
	procd_set_param respawn
	procd_close_instance
}
