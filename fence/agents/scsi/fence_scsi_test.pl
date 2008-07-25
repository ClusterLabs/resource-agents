#!/usr/bin/perl

use POSIX;
use IPC::Open3;
use Getopt::Std;

my @devices;
my %results;

#BEGIN_VERSION_GENERATION
$RELEASE_VERSION="";
$REDHAT_COPYRIGHT="";
$BUILD_DATE="";
#END_VERSION_GENERATION

sub get_scsi_block_devices
{
    my $block_dir = "/sys/block";

    opendir(DIR, $block_dir) or die "$!\n";

    my @block_devices = grep { /^sd*/ } readdir(DIR);

    closedir(DIR);

    for $block_dev (@block_devices)
    {
	push(@devices, "/dev/" . $block_dev);
    }
}

sub get_cluster_vol_devices
{
    my ($in, $out, $err);

    my $cmd = "vgs --config 'global { locking_type = 0 }'" .
              "    --noheadings --separator : -o vg_attr,pv_name";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "[error] unable to execute vgs command.\n" if WEXITSTATUS($?);

    while (<$out>)
    {
	chomp;

	my ($vg_attr, $pv_name) = split(/:/, $_);

	if ($vg_attr =~ /.*c$/)
	{
	    ###### DEBUG ######
	    print "DEBUG: pv_name = $pv_name\n";

	    push(@devices, $pv_name);
	}
    }

    close($in);
    close($out);
    close($err);
}

sub register_device
{
    my ($dev, $key) = @_;
    my ($in, $out, $err);

    my $cmd = "sg_persist -n -d $dev -o -G -S $key";
    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    $results{$dev}[0] = WEXITSTATUS($?);

    close($in);
    close($out);
    close($err);
}

sub unregister_device
{
    my ($dev, $key) = @_;
    my ($in, $out, $err);

    my $cmd = "sg_persist -n -d $dev -o -G -K $key -S 0";
    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    $results{$dev}[1] = WEXITSTATUS($?);

    close($in);
    close($out);
    close($err);
}

sub test_devices
{
    my $key = "0xDEADBEEF";

    foreach $dev (@devices)
    {
	register_device($dev, $key);
	unregister_device($dev, $key);
    }
}

sub check_config_fence
{
    my ($in, $out, $err);
    my $cmd = "ccs_tool query /cluster/fencedevices/fencedevice[\@agent=\\\"fence_scsi\\\"]";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    return ($?>>8);
}

sub print_results
{
    my $device_count = scalar(@devices);

    my $failure_count = 0;
    my $success_count = 0;

    print "\nAttempted to register with devices:\n";
    print "-------------------------------------\n";

    for $dev (@devices)
    {
	print "\t$dev\t";
	if ($results{$dev}[0] == 0)
	{
	    $success_count++;
	    print "Success\n";
	}
	else
	{
	    $failure_count++;
	    print "Failure\n";
	}
    }

    print "-------------------------------------\n";
    print "Number of devices tested: $device_count\n";
    print "Number of devices passed: $success_count\n";
    print "Number of devices failed: $failure_count\n\n";

    if ($failure_count != 0)
    {
	exit(1);
    }
}

sub print_usage
{
    print "\nUsage: fence_scsi_test [-c|-s] [-d] [-h]\n\n";

    print "Options:\n\n";

    print "  -c     Cluster mode. This mode is intended to test\n";
    print "         SCSI persistent reservation capabilties for\n";
    print "         devices that are part of existing clustered\n";
    print "         volumes. Only devices in LVM cluster volumes\n";
    print "         will be tested.\n\n";
    print "  -s     SCSI mode. This mode is intended to test SCSI\n";
    print "         persistent reservation capabilities for all SCSI\n";
    print "         devices visible on a node.\n\n";
    print "  -d     Debug flag. This will print debugging information\n";
    print "         such as the actual commands being run to register\n";
    print "         and unregister a device.\n\n";
    print "  -h     Help. Prints out this usage information.\n\n";
}

### MAIN #######################################################################

if (getopts("cdhst:v") == 0)
{
    print_usage;
    exit(1);
}

if ($opt_h)
{
    print_usage;
    exit(0);
}

if ($opt_c)
{
    print "\nTesting devices in cluster volumes...\n";
    get_cluster_vol_devices;
    test_devices;
    print_results;
}

if ($opt_s)
{
    print "\nTesting all SCSI block devices...\n";
    get_scsi_block_devices;
    test_devices;
    print_results;
}

if ($opt_t)
{
    if ($opt_t eq "fence")
    {
	exit check_config_fence;
    }
}

if (!$opt_c && !$opt_s && !$opt_t)
{
    print "\nPlease specify either cluster or SCSI mode.\n";
    print_usage;
    exit(1);
}
