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
        	'target_name' : 'nrappkit',
          'type' : 'static_library',

          'include_dirs' : [
              # EXTERNAL
              


              # INTERNAL
	      'src/event',
	      'src/log',
	      'src/registry',
	      'src/share',
	      'src/stats',
	      'src/util',
	      'src/util/libekr'
          ],

          'sources' : [
	      # Shared
#              './src/share/nr_api.h',
              './src/share/nr_common.h',
#              './src/share/nr_dynlib.h',
              './src/share/nr_reg_keys.h',
#              './src/share/nr_startup.c',
#              './src/share/nr_startup.h',
#              './src/share/nrappkit_static_plugins.c',

              # libekr
              './src/util/libekr/assoc.h',
              './src/util/libekr/debug.c',
              './src/util/libekr/debug.h',
              './src/util/libekr/r_assoc.c',
              './src/util/libekr/r_assoc.h',
#              './src/util/libekr/r_assoc_test.c',
              './src/util/libekr/r_bitfield.c',
              './src/util/libekr/r_bitfield.h',
              './src/util/libekr/r_common.h',
              './src/util/libekr/r_crc32.c',
              './src/util/libekr/r_crc32.h',
              './src/util/libekr/r_data.c',
              './src/util/libekr/r_data.h',
              './src/util/libekr/r_defaults.h',
              './src/util/libekr/r_errors.c',
              './src/util/libekr/r_errors.h',
              './src/util/libekr/r_includes.h',
              './src/util/libekr/r_list.c',
              './src/util/libekr/r_list.h',
              './src/util/libekr/r_macros.h',
              './src/util/libekr/r_memory.c',
              './src/util/libekr/r_memory.h',
              './src/util/libekr/r_replace.c',
              './src/util/libekr/r_thread.h',
              './src/util/libekr/r_time.c',
              './src/util/libekr/r_time.h',
              './src/util/libekr/r_types.h',
              './src/util/libekr/debug.c',
              './src/util/libekr/debug.h',

	      # Utilities
              './src/util/byteorder.c',
              './src/util/byteorder.h',
              #'./src/util/escape.c',
              #'./src/util/escape.h',
              #'./src/util/filename.c',
              #'./src/util/getopt.c',
              #'./src/util/getopt.h',
              './src/util/hex.c',
              './src/util/hex.h',
	      #'./src/util/mem_util.c',
              #'./src/util/mem_util.h',
              #'./src/util/mutex.c',
              #'./src/util/mutex.h',
              './src/util/p_buf.c',
              './src/util/p_buf.h',
              #'./src/util/ssl_util.c',
              #'./src/util/ssl_util.h',
              './src/util/util.c',
              './src/util/util.h',
              #'./src/util/util_db.c',
              #'./src/util/util_db.h',

	      # Events
#              './src/event/async_timer.c',
              './src/event/async_timer.h',
#              './src/event/async_wait.c',
              './src/event/async_wait.h',
              './src/event/async_wait_int.h',

	      # Logging
              './src/log/r_log.c',
              './src/log/r_log.h',
              #'./src/log/r_log_plugin.c',

	      # Registry
              './src/registry/c2ru.c',
              './src/registry/c2ru.h',
              #'./src/registry/mod_registry/mod_registry.c',
              #'./src/registry/nrregctl.c',
              #'./src/registry/nrregistryctl.c',
              './src/registry/registry.c',
              './src/registry/registry.h',
              './src/registry/registry_int.h',
              './src/registry/registry_local.c',
              #'./src/registry/registry_plugin.c',
              './src/registry/registry_vtbl.h',
              './src/registry/registrycb.c',
              #'./src/registry/registryd.c',
              #'./src/registry/regrpc.h',
              #'./src/registry/regrpc_client.c',
              #'./src/registry/regrpc_client.h',
              #'./src/registry/regrpc_client_cb.c',
              #'./src/registry/regrpc_clnt.c',
              #'./src/registry/regrpc_server.c',
              #'./src/registry/regrpc_svc.c',
              #'./src/registry/regrpc_xdr.c',

	      # Statistics
              #'./src/stats/nrstats.c',
              #'./src/stats/nrstats.h',
              #'./src/stats/nrstats_app.c',
              #'./src/stats/nrstats_int.h',
              #'./src/stats/nrstats_memory.c',
          ],
          
          'defines' : [
              'SANITY_CHECKS',
          ],
          
          'conditions' : [
              ## Mac
              [ 'OS == "mac"', {
                'cflags_mozilla': [
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
                 'defines' : [
                     'WIN',
                     '__UNUSED__=""',
                     'HAVE_STRDUP=1',
                     'SIZEOF_SHORT=2',
                     'SIZEOF_UNSIGNED_SHORT=2',
                     'SIZEOF_INT=4',
                     'SIZEOF_UNSIGNED_INT=4',
                     'SIZEOF_LONG=4',
                     'SIZEOF_UNSIGNED_LONG=4',
                     'SIZEOF_LONG_LONG=8',
                     'SIZEOF_UNSIGNED_LONG_LONG=8',
                     'NO_REG_RPC'
                 ],

		 'include_dirs': [
		     'src/port/win32/include'
		 ],

		 'sources': [
              	      './src/port/win32/include/csi_platform.h',
	              './src/port/win32/include/sys/queue.h',
		 ],
              }],

              
              ## Linux
              [ 'OS == "linux"', {
                'cflags_mozilla': [
                    '-Werror',
                    '-Wall',
                    '-Wno-parentheses',
                    '-Wno-strict-prototypes',
                    '-Wmissing-prototypes',
                 ],
                 'defines' : [
                     'LINUX',
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
		     'src/port/linux/include'
		 ],
		 
		 'sources': [
              	      './src/port/linux/include/csi_platform.h',
	              './src/port/linux/include/sys/queue.h',
		 ],
              }]
          ]
      }]
}





