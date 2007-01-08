#!/usr/local/bin/perl
#
#   $Id: 40bindparam.t 8518 2007-01-06 20:48:33Z capttofu $ 
#
#   This is a skeleton test. For writing new tests, take this file
#   and modify/extend it.
#

$^W = 1;


#
#   Make -w happy
#
$test_dsn = '';
$test_user = '';
$test_password = '';
$sql_mode_feature=1;


#
#   Include lib.pl
#
use DBI ();
use vars qw($COL_NULLABLE);
$mdriver = "";
foreach $file ("lib.pl", "t/lib.pl") {
    do $file; if ($@) { print STDERR "Error while executing lib.pl: $@\n";
			   exit 10;
		      }
    if ($mdriver ne '') {
	last;
    }
}
if ($mdriver eq 'pNET') {
    print "1..0\n";
    exit 0;
}

sub ServerError() {
    my $err = $DBI::errstr;  # Hate -w ...
    print STDERR ("Cannot connect: ", $DBI::errstr, "\n",
	"\tEither your server is not up and running or you have no\n",
	"\tpermissions for acessing the DSN $test_dsn.\n",
	"\tThis test requires a running server and write permissions.\n",
	"\tPlease make sure your server is running and you have\n",
	"\tpermissions, then retry.\n");
    exit 10;
}

if (!defined(&SQL_VARCHAR)) {
    eval "sub SQL_VARCHAR { 12 }";
}
if (!defined(&SQL_INTEGER)) {
    eval "sub SQL_INTEGER { 4 }";
}
$dbh = DBI->connect($test_dsn, $test_user, $test_password,
  { RaiseError => 1, AutoCommit => 1}) or ServerError() ;

$sth= $dbh->prepare("select version()") or
  DbiError($dbh->err, $dbh->errstr);

$sth->execute() or 
  DbiError($dbh->err, $dbh->errstr);

$row= $sth->fetchrow_arrayref() or
  DbiError($dbh->err, $dbh->errstr);

# 
# DROP/CREATE PROCEDURE will give syntax error 
# for these versions
#
if ($row->[0] =~ /^4\.0/ || $row->[0] =~ /^3/)
{
  $sql_mode_feature= 0;
}

