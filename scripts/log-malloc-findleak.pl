#!/usr/bin/perl -w
# log-malloc2 / findleak
#	Find memory leaks in log-malloc trace file
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
package log_malloc::findleak;

use strict;
use Cwd;
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
my $LOGMALLOC_HAVE_BT = 0;
$LOGMALLOC_HAVE_BT = 1
	if(eval { require "backtrace2line.pl" });

# EXEC
sub main(@);
exit(main(@ARGV)) if(!caller());

#
# PUBLIC FUNCTIONS
#

# parse(\@lines): (\%address_map, \%address_data, \%other_data)
sub parse(\@)
{
	my ($lines) = @_;

	my $init = 0;
	my (%map, %data, %other, $payload);
	for(my $ii = 0; $ii <= $#$lines; $ii++)
	{
		$init = 1, $payload = undef, ($2 ? %map = %data = () : undef), next
			if($$lines[$ii] =~ /^\+ ((INIT)|FINI)/o);

		if($$lines[$ii] =~ /^\+ /o)
		{
			# splitting
			# + FUNCTION MEM-CHANGE MEM-IN? MEM-OUT? (FUNCTION-PARAMS) [MEM-STATUS:MEM-STATUS-USABLE]
			my (undef, $func, $size, $addr1, $addr2) = split(/ /o, $$lines[$ii]);

			my $key = $addr1;
			if($func eq 'realloc' && $addr1 ne $addr2)
			{
				$map{ $addr2 } = $map{ $addr1 };
				delete($map{ $addr1 });
				$key = $addr2;
			}

			$map{ $key } += $size;
			$payload = [];
			push(@{$data{ $key }}, { 
				call => $func,
				line => $ii + 1,
				change => $size,
				backtrace => $payload});
		}
		elsif($$lines[$ii] =~ /# (\w+) (.+?)$/o)
		{
			my ($key, $value) = ($1, $2);

			# check if payload there
			if($$lines[$ii + 1] =~ /^\W /o)
			{
				$other{$key} = $value;
			}
			else
			{
				$payload = [];
				${$other{$key}}{$value} = $payload;
			}
		}
		# other operation, stop payloading
		elsif($$lines[$ii] =~ /^\W /o)
		{
			$payload = undef;
		}
		elsif($payload)
		{
			chomp($$lines[$ii]);

			push(@$payload, $$lines[$ii]);
		}
	}
	return (\%map, \%data, \%other)
}

# process($pid, \@lines, %params): \%leaks
sub process($\@%)
{
	my ($pid, $lines, %params) = @_;

	my ($map, $data, $other) = parse(@$lines);

	# filter non-freed mem allocs
	my %leaks;
	foreach my $key (keys(%$map))
	{
		next
			if(!($map->{$key}));

		$leaks{$key} = $data->{$key};
	}

	# translate
	if($params{'translate'})
	{
		warn("WARN: backtrace2line.pl not found, can not translate !\n")
			if(!$LOGMALLOC_HAVE_BT);

		my ($cwd, $maps);
		$pid = $other->{'PID'}
			if($other && $other->{'PID'});
		$cwd = $other->{'CWD'}
			if($other && $other->{'CWD'});
		$maps = $other->{'FILE'}->{'/proc/self/maps'}
			if($other && $other->{'FILE'} && $other->{'FILE'}->{'/proc/self/maps'});

		foreach my $key (keys(%leaks))
		{
			foreach my $rec (@{$leaks{$key}})
			{
				my $bt = $rec->{'backtrace'};

				my @lines = log_malloc::backtrace2line::process($maps, $cwd, $pid, @$bt);

				if(@lines)
				{
					$rec->{'backtrace-ori'} = $rec->{'backtrace'};
					$rec->{'backtrace'} = \@lines;
				}
			}
		}
	}

	return \%leaks;
}

sub main(@)
{
	my (@argv) = @_;
	my ($file, $verbose, $pid, $fullName, $man, $help);
	my $no_translate=0;

	@ARGV = @argv;
	GetOptions(
		"<>"		=> sub { $file = $_[0] . ''; },
		"p|pid=i"	=> \$pid,
		"no-translate"	=> \$no_translate,
		"full-names"	=> \$fullName,
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
	my $result = process($pid, @lines, translate => !$no_translate);

	# color output
	my ($c_BOLD, $c_RST) = ("\033\[1m", "\033\[0m");
	($c_BOLD, $c_RST) = ('', '')
		if(!-t STDOUT);

	# printout
	if(keys(%$result))
	{
		printf("${c_BOLD}SUSPECTED %d LEAKS:${c_RST}\n", scalar keys(%$result));
	}
	else
	{
		print "NO LEAKS FOUND (HURRAY!)\n";
	}

	# iterate results
	foreach my $key (sort { ${$result->{$a}}[0]->{line} <=>  ${$result->{$b}}[0]->{line} } keys(%$result))
	{
		# TODO: we care only about first allocation here !!
		my $rec = ${$result->{$key}}[0];

		printf(" ${c_BOLD}%-10s leaked %d bytes (%0.2f KiB) allocated by %s (line: %d)${c_RST}\n",
				$key, $rec->{change}, $rec->{change} / 1024,
				$rec->{call}, $rec->{line});

		# translated bactrace
		if(exists($rec->{'backtrace'}))
		{
			# get length for pretty-print
			my ($function_len, $filepos_len, $translated) = (20, 25);
			foreach my $line (@{$rec->{'backtrace'}})
			{
				next
					if(!ref($line));

				$function_len = length($line->{function})
					if(length($line->{function}) > $function_len);

				# shorten filename
				$line->{fileFull} = $line->{file};
				if(!$fullName)
				{
					my $dir = basename(dirname($line->{file}));

					$line->{file} = basename($line->{file});
					$line->{file} = $dir . "/" . $line->{file}
						if($dir && $dir ne '.');
				}

				$line->{filepos} = sprintf("%s:%s", $line->{file}, $line->{line});
				$filepos_len = length($line->{filepos})
					if(length($line->{filepos}) > $filepos_len);

				$translated = 1;
			}

			goto SYMBOLS_ONLY
				if(!$translated);

			my $fmt = sprintf("\t%%-%ds %%-%ds %%s\n",
					$function_len, $filepos_len);

			printf($fmt, "FUNCTION", "FILE", "SYMBOL")
				if($function_len || $filepos_len);

			# pretty print
			foreach my $line (@{$rec->{'backtrace'}})
			{
				if(ref($line))
				{
					printf($fmt,
						$line->{function}, $line->{filepos},
						$line->{sym});
				}
				else
				{
					printf($fmt, '', '',
						$line);
				}
			}
		}
		# only symbols
		else
		{
SYMBOLS_ONLY:
			foreach my $line (@{$rec->{'backtrace'}})
			{
				printf("\t%s\n", $line);
			}
		}
	}

	return 0;
}

1;

=pod

=head1 NAME

log-malloc-findleak - find suspected memory leaks in log-malloc2 trace file

=head1 SYNOPSIS

log-malloc-findleak [ OPTIONS ] I<TRACE-FILE>

=head1 DESCRIPTION

This script analyzes input trace file (or part of it) produced by log-malloc2 library, and prints out
suspected memory leaks along with translated backtrace path to code allocating that memory.

NOTE: This script can be also used as perl module.

=head1 ARGUMENTS

=over 4

=item I<TRACE-FILE>

Path to file containing log-malloc2 trace (can be only part of it).

=back

=head1 OPTIONS

=over 4

=item B<-p> I<PID>

=item B<--pid> I<PID>

Pid of a B<still> running process, that generated given trace. This is primarily needed for backtrace
to work if ASLR is enabled.

=item B<--full-names>

Will force full filenames with path to be shown in backtrace and not only filenames with parent directory.

=item B<--no-translate>

Will not translate backtrace, but print only backtrace symbols as they are in trace file.

=item B<-h>

=item B<--help>

Print help.

=item B<--man>

Show man page.

=back

=head1 EXAMPLES

	# full trace file (MEMORY MAP included)
	$ log-malloc-findleak /tmp/lm.trace
	SUSPECTED 1 LEAKS:
	 0x7f00af5ac800 leaked 2000 bytes (1.95 KiB) allocated by malloc (line: 18)
		FUNCTION             FILE                      SYMBOL
		main                 examples/leak-01.c:7      ./leak-01(main+0x32)[0x7f00ad7a4af2]
		                                               /lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb]
		_start               ??:?                      ./leak-01(+0x9a9)[0x7f00ad7a49a9]
		                                               [0x0]

	# full trace file, but leak-01 has been stripped
	SUSPECTED 1 LEAKS:
	 0x7f00af5ac800 leaked 2000 bytes (1.95 KiB) allocated by malloc (line: 18)
		./leak-01(main+0x32)[0x7f00ad7a4af2]
		/lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb]
		./leak-01(+0x9a9)[0x7f00ad7a49a9]
		[0x0]

	$ log-malloc-findleak /tmp/lm.trace --no-translate
	SUSPECTED 1 LEAKS:
	 0x7f00af5ac800 leaked 2000 bytes (1.95 KiB) allocated by malloc (line: 18)
		./leak-01(main+0x32)[0x7f00ad7a4af2]
		/lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb]
		./leak-01(+0x9a9)[0x7f00ad7a49a9]
		[0x0]

	# incomplete trace file, leak-02 still running but PID is in trace file
	$ log-malloc-findleak /tmp/lm2.trace
	SUSPECTED 2 LEAKS:
	 0x7fe0c69fc800 leaked 2000 bytes (1.95 KiB) allocated by malloc (line: 18)
        	FUNCTION             FILE                      SYMBOL
	        main                 examples/leak-02.c:8      ./examples/leak-02(main+0x32)[0x7fe0c59e1b42]
	                                                       /lib64/libc.so.6(__libc_start_main+0x11b)[0x7fe0c52201cb]
	        _start               ??:?                      ./examples/leak-02(+0x9f9)[0x7fe0c59e19f9]
                                                       [0x0]
	 0x7fe0c69fd000 leaked 100 bytes (0.10 KiB) allocated by malloc (line: 23)
	        FUNCTION             FILE                      SYMBOL
	        main                 examples/leak-02.c:10     ./examples/leak-02(main+0x40)[0x7fe0c59e1b50]
	                                                       /lib64/libc.so.6(__libc_start_main+0x11b)[0x7fe0c52201cb]
	        _start               ??:?                      ./examples/leak-02(+0x9f9)[0x7fe0c59e19f9]
                                                       [0x0]

	# excerpt of trace file, leak-02 still running and no PID in trace file
	$ log-malloc-findleak /tmp/lm2.trace-part --pid 21134
	SUSPECTED 1 LEAKS:
	 0x7f4688a04000 leaked 100 bytes (0.10 KiB) allocated by malloc (line: 1)
		FUNCTION             FILE                      SYMBOL
		main                 examples/leak-02.c:10     ./examples/leak-02(main+0x40)[0x7f4687a6fb50]
		                                               /lib64/libc.so.6(__libc_start_main+0x11b)[0x7f46872ae1cb]
		_start               ??:?                      ./examples/leak-02(+0x9f9)[0x7f4687a6f9f9]
		                                               [0x0]

=head1 LICENSE

This script is released under GNU GPLv3 License.
See L<http://www.gnu.org/licenses/gpl.html>.

=head1 AUTHOR

Samuel Behan - L<http://devel.dob.sk/log-malloc2/>, L<https://github.com/samsk/log-malloc2>

=head1 SEE ALSO

L<log-malloc>

=cut

# EOF
