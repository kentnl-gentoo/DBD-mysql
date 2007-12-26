#!/usr/bin/perl

my $foo;
open(DATA, "<test_data.txt") or print "$! can't open test_data.txt!\n";
print "testing...\n";
{
  local undef $/; 
  $foo=<DATA>;
} 
close(DATA);
print "foo $foo\n";
