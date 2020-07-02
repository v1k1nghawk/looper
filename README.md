# looper
* _Purpose:_ A switching (bridge) loop detection by a broadcast storm.
* _Usage:_ **looper** [[**-s**|**--silent** [deadline]] | [**-v**|**--verbose**] | [**-h**|**--help**]]

    **deadline** is a timeout, in seconds, before looper exits (in silent mode).

* _Tip for UNIX-like OS:_ to test specific network interfaces, the corresponding IP addresses + the correct hostname (from /etc/hostname) must be present in the /etc/hosts file.
