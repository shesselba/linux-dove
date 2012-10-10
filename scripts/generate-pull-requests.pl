#!/usr/bin/perl
use strict;
use MIME::Lite;

#
# Script to create threaded series of pull requests out of
# list of signed tags in the $pull_requests_config. Search
# for asdf in the script to configure it.
#
# Written by Tony Lindgren <tony@atomide.com>
# Licensed under whatever you prefer.
#

my $from = "Firstname Lastname <asdf\@asdf.asdf>";
my $to = "Firstname Lastname <asdf\@asdf.asdf>, Firstname Lastname <asdf\@asdf.asdf>";
my $cc = "some-mailing-list\@lists.infradead.org";

my $smtp_host = "asdf:25";
my $user = "asdf";
my $pass = "asdf";

#
# Format for the config file separated by tabs:
# startcommmit	signed-tag	"mail subject line description"
# ...
#
my $pull_requests_config = $ENV{"HOME"}."/.pull-requests";

#
# Local Linux git tree
#
my $git_dir = $ENV{"HOME"}."/src/linux-2.6";

#
# Remote Linux git tree
#
my $remotetree = "git://git.kernel.org/pub/scm/linux/kernel/git/asdf/asdf";

#
# Output for test mails in mbox format
#
my $mbox = "";

my @pull_requests;

my $send_mail = 0;

sub print_usage() {
	print("Usage: $0 read the script for options\n");
	exit 1;
}

sub parse_args() {
	if (@ARGV[0] eq "--help") {
		print_usage();
	}

	if (@ARGV[0] =~ /--really-send/) {
		$send_mail = 1;
	}

	if ( -f $mbox ) {
		printf("error: mbox already exists: %s\n", $mbox);
		exit 2;
	}
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
		$msg->send("smtp", $smtp_host,
		     AuthUser=>$user, AuthPass=>$pass);
	}

}

#
# Main program
#
parse_args();
parse_pull_requests();
number_messages();
if ($send_mail) {
	send_email();
} else {
	$mbox=`mktemp /tmp/mbox-XXXXXXXX`;
	print_mbox();
	printf("Dry run complete, emails in $mbox\n");
}
