#!/bin/bash
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Mozilla code.
#
# The Initial Developer of the Original Code is the Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2012
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

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
