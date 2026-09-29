#!/usr/bin/env python3
"""Test app for the remote text context live test (spare X server only).

A window with a button, a text entry, a password entry and a multi-line view. It writes each
widget's root-window rectangle to $TC_DIR/geometry.json, then follows commands written to
$TC_DIR/cmd ("entry", "password", "view", "button", "quit"), moving keyboard focus itself. Nothing
is typed and no input is sent anywhere."""
import json, os, sys
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib

D = os.environ["TC_DIR"]
win = Gtk.Window(title="Nova text field test")
win.set_default_size(700, 400)
win.move(120, 90)
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin=20)
button = Gtk.Button(label="A button")
entry = Gtk.Entry(); entry.set_text("hello")
password = Gtk.Entry(); password.set_visibility(False); password.set_text("secret")
view = Gtk.TextView(); view.set_size_request(400, 120)
for w in (button, entry, password, view):
    box.pack_start(w, False, False, 0)
win.add(box)
win.connect("destroy", Gtk.main_quit)
win.show_all()
widgets = {"button": button, "entry": entry, "password": password, "view": view}
last = {"cmd": None}

def rect(w):
    gw = Gtk.Widget.get_window(w)
    ox, oy = gw.get_origin()[1:]
    a = w.get_allocation()
    # Allocation is relative to the parent GdkWindow, which get_origin already covers.
    return [ox + a.x, oy + a.y, a.width, a.height]

def geometry():
    json.dump({k: rect(w) for k, w in widgets.items()}, open(os.path.join(D, "geometry.json.tmp"), "w"))
    os.replace(os.path.join(D, "geometry.json.tmp"), os.path.join(D, "geometry.json"))
    return False

def poll():
    p = os.path.join(D, "cmd")
    try:
        cmd = open(p).read().strip()
    except OSError:
        return True
    if cmd and cmd != last["cmd"]:
        last["cmd"] = cmd
        name = cmd.split(":")[0]
        if name == "quit":
            Gtk.main_quit(); return False
        if name in widgets:
            win.present()
            widgets[name].grab_focus()
            print("focus", name, flush=True)
    return True

button.grab_focus()
GLib.timeout_add(700, geometry)
GLib.timeout_add(50, poll)
Gtk.main()
