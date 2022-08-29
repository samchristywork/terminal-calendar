#!/bin/bash

# This is an example. You should modify it to work with your own setup.

sed '1s/^/let data=/' data.json > public/data.js && \
  scp public/data.js public/index.html public/script.js \
  user@example.com:~/calendar/my-calendar
