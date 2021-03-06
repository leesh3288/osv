/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)uipc_domain.c	8.2 (Berkeley) 10/18/93
 */

#include <errno.h>
#include <osv/initialize.hh>
#include <bsd/porting/netport.h>
#include <bsd/porting/uma_stub.h>
#include <bsd/porting/callout.h>
#include <sys/cdefs.h>

#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/sys/sys/protosw.h>
#include <bsd/sys/sys/domain.h>
#include <bsd/sys/sys/eventhandler.h>
#include <bsd/sys/sys/mbuf.h>

#include <bsd/sys/net/vnet.h>


/*
 * System initialization
 *
 * Note: domain initialization takes place on a per domain basis
 * as a result of traversing a SYSINIT linker set.  Most likely,
 * each domain would want to call DOMAIN_SET(9) itself, which
 * would cause the domain to be added just after domaininit()
 * is called during startup.
 *
 * See DOMAIN_SET(9) for details on its use.
 */

SYSINIT(domain, SI_SUB_PROTO_DOMAININIT, SI_ORDER_ANY, domaininit, NULL);

SYSINIT(domainfin, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_FIRST, domainfinalize,
    NULL);

static struct callout pffast_callout;
static struct callout pfslow_callout;

static void	pffasttimo(void *);
static void	pfslowtimo(void *);

struct domain *domains;		/* registered protocol domains */
int domain_init_status = 0;
static struct mtx dom_mtx;		/* domain list lock */

/*
 * Dummy protocol specific user requests function pointer array.
 * All functions return EOPNOTSUPP.
 */
struct pr_usrreqs nousrreqs = initialize_with([] (pr_usrreqs& x) {
	x.pru_accept =		pru_accept_notsupp;
	x.pru_attach =		pru_attach_notsupp;
	x.pru_bind =		pru_bind_notsupp;
	x.pru_connect =		pru_connect_notsupp;
	x.pru_connect2 =		pru_connect2_notsupp;
	x.pru_control =		pru_control_notsupp;
	x.pru_disconnect	=	pru_disconnect_notsupp;
	x.pru_listen =		pru_listen_notsupp;
	x.pru_peeraddr =		pru_peeraddr_notsupp;
	x.pru_rcvd =		pru_rcvd_notsupp;
	x.pru_rcvoob =		pru_rcvoob_notsupp;
	x.pru_send =		pru_send_notsupp;
	x.pru_sense =		pru_sense_null;
	x.pru_shutdown =		pru_shutdown_notsupp;
	x.pru_sockaddr =		pru_sockaddr_notsupp;
	x.pru_sosend =		pru_sosend_notsupp;
	x.pru_soreceive =	pru_soreceive_notsupp;
	x.pru_sopoll =		pru_sopoll_notsupp;
});

static void
protosw_init(struct protosw *pr)
{
	struct pr_usrreqs *pu;

	pu = pr->pr_usrreqs;
	KASSERT(pu != NULL, ("protosw_init: %ssw[%d] has no usrreqs!",
	    pr->pr_domain->dom_name,
	    (int)(pr - pr->pr_domain->dom_protosw)));

	/*
	 * Protocol switch methods fall into three categories: mandatory,
	 * mandatory but protosw_init() provides a default, and optional.
	 *
	 * For true protocols (i.e., pru_attach != NULL), KASSERT truly
	 * mandatory methods with no defaults, and initialize defaults for
	 * other mandatory methods if the protocol hasn't defined an
	 * implementation (NULL function pointer).
	 */
#if 0
	if (pu->pru_attach != NULL) {
		KASSERT(pu->pru_abort != NULL,
		    ("protosw_init: %ssw[%d] pru_abort NULL",
		    pr->pr_domain->dom_name,
		    (int)(pr - pr->pr_domain->dom_protosw)));
		KASSERT(pu->pru_send != NULL,
		    ("protosw_init: %ssw[%d] pru_send NULL",
		    pr->pr_domain->dom_name,
		    (int)(pr - pr->pr_domain->dom_protosw)));
	}
#endif

#define DEFAULT(foo, bar)	if ((foo) == NULL)  (foo) = (bar)
	DEFAULT(pu->pru_accept, pru_accept_notsupp);
	DEFAULT(pu->pru_bind, pru_bind_notsupp);
	DEFAULT(pu->pru_connect, pru_connect_notsupp);
	DEFAULT(pu->pru_connect2, pru_connect2_notsupp);
	DEFAULT(pu->pru_control, pru_control_notsupp);
	DEFAULT(pu->pru_disconnect, pru_disconnect_notsupp);
	DEFAULT(pu->pru_listen, pru_listen_notsupp);
	DEFAULT(pu->pru_peeraddr, pru_peeraddr_notsupp);
	DEFAULT(pu->pru_rcvd, pru_rcvd_notsupp);
	DEFAULT(pu->pru_rcvoob, pru_rcvoob_notsupp);
	DEFAULT(pu->pru_sense, pru_sense_null);
	DEFAULT(pu->pru_shutdown, pru_shutdown_notsupp);
	DEFAULT(pu->pru_sockaddr, pru_sockaddr_notsupp);
	DEFAULT(pu->pru_sosend, sosend_generic);
	DEFAULT(pu->pru_soreceive, soreceive_generic);
	DEFAULT(pu->pru_sopoll, sopoll_generic);
#undef DEFAULT
	if (pr->pr_init)
		(*pr->pr_init)();
}

