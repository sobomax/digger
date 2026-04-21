#include "public/usipy_platform.h"
#include "digger_version.h"

static const struct usipy_str _digger_reload_header =
  USIPY_2STR(DIGGER_SIP_BRANDING);

static const struct usipy_str *
netsim_sip_platform_get_user_agent(void)
{

    return (&_digger_reload_header);
}

static const struct usipy_str *
netsim_sip_platform_get_server(void)
{

    return (&_digger_reload_header);
}

#if defined(_WIN32)
static const struct usipy_platform _netsim_sip_platform = {
#else
const struct usipy_platform usipy_platform = {
#endif
  .fallback = &usipy_platform_default,
  .default_udp_port = &DEFAULT_UDP_PORT_s,
  .default_udp_port_i = DEFAULT_UDP_PORT,
  .mono_ms = usipy_platform_delegate_mono_ms,
  .sleep_until_ms = usipy_platform_delegate_sleep_until_ms,
  .random_fill = usipy_platform_delegate_random_fill,
  .log_vwrite = usipy_platform_delegate_log_vwrite,
  .get_user_agent = netsim_sip_platform_get_user_agent,
  .get_server = netsim_sip_platform_get_server,
};

#if defined(_WIN32)
const struct usipy_platform *
usipy_platform_get(void)
{

    return (&_netsim_sip_platform);
}
#endif
