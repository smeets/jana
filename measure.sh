#!/bin/bash
# usage: pdiff.sh exp-name

GATEWAY=$(ip route show | grep default | sed -n 's/[^0-9]\+\([0-9\.]\+\).*/\1/p')
LOCAL=$(ip route get $GATEWAY | sed -n 's/.*src \([0-9\.]\+\)/\1/p')

XYZ=$1

rm -rf ./data/$XYZ
mkdir -p ./data/$XYZ

JANALOG_CSV="$(pwd)/data/$XYZ/logfile.csv"
CAPTURE_PCAP="$(pwd)/data/$XYZ/capture.pcap"
CAPTURE_CSV="$(pwd)/data/$XYZ/capture.csv"
FINAL_CSV="$(pwd)/data/$XYZ/result.csv"

echo "$LOCAL @ $GATEWAY"
echo "jana     > $JANALOG_CSV"
echo "tshark   > $CAPTURE_PCAP"
echo "pcap2csv > $CAPTURE_CSV"
echo "final    > $FINAL_CSV"

tshark -F pcap -w $CAPTURE_PCAP -f "udp"  2>/dev/null &
TPID=$(pgrep tshark)

sleep 5s

./src/jana -c $GATEWAY -f $JANALOG_CSV -d uniform n=0,k=1020

sleep 10s
kill $TPID > /dev/null 2>&1

while [ 1 ]
do
    if ps -p $TPID
    then
        sleep 1
    else
        break
    fi
done

sleep 10s

./src/pcap2csv $CAPTURE_PCAP -c $LOCAL -s $GATEWAY > $CAPTURE_CSV
./src/calc $CAPTURE_CSV $JANALOG_CSV > $FINAL_CSV
