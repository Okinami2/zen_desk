#!/bin/sh
cat << 'DATA' | awk '
    /Name="ZenDesk_EC11_Knob"/ { found=1; next }
    found && /Handlers=/ {
        match($0, /event[0-9]+/);
        if (RSTART > 0) {
            last_event = substr($0, RSTART, RLENGTH);
        }
        found=0;
    }
    END { print last_event }
'
I: Bus=0003 Vendor=0000 Product=0000 Version=0000
N: Name="ZenDesk_EC11_Knob"
P: Phys=
S: Sysfs=/devices/virtual/input/input4
U: Uniq=
H: Handlers=kbd event4 
B: PROP=0

I: Bus=0003 Vendor=0000 Product=0000 Version=0000
N: Name="ZenDesk_EC11_Knob"
P: Phys=
S: Sysfs=/devices/virtual/input/input5
U: Uniq=
H: Handlers=kbd event5 
B: PROP=0
DATA
