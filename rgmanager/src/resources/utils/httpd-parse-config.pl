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
##  This script removes <IfDefine foo> sections from the
##  Apache httpd.conf file. This is quite useful because we
##  don't have any direct access to the parsed configuration
##  file of the httpd server.
##
##  Usage: ./httpd-parse-config.pl -Dfoo1 -Dfoo2 < httpd.conf
##         where fooX are defines as passed to the httpd server
##
##  Note:  All whitespace characters at the beginning and end 
##         of lines are removed.
##
use strict;

my @defines = ();
## Default behaviour is to show all lines when we are not
## in the <IfDefine foo> sections.
my @show = (1);

sub testIfDefine($) {
	my $param = $1;
	my $positiveTest = 1;
	if ($param =~ /^!(.*)$/) {
		$param = $1;
		$positiveTest = 0;
	}

	foreach my $def (@defines) {
		if ($def eq $param) {
			return $positiveTest;
		}
	}

	return (1-$positiveTest);	
}

foreach my $arg (@ARGV) {
	if ($arg =~ /^-D(.*)$/) {
		push(@defines, $1);
	}
}

## Parse config file and remove IfDefine sections 
while (my $line = <STDIN>) {
	chomp($line);
	$line =~ s/^\s*(.*?)\s*$/$1/;
	if ($line =~ /<IfDefine (.*)>/) {
		if (testIfDefine($1) == 1) {
			if ($show[$#show] == 1) {
				push (@show, 1);
			} else {
				push (@show, 0);
			}
		} else {
			push (@show, 0);
		}
	} elsif ($line =~ /<\/IfDefine>/) {
		pop(@show);
	} elsif ($show[$#show] == 1) {
		print $line, "\n";
	}
}

