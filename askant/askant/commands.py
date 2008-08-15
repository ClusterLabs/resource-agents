"""Command line parsing and processing for askant"""

import fs
import os
import sys
import time
import about
import signal
import tempfile
from sysfs import Sysfs, SysfsException
from blktrace import Blktrace, BlktraceException
from optparse import OptionParser, make_option

class NoFSPluginException(Exception):

	"""A generic Exception class for when file system plugins are missing"""

	pass # Nothing special about this exception

class Commands:

	"""Provides a convenient interface to the askant commands"""

	def __init__(self):

		self.commands = {}
		self.parser = OptionParser(usage="%prog COMMAND [options]",
				version=about.version)


	def register_command(self, command):
		self.commands[str(command)] = command
	
	def __lookup_command(self, command):
		try:
			return self.commands[command]
		except KeyError, k:
			return DefaultCommand()
	
	def __parse_command(self, command, argv):
		self.parser.prog = "%s %s" %\
			(self.parser.get_prog_name(), str(command))
		self.parser.set_usage(command.get_usage())
		optlst = command.get_options()
		for o in optlst:
			self.parser.add_option(o)
		self.parser.set_description(command.get_help())

		if str(command) != "":
			argv = argv[1:]

		return self.parser.parse_args(argv)

	def process_argv(self, argv):
		try:
			command = self.__lookup_command(argv[1])
		except IndexError, i:
			# This exits
			self.parser.error("No command specified. Try --help")

		options, args = self.__parse_command(command, argv[1:])

		command.do_command(options, args, self)

class Command:
	def __init__(self):
		self.options = []

class DefaultCommand(Command):
	def __str__(self):
		return ""

	def get_options(self):
		self.options.append(make_option("-c","--commands",
			action="store_true",
			help="Lists available commands"))
		return self.options

	def get_usage(self):
		return "%prog COMMAND [options] [args]"

	def get_help(self):
		return "Askant is a tool for tracing I/O activity in Linux "\
			"and adding extra file system context by "\
			"parsing file system data directly."
	
	def do_command(self, options, args, base):
		if options.commands:
			base.parser.print_usage()
			print "Available commands:"
			keys = base.commands.keys()
			keys.sort()
			for k in keys:
				print " %10s  %s" %\
					(k, base.commands[k].get_help())
			print ""
			print "For command-specific help, use "\
				"%sCOMMAND --help" % base.parser.get_prog_name()
		else:
			base.parser.error("Command not found. Try --commands")


class FSCommandTemplate(Command):
	def __init__(self):
		self.fsmod = None
		self.block_table = {}
		self.block_func = self.__report_hook
		self.sector_size = None
		self.offset = None
		self.options = [
			make_option("-d","--device", metavar="DEVICE",
				help="The device to analyse."),
		]

	def load_fs_plugin(self, fsname):
		try:
			self.fsmod = __import__("fs." + fsname,
					globals(), locals(), ["fs"])
		except ImportError, i:
			raise NoFSPluginException(fsname)

	def parse_fs(self, dev, hook=None):
		if hook is None:
			hook = self.block_func

		sfs = Sysfs(dev)
		offset = sfs.get_partition_start_sector()
		sector_size = sfs.get_dev_sector_size()

		self.fsmod.set_report_hook(hook)

		try:
	                sfs = Sysfs(dev)
        	        self.offset = sfs.get_partition_start_sector()
                	self.sector_size = sfs.get_dev_sector_size()
			signal.signal(signal.SIGINT, self.fsmod.handle_sigint)
			self.blk_size = self.fsmod.get_block_size(dev)
			self.fsmod.parsefs(dev)
			signal.signal(signal.SIGINT, signal.SIG_DFL)
		except IOError, i:
			print >>sys.stderr, str(i)
			sys.exit(1)

	def set_outfile(self, outfile):
		self.outfile = outfile

	def trace(self, options, dump_before):

		sfs = Sysfs(options.device)
		self.offset = sfs.get_partition_start_sector()
		self.sector_size = sfs.get_dev_sector_size()

		tmstamp = time.strftime('%Y%m%d%H%M%S')
		self.block_func = self.__report_hook
		try:
			self.outfile = open(os.path.join(options.outdir, "blocks-%s" %
				tmstamp), 'w')
		except IOError, e:
			print >>sys.stderr, str(e)
			sys.exit(1)

		if dump_before:
			print >>sys.stderr, "Gathering block data..."
			self.parse_fs(options.device)
			self.outfile.close()

		if options.debugfs:
			bt = Blktrace(options.device, options.debugfs)
		else:
			bt = Blktrace(options.device)

		os.chdir(options.outdir) # Blktrace doesn't use absolute paths
		print >>sys.stderr, "Tracing. Hit Ctrl-C to end..."
		try:
			signal.signal(signal.SIGINT, bt.handle_sigint)
			bt.trace(tmstamp)
			signal.signal(signal.SIGINT, signal.SIG_DFL)
		except BlktraceException, b:
			signal.signal(signal.SIGINT, signal.SIG_DFL)
			print >>sys.stderr, str(b)
			sys.exit(1)

		if not dump_before:
			print >>sys.stderr, "Gathering block data..."
			self.parse_fs(options.device)
			self.outfile.close()

		print >>sys.stderr, "Matching blocks..."
		blockdb = open(self.outfile.name, 'r')
		blocks = {}
		for l in blockdb.readlines():
			s = l.split('\t')
			blocks[int(s[0])] = tuple(s[1:])

		bt.parse(os.path.join(options.outdir, tmstamp), blocks.__getitem__)
		blockdb.close()

	def __report_hook(self, blk, type, parent, fn):
		if not fn:
			fn = ""
		try:
			self.outfile.write("%d\t%d\t%s\t%d\t\"%s\"\n" % (
			((blk * self.blk_size)/self.sector_size) + self.offset,
			blk, type, parent, fn))
		except Exception, e:
			print str(e)

	def __dict_hook(self, blk, type, parent, fn):
		self.block_table[
		    ((blk * self.blk_size)/self.sector_size)+self.offset] =\
		    (blk, type, parent, fn)


