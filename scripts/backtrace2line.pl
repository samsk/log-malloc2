#!/usr/bin/perl -w
#
# log-malloc2 / backtrace2line
#	Translate backtrace() output into functions, file names and line numbers (supports ASLR)
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
package log_malloc::backtrace2line;

use strict;
use Cwd;
use Getopt::Long;
use Pod::Usage;
use Data::Dumper;
use File::Basename;

# VERSION
our $VERSION = '0.4';

# CONFIGS
our $VERBOSE = 0;
our $WARNING = 0;	# set to -1 to disable globally
my @ADDR2LINE = ("addr2line", "-C");
my $LINUX_ALSR_CONFIG = "/proc/sys/kernel/randomize_va_space";

# EXEC
sub main(@);
exit(main(@ARGV)) if(!caller());

#
# INTERNAL FUNCTIONS
#

sub addr2num($)
{
	my ($addr) = @_;

	no warnings 'portable';
	return hex($addr);
}

sub readFile($)
{
	my ($fd) = @_;

	my @arr;
	while(my $line = <$fd>)
	{
		chomp($line);

		# read till line with only '.' or beginning with '='
		last
			if($line =~ /^(\.$|=)/o);

		push(@arr, $line);
	}
	return @arr;
}

sub verbose($$)
{
	my ($lev, $msg) = @_;

	return print(STDERR $msg)
		if($lev <= $VERBOSE);
	return 0;
}

sub read_mapsFile($$)
{
	my ($mapsFile, $pid) = @_;

	# mapsFile can be an ARRAY (for moduline call)
	if(ref($mapsFile) eq 'ARRAY')
	{
		return @$mapsFile;
	}
	elsif($pid)
	{
		$mapsFile = sprintf("/proc/%d/maps", $pid);
	}
	elsif(ref($mapsFile))
	{
		die("[programming error] invalid mapsFile=$mapsFile argument !");
	}

	# read from user supplied maps file
	my $fd;
	return ()
		if(!$mapsFile || (!open($fd, $mapsFile)));

	my @maps = readFile($fd);
	close($fd);

	return @maps;
}

sub get_libmap(@)
{
	my (@maps) = @_;

	no warnings 'portable';
	my %libs;
	foreach my $line (@maps)
	{
		my ($range, $perms, $offset, $dev, $inode, $path) = split(/\s+/o, $line);
		my ($addr_from, $addr_to) = split(/-/o, $range);

		# need only executable sections
		next
			if($perms !~ /x/o || !$path || $path =~ /^\[\w+\]$/o);

		# unhex
		$addr_from = addr2num($addr_from);
		$addr_to = addr2num($addr_to);

		my $libname = basename($path);
		$libs{$libname} = $addr_from;
		$libs{'-' . $libname} = $path;

		$libs{$addr_from} = $addr_to;
		$libs{$addr_to} = $path;
	}
	# all-in-one hash (not very effective, but who cares here :)
	#	basename	=> addr_from
	#	-basename	=> absolute-path
	#	addr_from	=> addr_to
	#	addr_to		=> absolute-path
	return %libs;
}

sub get_wd($$)
{
	my ($wd, $pid) = @_;

	return $wd || undef
		if($wd || !$pid);

	my $lnk = sprintf("/proc/%d/cwd", $pid);
	return readlink($lnk);
}

sub addr2libname(\%$)
{
	my ($libmap, $addr) = @_;

	foreach my $addr_from (keys(%$libmap))
	{
		my $addr_to = $libmap->{$addr_from};

		# dummy format, filter it
		next
			if($addr_from !~ /^[[:digit:]]+$/o || $addr_to !~ /^[[:digit:]]+$/o);

		# check range
		next
			if($addr <= $addr_from || $addr >= $addr_to);
		
		return ($libmap->{$addr_to}, $addr_from);
	}
	return undef;
}

#
# PUBLIC FUNCTIONS
#

