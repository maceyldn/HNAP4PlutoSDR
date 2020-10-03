#!/bin/sh

[ -f /root/basestation ] || exit 0

# Autostart the client service

start() {
  	if [ `fw_printenv -n hnap_cli_autostart` = 1 ]
	then
		printf "Starting client application"
		/root/./client &
	fi
	echo "done"
}


stop() {
	printf "Stopping client: "
	killall client
	echo "done"
}

restart() {
	stop
	start
}

# See how we were called.
case "$1" in
  start)
	start
	;;
  stop)
	stop
	;;
  restart|reload)
	restart
	;;
  *)
	echo "Usage: $0 {start|stop|reload|restart}"
	exit 1
esac

exit $?
