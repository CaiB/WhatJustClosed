# WhatJustClosed

Using the Win32 API, adds a hook to be notified of all closing windows, and outputs information about each onto the console while running. Initially created to figure out what a rapidly appearing and disappearing window was. Tracking by process creation didn't lead to good results, because processes can open and close windows at will, hence this actually looks at windows closing.

A 64-bit compiled version can only detect windows closing of 64-bit programs, and vice versa for 32-bit.

The following command-line switches are available:

| Switch | Description |
|---|---|
| `/32` | Use when running the 64-bit version to allow listening to both 64- and 32-bit processes. |
| `/inv` | Output information about windows closing that are not visible at the time of closing. Some programs hide windows before technically closing them, this shows those (and many others). |

Precompiled binaries can be found in the [releases](https://github.com/CaiB/WhatJustClosed/releases).

I make no guarantees that this program won't cause your computer to spontaneously combust. It may also trigger some antivirus or anticheat software, as it requires injecting a helper DLL into other processes to intercept window close messages.

<img alt="WhatJustClosed Demo" src="wjcdemo.gif">