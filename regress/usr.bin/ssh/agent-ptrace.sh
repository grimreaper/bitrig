#	$OpenBSD: agent-ptrace.sh,v 1.2 2014/02/27 21:21:25 djm Exp $
#	Placed in the Public Domain.

tid="disallow agent ptrace attach"

trace "start agent"
eval `${SSHAGENT} -s` > /dev/null
r=$?
if [ $r -ne 0 ]; then
	fail "could not start ssh-agent: exit code $r"
else
	# ls -l ${SSH_AUTH_SOCK}
	gdb ${SSHAGENT} ${SSH_AGENT_PID} > ${OBJ}/gdb.out 2>&1 << EOF
		quit
EOF
	r=$?
	if [ $r -ne 0 ]; then
		fail "gdb failed: exit code $r"
	fi
	grep -q 'ptrace: Operation not permitted.' ${OBJ}/gdb.out
	r=$?
	rm -f ${OBJ}/gdb.out
	if [ $r -ne 0 ]; then
		fail "ptrace succeeded?: exit code $r"
	fi

	trace "kill agent"
	${SSHAGENT} -k > /dev/null
fi
