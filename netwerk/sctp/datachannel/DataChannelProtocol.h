/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2012
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef NETWERK_SCTP_DATACHANNEL_DATACHANNELPROTOCOL_H_
#define NETWERK_SCTP_DATACHANNEL_DATACHANNELPROTOCOL_H_

#if !defined (__Userspace_os_Windows)
#define SCTP_PACKED __attribute__((packed))
#else
#pragma pack (push, 1)
#define SCTP_PACKED
#endif

#define DATA_CHANNEL_PPID_CONTROL   50
#define DATA_CHANNEL_PPID_DOMSTRING 51
#define DATA_CHANNEL_PPID_BINARY    52

#define INVALID_STREAM (0xFFFF)

struct rtcweb_datachannel_open {
  uint8_t  msg_type; // DATA_CHANNEL_OPEN
  uint8_t  channel_type;  
  uint16_t flags;
  uint16_t reliability_params;
  int16_t  priority;
  char     label[1]; // keep VC++ happy...
} SCTP_PACKED;

struct rtcweb_datachannel_open_response {
  uint8_t  msg_type; // DATA_CHANNEL_OPEN_RESPONSE
  uint8_t  error;    // 0 == no error
  uint16_t flags;
  uint16_t reverse_stream;
} SCTP_PACKED;

struct rtcweb_datachannel_ack {
  uint8_t  msg_type; // DATA_CHANNEL_ACK
} SCTP_PACKED;

/* msg_type values: */
#define DATA_CHANNEL_OPEN                     0
#define DATA_CHANNEL_OPEN_RESPONSE            1
#define DATA_CHANNEL_ACK                      2

/* channel_type values: */
#define DATA_CHANNEL_RELIABLE                 0
#define DATA_CHANNEL_RELIABLE_STREAM          1
#define DATA_CHANNEL_UNRELIABLE               2
#define DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT  3
#define DATA_CHANNEL_PARTIAL_RELIABLE_TIMED   4

/* flags values: */
#define DATA_CHANNEL_FLAG_OUT_OF_ORDER_ALLOWED 0x0001
/* all other bits reserved and should be set to 0 */


#define ERR_DATA_CHANNEL_ALREADY_OPEN   1
#define ERR_DATA_CHANNEL_NONE_AVAILABLE 2

#endif

