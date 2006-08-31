#!/usr/bin/perl -w

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

