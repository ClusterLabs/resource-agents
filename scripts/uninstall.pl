#!/usr/bin/perl

##    Description: Basically the reverse of the install program, except it
##                 only supports a list of files and a directory as arguments

$| = 1;

use Getopt::Std;

# list all valid options here.  User will get errors if invalid options are 
# specified on the command line
getopts('hD');

$args = 1;

# We need at least two arguments to uninstall
if(!defined($ARGV[1])) {
    $args = 0;
}
    
# if the user set the help flag or didn't provide enough args, print help 
# and die.
if(defined($opt_h) || ($args == 0)) {
  $msg = "usage: $0 [OPTIONS] TARGET DIRECTORY\n";
  $msg = $msg . "\t-D\tRemove specified directory if empty\n";
  $msg = $msg . "\t-h\tDisplay this help message\n";
  die $msg;
}

# find out how many command line arguments we have
$length = $#ARGV;
# We need a special case if there is only one file specified
if($length > 1) {
  @filelist = @ARGV;
  $#filelist = $length - 1;
}
else {
  @filelist = @ARGV[0];
}

# the last argument is the directory
$dir = @ARGV[$length];

# prepend the directory name to all files in the filelist
$i = 0;
print "Attempting to remove the following files from directory $dir/:\n";
while($i < $length) {
  print "@filelist[$i] ";
  @filelist[$i] = "$dir/" . @filelist[$i];
  $i++;
}
print "\n";
  
#print "Files:@filelist\n";
#print "Directory: $dir\n";

# delete the files in filelist
$unlinked = unlink @filelist;
if($unlinked < $length) {
  print "Error! Unable to remove all files in $dir:\n\tYou may have to manually delete some of them.\n"
}
# if user specifed they want the directory deleted, try to delete it.  Print 
# error message if not able to delete directory, including error.
if(defined($opt_D)) {
  $result = rmdir($dir);
  if($result == FALSE) {
    print "Error! Unable to remove directory $dir/:\n\t$!\n";
  }
}


