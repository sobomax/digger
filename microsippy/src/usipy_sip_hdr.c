#include <stddef.h>

#include "usipy_types.h"
#include "public/usipy_str.h"
#include "usipy_sip_hdr.h"
#include "public/usipy_sip_hdr_types.h"
#include "usipy_sip_hdr_db.h"

int
usipy_sip_hdr_preparse(struct usipy_sip_hdr *shp, struct usipy_sip_hdr *ehp)
{
     const struct usipy_hdr_db_entr *hdep;
     int nextra;

     if (usipy_str_split(&shp->onwire.full, ':', &shp->onwire.name,
       &shp->onwire.value) != 0)
         return (-1);
     usipy_str_trm_e(&shp->onwire.name);
     hdep = usipy_hdr_db_lookup(&shp->onwire.name);
     if (hdep == NULL)
	 return (-1);
     shp->onwire.hf_type = hdep;
     if (!usipy_hdr_iscmpct(hdep)) {
         shp->hf_type = hdep;
     } else {
         shp->hf_type = usipy_hdr_db_byid(hdep->cantype);
     }
     nextra = 0;
     if (hdep->flags.csl_allowed) {
         for (struct usipy_str csp = shp->onwire.value; csp.l > 0;) {
             if (usipy_str_split(&csp, ',', &shp->onwire.value, &csp) != 0) {
                 break;
             }
             if ((shp + 2) > ehp)
                 return (-1);
             usipy_str_ltrm_b(&shp->onwire.value);
             usipy_str_ltrm_e(&shp->onwire.value);
             nextra += 1;
             shp++;
             shp->hf_type = shp[-1].hf_type;
             shp->onwire.value = csp;
             shp->onwire.hf_type = shp[-1].onwire.hf_type;
             shp->onwire.name = shp[-1].onwire.name;
         }
     }
     usipy_str_ltrm_b(&shp->onwire.value);
     usipy_str_ltrm_e(&shp->onwire.value);
     return (nextra);
}
