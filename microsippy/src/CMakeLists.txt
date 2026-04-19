set(src_microsippy
 "usipy_sip_tm.c" "usipy_sip_tm_uac.c" "usipy_sip_tm_uas.c" "usipy_sip_tm_dialog.c"
 "sip_ua/usipy_sip_ua.c" "sip_ua/usipy_sip_ua_idle.c" "sip_ua/usipy_sip_ua_trying.c"
 "sip_ua/usipy_sip_ua_dialing.c" "sip_ua/usipy_sip_ua_connected.c"
 "sip_ua/usipy_sip_ua_disconnected.c" "sip_ua/usipy_sip_ua_internal.h"
 "usipy_sip_udp_task.c" "usipy_sip_udp_task.h"
 "public/usipy_str.h" "public/usipy_sip_hdr_types.h" "public/usipy_sip_msg.h" "usipy_sip_msg.c"
 "usipy_msg_heap.c" "public/usipy_msg_heap.h" "usipy_str.c" "usipy_sip_hdr.c"
 "usipy_sip_hdr.h" "usipy_fast_parser.c" "usipy_fast_parser.h"
 "usipy_sip_hdr_db.c" "usipy_sip_hdr_db.h" "usipy_sip_sline.c"
 "public/usipy_sip_sline.h" "usipy_misc.c" "usipy_misc.h" "usipy_sip_method_db.c"
 "public/usipy_sip_tm.h"
 "public/usipy_sip_dialog.h" "public/usipy_sip_ua.h" "usipy_sip_dialog.c" "usipy_sip_tm_internal.h"
 "usipy_sip_tm_priv.h"
 "usipy_sip_method_db.h" "public/usipy_sip_method_types.h" "usipy_sip_hdr_cseq.c"
 "usipy_sip_hdr_cseq.h" "usipy_sip_hdr_via.c"
 "usipy_sip_hdr_via.h" "usipy_types.h" "usipy_sip_uri.c" "usipy_sip_uri.h"
 "usipy_sip_req.c" "usipy_sip_req.h" "usipy_debug.h" "usipy_msg_heap_rb.h"
 "usipy_msg_heap_inl.h" "usipy_sip_hdr_db_pdata.h" "usipy_sip_hdr_onetoken.c"
 "usipy_sip_hdr_onetoken.h" "usipy_sip_hdr_nameaddr.c" "usipy_sip_hdr_nameaddr.h"
 "usipy_sip_hdr_auth.c" "usipy_sip_hdr_auth.h"
 "usipy_sip_hdr_authz.c" "usipy_sip_hdr_authz.h"
 "external/mackron_md5/md5.c"
 "usipy_sip_tid.c" "usipy_sip_tid.h" "usipy_sip_res.c" "usipy_sip_res.h"
 "bits/turbocompare.h" "bits/endian.h"
)
add_library(microsippy STATIC ${src_microsippy})

target_include_directories(microsippy PRIVATE
  "."
)
