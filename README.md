![Banner](https://s-christy.com/status-banner-service/terminal-calendar/banner-slim.svg)

## Overview

This is a personal organization tool that can be used to keep track of events,
goals, and meetings. Here is a screenshot that illustrates what the program
looks like:

<p align="center">
  <img alt="Screenshot" src="./sample_annotated.png">
<p>

| ID | Name                    | Description                                                                                               |
|----|-------------------------|-----------------------------------------------------------------------------------------------------------|
| 1  | File modified indicator | The symbol `(*)` indicates that data has been modified and needs to be saved to disk with the 's' key.    |
| 2  | Calendar pane           | The left pane of the application shows the days of the year where each week is a line.                    |
| 3  | Currently selected day  | This is what the cursor looks like.                                                                       |
| 4  | Day pane                | This pane shows the data associated with the current day. Press "Enter" to edit this data.                |
| 5  | Week pane               | This pane shows the data that recurs weekly. Press the number keys to interact, and "r" to edit the text. |
| 6  | Completion breakdown    | This indicates how many tasks are marked as complete, in progress, failed, and deferred.                  |
| 7  | Status line             | Messages will be shown here for some operations like saving the file.                                     |
| 8  | Help                    | This text indicates to the user that they can get help for the application by typing '?'.                 |

## Calendar Pane

The left pane consists of a calendar that can be navigated using the h, j, k,
and l keys. You can press "Enter" to edit the data associated with that day
using a text editor. The currently selected day is marked in black text with a
white background, and days with data associated with them are in bold.

## File Modification Indicator

When a change has been made to the data associated with your calendar, the `(*)`
indicator will appear in the top left corner of the window. In this state, the
data has been changed but has not yet been written to disk. You can write the
data to disk using 's', or you can discard the changes with 'ctrl-c'.

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

```bash
#!/bin/bash

sed '1s/^/let data=/' data.json > public/data.js && \
  scp public/data.js public/index.html public/script.js \
  user@example.com:~/calendar/my-calendar
```

## Color Coding

This program has a very primitive form of syntax highlighting to enhance
productivity. The current scheme is to highlight text according to this table:

| First Character | Color  | Meaning     |
|-----------------|--------|-------------|
| +               | Green  | Done        |
| o               | Yellow | In progress |
| -               | Red    | Failed      |
| x               | Blue   | Deferred    |

There is a Vim syntax file that is included in this repository that I use to
keep the color coding in my editor. To use it, move it to `~/.vim/syntax/` or
`~/.config/nvim/syntax/` if you have `neovim` instead.

## Backlog

Press 'b' to edit the backlog. This is for data that does not have a date
associated with it. Data in the backlog will not be counted in the
green/yellow/red/blue counters in the top right of the screen. If there is data
in the backlog it will show up in magenta at the top of the screen.

## Status Line

The status line provides messages to the user, which range from confirmations
for some operations like saving to warnings for dangerous operations. The status
line is located in the bottom left of the screen.

## Searching

You can search for terms using the '/' and '\' keys. The former matches strings
in the daily data, and the latter does the same but case insensitively. As you
are typing the string to search for you will see your search term populated on
the status-line in the bottom left of the screen. Days that match the string you
are typing will be highlighted with a yellow background. Clear your search by
searching for the empty string (i.e., press '/' then press 'Enter'. You can also
exit the search mode by pressing 'Backspace' until the search string is cleared.

## View Modes

The user can toggle the way the calendar on the left pane is rendered with the
'e' key. There are currently three view modes:

| ID | Mode     | Description                                                                           |
|----|----------|---------------------------------------------------------------------------------------|
| 1  | Standard | This is the typical mode that arranges the days in week groups starting each Sunday.  |
| 2  | Count    | This mode shows the event counts per day instead of the day of the month.             |
| 3  | Month    | This mode shows the month index of the day where January is "0" and December is "11". |

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
| b                | Edit the backlog.                                 |
| r                | Edit the recurring task for that day of the week. |
| e                | Cycles views in the calendar pane.                |
| /                | Search for a string in day data using regex.      |
| \                | Same as '/', but is case insensitive.             |
| Cursor keys      | Scroll the calendar.                              |

## Text Editor

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

## Usage

The terminal calendar can be invoked as described in the usage statement:

```
Usage: terminal_calendar [options]
 -b,--num_backups The number of backup files to keep (default 10).
 -c,--command     The command to be run when "printing" (default `./print.sh`).
 -d,--backup_dir  The directory to store backup files in (default ~/.terminal_calendar_backup/).
 -e,--editor      The command representing the text editor to use (default vim).
 -f,--file        Calendar file to use. Default "calendar.json".
 -h,--help        Print this usage message.
 -l,--log-file    The name of the log file to be used.
 -n,--no-clear    Do not clear the screen on shutdown.
 -o,--lock-file   The name of the lock file to be used (default /tmp/termcal.lock).
 -v,--verbose     Display additional logging information.
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
