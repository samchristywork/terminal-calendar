#!/bin/bash

usage (){
  echo "Usage: $(basename $0) filename"
  echo
  echo "The specified file should have one item for each line. Each item will be added"
  echo "to the calendar on a new day starting the day after the script is run. Thus, if"
  echo "the script is run on 2022-08-05, then the first line of the file will be added"
  echo "to 2022-08-06, the second line to 2022-08-07, etc..."
  exit 1
}

[ "$#" -eq "1" ] || usage

i=1

while read line; do
  terminal_calendar --cli append $(date -Idate -d "$i day") "$line"
  i=$((i+1))
done < $1
