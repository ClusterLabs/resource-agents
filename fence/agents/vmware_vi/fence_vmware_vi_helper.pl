#!/usr/bin/perl

use strict;
use warnings;

my ($RELEASE_VERSION, $REDHAT_COPYRIGHT, $BUILD_DATE);

#BEGIN_VERSION_GENERATION
$RELEASE_VERSION="";
$REDHAT_COPYRIGHT="";
$BUILD_DATE="";
#END_VERSION_GENERATION

#### FUNCTIONS #####
# Show error message
sub show_error {
  print STDERR @_;
}

sub my_exit {
  my ($exit_code)=@_;
  
  # Disconnect from server
  Util::disconnect();

  exit $exit_code;
}

#### Global variables #####
# Aditional options
my %opts = (
   'operation' => {
      type => "=s",
      help => "The operation to perform (on,off,list). Operations on/off require name of the virtual machine",
      default => "list",
      required => 0,
   },
   'vmname' => {
      type => "=s",
      help => "The name of the virtual machine",
      required => 0,
   },
   'datacenter' => {
      type => "=s",
      help => "The name of the datacenter",
      required => 0,
   }
);

#################
##### MAIN ######
#################

# Conditional use of VIRuntime
eval "use VMware::VIRuntime;";

if ($@) {
  show_error "Please install VI Perl API package to use this tool!\n";
  exit 1;
}

# Parse options
Opts::add_options(%opts);
Opts::parse();
Opts::validate();

if (!(Opts::get_option('operation')=~/^(on|off|list)$/i)) {
  show_error "Operation should be on, off or list!\n";
  exit 2;
}

my $operation=lc(Opts::get_option('operation'));

if (($operation ne 'list') && (!defined Opts::get_option('vmname'))) {
  show_error "Operation on, off require vmname parameter!\n";
  exit 2;
}


# Try connect to machine
eval {
  Util::connect();
};

if ($@) {
  show_error "Cannot connect to server!\nVMware error:".$@;
  exit 3;
}

my ($datacenter, $datacenter_view, $vm_views,$vm);
# We are connected to machine

# If user want's datacenter, we must first find datacenter
my %filter=(view_type => 'VirtualMachine');

if( defined (Opts::get_option('datacenter')) ) {
  $datacenter = Opts::get_option('datacenter');
  $datacenter_view = Vim::find_entity_view(view_type => 'Datacenter',
                                            filter => { name => $datacenter });
  if (!$datacenter_view) {
    show_error "Cannot find datacenter ".$datacenter."!\n";

    my_exit 4;
  }

  $filter{'begin_entity'}=$datacenter_view;
}

if ($operation ne 'list') {
  $filter{'filter'}= {"config.name" => Opts::get_option('vmname')};
}   

$vm_views = Vim::find_entity_views(%filter);

my $found=0;

# Traverse all found vm
foreach $vm(@$vm_views) {
  if ($operation eq 'list') {
    print $vm->name."\t".$vm->runtime->powerState->val."\n";
  } elsif ($operation eq 'on') {
    eval {
      $vm->PowerOnVM();
    };
    
    if ($@) {
      show_error "Cannot power on vm ".Opts::get_option('vmname')."!\nVMware error:".$@;
      my_exit 6;
    }      
  } elsif ($operation eq 'off') {
    eval {
      $vm->PowerOffVM();
    };
    
    if ($@) {
      show_error "Cannot power off vm ".Opts::get_option('vmname')."!\nVMware error:".$@;
      my_exit 6;
    }      
  } else {
    show_error "Operation should be on, off or list!\n";
    my_exit 2;
  }
  $found++;
}

if (!$found) {
  show_error "Cannot find vm ".Opts::get_option('vmname')."!\n";
  my_exit 5;
}

# Should be 0 -> success all, or 6 in case of error
my_exit 0;

__END__

=head1 NAME

fence_vmware_vi_helper - Perform list of virtual machines and 
               poweron, poweroff  of operations on virtual machines.

=head1 SYNOPSIS

 fence_vmware_vi_helper --operation <on|off|list> [options]

=head1 DESCRIPTION

This VI Perl command-line utility provides an interface for 
seven common provisioning operations on one or more virtual 
machines: powering on, powering off and listing virtual mode.

=head1 OPTIONS

=head2 GENERAL OPTIONS

=over

=item B<operation>

Operation to be performed.  One of the following:

  <on> (power on one or more virtual machines),
  <off> (power off one  or more virtual machines),
  <list> (list virtual machines and their status)

=item B<vmname>

Optional. Name of the virtual machine on which the
operation is to be performed. 

=item B<datacenter>

Optional. Name of the  datacenter for the virtual machine(s).
Operations will be performed on all the virtual machines under the given datacenter.

=back

=head1 EXAMPLES

Power on a virtual machine

   fence_vmware_vi_helper --username administrator --password administrator --operation on
                --vmname rhel --server win1

   fence_vmware_vi_helper --username administrator --password administrator --operation on
                --vmname rhel --server win1 --datacenter Datacenter

Power off a virtual machine

   fence_vmware_vi_helper --username administrator --password administrator --operation off
                --vmname rhel --server win1

   perl fence_vmware_vi_helper --username administrator --password administrator --operation off
                --vmname rhel --server win1 --datacenter Datacenter

List of virtual machines

   fence_vmware_vi_helper --username administrator --password administrator --server win1

   fence_vmware_vi_helper --username administrator --password administrator --server win1
                --operation list

=head1 SUPPORTED PLATFORMS

All operations supported on ESX 3.0.1

All operations supported on Virtual Center 2.0.1
