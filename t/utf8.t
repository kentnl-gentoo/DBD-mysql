#!/usr/local/bin/perl
#
#   $Id$
#
#   This checks for UTF-8 support.
#


#
#   Make -w happy
#
use vars qw($test_dsn $test_user $test_password $mdriver $verbose $state
	    $dbdriver);
use vars qw($COL_NULLABLE $COL_KEY);
$test_dsn = '';
$test_user = '';
$test_password = '';


#
#   Include lib.pl
#
use DBI;
use strict;
$mdriver = "";
{
    my $file;
    foreach $file ("lib.pl", "t/lib.pl") {
	do $file; if ($@) { print STDERR "Error while executing lib.pl: $@\n";
			    exit 10;
			}
	if ($mdriver ne '') {
	    last;
	}
    }
}

sub ServerError() {
    print STDERR ("Cannot connect: ", $DBI::errstr, "\n",
	"\tEither your server is not up and running or you have no\n",
	"\tpermissions for acessing the DSN $test_dsn.\n",
	"\tThis test requires a running server and write permissions.\n",
	"\tPlease make sure your server is running and you have\n",
	"\tpermissions, then retry.\n");
    exit 10;
}

#
#   Main loop; leave this untouched, put tests after creating
#   the new table.
#
while (Testing()) {
    my ($dbh, $sth, $query);

    #
    #   Connect to the database
    Test($state or ($dbh = DBI->connect($test_dsn, $test_user,
					$test_password, {mysql_enable_utf8 => 1})))
	   or ServerError();
	
	# Test($state or ($dbh->do("SET NAMES UTF8")))
	#     or ErrMsg( "Couldn't set connection to UTF-8 mode\n" );

    #
    #   Find a possible new table name
    #
    my $table = '';
    Test($state or $table = FindNewTable($dbh))
	   or ErrMsgF("Cannot determine a legal table name: Error %s.\n",
		      $dbh->errstr);

    #
    #   Create a new table; In an ideal world, it'd be more sensible to
    #   make the whole database UTF8...
    #
    $query = "CREATE TABLE $table (name VARCHAR(64)) CHARACTER SET utf8";
    Test($state or $dbh->do($query))
    	or ErrMsgF("Cannot create table: Error %s.\n", $dbh->errstr);


    #
    #   and here's the right place for inserting new tests:
    #

    my $utf8_str        = "\x{0100}dam";     # "Adam" with a macron.
    my $quoted_utf8_str = "'\x{0100}dam'";
    Test( $state or ( $dbh->quote( $utf8_str ) eq $quoted_utf8_str ) )
      or ErrMsg( "Failed to retain UTF-8 flag when quoting.\n" );

    Test( $state or ( $dbh->{ mysql_enable_utf8 } ) )
      or ErrMsg( "mysql_enable_utf8 didn't survive connect()\n" );

    $query = qq{INSERT INTO $table (name) VALUES (?)};
    Test( $state or $dbh->do( $query, {}, $utf8_str ) )
      or ErrMsgF( "INSERT failed: query $query, error %s.\n", $dbh->errstr );

    $query = "SELECT name FROM $table LIMIT 1";
    Test( $state or ($sth = $dbh->prepare( $query ) ) )
      or ErrMsgF( "prepare failed: query $query, error %s.\n", $dbh->errstr );

    Test($state or $sth->execute)
      or ErrMsgF( "execute failed: query $query, error %s.\n", $dbh->errstr );

    my $ref;
    Test( $state or defined( $ref = $sth->fetchrow_arrayref ) )
      or ErrMsgF( "fetch failed: query $query, error %s.\n", $sth->errstr );

    # Finally, check that we got back UTF-8 correctly.
    Test( $state or ($ref->[0] eq $utf8_str) )
      or ErrMsgF( "got back '$ref->[0]' instead of '$utf8_str'.\n" );

    Test( $state or $sth->finish )
      or ErrMsgF( "Cannot finish: %s.\n", $sth->errstr );

    #
    #   Finally drop the test table.
    #
    Test($state or $dbh->do("DROP TABLE $table"))
	   or ErrMsgF("Cannot DROP test table $table: %s.\n",
		      $dbh->errstr);

    #   ... and disconnect
    Test($state or $dbh->disconnect)
	or ErrMsgF("Cannot disconnect: %s.\n", $dbh->errmsg);
}
