#!/usr/bin/perl -w
# log-malloc2 / trackusage
#	Outputs memory usage
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
package log_malloc::trackusage;

use strict;
use Getopt::Long;
use Pod::Usage;
use Data::Dumper;

# VERSION
our $VERSION = "0.4";

# EXEC
sub main(@);
exit(main(@ARGV)) if(!caller());

#
# PUBLIC FUNCTIONS
#

# process(\@lines, @use_real_usage): @usage
sub process(\@;$)
{
	my ($lines, $rusage) = @_;
	my ($min, $max, $rmin, $rmax) = (0, 0, 0, 0);

PROCESS:
	my @result;
	my $init = 0;
	for(my $ii = 0; $ii <= $#$lines; $ii++)
	{
		$init = 1, next
			if($init != 2 && $$lines[$ii] =~ /^\+ (INIT|FINI)/o);
		next
			if(!$init);

		# matching [MEM-STATUS:MEM-STATUS-USABLE] in
		# + FUNCTION MEM-CHANGE MEM-IN? MEM-OUT? (FUNCTION-PARAMS) [MEM-STATUS:MEM-STATUS-USABLE]
		if($$lines[$ii] =~ /^\+.*?\[(\d+):(\d*)\]/o)
		{
			my ($use, $ruse) = ($1, $2);
			my $val = $use;

			$val = $ruse
				if($rusage && defined($ruse) && $ruse ne '');
			push(@result, $val);
		}
	}

	# this was only a snippet of log-malloc trace file
	if(!$init)
	{
		$init = 2;
		goto PROCESS;
	}
	return @result;
}

#
# MAIN
#

sub main(@)
{
	my (@argv) = @_;
	my ($file, $usable_size, $verbose, $man, $help);

	@ARGV = @argv;
	GetOptions(
		"<>"		=> sub { $file = $_[0] . ''; },
		"usable-size"	=> \$usable_size,
		"h|?|help"	=> \$help,
		"man"		=> \$man,
	) || pod2usage( -verbose => 0, -exitval => 1 );
	@argv = @ARGV;

	pod2usage( -verbose => 1 )
		if($help);
	pod2usage( -verbose => 3 )
		if($man);

	pod2usage( -msg => "$0: log-malloc trace filename required",
		-verbose => 0, -exitval => 1 )
		if(!$file);

	# whole file to memory
	my $fd;
	die("$0: failed to open file '$file' - $!\n")
		if(!open($fd, $file));

	my (@lines) = <$fd>;
	close($fd);

	# process data
	my (@result) = process(@lines, $usable_size);

	# print out
	foreach my $elem (@result)
	{
		print $elem . "\n";
	}

	return 0;
}

1;

=pod

=head1 NAME

log-malloc-trackusage - track allocated memory usage

=head1 SYNOPSIS

log-malloc-trackusage [ OPTIONS ] I<TRACE-FILE>

=head1 DESCRIPTION

This script analyzes input trace file and prints out how memory usage changed by every
memory allocation/release.

NOTE: This script can be also used as perl module.

=head1 ARGUMENTS

=over 4

=item I<TRACE-FILE>

Path to file containing log-malloc2 trace (can be only part of it).

=back

=head1 OPTIONS

=over 4

=item B<--usable-size>

Prints really allocated/assigned memory instead of how much memory has been requested.

=item B<-h>

=item B<--help>

Print help.

=item B<--man>

Show man page.

=back

=head1 EXAMPLES

	$ log-malloc-trackusage /tmp/lm.trace
	3736
	3836
	3736

=head1 LICENSE

This script is released under GNU GPLv3 License.
See L<http://www.gnu.org/licenses/gpl.html>.

=head1 AUTHOR

Samuel Behan - L<http://devel.dob.sk/log-malloc2/>, L<https://github.com/samsk/log-malloc2>

=head1 SEE ALSO

L<log-malloc>

=cut

#EOF