# addr2lime(@executable, @symbols): @resolved_symbols
sub addr2line($@)
{
	my ($exe, @symbols) = @_;

	my @cmd = (@ADDR2LINE, "-f", "-e", $exe, @symbols);

	# XXX: improve this
	my @lines = `@cmd`;
	warn("\t - addr2line filed while executing '@cmd'\n"), return ()
		if($? != 0);

	my %addr;
	my $symIdx = 0;
	chomp(@lines);
	for(my $ii = 0; $ii <= $#lines; $ii++)
	{
		my $line = $lines[$ii];
		my $sym = $symbols[$symIdx];

		# every 1st line is function
		my $function = $line;
		my $location = $lines[++$ii];

		if($function eq "??" && $location eq "??:0")
		{
			$addr{$sym || $symIdx} = undef;
		}
		else
		{
			my ($f, $l) = split(/:/o, $location, 2);

			$addr{$sym || $symIdx} = { function => $function, file => $f, line => $l };
		}

		$symIdx++;
	}

	return %addr;
}

# process($mapsFile, $workingDir, $pid, @symbols): @resolved_symbols
sub process($$$@)
{
	my ($mapsFile, $wd, $pid, @symbols) = @_;

	# get wordkir
	my $cwd = get_wd($wd, $pid);

	# get library map from maps file
	my (@maps, %libs);
	@maps = read_mapsFile($mapsFile, $pid);
	if(@maps)
	{
		%libs = get_libmap(@maps);
		return wantarray ? (undef, "failed to get library maps") : undef
			if(!keys(%libs));
	}

	# parse input
	my @data;
	foreach my $sym (@symbols)
	{
		my $retry = 0;

		chomp($sym);
		push(@data, $sym);	# as fallback return input symbol

		next if($sym eq "[0x0]");
		verbose(1, "SYM_PARSE_FAILED: $sym\n"), next
			if(!($sym =~ /^\s*((.*?)\((.*?)\))?\[(.*?)\]\s*$/o));
		my ($exe, $offset, $addr) = ($2, $3, $4);

		my $libOffset = 0;
		my $addrNum = addr2num($addr);

		if($exe && $exe ne '*')
		{
			my $exeBN = basename($exe);

			# try to find absolute path via maps file
			$exe = $libs{'-' . $exeBN}
				if(exists($libs{'-' . $exeBN}));

			# not asolute path yet (try it with work-dir)
			my $exeOld = $exe;
			if($cwd && $exe !~ /^\//o)
			{
				my $exe2 = $cwd . '/' . $exe;
				$exe2 = Cwd::abs_path($exe2);

				verbose(1, "EXE_ABSPATH_FAILED: $exe\n"), next
					if(!$exe2 || ! -e $exe2);
				$exe = $exe2;
			}

			verbose(1, "EXE_NOT_FOUND: $exeOld\n"), next
				if(!$exe || !-f $exe);

			# need real file not symlink
			$exeOld = $exe;
			while(defined($exe) && -l $exe && $retry++ < 10)
			{
				my $dir = dirname($exe);

				$exeOld = $exe;
				$exe = readlink($exe);

				$exe = $dir . '/' . $exe
					if($dir && $exe && $exe !~ /^\//o);
			}

			verbose(1, "EXE_SYMLINK_FOLLOW_FAILED: $exeOld\n"), next
				if(!defined($exe));

			# get absolute path
			$exe = Cwd::abs_path($exe);

			verbose(1, "EXE_NOT_FOUND: $exe\n"), next
				if(!-f $exe);

			# library offset via basename
			$exeBN = basename($exe);
			$libOffset = $libs{$exeBN}
				if(exists($libs{$exeBN}));
		}
		else
		{
			if($WARNING == 0 && !%libs)
			{
				warn("WARNING: incomplete SYMBOL (without libname), but no --pid or --maps-file provided !\n");
				$WARNING = 1;
			}
		
			($exe, $libOffset) = addr2libname(%libs, $addrNum);

			verbose(1, "ADDR_NOT_MAPPED: $addr\n"), next
				if(!$exe);

			verbose(1, "EXE_NOT_FOUND: $exe\n"), next
				if(!-f $exe);
		}

		# addr offset
		my $addrReal = $addrNum - $libOffset;
		my $addrHex = sprintf("0x%02x", $addrReal);

		# success
		pop(@data);
		push(@data, { exe => $exe, offset => $offset, addr_sym => $addr,
				addr_num => $addrNum, addr_real => $addrReal,
				addr_hex => $addrHex,
				sym => $sym });
	}

	# group translate
	my @lines;
	for(my $ii = 0; $ii <= $#data; $ii++)
	{
		my $rec = $data[$ii];

		push(@lines, $rec), next
			if(!ref($rec) || !exists($rec->{exe}));

		# collect all addr from same exe
		my %translate = ( $rec->{addr_hex} => $ii);
		for(my $oo = $ii; $oo <= $#data; $oo++)
		{
			next
				if(!ref($data[$oo])
					|| !exists($data[$oo]->{exe})
					|| $data[$oo]->{exe} ne $rec->{exe});
			$translate{ $data[$oo]->{addr_hex} } = $oo;
		}

		# translate from exe
		my %translated = addr2line($rec->{exe}, keys(%translate));
		push(@lines, $rec), next
			if(!keys(%translated));

		# update data list
		foreach my $addr (keys(%translated))
		{
			my $idx = $translate{ $addr };
			die("[programming error]")
				if(!defined($idx));

			# not translated
			$data[$idx] = $data[$idx]->{sym}, next
				if(!$translated{ $addr });

			my $sym = $data[$idx]->{sym};
			$data[$idx] = $translated{ $addr };
			$data[$idx]->{sym} = $sym;
		}

		# current record translated
		if(defined($data[$ii]))
		{	push(@lines, $data[$ii]);	}
		else	# resolve failed
		{	push(@lines, $rec->{sym});	}
	}

	return @lines;
}

# checkASLR(): bool
sub checkASLR()
{
	my $enabled = 0;

	# LINUX randomization check
	if(-e $LINUX_ALSR_CONFIG
		&& open(my $fd, $LINUX_ALSR_CONFIG))
	{
		my $value = <$fd>;

		close($fd);

		# value is non-zero
		# 0 - no randomization
		# 1 - randomize libs, stack, mmap, VDSO, heap
		# 2 - also brk()
		$enabled = ($value != 0);
	}
	return $enabled;
}

#
# MAIN
#

sub main(@)
{
	my (@argv) = @_;
	my ($exe, $pid, $mapsFile, $workDir, @symbols, $fullName, $man, $help);

	@ARGV = @argv;
	GetOptions(
		"<>"		=> sub { push(@symbols, "$_[0]"); },
		"p|pid=i"	=> \$pid,
		"m|maps-file=s"	=> \$mapsFile,
		"wd|work-dir=s"	=> \$workDir,
		"full-filename"	=> \$fullName,
		"demangle=s"	=> sub { push(@ADDR2LINE, "--demangle=$_[1]"); },
		"v|verbose"	=> \$VERBOSE,
		"h|?|help"	=> \$help,
		"man"		=> \$man,
	) || pod2usage( -verbose => 0, -exitval => 1 );
	@argv = @ARGV;

	pod2usage( -verbose => 1 )
		if($help);
	pod2usage( -verbose => 3 )
		if($man);

	# read from STDIN or from file
	if(!@symbols || 
		(my $symFile = ($#symbols == 0 && -e $symbols[0])))
	{
		my $fd = \*STDIN;

		die("$0: failed to open symbols file '$symbols[0]' - $!\n")
			if($symFile && $symbols[0] ne "-" && !open($fd, $symbols[0]));

		# read symbols
		@symbols = readFile($fd);

		# read maps
		if(!$mapsFile || ($mapsFile && $mapsFile eq "-"))
		{
			$mapsFile = [];
			@$mapsFile = readFile($fd);
		}

		close($fd);
	}

	# ASLR check
	warn("WARNING: ASLR enabled, but no --pid or --maps-file provided !\n")
		if(checkASLR() && (!$pid && !$mapsFile) && $WARNING >= 0);

	# process symbols now
	my @data = process($mapsFile, $workDir, $pid, @symbols);

	# error
	die("$0: $data[1]\n")
		if(!defined($data[0]));

	foreach my $rec (@data)
	{
		if(ref($rec))
		{
			my $file = $rec->{file};
			$file = basename($file)
				if(!$fullName);

			printf("%s at %s:%s\n", $rec->{function}, $file, $rec->{line});
		}
		else
		{
			printf("%s (TRANSLATE FAILED)\n", $rec);
		}
	}

	return 0;
}

1;

=pod

=head1 NAME

backtrace2line - convert libc() backtrace() output into file names and line numbers.

=head1 SYNOPSIS

backtrace2line [ OPTIONS ] [ I<SYMBOL-1> ...  I<SYMBOL-N> ]

backtrace2line [ OPTIONS ] I<BACKTRACE-FILE>

=head1 DESCRIPTION

This script converts output of backtrace_symbols() or backtrace_symbols_fd() into file names and line numbers.
It also supports conversion of backtraces produces on machines with an active ASLR (Address space layout randomization),
if pid or process memory map is provided (/proc/<pid>/maps).

NOTE: This script can be also used as perl module.

=head1 ARGUMENTS

=over 4

=item I<BACKTRACE-FILE>

Path to backtrace file. This file must contain symbols (one per line) and I<might> contain also content of 
the maps file, divided by line with single dot (see B<EXAMPLES>) or line begging with '=' character.
The same applies if reading backtrace from stdin.

=item I<SYMBOL>

Symbol as generated by backtrace_symbols() function (ie. ./leak-01(main+0x40)[0x7f00ad7a4b00]).

=back

=head1 OPTIONS

=over 4

=item B<-p> I<PID>

=item B<--pid> I<PID>

Pid of a B<still> running process, that generated given backtrace (needed only if ASLR active or
if backtrace sumbol contains no library, like when libunwind is used).
Pid is used to get process memory map (from /proc/<pid>/maps).

=item B<-m> I<MAPS-FILE>

=item B<--maps-file> I<MAPS-FILE>

Path to a file with process memory map of a backtraced process. This is required if ALSR is
active or if backtrace has been generated by libunwind.
Data from maps file is used to identify functions location (IP is checked against mapped range)
or to find full path of the application/libraries from symbol(s).

=item B<-wd> I<WORK-DIR>

=item B<--work-dir> I<WORK-DIR>

Original work or start dir of backtraced process (needed only if backtrace contains relative paths,
and maps file has not been provided).

=item B<--demangle> I<STYLE>

Passes given --demangle I<STYLE> parameter to B<addr2line> when translating symbols.

=item B<-v>

=item B<--verbose>

Enable verbose mode. This can help you to findout why symbols translation failed.

=item B<-h>

=item B<--help>

Print help.

=item B<--man>

Show man page.

=back

=head1 EXAMPLES

	# pass symbols and maps divided by single dot on line
	$ backtrace2line
	./leak-01(main+0x32)[0x7f00ad7a4af2]
	/lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb]
	./leak-01(+0x9a9)[0x7f00ad7a49a9]
	[0x0]
	.
	7f00ac983000-7f00ac999000 r-xp 00000000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
	7f00ac999000-7f00acb98000 ---p 00016000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
	7f00acb98000-7f00acb99000 r--p 00015000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
	7f00acb99000-7f00acb9a000 rw-p 00016000 fe:02 911050                     /usr/lib64/gcc/x86_64-pc-linux-gnu/4.8.3/libgcc_s.so.1
	7f00acb9a000-7f00acb9d000 r-xp 00000000 fe:02 819448                     /lib64/libdl-2.19.so
	7f00acb9d000-7f00acd9c000 ---p 00003000 fe:02 819448                     /lib64/libdl-2.19.so
	7f00acd9c000-7f00acd9d000 r--p 00002000 fe:02 819448                     /lib64/libdl-2.19.so
	7f00acd9d000-7f00acd9e000 rw-p 00003000 fe:02 819448                     /lib64/libdl-2.19.so
	7f00acd9e000-7f00acdb8000 r-xp 00000000 fe:02 818913                     /lib64/libpthread-2.19.so
	7f00acdb8000-7f00acfb8000 ---p 0001a000 fe:02 818913                     /lib64/libpthread-2.19.so
	7f00acfb8000-7f00acfb9000 r--p 0001a000 fe:02 818913                     /lib64/libpthread-2.19.so
	7f00acfb9000-7f00acfba000 rw-p 0001b000 fe:02 818913                     /lib64/libpthread-2.19.so
	7f00acfba000-7f00acfbe000 rw-p 00000000 00:00 0
	7f00acfbe000-7f00ad170000 r-xp 00000000 fe:02 819451                     /lib64/libc-2.19.so
	7f00ad170000-7f00ad370000 ---p 001b2000 fe:02 819451                     /lib64/libc-2.19.so
	7f00ad370000-7f00ad374000 r--p 001b2000 fe:02 819451                     /lib64/libc-2.19.so
	7f00ad374000-7f00ad376000 rw-p 001b6000 fe:02 819451                     /lib64/libc-2.19.so
	7f00ad376000-7f00ad37a000 rw-p 00000000 00:00 0
	7f00ad37a000-7f00ad37e000 r-xp 00000000 fe:02 1101433                    /usr/local/lib64/liblog-malloc2.so.1.0.0
	7f00ad37e000-7f00ad57d000 ---p 00004000 fe:02 1101433                    /usr/local/lib64/liblog-malloc2.so.1.0.0
	7f00ad57d000-7f00ad57e000 r--p 00003000 fe:02 1101433                    /usr/local/lib64/liblog-malloc2.so.1.0.0
	7f00ad57e000-7f00ad57f000 rw-p 00004000 fe:02 1101433                    /usr/local/lib64/liblog-malloc2.so.1.0.0
	7f00ad57f000-7f00ad5a1000 r-xp 00000000 fe:02 818908                     /lib64/ld-2.19.so
	7f00ad79c000-7f00ad7a1000 rw-p 00000000 00:00 0
	7f00ad7a1000-7f00ad7a2000 r--p 00022000 fe:02 818908                     /lib64/ld-2.19.so
	7f00ad7a2000-7f00ad7a3000 rw-p 00023000 fe:02 818908                     /lib64/ld-2.19.so
	7f00ad7a3000-7f00ad7a4000 rw-p 00000000 00:00 0
	7f00ad7a4000-7f00ad7a5000 r-xp 00000000 fe:02 10097                      /home/sam/devel/log-malloc2/examples/leak-01
	7f00ad9a4000-7f00ad9a5000 r--p 00000000 fe:02 10097                      /home/sam/devel/log-malloc2/examples/leak-01
	7f00ad9a5000-7f00ad9a6000 rw-p 00001000 fe:02 10097                      /home/sam/devel/log-malloc2/examples/leak-01
	7f00af5ac000-7f00af5cd000 rw-p 00000000 00:00 0                          [heap]
	7ffffbe14000-7ffffbe36000 rw-p 00000000 00:00 0                          [stack]
	7ffffbef5000-7ffffbef7000 r-xp 00000000 00:00 0                          [vdso]
	ffffffffff600000-ffffffffff601000 r-xp 00000000 00:00 0                  [vsyscall]
	.
	main at leak-01.c:7
	/lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb] (TRANSLATE FAILED)
	_start at ??:?
	[0x0] (TRANSLATE FAILED)

	$ backtrace2line "./leak-01(main+0x32)[0x7f00ad7a4af2]" "/lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb]" -v
	WARNING: ASLR enabled, but no --pid or --maps-file provided !
	EXE_NOT_FOUND: ./leak-01
	./leak-01(main+0x32)[0x7f00ad7a4af2] (TRANSLATE FAILED)
	/lib64/libc.so.6(__libc_start_main+0x11b)[0x7f00acfe31cb] (TRANSLATE FAILED)

=head1 LICENSE

This script is released under GNU GPLv3 License.
See L<http://www.gnu.org/licenses/gpl.html>.

=head1 AUTHOR

Samuel Behan - L<http://devel.dob.sk/log-malloc2/>, L<https://github.com/samsk/log-malloc2>

=head1 SEE ALSO

L<addr2line>, L<log-malloc>

=cut

# EOF
