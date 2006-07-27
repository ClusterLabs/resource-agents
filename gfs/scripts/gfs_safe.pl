#!/usr/bin/perl -w

use strict;

use vars qw($opt_e $opt_t $opt_v);

use Getopt::Std;
use IPC::Open3;

if ($#ARGV < 0) {
    print "$0: too few arguments.\n";
    exit;
}

getopts('e:tv');

my ($in, $out, $err, $cmd, $pid, $vol);

$vol = $ARGV[$#ARGV];

$cmd = "lvchange -a n $vol";
$pid = open3($in, $out, $err, $cmd);

waitpid($pid, 0);

if ($?>>8)
{
    while (<$out>)
    {
	print "$_\n";
    }
    exit($?>>8);
}
else
{
    print "* Volume '$vol' is available for maintenance.\n";

    if ($opt_t)
    {
	exit;
    }

    if ($opt_e)
    {

	$cmd = "lvchange -a ey $vol";
	$pid = open3($in, $out, $err, $cmd);

	waitpid($pid, 0);

	if ($?>>8)
	{
	    while (<$out>)
	    {
		print "$_\n";
	    }
	    exit($?>>8);
	}

	$cmd = "$opt_e $vol";
	print "$cmd\n";
	$pid = open3($in, $out, $err, $cmd);

	while (<$out>)
	{
	    print "$_";
	}

	waitpid($pid, 0);

	$cmd = "lvchange -a en $vol";
	$pid = open3($in, $out, $err, $cmd);

	if ($?>>8)
	{
	    while (<$out>)
	    {
		print "$_\n";
	    }
	    exit($?>>8);
	}
    }
    exit(0);
}
