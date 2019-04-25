#!/bin/bash
#
# generates wireshark.csv and outputs data in stdout

GATEWAY=$(ip route show | grep default | sed -n 's/[^0-9]\+\([0-9\.]\+\).*/\1/p')
LOCAL=$(ip route get $GATEWAY | sed -n 's/.*src \([0-9\.]\+\)/\1/p')

XYZ=$(date +"%Y-%m-%d_%H-%M-%S")
JANALOG_CSV="logfile-$XYZ.csv"
CAPTURE_PCAP="capture-$XYZ.pcap"
CAPTURE_CSV="$(basename "$CAPTURE_PCAP" .pcap).csv"
FINAL_CSV="data-$XYZ.csv"

echo "local    > $LOCAL"
echo "gateway  > $GATEWAY"
echo "jana     > $JANALOG_CSV"
echo "tshark   > $CAPTURE_PCAP"
echo "pcap2csv > $CAPTURE_CSV"
echo "calc     > $FINAL_CSV"

# tshark -i $INTERFACE -F pcap -w $PCAP -f "udp" -a "12s??" &
# src/jana -c $GATEWAY -f $JANALOG_CSV
# ./src/pcap2csv $CAPTURE_PCAP -c $LOCAL -s $GATEWAY > $CAPTURE_CSV
# ./src/calc $CAPTURE_CSV $JANALOG_CSV > $FINAL_CSV
