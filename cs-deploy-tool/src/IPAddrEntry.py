#
#  Copyright Red Hat, Inc. 2005
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 2, or (at your option) any
#  later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to the
#  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
#  MA 02139, USA.

# Author: Jim Parsons <jparsons@redhat.com>
#


import gtk


class IP (gtk.HBox):
  def __init__ (self):
    gtk.HBox.__init__ (self)
    self.set_spacing (3)

    self.e1 = gtk.Entry ()
    self.e1.set_property ("xalign", 1.0)
    self.e1.set_property ("width-chars", 3)
    self.e1.set_property ("max_length", 3)
    self.pack_start (self.e1, False, False, 0)
    self.pack_start (gtk.Label ('.'), False, False, 0)

    self.e2 = gtk.Entry ()
    self.e2.set_property ("xalign", 1.0)
    self.e2.set_property ("width-chars", 3)
    self.e2.set_property ("max_length", 3)
    self.pack_start (self.e2, False, False, 0)
    self.pack_start (gtk.Label ('.'), False, False, 0)

    self.e3 = gtk.Entry ()
    self.e3.set_property ("xalign", 1.0)
    self.e3.set_property ("width-chars", 3)
    self.e3.set_property ("max_length", 3)
    self.pack_start (self.e3, False, False, 0)
    self.pack_start (gtk.Label ('.'), False, False, 0)

    self.e4 = gtk.Entry ()
    self.e4.set_property ("xalign", 1.0)
    self.e4.set_property ("width-chars", 3)
    self.e4.set_property ("max_length", 3)
    self.pack_start (self.e4, False, False, 0)
    
    self.e1.connect('key-press-event', self.key_press)
    self.e2.connect('key-press-event', self.key_press)
    self.e3.connect('key-press-event', self.key_press)
    self.e4.connect('key-press-event', self.key_press)

  def key_press(self, obj, event, *args):
    change_focus = False
    stop_event = True
    
    ch = event.string
    if ch in '0123456789':
      stop_event = False
    elif ch == '.':
      change_focus = True
    
    if change_focus:
      if obj == self.e1:
        self.e2.grab_focus()
      elif obj == self.e2:
        self.e3.grab_focus()
      elif obj == self.e3:
        self.e4.grab_focus()
    return stop_event
      
    
  def clear(self):
    self.e1.set_text("")
    self.e2.set_text("")
    self.e3.set_text("")
    self.e4.set_text("")

  def getAddrAsString(self):
    rtval = self.e1.get_text().strip() + "." + \
            self.e2.get_text().strip() + "." + \
            self.e3.get_text().strip() + "." + \
            self.e4.get_text().strip()

    return rtval

  def getAddrAsList(self):
    rtlist = list()
    rtlist.append(self.e1.get_text().strip())
    rtlist.append(self.e2.get_text().strip())
    rtlist.append(self.e3.get_text().strip())
    rtlist.append(self.e4.get_text().strip())

    return rtlist

  def setAddrFromString(self, addr):
    octets = addr.split(".")
    self.e1.set_text(octets[0])
    self.e2.set_text(octets[1])
    self.e3.set_text(octets[2])
    self.e4.set_text(octets[3])

  def setAddrFromList(self, addr):
    pass
  
  def isValid(self):
    for num_str in self.getAddrAsList():
      try:
        num = int(num_str)
        if num > 255 or num < 0:
          return False
      except:
        return False
    return True
