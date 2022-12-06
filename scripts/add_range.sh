#!/bin/bash

i=1

while read line; do
  terminal_calendar --cli append $(date -Idate -d "$i day") "$line"
  i=$((i+1))
done < input.txt
