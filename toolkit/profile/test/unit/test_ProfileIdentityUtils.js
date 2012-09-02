/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the functions located directly in the "ProfileIdentity" object.
 */

function run_test()
{
  do_get_profile();

  run_next_test();
}

add_test(function testExample() {
  do_check_true(ProfileIdentityUtils.localIdentities[0] == "test@example.com");
  ProfileIdentityUtils.example();
  run_next_test();
});
