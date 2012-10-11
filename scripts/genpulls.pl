#!/usr/bin/perl
use strict;
use MIME::Lite;
use Getopt::Long qw(:config no_auto_abbrev);

#
# Script to create threaded series of pull requests out of
# list of signed tags in the $pull_requests_config.
#
# Written by Tony Lindgren <tony@atomide.com>
# Contributions by Jason Cooper <jason@lakedaemon.net>
#
# Significant portions wholeheartedly copied from scripts/get_maintainer.pl by
# Joe Perches.
#
# Licensed under the terms of the GNU GPL License version 2
#
my $V = "0.1";

my $from = "";
my $to = "";
my $cc = "";

my $smtp_host = "";
my $user = "";
my $pass = "";

#
# Format for the config file separated by tabs:
# startcommmit	signed-tag	"mail subject line description"
# ...
#
my $pull_requests_config = $ENV{"HOME"}."/.pull-requests";
my $config_fn = $ENV{"HOME"}."/.genpullrqs.conf";

#
# Local Linux git tree
#
my $git_dir = "";

#
# Remote Linux git tree
#
my $remotetree = "";

#
# Output for test mails in mbox format
#
my $mbox = "";

my @pull_requests;

my $send_mail = 0;
my $help = 0;

sub print_usage() {
	print <<EOT;
Usage: $0 [options]
Version: $V

Options:
	--from		"email"		Sender of this mess
	--to		"email"		Receiver(s) of this mess
	--cc		"email"		Receiver(s) of copies of this mess
	--smtp-server	"host[:port]"	Your SMTP server
	--smtp-user	"name"		Username for SMTP server
	--smtp-pass	"pass"		Password for SMTP server
	-s|--src	"path"		Path to Linux git tree
	-r|--remote	"git uri"	Public git tree others can read
	-c|--config	"file"		config [$config_fn]
	-f|--file	"file"		list of pullrqs for this run
	--really-send			Don't dry-run it, really send
	-h|--help|--usage		duh.

EOT
}

sub add_message($ $) {
	my ($subject, $text) = @_;

	my $msg = MIME::Lite->new(
		From     => $from,
		To       => $to,
		Cc       => $cc,
		Subject  => $subject,
		Type     => 'TEXT',
		Encoding => '7bit',
		Data	 => $text,
	);
	$msg->attr('content-type.charset' => 'UTF-8');

	push(@pull_requests, $msg);
}

sub generate_pull_request($ $) {
	my ($from, $to) = @_;
	my $pull_req = "";

	chdir($git_dir) or die("Could not change to source dir: $!\n");

	open(CMD, "git request-pull $from $remotetree $to|") or
	    die("Could not run: $!\n");
	while(<CMD>) {
		$pull_req .= $_;
	}
	close(CMD);

	return $pull_req;
}

sub parse_pull_requests() {

	open(IN, "<$pull_requests_config") or die("Could not open config: $!\n");
	while(<IN>) {
		chomp();
		if (/^#/) {
			next;
		}
		if ($_ eq "") {
			next;
		}

		s/\t+/\t/g;

		my ($from, $to, $subject, $ignore) = split("\t", $_);
		$subject =~ s/\"+//g;
		$subject = "[GIT PULL] ".$subject;

		my $content = generate_pull_request($from, $to);

		add_message($subject, $content);
	}
	close(IN);

}

sub number_messages() {
	my $nr_msg = 0;
	my $i = 1;
	my $message_id = sprintf("<pull-%i-%i>", time(), rand(1000000));

	foreach my $msg (@pull_requests) {
		$nr_msg++;
	}

	foreach my $msg (@pull_requests) {
		my $index = sprintf("%i/%i", $i, $nr_msg);
		my $subject = $msg->get("Subject");
		$subject =~ s/GIT PULL/GIT PULL $index/;
		$msg->replace("Subject", $subject);
		if ($i == 1) {
			$msg->add("Message-ID", $message_id);
		} else {
			$msg->add("In-Reply-To", $message_id);
		}
		$i++;
	}
}

sub print_mbox() {
	open(MBOX, ">>$mbox") or die("Could not open mbox: $!\n");
	printf("Printing messages to mbox at %s...\n", $mbox);
	foreach my $msg (@pull_requests) {
		my $date = $msg->get("Date");
		#$date =~ s/\n//;
		$date =~ s/, / /;
		my($day, $dom, $month, $year, $time, $tz) = split(" ", $date);
		$date = sprintf("From nobody %s %s %s %s %s",
			$day, $month, $dom, $time, $year);
		printf(MBOX "%s\n", $date);
		$msg->print(\*MBOX);
	}
	close(MBOX);
}

sub send_email() {
	printf("Sending messages...\n");
	foreach my $msg (@pull_requests) {
		if ($user =~ m//) {
			$msg->send("smtp", $smtp_host);
		} else {
			$msg->send("smtp", $smtp_host,
				AuthUser=>$user, AuthPass=>$pass);
		}
	}

}

#
# Main program
#
if (-f $config_fn) {
	my @conf_args;
	open(my $conffile, '<', "$config_fn")
		or warn "$0: $config_fn: $!\n";

	while (<$conffile>) {
		my $line = $_;

		$line =~ s/\s*\n?$//g;
		$line =~ s/^\s*//g;
		$line =~ s/\s+/ /g;

		next if ($line =~ m/^\s*#/);
		next if ($line =~ m/^\s*$/);

		my ($opt, $arg) = split(" ", $line, 2);
		push (@conf_args, $opt);
		push (@conf_args, $arg);
	}
	close($conffile);
	unshift(@ARGV, @conf_args) if @conf_args;
}

if (!GetOptions(
		'from=s' => \$from,
		'to=s' => \$to,
		'cc=s' => \$cc,
		'smtp-server=s' => \$smtp_host,
		'smtp-user=s' => \$user,
		'smtp-pass=s' => \$pass,
		's|src=s' => \$git_dir,
		'r|remote=s' => \$remotetree,
		'c|config=s' => \$config_fn,
		'f|file=s' => \$pull_requests_config,
		'really-send' => \$send_mail,
		'h|help|usage' => \$help,
		)) {
	die "$0: invalid argument - use --help if necessary\n";
}

if ($help != 0) {
	print_usage();
	exit 0;
}

parse_pull_requests();
number_messages();
if ($send_mail) {
	send_email();
} else {
	$mbox=`mktemp /tmp/mbox-XXXXXXXX`;
	print_mbox();
	printf("Dry run complete, emails in $mbox\n");
}
