#!/usr/bin/perl -w
#
# Copyright (C) 1997-2003 Sistina Software, Inc.  All rights reserved.
# Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#

##
##  Parse named.conf (from STDIN) and add options from cluster.conf
##  
##  ./named-parse-config.pl "directory" "pid-file" "listen-on" "set source <true | false>"
##
use strict;

if ($#ARGV < 3) {
	die ("Not enough arguments");
}

while (my $line = <STDIN>) {
	chomp($line);
	$line =~ s/(.*?)\s*$/$1/;
	if ($line =~ /^\s*options\s+\{/) {
		print $line, "\n";
		print "\tdirectory \"$ARGV[0]\";\n";
		print "\tpid-file \"$ARGV[1]\";\n";
		print "\tlisten-on { $ARGV[2] };\n";
		if ($ARGV[3] =~ "1|true|TRUE|yes|YES|on|ON") {
			print "\tnotify-source $ARGV[2];\n";
			print "\ttransfer-source $ARGV[2];\n";
			print "\tquery-source $ARGV[2];\n";
		}
	} else {
		print $line, "\n";
	}
}

