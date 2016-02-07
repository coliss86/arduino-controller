#!/usr/bin/perl -w
# read the temperature from the arduino and stores it in the influxdb

use strict;
use warnings;
use Date::Manip;
use Device::SerialPort;
use Config::Simple;
use Time::HiRes;
use InfluxDB;

my $temperature_dir = "/var/lib/temperature/";
my $status = "";
# debug
my $debug = 0;
# output
my $output = 0;
# maximum number of loop to wait for locks to free
my $nb_max_locks_wait = 6;
# locks
my $filenames_lock = "/var/run/arduino/arduino_lock.";
my $filename_lock = "$filenames_lock$$";

if ($#ARGV == 0) {
    if ( $ARGV[0] eq "-d" ) {
        $debug = 1;
    } elsif ( $ARGV[0] eq "-o" ) {
        $output = 1;
    } else {
        print "$0 usage:
            -d : debug output\n";
        exit 1;
    }
}

$SIG{'INT'} = \&signal_handler;
$SIG{'TERM'} = \&signal_handler;

check_lock($filename_lock, $filenames_lock, 1);

my $cfg = new Config::Simple("/etc/arduino.cfg") or die_cleanly(Config::Simple->error());
my $portName = $cfg->param('tty');
die_cleanly("port not defined") unless (defined $portName);

my @SENSORS = ();
my @SENSORS_NAMES = ();
my $i = 0;
for ($i = 1; $i < 6; $i++) {
    our ($rom, $name) = split(':',$cfg->param('sensor' . $i));
    $rom =~ s/^\s+|\s+$//g;
    push (@SENSORS, $rom);
    $name =~ s/^\s+|\s+$//g;
    push (@SENSORS_NAMES, $name);
}

# Set up the serial port
my $port = Device::SerialPort->new($portName) || die_cleanly("Can't open $portName: $!");
$port->databits(8);
$port->baudrate(9600);
$port->parity("none");
$port->stopbits(1);
# disable reset of the arduino when connecting to it
#$port->dtr_active(0); 

my $start = [ Time::HiRes::gettimeofday( ) ];
my $prev = "";
my $found = 0;
my $st = "";
my %data = ();
my $cpt = 0;

empty_serial_buffer();

# send command
$port->write("t\n");

$found = 0;
while ( !$found && Time::HiRes::tv_interval( $start ) < 10 ) {
    my $byte = $port->read(1);
    if ($byte eq "") {
        next;
    }
# \n reached ?
    if ( ord $byte ==  '10') { 
        log_debug("$st");
        if ($st eq "   \r") {
            $found = 1;
        }
        if ($st =~ m/ROM = \[ (.*) \].*: (.*) .*/) {
            $data{$1} = $2;
            log_debug("$1 : $2");
        }

        $st = "";
    } else {
        $st .= $byte;
    }
}

die_cleanly("ERROR : No data read") unless ($found);

empty_serial_buffer();
$port->close;

my $tempbuf = temp_to_buff(%data);

if ($output) {
    print_temp($tempbuf);
} elsif (!$debug) {
    update_influxdb(%data);
    log_temp($tempbuf);
} else {
    print "not updating database\n";
}

exit_cleanly(0);

#####################
# log debug
sub log_debug {
    my $msg = shift;
    print "$msg\n" unless (!$debug);
}

#####################
# empty the serial buffer by reading all data available
sub empty_serial_buffer {
    my $cpt = 0;
    while ($cpt < 200) {
        $port->lookfor;
        $cpt++;
    }
}

#####################
# Print help message
# @param $program_name
sub print_usage {
    print "$0\n
            -d : debug output\n";

    exit (1);
}

#####################
# return a string with temperature
# @param %data
sub temp_to_buff {
    my @data = shift;

    my $string;
    my $i = 0;
    for ($i = 0; $i < 5; $i++) {
        if (defined $data{$SENSORS[$i]}) {
            $string .= $SENSORS_NAMES[$i] . "=".$data{$SENSORS[$i]}." - ";
        }
    }
    return $string;
}

#####################
# log temperature to file
# @param @string
sub log_temp {
    my $string = shift;

    my $date = UnixDate(ParseDate('today'), "%Y-%m-%d");
    my $file = $temperature_dir."temperature.".$date.".log";
    open (MYFILE, ">>$file");
    my $ts = UnixDate(ParseDate('now'), "%Y-%m-%d %H:%M:%S");
    print MYFILE "$ts : $string\n";
    close (MYFILE);
}

#####################
# update influxdb
# @param %data
sub update_influxdb {
    my @data = shift;

    my @points = ();
    my @columns = ();
    my $i = 0;
    for ($i = 0; $i < 5; $i++) {
        if (defined $data{$SENSORS[$i]}) {
            push(@points, $data{$SENSORS[$i]});
            push(@columns, $SENSORS_NAMES[$i]);
        }
    }

    my $ix = InfluxDB->new(
        host     => '127.0.0.1',
        port     => 8086,
        username => 'root',
        password => 'root',
        database => 'temperature',
    );
 
    $ix->write_points(
        data => {
            name    => "temperature",
            columns => [@columns],
            points  => [[@points]],
        },
    ) or die_cleanly("influx db error : " . $ix->errstr);
}

#####################
# print temperature 
# @param @string
sub print_temp {
    my $string = shift;
    $string =~ s/ - /\n/g;
    print $string;
}

#####################
# check if the number of lock is superior to the specified number and sleep
# if so, otherwise returns
# @param $lock : the lock to create 
# @param $locks : directory containing locks + basename of locks
# @param $nb_max_locks : max number of locks
sub check_lock {
    my $lock = shift;
    my $locks = shift;
    my $nb_max_locks = shift;

    my $cpt = 0;
    my $is_locked = 0;
    do {
        $is_locked = 0;
        exec_cmd("touch $lock");

        my @markers = <$locks*>;
        if ($#markers >= $nb_max_locks) {
            log_debug("Ceil[$nb_max_locks] of locks reached : $locks => sleeping.");
            delete_file($lock);
            sleep(20);
            $is_locked = 1;
            $cpt++;
        }
    } while ($is_locked && $cpt < $nb_max_locks_wait);

    if ($cpt >= $nb_max_locks_wait) {
        die_cleanly("Waited to long for locks $locks to free");
    }
}

#####################
# execute the specified command 
# exit cleanly if something wrong happened
# @param $cmd : command to execute
sub exec_cmd {
    my $cmd = shift;
    log_debug("cmd : $cmd");
    my $ret = system( $cmd );
    if ($ret != 0) {
        die_cleanly("command failed : '$cmd' : $!");
    }
}

#####################
# Print an error message, delete temp file, close the connection to the
# database, remove the lock if it exits and exit
# @param $msg : error to display
sub die_cleanly {
    my $msg = shift;
    printf("### ERROR : $msg\n");
    exit_cleanly(1);
}

#####################
# Delete temp file, close the connection to the database, remove the lock if it
# exits and exit with the specified code
# @param $exit_status : exit code
sub exit_cleanly {
    my $exit_status = shift;

# remove the lock file
    unlink($filename_lock) if (-f $filename_lock);

    if ($exit_status == 0) {
        log_debug("#### Finished ####");
    }
    exit($exit_status);
}

#####################
# Delete file. If an error occurs, call die_cleanly
# @param $file
sub delete_file {
    my $file = shift;
    if (-f $file) {
        unlink($file) or die_cleanly("Cannot remove file '$file' : $!");
    }
}

#####################
# signal handler
sub signal_handler {
    my $signame = shift;
    die_cleanly("Signal [$signame] catched");
}

