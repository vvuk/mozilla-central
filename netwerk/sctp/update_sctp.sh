#!/bin/bash
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# assume $1 is the directory with a CVS checkout of the libsctp source
#
# sctp usrlib source is available (via cvs) at:
# :ext:anoncvs@stewart.chicago.il.us:/usr/sctpCVS
# password: sctp
#
# also assumes we've made *NO* changes to the SCTP sources!  If we do, we have to merge by
# hand after this process, or use a more complex on.
if [ "$1" ] ; then
  cp $1/KERN/usrsctp/usrsctplib/*.c $1/KERN/usrsctp/usrsctplib/*.h netwerk/sctp/src
  cp $1/KERN/usrsctp/usrsctplib/netinet/*.c $1/KERN/usrsctp/usrsctplib/netinet/*.h netwerk/sctp/src/netinet
  cp $1/KERN/usrsctp/usrsctplib/netinet6/*.c $1/KERN/usrsctp/usrsctplib/netinet6/*.h netwerk/sctp/src/netinet6

  hg addremove netwerk/sctp/src --include "**.c" --include "**.h" --similarity 90

  export date=`date`
  echo "sctp updated from $1 on $date" >> netwerk/sctp/sctp_update.log
else
  echo "usage: $0 sctp_directory"
fi
