#!/usr/bin/perl -w

##
##  Parse named.conf (from STDIN) and add options from cluster.conf
##  
##  ./named-parse-config.pl "directory" "pid-file" "listen-on"
##
use strict;

if ($#ARGV < 2) {
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
	} else {
		print $line, "\n";
	}
}

