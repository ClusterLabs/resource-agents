#!/usr/bin/perl -w

if(scalar(@ARGV) != 1){
    print STDERR "Wrong number of arguments.\n";
    exit(1);
}

$component = $ARGV[0];

unless(-d $component){
    print STDERR "${component}:: No such directory.  Skipping...\n";
    exit(0);
}

$output = `cvs log $component/configure`;

if($output =~ /symbolic names:\n\s+(.*):\s/){
    print "${component}:: Syncing with latest tag ($1)\n";
    `cvs update -r $1 $component`;
} else {
    print STDERR "${component}:: Untagged, can not sync.\n";
    exit(1);
}
