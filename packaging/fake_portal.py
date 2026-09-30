#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for the XDG desktop portal's Settings interface, for
LibreMerge's --selftest-portal: it answers color-scheme = prefer dark and,
a second after the first question, switches to prefer light and says so
with SettingChanged, the way GNOME does when the user flips its style.
Once it owns the portal's name it runs the command after "--" and exits
with its status:

    dbus-run-session -- python3 fake_portal.py -- libremerge --selftest-portal

--legacy leaves out ReadOne and answers only the deprecated Read, whose
value comes wrapped in two variants (portals older than 1.15).
Needs python3-dbus and python3-gi.
"""
import subprocess
import sys

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

INTERFACE = "org.freedesktop.portal.Settings"
NAMESPACE = "org.freedesktop.appearance"
KEY = "color-scheme"
PREFER_DARK, PREFER_LIGHT = 1, 2
OPTIONS = sys.argv[1:sys.argv.index("--")] if "--" in sys.argv else sys.argv[1:]
COMMAND = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
LEGACY = "--legacy" in OPTIONS


class Settings(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, "/org/freedesktop/portal/desktop")
        self.scheme = PREFER_DARK
        self.flip_scheduled = False

    def answer(self, namespace, key, wrapping):
        if (namespace, key) != (NAMESPACE, KEY):
            raise dbus.exceptions.DBusException(
                "no such setting", name="org.freedesktop.portal.Error.NotFound")
        if not self.flip_scheduled:
            self.flip_scheduled = True
            GLib.timeout_add(1000, self.flip)
        return dbus.UInt32(self.scheme, variant_level=wrapping)

    if not LEGACY:
        @dbus.service.method(INTERFACE, in_signature="ss", out_signature="v")
        def ReadOne(self, namespace, key):
            return self.answer(namespace, key, 1)

    @dbus.service.method(INTERFACE, in_signature="ss", out_signature="v")
    def Read(self, namespace, key):
        return self.answer(namespace, key, 2)

    @dbus.service.signal(INTERFACE, signature="ssv")
    def SettingChanged(self, namespace, key, value):
        pass

    def flip(self):
        self.scheme = PREFER_LIGHT
        self.SettingChanged(NAMESPACE, KEY, dbus.UInt32(PREFER_LIGHT, variant_level=1))
        return False


DBusGMainLoop(set_as_default=True)
session = dbus.SessionBus()
settings = Settings(session)
# claim the name last, so a caller never finds it without the object
name = dbus.service.BusName("org.freedesktop.portal.Desktop", session)
loop = GLib.MainLoop()
status = 0
if COMMAND:
    child = subprocess.Popen(COMMAND)

    def reap():
        global status
        if child.poll() is None:
            return True
        status = child.returncode
        loop.quit()
        return False

    GLib.timeout_add(100, reap)
else:
    print("fake portal ready", flush=True)
loop.run()
sys.exit(status)
