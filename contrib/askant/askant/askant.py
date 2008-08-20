"""
The main entry point for askant.
"""

import sys
import commands

def main():

	"""Run askant"""

	cmds = commands.Commands()
	cmds.register_command(commands.DumpFSCommand())
	cmds.register_command(commands.UnlinksCommand())
	cmds.register_command(commands.CreatesCommand())

	try:
		cmds.process_argv(sys.argv)
	except commands.NoFSPluginException, p:
		print >>sys.stderr, "Plugin not found: %s" %p


if __name__ == '__main__':
	main()
