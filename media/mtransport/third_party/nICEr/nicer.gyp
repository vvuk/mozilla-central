# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# nrappkit.gyp
#
#
{
  'targets' : [
      {
          'target_name' : 'nicer',
          'type' : 'static_library',

          'include_dirs' : [
              # EXTERNAL
              


              # INTERNAL
          ],

          'sources' : [
                # STUN
                "./src/stun/addrs.c",
                "./src/stun/addrs.h",
                "./src/stun/nr_socket_turn.c",
                "./src/stun/nr_socket_turn.h",
                "./src/stun/stun.h",
                "./src/stun/stun_build.c",
                "./src/stun/stun_build.h",
                "./src/stun/stun_client_ctx.c",
                "./src/stun/stun_client_ctx.h",
                "./src/stun/stun_codec.c",
                "./src/stun/stun_codec.h",
                "./src/stun/stun_hint.c",
                "./src/stun/stun_hint.h",
                "./src/stun/stun_msg.c",
                "./src/stun/stun_msg.h",
                "./src/stun/stun_proc.c",
                "./src/stun/stun_proc.h",
                "./src/stun/stun_reg.h",
                "./src/stun/stun_server_ctx.c",
                "./src/stun/stun_server_ctx.h",
                "./src/stun/stun_util.c",
                "./src/stun/stun_util.h",

          ],
          
          'defines' : [
              'SANITY_CHECKS',
              'USE_TURN',
              'USE_ICE',
              'USE_RFC_3489_BACKWARDS_COMPATIBLE',
              'USE_STUND_0_96',
              'USE_STUN_PEDANTIC',
              'USE_TURN'
          ],
          
          'conditions' : [
              ## Mac
              [ 'OS == "mac"', {
                'cflags': [
                    '-Werror',
                    '-Wall',
                    '-Wno-parentheses',
                    '-Wno-strict-prototypes',
                    '-Wmissing-prototypes',
                 ],
                 'defines' : [
                     'DARWIN',
                     'HAVE_LIBM=1',
                     'HAVE_STRDUP=1',
                     'HAVE_STRLCPY=1',
                     'HAVE_SYS_TIME_H=1',
                     'HAVE_VFPRINTF=1',
                     'NEW_STDIO'
                     'RETSIGTYPE=void',
                     'TIME_WITH_SYS_TIME_H=1',
                     '__UNUSED__="__attribute__((unused))"',
                 ],

		 'include_dirs': [
		     'src/port/darwin/include'
		 ],
		 
		 'sources': [
              	      './src/port/darwin/include/csi_platform.h',
	              './src/port/darwin/include/sys/queue.h',
		 ],
              }],
              
              ## Win
              [ 'OS == "win"', {

              }],

              
              ## Linux
              [ 'OS == "linux"', {

              }]
          ]
      }]
}

# gcc -g    -c -o ice_candidate.d /Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../ice/ice_candidate.c -MM -MG -DUSE_TURN -DDARWIN -DHAVE_SIN_LEN  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../ice/  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../net/ -DUSE_ICE -DUSE_RFC_3489_BACKWARDS_COMPATIBLE -DUSE_STUND_0_96 -DUSE_STUN_PEDANTIC -DUSE_TURN -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../stun/  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../util/  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../crypto/  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../ice/test/  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../net/test/  -I/Users/ekr/dev/alder/media/mtransport/third_party/nICEr/src/make/darwin/../../stun/test/ -g -Werror -Wall -Wno-parentheses -DHAVE_STRDUP -D__UNUSED__="__attribute__((unused))" -Drestrict=__restrict__ -I../../../../nrappkit/src/make/darwin -I../../../../nrappkit//src/util -I../../../../nrappkit//src/util/libekr -I../../../../nrappkit//src/port/darwin/include -I../../../../nrappkit//src/share -I../../../../nrappkit//src/registry -I../../../../nrappkit//src/stats -DOPENSSL -I../../../../openssl-0.9.8g/include -DSANITY_CHECKS


# "./src/crypto/nr_crypto.c",
# "./src/crypto/nr_crypto.h",
# "./src/crypto/nr_crypto_openssl.c",
# "./src/crypto/nr_crypto_openssl.h",
# "./src/ice/codewords.h",
# "./src/ice/ice_candidate.c",
# "./src/ice/ice_candidate.h",
# "./src/ice/ice_candidate_pair.c",
# "./src/ice/ice_candidate_pair.h",
# "./src/ice/ice_codeword.c",
# "./src/ice/ice_codeword.h",
# "./src/ice/ice_component.c",
# "./src/ice/ice_component.h",
# "./src/ice/ice_ctx.c",
# "./src/ice/ice_ctx.h",
# "./src/ice/ice_handler.h",
# "./src/ice/ice_media_stream.c",
# "./src/ice/ice_media_stream.h",
# "./src/ice/ice_parser.c",
# "./src/ice/ice_peer_ctx.c",
# "./src/ice/ice_peer_ctx.h",
# "./src/ice/ice_reg.h",
# "./src/ice/ice_socket.c",
# "./src/ice/ice_socket.h",
# "./src/ice/test/ice_test.c",
# "./src/net/nr_socket.c",
# "./src/net/nr_socket.h",
# "./src/net/nr_socket_local.c",
# "./src/net/nr_socket_local.h",
# "./src/net/test/evil_test.c",
# "./src/net/test/nr_socket_evil.c",
# "./src/net/test/nr_socket_evil.h",
# "./src/net/test/nr_socket_nat.c",
# "./src/net/test/nr_socket_nat.h",
# "./src/net/test/nr_socket_nat_test.c",
# "./src/net/test/nr_socket_test.c",
# "./src/net/test/transport_addr_reg_test.c",
# "./src/net/transport_addr.c",
# "./src/net/transport_addr.h",
# "./src/net/transport_addr_reg.c",
# "./src/net/transport_addr_reg.h",
# "./src/stun/test/client.c",
# "./src/stun/test/dup_addr_test.c",
# "./src/stun/test/odd_test_vectors.c",
# "./src/stun/test/remi_vector_test.c",
# "./src/stun/test/server.c",
# "./src/stun/test/stun_loopback_test.c",
# "./src/stun/test/stun_test_util.c",
# "./src/stun/test/stun_test_util.h",
# "./src/stun/test/tlsServer.c",
# "./src/stun/test/turn_loopback_test.c",
# "./src/stun/test/turn_test_client.c",
# "./src/stun/test/turn_test_echo_server.c",
# "./src/stun/test/turn_test_server.c",
# "./src/stun/test/turn_test_server_util.c",
# "./src/stun/test/udp.c",
# "./src/stun/test/udp.h",
# "./src/stun/turn_client_ctx.c",
# "./src/stun/turn_client_ctx.h",
# "./src/util/cb_args.c",
# "./src/util/cb_args.h",
# "./src/util/ice_util.c",
# "./src/util/ice_util.h",
# "./src/util/mbslen.c",
# "./src/util/mbslen.h",
