use strict;
use warnings;

use Test::More;
use DBI;
use DBI::Const::GetInfoType;
$|= 1;

my $rows = 0;
my $sth;
my $testInsertVals;
use vars qw($test_dsn $test_user $test_password);
use lib 't', '.';
require 'lib.pl';

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_password,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};
if ($@) {
    plan skip_all => "no database connection";
}
plan tests => 111;

ok(defined $dbh, "Connected to database");

ok($dbh->do("DROP TABLE IF EXISTS dbd_mysql_t35"), "making slate clean");

ok($dbh->do("CREATE TABLE dbd_mysql_t35 (id INT(4), name VARCHAR(64))"), "creating table");

ok(($sth = $dbh->prepare("INSERT INTO dbd_mysql_t35 VALUES (?,?)")));

print "PERL testing insertion of values from previous prepare of insert statement:\n";
for (my $i = 0 ; $i < 100; $i++) {
  my @chars = grep !/[0O1Iil]/, 0..9, 'A'..'Z', 'a'..'z';
  my $random_chars = join '', map { $chars[rand @chars] } 0 .. 16;
# save these values for later testing
  $testInsertVals->{$i} = $random_chars;
  ok(($rows = $sth->execute($i, $random_chars)));
}
print "PERL rows : " . $rows . "\n";

print "PERL testing prepare of select statement with LIMIT placeholders:\n";
ok($sth = $dbh->prepare("SELECT * FROM dbd_mysql_t35 LIMIT ?, ?"));

print "PERL testing exec of bind vars for LIMIT\n";
ok($sth->execute(20, 50));

my ($row, $errstr, $array_ref);
ok( (defined($array_ref = $sth->fetchall_arrayref) &&
  (!defined($errstr = $sth->errstr) || $sth->errstr eq '')));

ok(@$array_ref == 50);

ok($sth->finish);

ok($dbh->do("DROP TABLE dbd_mysql_t35"));

ok($dbh->disconnect);
