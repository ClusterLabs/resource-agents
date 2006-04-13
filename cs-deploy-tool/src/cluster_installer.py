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

# Author: Stanko Kupcevic <kupcevic@redhat.com>
#


import sys
import os
import time

try:
    import gtk
    import gtk.glade
except RuntimeError, e:
    print 'Unable to initialize graphical environment.'
    print 'Most likely cause of failure is that the tool was not run using a graphical environment.'
    print 'Please either start your graphical user interface or set your DISPLAY variable.'
    
    sys.exit(-1)


from constants import *
sys.path.append(INSTALLDIR)
if os.access(USER_MODULES_PATH, os.F_OK):
    sys.path.append(USER_MODULES_PATH)


from environment import Environment
from rpm_installer import RPMInstaller, YUM_Interface, UP2DATE_Interface
from main_win import MainWin



def infoMessage(message):
    dlg = gtk.MessageDialog(None, 0,
                            gtk.MESSAGE_INFO, gtk.BUTTONS_OK,
                            message)
    dlg.show_all()
    rc = dlg.run()
    dlg.destroy()
    return rc

def __get_ads_classes(module):
    from ads_base import ADSBase
    from module_parser import ModuleParser
    mp = ModuleParser()
    mp.import_all_into_module(module)
    ads_classes = []
    for module in mp.get_modules(module):
        ads = None
        for c in mp.get_classes(module):
            if mp.share_functions(ADSBase, c) and c != ADSBase:
                ads = c
        if ads != None:
            ads_classes.append(ads)
    return ads_classes
def get_ads_classes():
    # user's ADSs
    ads_classes = []
    user_ADSs_path = USER_MODULES_PATH + '/user_ADSs'
    if os.access(user_ADSs_path, os.F_OK):
        files = os.listdir(user_ADSs_path)
        if len(files) > 0:
            if '__init__.py' not in files:
                open(user_ADSs_path + '/__init__.py', 'w').write('\n')
            import user_ADSs
            ads_classes += __get_ads_classes(user_ADSs)
    # system's ADSs
    import ADSs
    ads_classes += __get_ads_classes(ADSs)
    return ads_classes



if os.access('/proc/cluster', os.F_OK):
    infoMessage('This PC is a node of existing cluster.\nRun system-config-cluster to configure it.')
else:
    
    cluster_name = str(time.time()).replace('.', '')[:15]
    environ = Environment('cluster_' + cluster_name + '_deployment_log')
    
    rpm_installer = None
    fc5 = False
    if '--fc5' in sys.argv:
        rpm_installer = RPMInstaller(environ, YUM_Interface)
        fc5 = True
    else:
        rpm_installer = RPMInstaller(environ, UP2DATE_Interface)
    ads_classes = get_ads_classes()
    
    window = MainWin(cluster_name,
                     environ, 
                     rpm_installer,
                     ads_classes,
                     fc5)
    
    gtk.main()
