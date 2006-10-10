#!/usr/bin/perl -w

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