/*
 * Add a new protocol domain to the list of supported domains
 * Note: you cant unload it again because a socket may be using it.
 * XXX can't fail at this time.
 */
void
domain_init(void *arg)
{
	struct domain *dp = (domain *)arg;
	struct protosw *pr;

	if (dp->dom_init)
		(*dp->dom_init)();
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		protosw_init(pr);
	/*
	 * update global information about maximums
	 */
	max_hdr = max_linkhdr + max_protohdr;
	max_datalen = MHLEN - max_hdr;
	if (max_datalen < 1)
		panic("%s: max_datalen < 1", __func__);
}

/*
 * Add a new protocol domain to the list of supported domains
 * Note: you cant unload it again because a socket may be using it.
 * XXX can't fail at this time.
 */
void
domain_add(void *data)
{
	struct domain *dp;

	dp = (struct domain *)data;
	mtx_lock(&dom_mtx);
	dp->dom_next = domains;
	domains = dp;

	KASSERT(domain_init_status >= 1,
	    ("attempt to domain_add(%s) before domaininit()",
	    dp->dom_name));
#ifndef INVARIANTS
	if (domain_init_status < 1)
		printf("WARNING: attempt to domain_add(%s) before "
		    "domaininit()\n", dp->dom_name);
#endif
#ifdef notyet
	KASSERT(domain_init_status < 2,
	    ("attempt to domain_add(%s) after domainfinalize()",
	    dp->dom_name));
#else
	if (domain_init_status >= 2)
		printf("WARNING: attempt to domain_add(%s) after "
		    "domainfinalize()\n", dp->dom_name);
#endif
	mtx_unlock(&dom_mtx);
}

/* ARGSUSED*/
void
domaininit(void *dummy)
{

    mtx_init(&dom_mtx, "domain list", 0, MTX_DEF);

	if (max_linkhdr < 16)		/* XXX */
		max_linkhdr = 16;

	callout_init(&pffast_callout, CALLOUT_MPSAFE);
	callout_init(&pfslow_callout, CALLOUT_MPSAFE);

	mtx_lock(&dom_mtx);
	KASSERT(domain_init_status == 0, ("domaininit called too late!"));
	domain_init_status = 1;
	mtx_unlock(&dom_mtx);
}

/* ARGSUSED*/
void
domainfinalize(void *dummy)
{

	mtx_lock(&dom_mtx);
	KASSERT(domain_init_status == 1, ("domainfinalize called too late!"));
	domain_init_status = 2;
	mtx_unlock(&dom_mtx);	

	callout_reset(&pffast_callout, 1, pffasttimo, NULL);
	callout_reset(&pfslow_callout, 1, pfslowtimo, NULL);
}

struct domain *
pffinddomain(int family)
{
	struct domain *dp;
	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			return dp;
	return 0;
}

struct protosw *
pffindtype(struct domain *dp, int type)
{
	if (!dp)
		return 0;

	struct protosw *pr;
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		if (pr->pr_type && pr->pr_type == type)
			return (pr);
	return (0);
}

struct protosw *
pffindproto(struct domain *dp, int protocol, int type)
{
	if (!dp)
		return 0;

	struct protosw *pr;
	struct protosw *maybe = 0;

	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
		if ((pr->pr_protocol == protocol) && (pr->pr_type == type))
			return (pr);

		if (type == SOCK_RAW && pr->pr_type == SOCK_RAW &&
		    pr->pr_protocol == 0 && maybe == (struct protosw *)0)
			maybe = pr;
	}
	return (maybe);
}

/*
 * The caller must make sure that the new protocol is fully set up and ready to
 * accept requests before it is registered.
 */
