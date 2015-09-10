#!/usr/bin/perl -w
# log-malloc2
#	Execute command with log-malloc tracer enabled
#
# Author: Samuel Behan <_samuel_._behan_(at)_dob_._sk> (C) 2013-2015
#
# License: GNU GPLv3 (http://www.gnu.org/licenses/gpl.html)
#
# Web:
#	http://devel.dob.sk/log-malloc2
#	http://blog.dob.sk/category/devel/log-malloc2 (howto, tutorials)
#	https://github.com/samsk/log-malloc2 (git repo)
#
#
package log_malloc;

use strict;
use Cwd;
use POSIX qw(dup2);
use Getopt::Long;
use Pod::Usage;
use Data::Dumper;
use File::Basename;

# VERSION
our $VERSION = "0.4";

my $LIBEXECDIR;
BEGIN {	$LIBEXECDIR = Cwd::abs_path(dirname(readlink(__FILE__) || __FILE__)); };

# include submodule (optional)
use lib $LIBEXECDIR;
my $LOGMALLOC_HAVE_PM = 0;
$LOGMALLOC_HAVE_PM = 1
	if(eval { require "log-malloc.pm"; });

# EXEC
sub main(@);
exit(main(@ARGV)) if(!caller());

#
# INTERNAL FUNCTIONS
#

sub rotateFile($)
{
	my ($file) = @_;

	for(my $ii = 1; $ii <= 200; $ii++)
	{
		my $fn = $file . "." . $ii;

		next
			if(-e $fn);
		rename($file, $fn);
		return 1;
	}
	return 0;
}

#
# MAIN
#

sub main(@)
{
	my (@argv) = @_;
	my ($logfile, $rotate, $verbose, $man, $help);

	# cmdline parsing
	@ARGV = @argv;
	eval {
		# XXX: using last is hacky way to stop getopt processing if non-option encourted
		#	=> Inadvisably Applied Perl
		no warnings 'exiting';
		GetOptions(
			"o|output=s"		=> \$logfile,
			"oo|ro|rotate-output=s" => sub { $logfile = $_[1]; $rotate = 1; },
			"v|verbose"		=> \$verbose,
			"<>"			=> sub { unshift(@ARGV, "$_[0]"); last; },
			"h|?|help"		=> \$help,
			"man"			=> \$man,
			) || pod2usage( -verbose => 0, -exitval => 1 );
	};
	@argv = @ARGV;

	pod2usage( -verbose => 1 )
		if($help);
	pod2usage( -verbose => 3 )
		if($man);

	pod2usage( -msg => "$0: command to execute is required !",
		-verbose => 0, -exitval => 1 )
		if(!@ARGV);

	# find LD_PRELOAD library path
	my $LD_PRELOAD;
	#	- preferr use of local library (if not installed)
	if($LIBEXECDIR && -d "$LIBEXECDIR/../.libs"
		&& -e "$LIBEXECDIR/../.libs/liblog-malloc2.so")
	{
		$LD_PRELOAD = "$LIBEXECDIR/../.libs/liblog-malloc2.so";
	}
	#	- get config from log-malloc.pm
	elsif($LOGMALLOC_HAVE_PM)
	{
		$LD_PRELOAD = log_malloc::config::LD_PRELOAD();
	}
	#	- get config from pkg-config
	else
	{
		$LD_PRELOAD = `pkg-config log-malloc2 --variable=LD_PRELOAD`;
		die("$0: failed to locate log-malloc2 library via pkg-config !\n")
			if($? != 0 || !$LD_PRELOAD);
	}

	# open trace file
	if($logfile)
	{
		rotateFile($logfile)
			if($rotate);
		unlink($logfile);

		my $fd;
		if($logfile ne "-")
		{
			die("$0: failed to open trace file '$logfile' - $!\n")
				if(!open($fd, ">", $logfile));
		}
		else
		{
			$fd = \*STDERR;
		}

		# logfile -> 1022
		die("$0: failed to dup2() trace fileno - $!\n")
			if(dup2(fileno($fd), 1022) < 0);

		close($fd);
	}

	# setup env
	$ENV{'LD_PRELOAD'} = $LD_PRELOAD;
	warn "LD_PRELOAD = $LD_PRELOAD\n"
		if($verbose);

	no warnings 'exec';
	warn("$0: failed to execute '@argv' - $!\n")
		if(!exec({ $argv[0] } @argv));

	unlink($logfile);
	return 1;
}

1;

=pod

=head1 NAME

log-malloc - start given command traced with log-malloc2 library

=head1 SYNOPSIS

log-malloc-trackusage [ OPTIONS ] I<COMMAND> ...

=head1 DESCRIPTION

This script start given command with log-malloc2 library preloaded, thus
enabling memory allocations tracing. By default all traces are written
to filedescriptor 1022, but can be redirected B<--output> to file.

liblog-malloc2.so selection:
	- if starting from compilation directory, .libs/liblog-malloc2.so
	- library path from log-malloc.pm
	- library path found via pkg-config

=head1 ARGUMENTS

=over 4

=item I<COMMAND>

Command with arguments that should be executed and traced.

=back

=head1 OPTIONS

=over 4

=item B<-o> I<FILE>

=item B<--output> I<FILE>

Redirect log-malloc2 trace to given I<FILE>. If '-' specified as B<FILE> than trace will be redirected to
STDERR.

=item B<-oo> I<FILE>

=item B<-ro> I<FILE>

=item B<--rotate-output> I<FILE>

The same as B<--output> but I<FILE> will be automatically rotated instead of being overwritten - FILE.1, FILE.2...

=item B<-v>

=item B<--verbose>

Enable verbose messages.

=item B<-h>

=item B<--help>

Print help.

=item B<--man>

Show man page.

=back

=head1 EXAMPLES

	$ log-malloc -o /tmp/lm.trace ./examples/leak-01

	 *** log-malloc trace-fd = 1022 ***


=head1 LICENSE

This script is released under GNU GPLv3 License.
See L<http://www.gnu.org/licenses/gpl.html>.

=head1 AUTHOR

Samuel Behan - L<http://devel.dob.sk/log-malloc2/>, L<https://github.com/samsk/log-malloc2>

=head1 SEE ALSO

L<log-malloc-findleak>, L<log-malloc-trackusage>

=cut

#EOF
