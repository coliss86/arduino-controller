#!/usr/bin/perl -w
# activate / deactivate pin
# doesn't handle the port type (relay) as the arduino take care of it
# when the arduino starts, all pin are low but for relay they're high

use strict;
use warnings;
use Device::SerialPort;
use Config::Simple;
use File::Basename;
use Time::HiRes;

my $status = "";
my $debug = 0;
# maximum number of loop to wait for locks to free
my $nb_max_locks_wait = 6;
# locks
my $filenames_lock = "/var/run/arduino/arduino_lock.";
my $filename_lock = "$filenames_lock$$";

if (scalar(@ARGV) == 0 || $ARGV[0] eq "-help" || $ARGV[0] eq "--help") {
    print_usage($0);
}

$SIG{'INT'} = \&signal_handler;
$SIG{'TERM'} = \&signal_handler;

check_lock($filename_lock, $filenames_lock, 1);

my $cfg = new Config::Simple("/etc/arduino.cfg") or die_cleanly(Config::Simple->error());
my $portName = $cfg->param('tty');
die_cleanly("port not defined") unless (defined $portName);

my $arg = 0;
if (scalar(@ARGV) == 4 || scalar(@ARGV) == 3) {
    $arg++;
    $portName = $ARGV[$arg];
    $arg++;
}

my $pin = 0;
my $state = "";
if ($ARGV[$arg] eq "status") {
    $status = "status";
    print "Status\n";
} else {
    $pin = $ARGV[$arg];
    $arg++;
    $state = $ARGV[$arg];

    if ($pin =~ /^-?\d+\z/) {
# $pin is an integer
        if ($pin < $cfg->param('port_start') || $pin > $cfg->param('port_end')) {
            print "Pin out of range : [".$cfg->param('port_start')." - ".$cfg->param('port_end')."]\n";
            print_usage($0);
        }
    } else {
# $pin is a pin name
        my $i = 0;
        my $found = 0;
        for ($i = $cfg->param('port_start'); $i <= $cfg->param('port_end'); $i++) {
            my $value = $cfg->param("port$i");
            if (defined $value && $value eq $pin) {
                $pin = $i;
                $found = 1;
                last;
            }
        }
        if ($found == 0) {
            print "unknown pin name : $pin\n";
            exit_cleanly(1);
        }
    }

    if ($state ne 'high' && $state ne 'low' && $state ne 'off' && $state ne 'on' && $state ne 'toggle') {
        print_usage($0);
    }
}

#print "pin $pin\n"; 

# Set up the serial port
# 19200, 81N on the USB ftdi driver
my $port = Device::SerialPort->new($portName) || die_cleanly("Can't open $portName: $!");
$port->databits(8);
$port->baudrate(9600);
$port->parity("none");
$port->stopbits(1);

my $start = [ Time::HiRes::gettimeofday( ) ];

# workaround : few iteration are made if needed, sometimes, the status doesn't get well transmit
# the begin or end is missing, so asking it again seems to solve the problem.
my $prev = "";
my $found = 0;
my $cpt = 0;
do {
    empty_serial_buffer();

    $port->write("s\n");
    my $st = "";
    while ( Time::HiRes::tv_interval( $start ) < 0.4 ) {
        my $byte = $port->read(1);
        if ($byte eq "") {
            next;
        }
# \n reached ?
        if ( ord $byte ==  '10') { 
            if ($status eq "status" && $st =~ m/.* => (\d)( \(R\))?/) {
                print "$st\n";
            }
# format expected :   2 => 0
            if ($st =~ m/.*$pin => (\d)( \(R\))?/) {
                $prev = $1;
                $found = 1;
                last;
            }
            $st = "";
        } else {
            $st .= $byte;
        }
    }
    $cpt++;
} while ($status ne "status" && $found == 0 && $cpt < 2);

empty_serial_buffer();
if ($status eq "status") {
    $port->close;
    exit_cleanly(0);
}

if ($found == 0) {
    $port->close;
    die_cleanly("Pin number $pin not found in the arduino");
}

my $value;
if ($state eq 'toggle') {
    if ($prev eq "0") {
        $value = "1";
    } else {
        $value = "0";
    }
} else {
    if ($state eq 'high' || $state eq 'on') {
        $value = "1";
    } else {
        $value = "0";
    }
}

$value = "$pin=$value";

$port->write("$value\n");

empty_serial_buffer();


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
    print "$0 [-p dev] (<pin> <state> | status)
        <dev>   - device [/dev/ttyACM0]
        status  - retrieve the current status of the outputs
        <pin>   - pin number
        <action> - low|off / high|on / toggle\n";

    exit_cleanly(1);
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
            sleep(5);
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

    $port->close if (defined $port);

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

