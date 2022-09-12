![Banner](https://s-christy.com/status-banner-service/terminal-calendar/banner-slim.svg)

## Overview

This is a personal organization tool that can be used to keep track of events,
goals, and meetings. Here is a screenshot that illustrates what the program
looks like:

<p align="center">
  <img alt="Screenshot" src="./sample_annotated.png">
<p>

The left pane consists of a calendar that can be navigated using the h, j, k,
and l keys. You can press "Enter" to edit the data associated with that day
using a text editor.

## Save File Format

The save file is located at `~/.terminal_calendar.json` by default. It is a JSON
file. A (very) small save file may look something like this:

```json
{
  "weekdays":  {
    "Sat":  {
      "data":  "Test\nData\n"
    }
  },
  "days":  {
    "2022-09-10":  {
      "data":  "o This is\n+ Test data\n",
      "mask":  4
    }
  }
}
```

The `mask` value is a bitmask of the user's "checking off" of the recurring
events for that day. The rest of this format should be self-explanatory.

## Text Editor Configuration

In order to edit calendar data, you need to have a text editor set up. By
default, the program will use Vim, but if you have your `$EDITOR` variable set
then that will be used instead, or you can specify an editor with the `-e` or
`--editor` options.

The program uses the `system` library function to call the program you specified
with a single filename argument.

## Print Command

Pressing the "p" key will run the `print.sh` script or whatever command you
specified with the `--command` option. This can be useful for a number of
use-cases, but it is primarily intended to be used to publish calendar data to a
website or server for online access and backups. I use something similar to the
following command so that I can see my calendar on a webpage even when I'm away
from my computer:

```bash
#!/bin/bash

sed '1s/^/let data=/' data.json > public/data.js && \
  scp public/data.js public/index.html public/script.js \
  user@example.com:~/calendar/my-calendar
```

## Searching

You can search for terms using the '/' and '\' keys. The former matches strings
in the daily data, and the latter does the same but case insensitively. As you
are typing the string to search for you will see your search term populated on
the status-line in the bottom left of the screen. Days that match the string you
are typing will be highlighted with a yellow background. Clear your search by
searching for the empty string (i.e., press '/' then press 'Enter'. You can also
exit the search mode by pressing 'Backspace' until the search string is cleared.

## Key Bindings

This is a comprehensive list of the default key bindings for this program:

| Key              | Action                                            |
|------------------|---------------------------------------------------|
| h, j, k, l       | Move the cursor left, down, up, or right.         |
| i, Space, Return | Edit the day under the cursor.                    |
| s                | Save the data to the `calendar.json` file.        |
| 1-9              | Toggle the indicators next to recurring tasks.    |
| q                | Quit.                                             |
| p                | 'Print' the calendar using the print script.      |
| 0                | Move the cursor to the current day.               |
| d                | Delete the data for the day under the cursor.     |
| r                | Edit the recurring task for that day of the week. |
| e                | Cycles views in the calendar pane.                |
| /                | Search for a string in day data using regex.      |
| \                | Same as '/', but is case insensitive.             |
| Cursor keys      | Scroll the calendar.                              |

## Usage

The terminal calendar can be invoked as described in the usage statement:

```
Usage: terminal_calendar [options]
 -c,--command   The command to be run when "printing" (default `./print.sh`).
 -e,--editor    The command representing the text editor to use (default vim).
 -f,--file      Calendar file to use. Default "calendar.json".
 -h,--help      Print this usage message.
 -l,--log-file  The name of the log file to be used.
 -n,--no-clear  Do not clear the screen on shutdown.
 -o,--lock-file The name of the lock file to be used (default /tmp/termcal.lock).
 -v,--verbose   Display additional logging information.
```

Users can also configure the editor by setting the `EDITOR` environment
variable.

## Dependencies

These are the dependencies for terminal-calendar:

```
gcc
libcjson-dev
libncurses-dev
make
some editor (default vim)
```

## License

This work is licensed under the GNU General Public License version 3 (GPLv3).

[<img src="https://s-christy.com/status-banner-service/GPLv3_Logo.svg" width="150" />](https://www.gnu.org/licenses/gpl-3.0.en.html)
