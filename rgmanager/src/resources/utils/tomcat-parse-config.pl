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
##  This script replace IP addresses on which tomcat server
##  should listen. Tomcat can't listen on every IP because that 
##  way we can run only on instance. 
##
##  Usage: ./tomcat-parse-config.pl ip1 ip2 < /etc/tomcat/server.xml
##         where ipXX defines an IP address [eg. 127.0.0.1 134.45.11.1]
##
##
use strict;

while (my $line = <STDIN>) {
	chomp ($line);

	if ($line =~ /(.*?)<Connector (.*)/) {
		my $tmp = $2;
		my $content = "<Connector ";
		my $start = $1;
		my $rest = "";

		while (($tmp =~ />/) == 0) {
			$content .= $tmp . "\n";
			$tmp = <STDIN>;
			chomp($tmp);
		}

		if ($tmp =~ /(.*?)>(.*)/) {
			$content .= $1 . ">\n";
			$rest = $2;
			chomp($rest);
		}
		
		print $start;
		foreach my $arg (@ARGV) {
			$content =~ s/\s+address=".*?"/ /;
			$content =~ s/Connector /Connector address="$arg" /;
			print $content;
		}
		print $rest;
	} else {
		print $line,"\n";
	}
}
