#!/bin/sh

# Configure pluto platform so that the waveform can be started
# Name of the custom FIR filter
FILTER=AD9361_256kSPS.ftr


# Config Link Layer Discovery Protocol to broadcast callsign
# This has to be done after lldp is started (S60)
CALLSIGN=`fw_printenv -n hnap_callsign`
lldpcli configure lldp tx-interval 60
lldpcli configure system description $CALLSIGN
lldpcli resume

# Install custom FIR filter
cat /root/$FILTER > /sys/bus/iio/devices/iio:device1/filter_fir_config

