#!/bin/sh

CONFIG='{"apn":"internet.telekom","roaming":"true", "username": "telekom", "password": "tm"}'
ubus call uqmid.modem.modem1 configure "$CONFIG"
