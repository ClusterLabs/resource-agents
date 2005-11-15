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


import gtk
import copy

import err

from storage_config import StorageConfig


class StorageInstance(StorageConfig):
    
    def __init__(self, storage, vg_name):
        StorageConfig.__init__(self)
        
        self.storage = storage
        
        self.__size_label = gtk.Label()
        self.__size_entry = gtk.Entry()
        self.__size_entry.set_text('0')
        self.__size_entry.set_width_chars(8)
        self.__mountpoint_label = gtk.Label()
        self.__mountpoint_entry = gtk.Entry()
        self.__mountpoint_entry.set_width_chars(20)
        
        self.__tooltip = gtk.Tooltips()
        
        self.set_size_label_text('Storage size (GB)')
        self.set_mountpoint_label_text('Mountpoint')
        size_tooltip = 'Size, in GB, that is to be carved out from shared storage.\n'
        size_tooltip += 'Leave enough room for GFS journals (number of nodes * 128 MB)'
        self.set_size_tooltip(size_tooltip)
        mountpoint_tooltip = 'Where to mount carved storage on nodes (mountpoint will be created on all nodes).'
        self.set_mountpoint_tooltip(mountpoint_tooltip)
        
        self.__size_entry.connect('key-press-event', self.size_key_press)
        self.__size_entry.connect('changed', self.size_changed)
        self.__mountpoint_entry.connect('key-press-event', self.mountpoint_key_press)
        self.__mountpoint_entry.connect('changed', self.mountpoint_changed)
        
        hbox = gtk.HBox(False, 3)
        hbox.pack_start(self.__size_label, False)
        hbox.pack_start(self.__size_entry, False)
        hbox.pack_start(self.__mountpoint_label, False)
        hbox.pack_start(self.__mountpoint_entry, False)
        self.__config_widget = hbox
        
        self.set_name('')
        self.__vgname = vg_name
        
        self.register_mountpoint_changed(None)
        self.set_enabled(True)
        
    
    def size_key_press(self, obj, event, *args):
        stop_event = True
        ch = event.string
        if ch == '.':
            if self.__size_entry.get_text().find('.') == -1:
                stop_event = False
        if ch in '0123456789':
            stop_event = False
        return stop_event
    def size_changed(self, *args):
        text = self.__size_entry.get_text()
        idx = text.find('.')
        if idx != -1:
            if len(text) > idx+2:
                self.__size_entry.set_text(text[:idx+2])
        
        size_free = self.storage.get_size_free()
        if size_free < 0:
            size = self.get_size() + size_free
            size_GB = size /1024.0/1024/1024
            size_str = '%.2f' % size_GB
            size_str = size_str[:len(size_str)-1]
            msg = 'Size entered is larger than free storage.\n'
            msg += 'It will be reduced to ' + str(size_str) + ' GB.'
            infoMessage(msg)
            self.__size_entry.set_text(size_str)
        self.storage.update_status()
    
    def register_mountpoint_changed(self, callback):
        self.__callback = callback
    def mountpoint_key_press(self, obj, event, *args):
        stop_event = False
        ch = event.string
        if ch in ' ` ! @ # $ % ^ & * ( ) | \\ { } [ ] \' " : ; ? < >' and ch != '':
            stop_event = True
        if len(self.get_mountpoint()) == 0 and ch != '/' and ch != '':
            stop_event = True
            msg = 'Mountpoint has to be an absolute path (it begins with \'/\')'
            infoMessage(msg)
        return stop_event
    def mountpoint_changed(self, *args):
        if self.__callback != None:
            s = self.get_mountpoint()
            self.__callback(s)
    
    def validate(self):
        if not self.get_enabled():
            return
        
        # validate name
        name = self.get_name()
        if name == '':
            msg = 'Internal: missing clusterfs name'
            raise err.Err(msg)
        for ch in name:
            if ch in ' ` ! @ # $ % ^ & * ( ) | \\ { } [ ] \' " : ; ? < >' and ch != '':
                msg = 'Internal: invalid clusterfs name'
                raise err.Err(msg)
        
        # validate mount point
        mnt_pt = self.get_mountpoint()
        if mnt_pt == '':
            msg = 'You need to specify a mount point'
            raise err.Err(msg)
        if mnt_pt[0] != '/':
            msg = 'Mount point has to be an absolute path'
            raise err.Err(msg)
        for ch in mnt_pt:
            if ch in ' ; ! | \' " ` $ &':
                msg = 'Mount point contains illegal characters'
                raise err.Err(msg)
        
        # validate size
        journal_size = 128 * 1024*1024 # 128 MB
        min_size = journal_size * len(self.storage.get_nodes())
        if self.get_size() <= min_size:
            msg = 'Journals cannot fit into storage. Minimum size required for journals is ' + str(min_size/1024/1024) + 'MB.'
            raise err.Err(msg)
    
    def get_enabled(self):
        return self.get_config_widget().get_property('sensitive')
    def set_enabled(self, bool):
        self.get_config_widget().set_sensitive(bool)
        self.size_changed()
        
    
    def get_devpath(self):
        return '/dev/' + self.__vgname + '/' + self.__name
    
    # get size in bytes
    def get_size(self):
        if not self.get_enabled():
            return 0
        t = self.__size_entry.get_text()
        if t == '' or t == '.':
            return 0
        size_GB = float(t)
        return int(size_GB * 1024 * 1024 * 1024)
    
    def get_mountpoint(self):
        if not self.get_enabled():
            return ''
        
        return self.__mountpoint_entry.get_text()
    
    def set_name(self, name):
        self.__name = name
    
    def get_name(self):
        return self.__name
    
    def get_config_widget(self):
        return self.__config_widget
    
    def generate_xml_local(self):
        if not self.get_enabled():
            return ('', '')
        return ('<clusterfs ref="%s">' % (self.__name), '</clusterfs>')
    
    def generate_xml(self):
        if not self.get_enabled():
            return ''
        
        template = '<clusterfs name="%s" '
        template += 'device="%s" force_unmount="0" fstype="gfs" '
        template += 'mountpoint="%s" options=""/>'
        
        dev = self.get_devpath()
        mountpoint = self.get_mountpoint()
        
        return template % (self.__name, dev, mountpoint)
    
    # cosmetics
    def set_size_label_text(self, text):
        self.__size_label.set_text(text)
    def set_mountpoint_label_text(self, text):
        self.__mountpoint_label.set_text(text)
    
    def get_size_tooltip(self):
        data = gtk.tooltips_data_get(self.__size_entry)
        if data == None:
            return ''
        else:
            return data[2]
    def set_size_tooltip(self, text):
        self.__tooltip.set_tip(self.__size_entry, text)
    
    def get_mountpoint_tooltip(self):
        data = gtk.tooltips_data_get(self.__mountpoint_entry)
        if data == None:
            return ''
        else:
            return data[2]
    def set_mountpoint_tooltip(self, text):
        self.__tooltip.set_tip(self.__mountpoint_entry, text)
        
    

