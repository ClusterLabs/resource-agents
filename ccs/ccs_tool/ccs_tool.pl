#!/usr/bin/perl -w

&main;
exit;

sub main {
    my $command = "";
    my @argv = ();

    foreach $x (@ARGV){
	if($x eq "-h" || $x eq "--help" || $x eq "help"){
	    print_usage(STDOUT);
	    exit;
	} else {
	    @argv = (@argv, $x);
	}
    }

    if(scalar(@argv) < 1){
	print_usage(STDERR);
	exit 1;
    }

    if($argv[0] eq "upgrade"){
	die "function not implemented.\n";
    }

    if($argv[0] eq "update"){
	update($argv[1]);
    }

}

sub print_usage {
    my $stream = $_[0];
    print $stream
	"Usage::\n".
	"  ccs_tool [options] <command>\n".
	"\n".
	"Commands:\n".
	"  help                Print this usage.\n".
	"  update [file]       Tells ccsd to upgrade to new config file.\n".
	"\n";
}

sub update {
    my $file = $_[0];
    my $output;
    my $desc;
    my $set=0;
    my $prev_version;
    my $new_version;

    `rm -f /etc/cluster/cluster.conf-rej`;

    if(system("ps -C ccsd > /dev/null")){
	die "ccsd is not running.\n";
    }

    $output = `sh -c 'ccs_test connect 2>&1'`;

    if($output =~ /Connection refused/){
	die "Unable to honor update request.  The cluster is not quorate.\n";
    }

    if($output !~ /Connection descriptor = (\d+)/){
	die "Unable to connect to ccsd.\n";
    }
    $desc = $1;

    $output = `sh -c "ccs_test get $desc. //\@config_version"`;
    if($output =~ /Value = <(\d+)>/){
	$prev_version = $1;
    } else {
	`sh -c "ccs_test disconnect $desc"`;
	die "Unable to determine the current config version.\n";
    }

    `sh -c "ccs_test disconnect $desc"`;

    if(!$file){
	$file = "/etc/cluster/cluster.conf";
    } else {
	if(system("cp $file /etc/cluster/cluster.conf")){
	    die "Unable to put file in place for update.\n".
		"Check permissions on /etc/cluster/\n";
	}
    }

    `killall -HUP ccsd`;

    TOP: while(1){
	sleep(1);
	if( -e "/etc/cluster/cluster.conf-rej"){
	    open(F, "< /etc/cluster/cluster.conf-rej");
	    print STDERR "Cluster update failed.\n\n";
	    while(<F>){
		if(/<!--.*Reason:/){
		    $set=1;
		    next;
		}
		if(/-->/){
		    last TOP;
		}
		if($set){
		    print STDERR;
		}
	    }
	    last TOP;
	}

	$output = `sh -c 'ccs_test connect 2>&1'`;
	if($output !~ /Connection descriptor = (\d+)/){
	    next;
	}
	$desc = $1;

	$output = "";
	$output = `sh -c "ccs_test get $desc. //\@config_version"`;
	if($output =~ /Value = <(\d+)>/){
	    if($1 > $prev_version){
		`sh -c "ccs_test disconnect $desc"`;
		print "Update from version $prev_version to version $1 complete.\n";
		last TOP;
	    }
	} else {
	    print STDERR "Unable to get version information after update.\n";
	}

	`sh -c "ccs_test disconnect $desc"`;
    }	
}

