#!/usr/bin/perl -w

if ($#ARGV == -1) {
  @targets = qw(ccs cman cman-kernel dlm dlm-kernel fence gfs gfs-kernel gnbd gnbd-kernel gulm iddev magma magma-plugins rgmanager);
} else {
  @targets = @ARGV;
}

$date = `date +%G%m%d%H%M%S`;
chomp($date);

foreach $target (@targets) {
  chdir $target;
  if (-e "make/$target.spec.in" && -e "make/release.mk.input" ) { 
    $version = getVersion(); 
    print $target ." ".$version."\n";
    open IFILE, "<make/$target.spec.in";
    open OFILE, ">make/$target.spec";
    while (<IFILE>) {
      $_ =~ s/\@VERS\@/${version}/;
      $_ =~ s/\@REL\@/${date}/;
      print OFILE "$_";
    }
    close IFILE;
    close OFILE;

    setReleaseFile(0,$version,$date);

    chdir "..";
    $newdir = $target."-".$version."-".$date;
    print `cp -r $target $newdir`;
    print `rm -rf 'find $newdir -name CVS'`;
    print `tar -zcf $newdir.tar.gz $newdir`;
    print `rm -rf $newdir`;
    print `rpmbuild --define "_srcrpmdir ./" --nodeps -ts $newdir.tar.gz `;
    print `rm $newdir.tar.gz`;
    chdir $target;
    setReleaseFile(1);
  }
  chdir "..";
}


# Get the latest tagged version string.
sub getVersion {
  $cvslogout = `cvs log configure`;
  @cvslog = split (/\n/,$cvslogout);
  $finished = 0;
  foreach (@cvslog) {
    if ($finished != 0) {
      $version = $_;
      $version =~ s/\D*(\d.*):.*/$1/;
      $version =~ s/_/\./g;
      return $version;
    }
    if ($_ =~ /^symbolic names:$/) { $finished = 1;}
  }
}

sub setReleaseFile {
  my ($reset,$version,$date) = @_;
  if ($reset == 1) {
    print `mv make/release.mk.input.bak make/release.mk.input`;
    print `rm make/*.spec`;
		    return;
		    }

		    open IFILE, "<make/release.mk.input";
		    open OFILE, ">make/release.mk.output";
		    while (<IFILE>) {
		    $_ =~ s/(^RELEASE_MAJOR = ).*/$1$version/;
    $_ =~ s/(^RELEASE_MINOR = ).*/$1$date/;
    print OFILE "$_";
  }
  close IFILE;
  close OFILE;

  print `mv make/release.mk.input make/release.mk.input.bak`;
  print `mv make/release.mk.output make/release.mk.input`;
} 
