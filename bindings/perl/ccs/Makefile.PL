use ExtUtils::MakeMaker;

my %INFOS = (
    'NAME'	=> 'Cluster::CCS',
    'VERSION_FROM' => 'CCS.pm', # finds $VERSION
    'AUTHOR'	=> 'Fabio M. Di Nitto <fdinitto@redhat.com>',
    'ABSTRACT'	=> 'Interface to Cluster Configuration Service library',
);

# read extra configurations from the commandline
my %params;
@params{qw(DEBUG DEFINE EXTRALIBDIR GDOME INC LIBS SKIP_SAX_INSTALL XMLPREFIX)}=();

@ARGV = grep {
  my ($key, $val) = split(/=/, $_, 2);
  if (exists $params{$key}) {
    $config{$key} = $val; 0
  } else { 1 }
} @ARGV;

$extralibdir = $config{EXTRALIBDIR};
delete $config{EXTRALIBDIR};

WriteMakefile(
              %INFOS,
              %config,
);

