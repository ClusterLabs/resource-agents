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


class ADSUtils:
    
    def __init__(self, storage, environ):
        self.__storage = storage
        self.__environ = environ
        
    
    # returns StorageConfig object,
    # to be used for application's storage configuration
    def get_new_storage_config(self):
        return self.__storage.get_new_instance()
    
    
    # execute command on localhost
    # bin - path to execute
    # args - list of arguments
    # returns (stdout, stderr, exit_status)
    def execute(self, bin, args):
        return self.__environ.execute(bin, args)
    
    
    # hostname - host to execute command on
    # bin - path to execute
    # args - list of arguments
    # returns (stdout, stderr, exit_status)
    def execute_remote(self, hostname, bin, args):
        return self.__environ.execute_remote(hostname, bin, args)
    
    
    # recursively copies localpath to remotepath on remote_hostname
    # returns (stderr, exit_status)
    def copy_to_host(self, localpath, remote_hostname, remotepath):
        return self.__environ.copy_to_host(localpath, remote_hostname, remotepath)
    
    
    # log message
    def log(self, msg, stdout=False):
        self.__environ.log(msg, stdout)