# -Werror -I../../util/libekr/ -I../../share/ -DDARWIN -fPIC -DHAVE_STRLCPY -DHAVE_SIN_LEN -DDEBUG -DSANITY_CHECKS -Wno-unused   -DDEBUG_IGNORE_TCP_CKSUM_ERRORS  -c -o captured.o ../../captured/main/captured.c   -I../../captured/main/  -I../../captured/  -I../../codec/  -I../../util/libekr/  -I../../util/libekr/threads/null/  -I../../event/ -I../../log/ -I../../log/  -I../../port/impl/extattr/xattr/  -I../../plugin/  -I../../plugin/api/  -I../../plugin/internal/  -I../../registry/  -I../../scripts/  -I../../share/  -I../../stats/main/  -I../../stats/  -I../../util/ -I../../port/darwin/include -I../../port/ -DLISTEND_CONSUMER='"reassd"' -DHAVE_LIBM=1 -DHAVE_SYS_TIME_H=1 -DSTDC_HEADERS=1 -DTIME_WITH_SYS_TIME=1 -DSIZEOF_SHORT=2 -DSIZEOF_UNSIGNED_SHORT=2 -DSIZEOF_INT=4 -DSIZEOF_UNSIGNED_INT=4 -DSIZEOF_LONG=4 -DSIZEOF_UNSIGNED_LONG=4 -DSIZEOF_LONG_LONG=8 -DSIZEOF_UNSIGNED_LONG_LONG=8 -DRETSIGTYPE=void -DHAVE_VPRINTF=1 -DHAVE_STRDUP=1 -DNEW_STDIO -Werror -Wall -Wno-parentheses -Wno-strict-prototypes -Wmissing-prototypes -DINSTALLED_BINARY_PATH='"/Users/ekr/dev/nrappkit/src/make/darwin/"' -DNR_ROOT_PATH='"/Users/ekr/dev/nrappkit/src/make/darwin/../../root/"' -D__UNUSED__="__attribute__((unused))"  -DCAPTURE_USER='"pcecap"' -Werror -Wno-unused -DNR_SHE_IGNORE_PRIVILEGES./examples/count/count.c


#              './src/port/freebsd/include/csi_platform.h',
#              './src/port/impl/extattr/bsd/bsd_extattr.c',
#              './src/port/impl/extattr/xattr/xattr_extattr.c',
#              './src/port/linux/include/csi_platform.h',
#              './src/port/linux/include/linux_funcs.h',
#              './src/port/linux/include/sys/queue.h',
#              './src/port/linux/include/sys/ttycom.h',
#              './src/port/nr_extattr.h',
#              './src/port/win32/include/csi_platform.h',
#              './src/port/win32/include/sys/queue.h',

# NOT NEEDED
#              './src/stats/main/nrstatsctl.c',
#              './src/tools/clic/main.c',
#              './src/tools/clic/types.h',
#              './src/tools/clic/utils.c',
#              './src/tools/clic/utils.h',
#              './src/tools/clim/main.c',
#              './src/tools/clim/types.h',
#              './src/tools/clim/utils.c',
#              './src/tools/clim/utils.h',
#              './src/she/she.c',
#              './src/she/she.h',
#              './src/she/she_action.c',
#              './src/she/she_complete.c',
#              './src/she/she_complete.h',
#              './src/she/she_ctx.c',
#              './src/she/she_debug.c',
#              './src/she/she_elide.c',
#              './src/she/she_gen.c',
#              './src/she/she_int.h',
#              './src/she/she_limits.c',
#              './src/she/she_match.c',
#              './src/she/she_no.c',
#              './src/she/she_no.h',
#              './src/she/she_parse.c',
#              './src/she/she_plugin.c',
#              './src/she/she_print.c',
#              './src/she/she_privileges.c',
#              './src/she/she_slurp.c',
#              './src/she/she_stack.c',
#              './src/nrsh/config/nr_sh_config_clock_timezone_command.c',
#              './src/nrsh/config/nr_sh_config_clock_timezone_command.h',
#              './src/nrsh/config/nr_sh_config_logging_command.c',
#              './src/nrsh/config/nr_sh_config_logging_command.h',
#              './src/nrsh/config/nr_sh_config_logging_util.c',
#              './src/nrsh/config/nr_sh_config_logging_util.h',
#              './src/nrsh/config/nr_sh_config_version_command.c',
#              './src/nrsh/config/nr_sh_config_version_command.h',
#              './src/nrsh/nrsh.c',
#              './examples/demo_plugin/demo_plugin.c',
#              './examples/demo_plugin/demo_stats.c',
#              './examples/demo_plugin/demo_stats.h',
#              './examples/demo_plugin/nr_sh_config_demo.c',
#              './src/captured/captured.h',
#              './src/captured/captured_c2r.h',
#              './src/captured/captured_plugin.c',
#              './src/captured/captured_reg.c',
#              './src/captured/captured_reg.h',
#              './src/captured/main/captured.c',
#              './src/codec/c_buf.c',
#              './src/codec/c_buf.h',
#              './src/codec/c_buf_nb_fill.c',
#              './src/codec/ed_ssl.c',
#              './src/codec/ed_ssl.h',
#              './src/plugin/api/api_force.c',
#              './src/plugin/api/api_force.h',
#              './src/plugin/internal/nr_plugin_int.c',
#              './src/plugin/internal/nr_plugin_int.h',
#              './src/plugin/nr_plugin.h',
