"""
Provides access to sysfs data pertaining to storage partitions.
"""

import os
import os.path

class SysfsException(Exception):
	"""
	An exception which is raised when things go wrong with Sysfs.
	"""
	pass # No functionality to add over Exception class

class Sysfs:
	"""
	Provides access to sysfs data pertaining to storage partitions.
	"""
	def __init__(self, partition, sysfs_path='/sys'):
		"""
		Instantiate a Sysfs object. partition should be a path to a
		disc partition device e.g. /dev/sda3.
		"""
		self.partition = partition
		self.device = self.__partition2parent()
		self.sysfs_path = sysfs_path
		line = self.__firstline(os.path.join(
				sysfs_path,'block',
				os.path.split(self.device)[1],
				os.path.split(self.partition)[1],
				'start'))
		self.partition_start = int(line)
		line = self.__firstline(os.path.join(
				sysfs_path,'block',
				os.path.split(self.device)[1],
				'queue',
				'hw_sector_size'))
		self.dev_sector_size = int(line)
		line = self.__firstline(os.path.join(
				sysfs_path,'block',
				os.path.split(self.device)[1],
				os.path.split(self.partition)[1],
				'size'))
		self.partition_size = int(line)

	def __firstline(self, fname):
		"""
		Read the first line of a file
		"""
		try:
			fobj = open(fname, 'r')
		except IOError:
			raise SysfsException(
				'Could not open file "%s" for reading. Please '
				'check your kernel is 2.6.25 or later and '
				'sysfs is mounted.' % fname)
		line = fobj.readline()
		fobj.close()
		return line

	def __partition2parent(self):
		"""
		Return the name of a partition device's parent device
		e.g. /dev/sda3 -> /dev/sda
		"""
		# This may need thinking about a bit more -
		# nothing in life can be this simple.
		return self.partition.rstrip('0123456789')


	def get_partition_start_sector(self):
		"""
		Look up the start sector of the partition.
		"""
		return self.partition_start
	
	def get_dev_sector_size(self):
		"""
		Look up the size of the device's sectors.
		"""
		return self.dev_sector_size
	
	def get_partition_size(self):
		"""
		Look up the size of the partition.
		"""
		return self.partition_size