class DumpFSCommand(FSCommandTemplate):
	def __str__(self):
		return "dumpfs"

	def get_options(self):
		self.options.append(make_option("-t","--type", metavar="FSTYPE",
			help="The type of file system on the device."))
		self.options.append(make_option("-o","--output", metavar="FILE",
			default="-",
			help="File to write output to or '-' for stdout "
			"(default)"))
		return self.options
	
	def get_usage(self):
		return "%prog [options]"
	
	def get_help(self):
		return "Dump fs block information in TSV format."
	
	def do_command(self, options, args, base):

		if not options.device:
			base.parser.error("No device specified. Use -d.")

		if not options.type:
			base.parser.error("No file system type specified. "
								"Use -t.")
		try:
			if options.output != "-":
				self.set_outfile(open(options.output, 'w'))
			else:
				self.set_outfile(sys.stdout)
		except IOError, e:
			print >>sys.stderr,\
				"Unable to open file for writing: %s" %\
				options.output
			return
		except KeyError:
			pass

		self.load_fs_plugin(options.type)
		try:
			self.parse_fs(options.device)
		except SysfsException, s:
			base.parser.error(s)
		
class UnlinksCommand(FSCommandTemplate):
	def __str__(self):
		return "unlinks"

	def get_options(self):
		self.options.append(make_option("-t","--type", metavar="FSTYPE",
			help="The type of file system on the device."))
		self.options.append(make_option("-g","--debugfs", metavar="PATH",
			default="/sys/kernel/debug",
			help="The path to the debugfs mountpoint. Default "
			"/sys/kernel/debug"))
		self.options.append(make_option("-D","--outdir", metavar="DIR",
			help="Directory to write output to. Required."))
		return self.options
	
	def get_usage(self):
		return "%prog [options]"
	
	def get_help(self):
		return "Trace block I/O activity during unlink tests."
	
	def do_command(self, options, args, base):
		if not options.device:
			base.parser.error("No device specified. Use -d.")

		self.device = options.device

		if not options.type:
			base.parser.error("No file system type specified. "
								"Use -t.")
		if not options.outdir:
			base.parser.error("No output directory specified. "
								"Use -D.")

		self.load_fs_plugin(options.type)
		self.trace(options, dump_before=True)

class CreatesCommand(FSCommandTemplate):
	def __str__(self):
		return "creates"

	def get_options(self):
		self.options.append(make_option("-t","--type", metavar="FSTYPE",
			help="The type of file system on the device."))
		self.options.append(make_option("-g","--debugfs", metavar="PATH",
			default="/sys/kernel/debug",
			help="The path to the debugfs mountpoint. Default "
			"/sys/kernel/debug"))
		self.options.append(make_option("-D","--outdir", metavar="DIR",
			help="Directory to write output to. Required."))
		return self.options
	
	def get_usage(self):
		return "%prog [options]"
	
	def get_help(self):
		return "Trace block I/O activity during create tests."
	
	def do_command(self, options, args, base):
		if not options.device:
			base.parser.error("No device specified. Use -d.")

		self.device = options.device

		if not options.type:
			base.parser.error("No file system type specified. "
								"Use -t.")
		if not options.outdir:
			base.parser.error("No output directory specified. "
								"Use -D.")

		self.load_fs_plugin(options.type)
		self.trace(options, dump_before=False)

