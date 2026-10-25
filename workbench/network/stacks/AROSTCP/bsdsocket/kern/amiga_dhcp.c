#include <conf.h>

#include <dos/dos.h>
#include <proto/dos.h>
#include <kern/amiga_gui.h>
#include <kern/amiga_dhcp.h>
#include <net/if.h>
#include <net/if_sana.h>
#include <net/sana2arp.h>   /* for autoip_start() */
#include <api/netservice_api.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <proto/exec.h>

extern struct ifnet *ifnet;

/*
 * DHCP client launching has been EXTERNALISED to the dhcp service daemon
 * (workbench/network/stacks/AROSTCP/services/dhcp, installed as
 * SYS:System/Network/Services/dhcpd and configured in db/services.d/dhcp).  The daemon
 * iterates interfaces (SIOCGIFDHCP), launches/manages the ISC dhclient, and
 * cycles it on reconfigure via netservices.library.
 *
 * The in-stack entry points below are now thin: addifent() and the interface
 * teardown still call run_dhclient()/kill_dhclient() etc. to mark intent, but
 * the actual process management is the daemon's job - the stack only records
 * the per-interface ifi_aros_usedhcp flag (set by addifent), which the daemon
 * reads back through SIOCGIFDHCP.  These remain as no-ops so existing callers
 * are undisturbed; autoip (IPv4LL) stays in the stack.
 */

/*
 * A new association (sana_connect) means a new network and a new lease: the
 * DHCP daemon's client must start over, and only the daemon knows it. Ask it
 * through its reconfigure-end signal, which stops its clients and launches
 * them again for the current interface set. Without this, a link lost and
 * regained (a dropped association, a laptop sleep) left the interface with
 * no address while dhclient went on believing in a lease the stack had
 * deleted. Before the daemon registers (the boot configuration) there is
 * nobody to tell, and the daemon's own first launch covers it.
 */
void run_dhclient(struct ifnet *ifp)
{
    struct NetService *svc;

    (void)ifp;
    svc = netservice_find((CONST_STRPTR)"dhcp");
    if(svc != NULL && (svc->ns_Flags & NSF_EXTERNAL) && svc->ns_Task != NULL &&
            svc->ns_SigEnd != 0) {
        __log(LOG_NOTICE, "%s%d: new association, DHCP restarted.",
              ifp->if_name, ifp->if_unit);
        Signal(svc->ns_Task, svc->ns_SigEnd);
    }
}
void kill_dhclient(struct ifnet *ifp)       { (void)ifp; }
#if INET6 && DHCP6
void run_dhclient6(struct ifnet *ifp)       { (void)ifp; }
void kill_dhclient6(struct ifnet *ifp)      { (void)ifp; }
#endif
void dhcp_stop_all(void)                    { }

/*
 * Called once the API is up (from the log/NETTRACE task) to deal with
 * interfaces that were configured before the stack was visible (IFF_DELAYUP).
 * DHCP for those is now handled by the dhcp daemon when it starts; here we only
 * clear the deferral flag and bring up IPv4LL (autoip) for AUTO interfaces that
 * neither use DHCP nor have a static address.
 */
void run_dhcp(void)
{
    struct ifnet *ifp;

    for(ifp = ifnet; ifp; ifp = ifp->if_next) {
        struct sana_softc *ssc = sana_softc_of(ifp);

        /* only a SANA-II interface is ever marked */
        if(ssc == NULL)
            continue;
        if(ssc->ss_cflags & SSF_DELAYUP) {
            ssc->ss_cflags &= ~SSF_DELAYUP;
            if(!ifp->if_data.ifi_aros_usedhcp &&
                    ssc->ss_ipaddr.s_addr == INADDR_ANY)
                autoip_start(ifp);  /* no DHCPv4 and no address assigned */
        }
    }
}