#
#   Main loop; leave this untouched, put tests after creating
#   the new table.
#
while (Testing()) {
    #
    #   Connect to the database
    Test($state or ($dbh = DBI->connect($test_dsn, $test_user,
					$test_password, {mysql_enable_utf8 => 1})))
	   or ServerError();

    #
    #   Find a possible new table name
    #
    Test($state or $table = FindNewTable($dbh))
	   or DbiError($dbh->err, $dbh->errstr);

    #
    #   Create a new table; EDIT THIS!
    #
    Test($state or ($def = TableDefinition($table,
					   ["id",   "INTEGER",  4, 0],
					   ["name", "CHAR",    64, $COL_NULLABLE]),
		    $dbh->do($def)))
	   or DbiError($dbh->err, $dbh->errstr);



    Test($state or $sth = $dbh->prepare("INSERT INTO $table"
	                                   . " VALUES (?, ?)"))
	   or DbiError($dbh->err, $dbh->errstr);

    #
    #   Insert some rows
    #

    # Automatic type detection
    my $numericVal = 1;
    my $charVal = "Alligator Descartes";
    Test($state or $sth->execute($numericVal, $charVal))
	   or DbiError($dbh->err, $dbh->errstr);

    # Does the driver remember the automatically detected type?
    Test($state or $sth->execute("3", "Jochen Wiedmann"))
	   or DbiError($dbh->err, $dbh->errstr);
    $numericVal = 2;
    $charVal = "Tim Bunce";
    Test($state or $sth->execute($numericVal, $charVal))
	   or DbiError($dbh->err, $dbh->errstr);

    # Now try the explicit type settings
    Test($state or $sth->bind_param(1, " 4", SQL_INTEGER()))
	or DbiError($dbh->err, $dbh->errstr);
    # umlaut equivelant is vowel followed by 'e'
    Test($state or $sth->bind_param(2, 'Andreas Koenig'))
	or DbiError($dbh->err, $dbh->errstr);
    Test($state or $sth->execute)
	   or DbiError($dbh->err, $dbh->errstr);

    # Works undef -> NULL?
    Test($state or $sth->bind_param(1, 5, SQL_INTEGER()))
	or DbiError($dbh->err, $dbh->errstr);
    Test($state or $sth->bind_param(2, undef))
	or DbiError($dbh->err, $dbh->errstr);

    Test($state or $sth->execute)
 	or DbiError($dbh->err, $dbh->errstr);

    # Test binding negative numbers [rt.cpan.org #18976]
    Test($state or $sth->bind_param(1, undef, SQL_INTEGER()))
      or DbiError($dbh->err, $dbh->errstr);
    Test($state or $sth->bind_param(2, undef))
      or DbiError($dbh->err, $dbh->errstr);
    Test($state or $sth->execute(-1, "abc"))
      or DbiError($dbh->err, $dbh->errstr);

    Test($state or undef $sth  ||  1);

    #
    #   Try various mixes of question marks, single and double quotes
    #
    Test($state or $dbh->do("INSERT INTO $table VALUES (6, '?')"))
	   or DbiError($dbh->err, $dbh->errstr);
    if ($mdriver eq 'mysql' or $mdriver eq 'mysqlEmb') {
        ($state or ! $sql_mode_feature) or $dbh->do('SET @old_sql_mode = @@sql_mode, @@sql_mode = \'\'');
	Test(($state or !$sql_mode_feature) or ($sql_mode_feature and $dbh->do("INSERT INTO $table VALUES (7, \"?\")")))
	    or DbiError($dbh->err, $dbh->errstr);
        ($state or ! $sql_mode_feature)  or ($sql_mode_feature and $dbh->do('SET @@sql_mode = @old_sql_mode'));
    }

    #
    #   And now retreive the rows using bind_columns
    #
    Test($state or $sth = $dbh->prepare("SELECT * FROM $table"
					   . " ORDER BY id"))
	   or DbiError($dbh->err, $dbh->errstr);

    Test($state or $sth->execute)
	   or DbiError($dbh->err, $dbh->errstr);

    Test($state or $sth->bind_columns(undef, \$id, \$name))
	   or DbiError($dbh->err, $dbh->errstr);

    Test($state or (($ref = $sth->fetch)  &&  $id == -1  &&
		   $name eq 'abc'))
	or print("Query returned id = $id, name = $name, expected -1,abc\n");

    Test($state or ($ref = $sth->fetch)  &&  $id == 1  &&
	 $name eq 'Alligator Descartes')
	or printf("Query returned id = %s, name = %s, ref = %s, %d\n",
		  $id, $name, $ref, scalar(@$ref));

    Test($state or (($ref = $sth->fetch)  &&  $id == 2  &&
		    $name eq 'Tim Bunce'))
	or printf("Query returned id = %s, name = %s, ref = %s, %d\n",
		  $id, $name, $ref, scalar(@$ref));

    Test($state or (($ref = $sth->fetch)  &&  $id == 3  &&
		    $name eq 'Jochen Wiedmann'))
	or printf("Query returned id = %s, name = %s, ref = %s, %d\n",
		  $id, $name, $ref, scalar(@$ref));

    Test($state or (($ref = $sth->fetch)  &&  $id == 4  &&
		    $name eq 'Andreas Koenig'))
	or printf("Query returned id = %s, name = %s, ref = %s, %d\n",
		  $id, $name, $ref, scalar(@$ref));
    Test($state or (($ref = $sth->fetch)  &&  $id == 5  &&
		    !defined($name)))
	or printf("Query returned id = %s, name = %s, ref = %s, %d\n",
		  $id, $name, $ref, scalar(@$ref));

    Test($state or (($ref = $sth->fetch)  &&  $id == 6  &&
		   $name eq '?'))
	or print("Query returned id = $id, name = $name, expected 6,?\n");

    Test(($state || !$sql_mode_feature) or (($ref = $sth->fetch)  &&  $id == 7  &&
          $name eq '?'))
      or print("Query returned id = $id, name = $name, expected 7,?\n");
    #
    #   Finally drop the test table.
    #
    Test($state or $dbh->do("DROP TABLE $table"))
	   or DbiError($dbh->err, $dbh->errstr);

    Test($state or undef $sth  or  1);

}
