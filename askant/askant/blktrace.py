"""
Wrapper classes for running blktrace and blkparse and using their output
"""

import os
import sys
import time
import signal
from subprocess import Popen
from subprocess import PIPE
from sysfs import Sysfs

class BlktraceException(Exception):

	def __init__(self, val, msg):
		Exception.__init__(self)
		self.val = val
		self.msg = msg
	
	def __str__(self):
		return self.msg

class Blktrace:

	def __init__(self, dev, debugfs='/sys/kernel/debug'):
		self.dev = dev
		self.debugfs = debugfs
		self.sysfs = Sysfs(dev)
		self.btpid = -1

	def handle_sigint(self, sig, frame):
		if self.btpid >= 0:
			os.kill(self.btpid, signal.SIGTERM)

	def trace(self, tracefile):

		if not self.dev:
			raise Exception("No device specified.")

		btargs = ['blktrace',
			'-d', self.dev,
			'-r', self.debugfs,
			'-o', tracefile]

		blktrace = Popen(btargs, bufsize=1, stdout=PIPE,
						stderr=open('/dev/null','w'))
		self.btpid = blktrace.pid
		btres = None
		while btres is None:
			time.sleep(1)
			btres = blktrace.poll()

		self.btpid = -1
		if btres:
			raise BlktraceException(btres,
				'blktrace exited with code ' + str(btres))



	def parse(self, tracefile, getblk):

		if not self.dev:
			raise Exception("No device specified.")

		offset = self.sysfs.get_partition_start_sector()

		bpargs = ['blkparse', '-i', tracefile]
		blkparse = Popen(bpargs, bufsize=1, stdout=PIPE)

		bpres = None
		while bpres is None:
			output = blkparse.stdout.readline().strip()
			if output:
				chunks = output.split()
				try:
					# chunks[7] is the sector number
					blk = list(getblk(int(chunks[7])))
					print "%s %s %s" %\
						(' '.join(blk[0:3]),\
						 output.strip(),
						 blk[3].strip())
				except KeyError:
					pass
				except ValueError:
					pass

			bpres = blkparse.poll()

		if bpres:
			raise BlktraceException(bpres,
				'blkparse exited with code ' + str(bpres))