class Storage:
    
    def __init__(self, environ, cluster_name, glade_xml, label=None):
        
        self.environ = environ
        
        self.glade_xml = glade_xml
        
        # node -> storage mapping
        self.__node_storage = {} # {node : {scsi_id : {partnum : size, ...}, ...}, ...}
        self.__storage_to_be_used = {} # {scsi_id : {partnum : size, ...}, ... }
        self.__scsi_model = {}
        
        self.__storage_instances = []
        
        self.label = label
        self.update_status()
        
        self.__cluster_name = cluster_name
        self.__vg_name = 'cluster_' + cluster_name
        
        self.__node_selinux = {} # node-> selinux mapping
        
    
    def get_nodes(self):
        return self.__node_storage.keys()[:]
    
    def get_new_instance(self):
        inst = StorageInstance(self, self.__vg_name)
        self.__storage_instances.append(inst)
        return inst
    
    def update_status(self):
        status = 'Storage: '
        
        # total
        size = self.get_size_total()
        sizeGB = size / 1024.0/1024/1024
        size_str = '%.2f' % sizeGB
        size_str = size_str[:len(size_str)-1]
        status += 'total ' + size_str + ' GB, '
        
        # free
        size_free = self.get_size_free()
        sizeGB_free = size_free / 1024.0/1024/1024
        size_str = '%.2f' % sizeGB_free
        size_str = size_str[:len(size_str)-1]
        status += 'free ' + size_str + ' GB'
        
        if self.label != None:
            self.label.set_text(status)

    def get_size_total(self,):
        size = 0
        for storage in self.__storage_to_be_used.values():
            for partnum in storage:
                size += storage[partnum]
        return size
    def get_size_free(self):
        size_total = self.get_size_total()
        size_used = 0
        for inst in self.__storage_instances:
            size_used += inst.get_size()
        size_free = size_total - size_used
        return size_free
    
    def add_node(self, node):
        self.__disable_selinux(node)
        self.__node_storage[node] = self.__probe_storage(node)
        
        # find shared storage
        shared = self.__storage_intersection()
        
        
        if len(self.__node_storage.keys()) == 1:
            self.__scsi_model = self.__probe_storage_model()
        
        if len(self.__node_storage.keys()) > 2:
            for scsi_id in self.__storage_to_be_used:
                if scsi_id not in shared:
                    self.__node_storage.pop(node)
                    raise err.Err('Node ' + node + ' doesn\'t have a scsi device with unique id ' + str(scsi_id) + ', that is to be used for shared storage.')
        
    
    def prompt_storage(self):
        shared = self.__storage_intersection()
        
        table = gtk.Table(len(shared.keys()) + 1, 5)
        table.set_row_spacings(3)
        table.set_col_spacings(10)
        
        # header
        table.attach(gtk.Label(), 0, 1, 0, 1, gtk.FILL, 0)
        table.attach(gtk.Label('Vendor and Model'), 1, 2, 0, 1, gtk.FILL, 0)
        table.attach(gtk.Label('Partition number'), 2, 3, 0, 1, gtk.FILL, 0)
        table.attach(gtk.Label('Size (GB)'), 3, 4, 0, 1, gtk.FILL, 0)
        table.attach(gtk.Label('SCSI ID'), 4, 5, 0, 1, gtk.FILL, 0)
        
        # entries
        res_list = []
        row = 1
        for scsi_id in shared:
            partnum_size = shared[scsi_id]
            parts = partnum_size.keys()[:]
            parts.sort()
            for part in parts:
                if len(parts) != 1 and part == '0':
                    continue
                sz = partnum_size[part]
                sz = sz / 1024.0/1024/1024 # GB
                size_str = '%.2f' % sz
                size_str = size_str[:len(size_str)-1]
                if sz < 0.1:
                    print scsi_id + ', partition ' + str(part) + ' too small, skipping'
                else:
                    check = gtk.CheckButton()
                    model = gtk.Label(self.__scsi_model[scsi_id])
                    size = gtk.Label(size_str)
                    id = gtk.Label(str(scsi_id))
                    if part == '0':
                        partnum = gtk.Label('Whole Device')
                    else:
                        partnum = gtk.Label(str(part))
                    
                    table.attach(check, 0, 1, row, row+1, gtk.FILL, 0)
                    table.attach(model, 1, 2, row, row+1, gtk.FILL, 0)
                    table.attach(partnum, 2, 3, row, row+1, gtk.FILL, 0)
                    table.attach(size, 3, 4, row, row+1, gtk.FILL, 0)
                    table.attach(id, 4, 5, row, row+1, gtk.FILL, 0)
                    
                    objects = [model, size, id, partnum]
                    #for obj in objects:
                    #    obj.set_sensitive(False)
                    
                    check.connect('toggled', self.__toggled, scsi_id, part, objects, res_list)
                    
                    row += 1
                
        insert_here = self.glade_xml.get_widget('storage_configuration_viewport')
        insert_here.remove(insert_here.get_child())
        insert_here.add(table)
        insert_here.resize_children()
        insert_here.show_all()
        
        # display window
        win = self.glade_xml.get_widget('storage_configuration')
        while win.run() != gtk.RESPONSE_OK:
            pass
        win.hide()
        
        res_dir = {}
        for scsi_id in shared:
            for p in res_list:
                sid = p[0]
                part = p[1]
                if sid == scsi_id:
                    if scsi_id not in res_dir:
                        res_dir[scsi_id] = {}
                    res_dir[scsi_id][part] = shared[scsi_id][part]
        self.__storage_to_be_used = res_dir
        
        self.update_status()
        
    
    def get_warning_string(self):
        if len(self.__storage_to_be_used.keys()) == 0:
            return ''
        msg = 'Data on following SCSI devices will be destroyed:\n\n'
        for scsi_id in self.__storage_to_be_used:
            msg += self.__scsi_model[scsi_id] + ' (' + scsi_id + '): '
            parts = self.__storage_to_be_used[scsi_id]
            if len(parts) == 1 and parts.keys()[0] == '0':
                msg += 'whole device\n'
            else:
                for part in parts:
                    msg += 'partition ' + part + ', '
                msg = msg[:len(msg)-2] + '\n'
        msg += '\nAre you sure?'
        return msg
    
    def generate_xml(self):
        storage_str = ''
        for inst in self.__storage_instances:
            xml = inst.generate_xml()
            if xml != '':
                storage_str += '\t\t\t' + xml + '\n'
        
        return storage_str.rstrip()
    
    def validate(self):
        for inst in self.__storage_instances:
            inst.validate()
        self.__validate_names()
        self.__validate_mountpoints()
    def __validate_names(self):
        names = []
        for inst in self.__storage_instances:
            if not inst.get_enabled():
                continue
            name = inst.get_name()
            if name == '':
                msg = 'Storage has to have a name.'
                raise err.Err(msg)
            elif name in names:
                msg = 'There are two storages with the same name: ' + name + '.'
                msg += '\nThat is not allowed!'
                raise err.Err(msg)
            elif len(name) >= 16:
                msg = 'Storage name has to be less than 16 chars long'
                raise err.Err(msg)
            else:
                names.append(name)
    def __validate_mountpoints(self):
        mnts = []
        for inst in self.__storage_instances:
            if not inst.get_enabled():
                continue
            mnt = inst.get_mountpoint()
            if mnt in mnts:
                msg = 'There are two equal mountpoints: ' + mnt + '.'
                msg += '\nThat is not allowed'
                raise err.Err(msg)
            else:
                mnts.append(mnt)
        return
    
    
    def configure_nodes(self):
        for node2 in self.__node_storage:
            self.__disable_selinux(node2)
        
        ### set up cLVM locking type, and start clvmd on each node ###
        for node2 in self.__node_storage:
            # configure LVM locking type
            # automatically configured by lvm2-cluster.rpm
            
            self.environ.execute_remote(node2, 'chkconfig', ['clvmd', 'on'])
            self.environ.execute_remote(node2, 'service', ['clvmd', 'start'])
            pass
        
        if len(self.__node_storage.keys()) == 0:
            for node2 in self.__node_storage:
                self.__restore_selinux(node2)
            return True
        
        # storage configuration is done on one node
        node = self.__node_storage.keys()[0]
        
        ### create pvs ###
        pvs = []
        scsi_ids = self.__get_scsi_ids(node)
        for scsi_id in self.__storage_to_be_used:
            # TODO: fix for multipathing
            devpath = scsi_ids[scsi_id][0]
            parts_dir = self.__storage_to_be_used[scsi_id]
            for partnum in parts_dir:
                if partnum == '0':
                    partpath = devpath
                else:
                    partpath = devpath + str(partnum)
                pvs.append(partpath)
        self.environ.execute_remote(node, 'pvcreate', pvs)
        
        ### create VG ###
        args = ['-c', 'y', self.__vg_name]
        for pv in pvs:
            args.append(pv)
        self.environ.execute_remote(node, 'vgcreate', args)
        
        ### carve LVs ###
        for inst in self.__storage_instances:
            if not inst.get_enabled():
                continue
            name = inst.get_name()
            size = inst.get_size()
            size_mb = size/1024/1024
            args = ['-L', str(size_mb)+'M', '-n', name, self.__vg_name]
            self.environ.execute_remote(node, 'lvcreate', args)
            
        
        ### format ###
        for inst in self.__storage_instances:
            if not inst.get_enabled():
                continue
            devpath = inst.get_devpath()
            gfs_table = inst.get_name()
            self.environ.log('Making gfs on dev ' + devpath)
            
            args = ['-t', self.__cluster_name+':'+gfs_table, '-p', 'lock_dlm', '-j', str(len(self.__node_storage.keys())), '-O', devpath]
            self.environ.execute_remote(node, 'gfs_mkfs', args)
        
        # mount storage on all nodes
        for node2 in self.__node_storage:
            for inst in self.__storage_instances:
                if inst.get_enabled():
                    devpath = inst.get_devpath()
                    mntpoint = inst.get_mountpoint()
                    self.environ.execute_remote(node2, 'mkdir', ['-p', mntpoint])
                    o, e, s = self.environ.execute_remote(node2, 'mount', ['-t', 'gfs', devpath, mntpoint])
                    if s != 0:
                        for node2 in self.__node_storage:
                            self.__restore_selinux(node2)
                        return False
        
        ### DONE ###
        for node2 in self.__node_storage:
            self.__restore_selinux(node2)
        return True
    
    
    
    
    ### internal ###
    
    def __toggled(self, butt, scsi_id, partnum, objects, res_list):
        if butt.get_active():
            #for obj in objects:
            #    obj.set_sensitive(True)
            res_list.append((scsi_id, partnum))
        else:
            #for obj in objects:
            #    obj.set_sensitive(False)
            for p in res_list[:]:
                if p[0] == scsi_id and p[1] == partnum:
                    res_list.remove(p)
        pass
    
    def __probe_storage_model(self):
        node = self.__node_storage.keys()[0]
        
        scsi_ids = self.__get_scsi_ids(node)
        
        scsi_model = {}
        
        for scsi_id in scsi_ids:
            dev = scsi_ids[scsi_id][0].replace('/dev/', '')
            
            o, e, s = self.environ.execute_remote(node, 'cat', ['/sys/block/' + dev + '/device/vendor'])
            vendor = o.strip()
            o, e, s = self.environ.execute_remote(node, 'cat', ['/sys/block/' + dev + '/device/model'])
            model = o.strip()
            
            name = vendor + ' ' + model
            name = name.strip()
            if name == '':
                name = 'Unknown model'
            scsi_model[scsi_id] = name
        return scsi_model
    
    def __probe_storage(self, node):
        scsi_ids = self.__get_scsi_ids(node)
        
        # reload partition tables    
        for scsi_id in scsi_ids:
            for dev in scsi_ids[scsi_id]:
                self.environ.execute_remote(node, 'blockdev', ['--rereadpt', dev])
        
        # parse blockdev --report
        devs = {} # {path : size, ... }
        rep_str, e, s = self.environ.execute_remote(node, 'blockdev', ['--report'])
        for line in rep_str.strip().splitlines():
            words = line.strip().split()
            if len(words) != 7:
                continue
            try:
                int(words[1])
            except:
                continue
            ss = int(words[2]) # sector size
            ssize = int(words[5]) # size in sectors
            path = words[6]
            devs[path] = ss * ssize
        
        # merge data
        ret = {}
        for scsi_id in scsi_ids:
            ret[scsi_id] = {}
            for dev in scsi_ids[scsi_id]:
                for dev_b in devs:
                    if dev == dev_b:
                        ret[scsi_id]['0'] = devs[dev_b]
                    elif dev in dev_b:
                        partnum = dev_b.replace(dev, '')
                        ret[scsi_id][partnum] = devs[dev_b]
        
        return ret
    
    def __get_scsi_ids(self, node):
        o, e, s = self.environ.execute_remote(node, 'ls', ['/sys/block/'])
        devs = o.strip().split()
        scsi_ids = {}
        for dev in devs:
            o, e, s = self.environ.execute_remote(node, 'scsi_id', ['-g', '-u', '-s', '/block/' + dev])
            if s != 0:
                continue
            scsi_id = o.strip()
            if scsi_id in scsi_ids:
                # for now, skip multipathing devices
                scsi_ids.pop(scsi_id)
                #scsi_ids[scsi_id].append('/dev/' + dev)
                print 'scsi device with scsi_id \'' + scsi_id + '\' is multipathed, not implemented, so removing'
            else:
                scsi_ids[scsi_id] = ['/dev/' + dev]
        return scsi_ids
    
    def __storage_intersection(self):
        shared = copy.deepcopy(self.__node_storage.values()[0])
        remove = []
        for node in self.__node_storage:
            node_storage = self.__node_storage[node]
            for scsi_id in shared:
                if scsi_id not in node_storage:
                    if scsi_id not in remove:
                        remove.append(scsi_id)
        for scsi_id in remove:
            shared.pop(scsi_id)
        return shared
    
    
    def __disable_selinux(self, node):
        o, e, s = self.environ.execute_remote(node, 'getenforce', [])
        self.__node_selinux[node] = o.strip()
        self.environ.execute_remote(node, 'setenforce', ['0'])
    def __restore_selinux(self, node):
        try:
            se = self.__node_selinux[node]
            self.environ.execute_remote(node, 'setenforce', [se])
        except:
            pass



def infoMessage(message):
    dlg = gtk.MessageDialog(None, 0,
                            gtk.MESSAGE_INFO, gtk.BUTTONS_OK,
                            message)
    dlg.show_all()
    rc = dlg.run()
    dlg.destroy()
    return rc
