#!/bin/sh

# Networking: create tap devices
# Note: this has to be done before the lldp service is started (S60)
tunctl -t tap0

#M0MUX - If type is client, obtain ip/route from dhcp
HNAP_TYPE=`fw_printenv -n hnap_type 2> /dev/null`

 if [ $HNAP_TYPE = 0 ]
 then
      TAP_IP_ADDR=`fw_printenv -n hnap_tap_ip`
      TAP_NET_MASK=`fw_printenv -n hnap_tap_mask`
      DFGW=`fw_printenv -n hnap_dfgw 2> /dev/null`

      TAP_DHCP_DNS=`fw_printenv -n hnap_tap_dhcp_dns 2> /dev/null || echo 192.168.2.1`
      TAP_DHCP_DEV=`fw_printenv -n hnap_tap_dhcp_dev 2> /dev/null || echo usb0`
      TAP_DHCP_GW=`fw_printenv -n hnap_tap_dhcp_gw 2> /dev/null || echo 192.168.2.1`
      TAP_DHCP_MASK=`fw_printenv -n hnap_tap_mask 2> /dev/null || echo 192.168.2.1`
      TAP_DHCP_START=`fw_printenv -n hnap_tap_dhcp_start 2> /dev/null || echo 192.168.2.1`
      TAP_DHCP_END=`fw_printenv -n hnap_tap_dhcp_end 2> /dev/null || echo 192.168.2.1`
      
      ifconfig tap0 $TAP_IP_ADDR netmask $TAP_NET_MASK
      ifconfig tap0 up

      #M0MUX - Add Default Route via eth0 interface
      ip route add 0.0.0.0/0 via $DFGW
      #M0MUX - Enable IP Forwarding
      echo 1 > /proc/sys/net/ipv4/ip_forward

      killall udhcpd
      ### /etc/udhcpd.conf ###
      #mod by DL5OP/M0MUX
      UDHCPD_CONF=/etc/udhcpd.conf
      echo "interface $TAP_DHCP_DEV" > $UDHCPD_CONF
      echo "start $TAP_DHCP_START" >> $UDHCPD_CONF
      echo "end $TAP_DHCP_END" >> $UDHCPD_CONF
      echo "option subnet $TAP_DHCP_MASK" >> $UDHCPD_CONF
      echo "option router $TAP_DHCP_GW" >> $UDHCPD_CONF
      echo "option dns $TAP_DHCP_DNS" >> $UDHCPD_CONF
      /usr/sbin/udhcpd /etc/udhcpd.conf &
      
 else

#     killall udhcpd

     ifconfig eth0 down
     ifconfig eth0 0.0.0.0 promisc up
     ifconfig tap0 up

     brctl addbr br0
     brctl addif br0 eth0 tap0
     ifconfig br0 up
     
  fi
