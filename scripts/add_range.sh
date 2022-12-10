#!/bin/bash

usage (){
  echo "Usage: $(basename $0) filename [increment]"
  echo
  echo "The specified file should have one item for each line. Each item will be added"
  echo "to the calendar on a new day starting the day after the script is run. Thus, if"
  echo "the script is run on 2022-08-05, then the first line of the file will be added"
  echo "to 2022-08-06, the second line to 2022-08-07, etc..."
  echo
  echo "The optional increment value is specified after the filename which is"
  echo "mandatory. The value defaults to 1. Any value other than a number will be"
  echo "counted as zero. A value of 2 will skip 1 day for every item added to the"
  echo "calendar, a value of 3 will skip 2 days, etc..."
  exit 1
}

increment=1

[ "$#" -eq "1" ] || { [ "$#" -eq "2" ] && { increment=$2; } } || usage

i=1

while read line; do
  terminal_calendar --cli append $(date -Idate -d "$i day") "$line"
  i=$((i+increment))
done < $1
