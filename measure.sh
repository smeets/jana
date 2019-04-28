#!/bin/bash
# usage: pdiff.sh exp-name

GATEWAY=$(ip route show | grep default | sed -n 's/[^0-9]\+\([0-9\.]\+\).*/\1/p')
LOCAL=$(ip route get $GATEWAY | sed -n 's/.*src \([0-9\.]\+\)/\1/p')

XYZ=$1
JANALOG_CSV="logfile-$XYZ.csv"
CAPTURE_PCAP="capture-$XYZ.pcap"
CAPTURE_CSV="$(basename "$CAPTURE_PCAP" .pcap).csv"
FINAL_CSV="data-$XYZ.csv"

echo "$LOCAL @ $GATEWAY"
echo "jana     > $JANALOG_CSV"
echo "tshark   > $CAPTURE_PCAP"
echo "pcap2csv > $CAPTURE_CSV"
echo "final    > $FINAL_CSV"

tshark -F pcap -w $CAPTURE_PCAP -f "udp"  2>/dev/null &
TPID=$(pgrep tshark)
# src/jana -c $GATEWAY -f $JANALOG_CSV

sleep 10s

while [ 1 ] 
do    
    if kill $TPID > /dev/null 2>&1
    then
        sleep 1
    else
        break
    fi
done

./src/pcap2csv $CAPTURE_PCAP -c $LOCAL -s $GATEWAY > $CAPTURE_CSV
./src/calc $CAPTURE_CSV $JANALOG_CSV > $FINAL_CSV
