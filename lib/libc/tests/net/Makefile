PACKAGE=	tests

ATF_TESTS_C+=	ether_test
ATF_TESTS_C+=	eui64_aton_test
ATF_TESTS_C+=	eui64_ntoa_test
ATF_TESTS_CXX+=	link_addr_test

CXXSTD.link_addr_test=	c++20

CFLAGS+=	-I${.CURDIR}

NETBSD_ATF_TESTS_C+=	getprotoent_test
NETBSD_ATF_TESTS_C+=	ether_aton_test

SRCS.ether_aton_test=	aton_ether_subr.c t_ether_aton.c

# TODO: hostent_test
NETBSD_ATF_TESTS_SH=	nsdispatch_test
NETBSD_ATF_TESTS_SH+=	protoent_test
NETBSD_ATF_TESTS_SH+=	servent_test

BINDIR=		${TESTSDIR}

PROGS=		h_nsd_recurse
PROGS+=		h_protoent
PROGS+=		h_servent
PROGS+=		h_dns_server

LIBADD.h_nsd_recurse+=	pthread

CLEANFILES+=	aton_ether_subr.c
aton_ether_subr.c: gen_ether_subr ${SRCTOP}/sys/net/if_ethersubr.c
	${__MAKE_SHELL} ${.ALLSRC} ${.TARGET}

.include "../Makefile.netbsd-tests"

TESTS_SUBDIRS=		getaddrinfo
${PACKAGE}FILES+=	hosts
${PACKAGE}FILES+=	resolv.conf

ATF_TESTS_SH_SED_servent_test=	-e 's,services.cdb,services.db,g'

.include <bsd.test.mk>