int
pf_proto_register(int family, struct protosw *npr)
{
	VNET_ITERATOR_DECL(vnet_iter);
	struct domain *dp;
	struct protosw *pr, *fpr;

	/* Sanity checks. */
	if (family == 0)
		return (EPFNOSUPPORT);
	if (npr->pr_type == 0)
		return (EPROTOTYPE);
	if (npr->pr_protocol == 0)
		return (EPROTONOSUPPORT);
	if (npr->pr_usrreqs == NULL)
		return (ENXIO);

	/* Try to find the specified domain based on the family. */
	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (EPFNOSUPPORT);

found:
	/* Initialize backpointer to struct domain. */
	npr->pr_domain = dp;
	fpr = NULL;

	/*
	 * Protect us against races when two protocol registrations for
	 * the same protocol happen at the same time.
	 */
	mtx_lock(&dom_mtx);

	/* The new protocol must not yet exist. */
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
		if ((pr->pr_type == npr->pr_type) &&
		    (pr->pr_protocol == npr->pr_protocol)) {
			mtx_unlock(&dom_mtx);
			return (EEXIST);	/* XXX: Check only protocol? */
		}
		/* While here, remember the first free spacer. */
		if ((fpr == NULL) && (pr->pr_protocol == PROTO_SPACER))
			fpr = pr;
	}

	/* If no free spacer is found we can't add the new protocol. */
	if (fpr == NULL) {
		mtx_unlock(&dom_mtx);
		return (ENOMEM);
	}

	/* Copy the new struct protosw over the spacer. */
	bcopy(npr, fpr, sizeof(*fpr));

	/* Job is done, no more protection required. */
	mtx_unlock(&dom_mtx);

	/* Initialize and activate the protocol. */
	VNET_LIST_RLOCK();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET_QUIET(vnet_iter);
		protosw_init(fpr);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK();

	return (0);
}

/*
 * The caller must make sure the protocol and its functions correctly shut down
 * all sockets and release all locks and memory references.
 */
int
pf_proto_unregister(int family, int protocol, int type)
{
	struct domain *dp;
	struct protosw *pr, *dpr;

	/* Sanity checks. */
	if (family == 0)
		return (EPFNOSUPPORT);
	if (protocol == 0)
		return (EPROTONOSUPPORT);
	if (type == 0)
		return (EPROTOTYPE);

	/* Try to find the specified domain based on the family type. */
	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (EPFNOSUPPORT);

found:
	dpr = NULL;

	/* Lock out everyone else while we are manipulating the protosw. */
	mtx_lock(&dom_mtx);

	/* The protocol must exist and only once. */
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
		if ((pr->pr_type == type) && (pr->pr_protocol == protocol)) {
			if (dpr != NULL) {
				mtx_unlock(&dom_mtx);
				return (EMLINK);   /* Should not happen! */
			} else
				dpr = pr;
		}
	}

	/* Protocol does not exist. */
	if (dpr == NULL) {
		mtx_unlock(&dom_mtx);
		return (EPROTONOSUPPORT);
	}

	/* De-orbit the protocol and make the slot available again. */
	dpr->pr_type = 0;
	dpr->pr_domain = dp;
	dpr->pr_protocol = PROTO_SPACER;
	dpr->pr_flags = 0;
	dpr->pr_input = NULL;
	dpr->pr_output = NULL;
	dpr->pr_ctlinput = NULL;
	dpr->pr_ctloutput = NULL;
	dpr->pr_init = NULL;
	dpr->pr_fasttimo = NULL;
	dpr->pr_slowtimo = NULL;
	dpr->pr_drain = NULL;
	dpr->pr_usrreqs = &nousrreqs;

	/* Job is done, not more protection required. */
	mtx_unlock(&dom_mtx);

	return (0);
}

void
pfctlinput(int cmd, struct bsd_sockaddr *sa)
{
	struct domain *dp;
	struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_ctlinput)
				(*pr->pr_ctlinput)(cmd, sa, (void *)0);
}

void
pfctlinput2(int cmd, struct bsd_sockaddr *sa, void *ctlparam)
{
	struct domain *dp;
	struct protosw *pr;

	if (!sa)
		return;
	for (dp = domains; dp; dp = dp->dom_next) {
		/*
		 * the check must be made by xx_ctlinput() anyways, to
		 * make sure we use data item pointed to by ctlparam in
		 * correct way.  the following check is made just for safety.
		 */
		if (dp->dom_family != sa->sa_family)
			continue;

		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_ctlinput)
				(*pr->pr_ctlinput)(cmd, sa, ctlparam);
	}
}

static void
pfslowtimo(void *arg)
{
	struct domain *dp;
	struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_slowtimo)
				(*pr->pr_slowtimo)();
	callout_reset(&pfslow_callout, hz/2, pfslowtimo, NULL);
}

static void
pffasttimo(void *arg)
{
	struct domain *dp;
	struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_fasttimo)
				(*pr->pr_fasttimo)();
	callout_reset(&pffast_callout, hz/5, pffasttimo, NULL);
}
