# $FreeBSD$

.include <bsd.own.mk>

PROG=lli

SRCDIR=	tools/lli
SRCS=	lli.cpp \
	RemoteMemoryManager.cpp \
	RemoteTargetExternal.cpp \
	RemoteTarget.cpp

LIBDEPS=llvmx86disassembler \
	llvmx86asmparser \
	llvmirreader \
	llvmasmparser \
	llvmbitreader \
	llvmx86codegen \
	llvmx86desc \
	llvmx86info \
	llvmx86instprinter \
	llvmx86utils \
	llvmselectiondag \
	llvmasmprinter \
	llvmmcparser \
	llvminstrumentation \
	llvminterpreter \
	llvmmcdisassembler \
	llvmmcjit \
	llvmcodegen \
	llvmobjcarcopts \
	llvmscalaropts \
	llvminstcombine \
	llvmtransformutils \
	llvmipa \
	llvmanalysis \
	llvmruntimedyld \
	llvmexecutionengine \
	llvmtarget \
	llvmmc \
	llvmobject \
	llvmcore \
	llvmsupport

DPADD=  ${LIBCURSES} ${LIBZ}
LDADD=  -lcurses
LDADD+= -lz

.include "../clang.prog.mk"
