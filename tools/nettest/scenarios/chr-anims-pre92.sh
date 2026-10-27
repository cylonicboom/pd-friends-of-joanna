# chr-anims with the client on the pre-92 anim behaviour (no loop / end
# frame from the wire, frame free-running). The chr-anim checks are expected
# to FAIL here - it's how we know they can see the bug.
TEAM_STAGE=0
TEAM_ROLES="b,c"
CLIENTS=1
RUN_SECS=90
EXTRA_CLIENT_ARGS="--net-animtail 0 --net-animcorrect 0"
