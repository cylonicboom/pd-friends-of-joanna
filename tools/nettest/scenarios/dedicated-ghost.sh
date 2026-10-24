# the one where the server is a ghost: the host runs --dedicated (nobody plays
# there), takes co-op and is hidden, cloaked and invincible on every machine;
# the client is the operative. distinct carousel picks (host 2, client 3)
# prove each seat wears its own player's pick across the wire.
TEAM_STAGE=0
TEAM_ROLES="c,b"
CLIENTS=1
RUN_SECS=120
EXTRA_HOST_ARGS="--dedicated --team-agent 2"
EXTRA_CLIENT_ARGS="--team-agent 3"
