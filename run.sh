#!/bin/bash
tmux has-session -t jana
if [[ $? -eq 1 ]]; then
	echo "starting jana session"
	tmux new-session -s jana -n def -d
	tmux split-window -t jana
fi

make jana
if [ $? -eq 0 ]; then
	tmux send-keys -t jana:def.0 C-c ENTER "src/jana -s 1" ENTER
	tmux send-keys -t jana:def.1 C-c ENTER "src/jana -c 127.0.0.1" ENTER
else
	echo "compilation errors"
fi
#tmux send-keys -t jana:build "make jana" ENTER
#tmux send-keys -t jana:server C-c ENTER "src/jana -s 1" ENTER
#tmux send-keys -t jana:client C-c ENTER "src/jana -c 127.0.0.1" ENTER
